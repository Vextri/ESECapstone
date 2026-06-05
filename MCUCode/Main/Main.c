/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// Pill Dispenser Application
// Prototype system for automated pill dispensing with profiles and testing
// Commands: D=dispense, S=status, B=button sim, L=load profile, W/X=manual motor, Q=quit

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/error.h"
#include "pill_dispenser.h"
#include "sensor_interrupts.h"
#include "stepper_control.h"
#include "esp_uart.h"
#include "pico_rtc.h"
#include <stdio.h>
#include <stdlib.h>

// Sensor callback functions
void on_piezo_detected(void) {
    printf(">>> PILL DROP DETECTED! (Piezo) <<<\n");
    // Could add automatic motor stop logic here
}

void on_hall_effect_detected(void) {
    printf(">>> HALL EFFECT: HIGH->LOW detected <<<\n");
}

void on_ir_detected(void) {
    printf(">>> IR BEAM EVENT! (IR Sensor) <<<\n");
    if (ir_is_beam_broken()) {
        printf("    Beam is BROKEN - pill detected!\n");
    } else {
        printf("    Beam is CLEAR - pill passed through!\n");
    }
    // Could add automatic motor stop logic here
}

int main() {
    // Initialize stdio for keyboard input
    stdio_init_all();
    
    // Initialize software RTC (time cleared — will request from ESP)
    rtc_init_module();
    
    // Initialize pill dispenser system (includes motor init and ESP UART init)
    dispenser_init();
    
    // Initialize sensor interrupt system
    sensor_interrupts_init();
    
    // Set up sensor callbacks
    piezo_set_callback(on_piezo_detected);
    hall_effect_set_callback(on_hall_effect_detected);
    ir_set_callback(on_ir_detected);
    
    // Load test profiles for prototyping
    // (profiles are now restored from flash automatically in dispenser_init)
    // dispenser_switch_to_profile is only needed if flash had a valid active slot
    
    printf("\n=== PILL DISPENSER PROTOTYPE ===\n");
    printf("Commands:\n");
    printf("D - Dispense dose (timed)\n");
    printf("F - Dispense dose (sensor feedback)\n");
    printf("M - Manual load profile (enter command via terminal)\n");
    printf("K - Show / set time\n");
    printf("S - Show status\n");
    printf("L - List all profiles\n");
    printf("B - Simulate button press\n");
    printf("1 - Switch to profile 1 (Vitamin D)\n");
    printf("2 - Switch to profile 2 (Aspirin)\n");
    printf("--- Sensor Controls ---\n");
    printf("P - Toggle piezo sensor\n");
    printf("H - Toggle hall effect sensor\n");
    printf("I - Toggle IR sensor\n");
    printf("R - Reset sensor counts\n");
    printf("T - Show sensor status\n");
    printf("G - Test GPIO states\n");
    printf("V - Monitor all sensor GPIOs continuously\n");
    printf("C - Debug interrupt configuration\n");
    printf("--- Motor 1 (Main Dispenser) ---\n");
    printf("W - Motor 1 forward\n");
    printf("N - Motor 1 backward\n");
    printf("X - Motor 1 stop\n");
    printf("--- Motor 2 ---\n");
    printf("A - Motor 2 forward\n");
    printf("Z - Motor 2 backward\n");
    printf("E - Motor 2 stop\n");
    printf("--- Motor 3 ---\n");
    printf("J - Motor 3 forward\n");
    printf("U - Motor 3 backward\n");
    printf("Y - Motor 3 stop\n");
    printf("Q - Quit\n");
    printf("==============================\n\n");

    char input;
    
    while (true) {
        // Check for keyboard input
        input = getchar_timeout_us(0);
        
        if (input != PICO_ERROR_TIMEOUT) {
            switch (input) {
                case 'd':
                case 'D':
                    dispenser_execute_dose();
                    break;
                    
                case 'f':
                case 'F':
                    dispenser_execute_dose_sensor_based();
                    break;

                case 'm':
                case 'M': {
                    printf("Enter profile command (10s timeout):\n");
                    printf("Format: CMD|action=LOAD_PROFILE|slot=<0-2>|med=<name>|total=<n>|dose=<n>|time=<ms>\n> ");
                    char line_buf[256];
                    int pos = 0;
                    int ch;
                    while (pos < (int)sizeof(line_buf) - 1) {
                        ch = getchar_timeout_us(10000000); // 10s per character
                        if (ch == PICO_ERROR_TIMEOUT) {
                            if (pos > 0) {
                                printf("\n"); // treat timeout as Enter if buffer has content
                            } else {
                                printf("\nTimeout — no input received, command cancelled.\n");
                                pos = 0;
                            }
                            break;
                        }
                        if (ch == '\n' || ch == '\r') {
                            printf("\n");
                            break;
                        }
                        printf("%c", (char)ch); // echo back
                        line_buf[pos++] = (char)ch;
                    }
                    line_buf[pos] = '\0';
                    if (pos > 0) {
                        esp_uart_inject_line(line_buf);
                    }
                    break;
                }

                case 'k':
                case 'K': {
                    char time_buf[32];
                    rtc_get_time_str(time_buf, sizeof(time_buf));
                    printf("Current time : %s\n", time_buf);
                    if (rtc_is_set()) {
                        uint16_t mod = rtc_get_minutes_of_day();
                        printf("Minutes today: %u (%02u:%02u)\n",
                               (unsigned)mod, (unsigned)(mod / 60), (unsigned)(mod % 60));
                    }
                    printf("Enter new time (YYYY-MM-DD HH:MM:SS or epoch integer, 10s timeout, Enter to skip):\n> ");
                    char time_input[32];
                    int tpos = 0;
                    int tch;
                    while (tpos < (int)sizeof(time_input) - 1) {
                        tch = getchar_timeout_us(10000000);
                        if (tch == PICO_ERROR_TIMEOUT) {
                            break; // skip silently
                        }
                        if (tch == '\n' || tch == '\r') {
                            printf("\n");
                            break;
                        }
                        printf("%c", (char)tch);
                        time_input[tpos++] = (char)tch;
                    }
                    time_input[tpos] = '\0';
                    if (tpos > 0) {
                        // Pure digits → treat as Unix epoch directly
                        bool all_digits = true;
                        for (int i = 0; i < tpos; i++) {
                            if (time_input[i] < '0' || time_input[i] > '9') {
                                all_digits = false;
                                break;
                            }
                        }
                        uint32_t epoch = 0;
                        if (all_digits) {
                            epoch = (uint32_t)strtoul(time_input, NULL, 10);
                        } else {
                            epoch = rtc_parse_datetime_str(time_input);
                        }
                        if (epoch > 0) {
                            rtc_set_epoch(epoch);
                            rtc_get_time_str(time_buf, sizeof(time_buf));
                            printf("Time updated: %s\n", time_buf);
                        } else {
                            printf("Invalid format. Use YYYY-MM-DD HH:MM:SS or a Unix epoch integer.\n");
                        }
                    } else {
                        printf("Time unchanged.\n");
                    }
                    break;
                }
                    
                case 's':
                case 'S':
                    dispenser_get_status();
                    break;
                    
                case 'b':
                case 'B':
                    dispenser_simulate_button_press();
                    break;
                    
                case 'l':
                case 'L':
                    dispenser_list_all_profiles();
                    break;
                    
                case '1':
                    dispenser_switch_to_profile(PROFILE_VITAMIN_D);
                    break;
                    
                case '2':
                    dispenser_switch_to_profile(PROFILE_ASPIRIN);
                    break;
                    
                // Sensor controls
                case 'p':
                case 'P':
                    if (piezo_is_enabled()) {
                        piezo_disable();
                    } else {
                        piezo_enable();
                    }
                    break;
                    
                case 'h':
                case 'H':
                    if (hall_effect_is_enabled()) {
                        hall_effect_disable();
                    } else {
                        hall_effect_enable();
                    }
                    break;
                    
                case 'i':
                case 'I':
                    if (ir_is_enabled()) {
                        ir_disable();
                    } else {
                        ir_enable();
                    }
                    break;
                    
                case 'r':
                case 'R':
                    sensor_interrupts_reset_all();
                    break;
                    
                case 't':
                case 'T':
                    sensor_interrupts_status();
                    break;
                    
                case 'g':
                case 'G':
                    sensor_interrupts_test_gpio_states();
                    break;
                    
                case 'v':
                case 'V':
                    printf("Continuous monitoring of ALL sensor GPIOs (press any key to stop)...\n");
                    while (getchar_timeout_us(100000) == PICO_ERROR_TIMEOUT) {
                        bool gpio5_state = gpio_get(5);  // Piezo
                        bool gpio6_state = gpio_get(6);  // Hall Effect
                        bool gpio7_state = gpio_get(7);  // IR Sensor
                        printf("GPIO5(Piezo): %s | GPIO6(Hall): %s | GPIO7(IR): %s\n", 
                               gpio5_state ? "HIGH" : "LOW", 
                               gpio6_state ? "HIGH" : "LOW",
                               gpio7_state ? "HIGH" : "LOW");
                        sleep_ms(200);
                    }
                    printf("GPIO monitoring stopped.\n");
                    break;
                    
                case 'c':
                case 'C':
                    printf("Checking interrupt configuration...\n");
                    sensor_interrupts_debug_config();
                    break;
                    
                // Motor 1 (main dispenser) manual control
                case 'w':
                case 'W':
                    printf("Motor 1 forward\n");
                    stepper_set_direction(STEPPER_MOTOR_1, STEPPER_FORWARD);
                    break;
                    
                case 'n':
                case 'N':
                    printf("Motor 1 backward\n");
                    stepper_set_direction(STEPPER_MOTOR_1, STEPPER_BACKWARD);
                    break;

                case 'x':
                case 'X':
                    printf("Motor 1 stop (steps: %lu)\n", stepper_get_step_count(STEPPER_MOTOR_1));
                    stepper_stop(STEPPER_MOTOR_1);
                    break;

                // Motor 2 manual control
                case 'a':
                case 'A':
                    printf("Motor 2 forward\n");
                    stepper_set_direction(STEPPER_MOTOR_2, STEPPER_FORWARD);
                    break;

                case 'z':
                case 'Z':
                    printf("Motor 2 backward\n");
                    stepper_set_direction(STEPPER_MOTOR_2, STEPPER_BACKWARD);
                    break;

                case 'e':
                case 'E':
                    printf("Motor 2 stop (steps: %lu)\n", stepper_get_step_count(STEPPER_MOTOR_2));
                    stepper_stop(STEPPER_MOTOR_2);
                    break;

                // Motor 3 manual control
                case 'j':
                case 'J':
                    printf("Motor 3 forward\n");
                    stepper_set_direction(STEPPER_MOTOR_3, STEPPER_FORWARD);
                    break;

                case 'u':
                case 'U':
                    printf("Motor 3 backward\n");
                    stepper_set_direction(STEPPER_MOTOR_3, STEPPER_BACKWARD);
                    break;

                case 'y':
                case 'Y':
                    printf("Motor 3 stop (steps: %lu)\n", stepper_get_step_count(STEPPER_MOTOR_3));
                    stepper_stop(STEPPER_MOTOR_3);
                    break;

                case 'q':
                case 'Q':
                    printf("Shutting down dispenser...\n");
                    stepper_stop(STEPPER_MOTOR_1);
                    stepper_stop(STEPPER_MOTOR_2);
                    stepper_stop(STEPPER_MOTOR_3);
                    return 0;
                    
                default:
                    break;
            }
        }
        
        // Poll ESP UART for incoming commands
        esp_uart_poll();

        // Advance stepper motor one half-step if due
        stepper_task();

        // Small delay to prevent excessive CPU usage
        sleep_ms(1);
    }

    return 0;
}