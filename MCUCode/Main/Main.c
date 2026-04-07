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
#include "motor_control.h"
#include "pill_dispenser.h"
#include "sensor_interrupts.h"
#include "stepper_control.h"
#include <stdio.h>

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
    
    // Initialize pill dispenser system (includes motor init)
    dispenser_init();

    // Initialize stepper motor
    stepper_init();
    
    // Initialize sensor interrupt system
    sensor_interrupts_init();
    
    // Set up sensor callbacks
    piezo_set_callback(on_piezo_detected);
    hall_effect_set_callback(on_hall_effect_detected);
    ir_set_callback(on_ir_detected);
    
    // Load test profiles for prototyping
    dispenser_load_profile_to_slot(PROFILE_VITAMIN_D, "Vitamin D", 15, 2, 1000); // 15 pills, 2 per dose, 1 second per pill
    dispenser_load_profile_to_slot(PROFILE_ASPIRIN, "Aspirin", 20, 1, 800);     // 20 pills, 1 per dose, 0.8 seconds per pill
    
    // Start with Vitamin D as active profile
    dispenser_switch_to_profile(PROFILE_VITAMIN_D);
    
    printf("\n=== PILL DISPENSER PROTOTYPE ===\n");
    printf("Commands:\n");
    printf("D - Dispense dose (timed)\n");
    printf("F - Dispense dose (sensor feedback)\n");
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
    printf("--- Manual Motor Control ---\n");
    printf("W - Motor forward\n");
    printf("N - Motor backward\n");
    printf("X - Motor stop\n");
    printf("--- Stepper Motor Control ---\n");
    printf("A - Stepper forward (continuous)\n");
    printf("Z - Stepper backward (continuous)\n");
    printf("E - Stepper stop\n");
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
                    
                // Manual motor control for testing/debugging
                case 'w':
                case 'W':
                    printf("Manual motor forward\n");
                    motor_forward();
                    break;
                    
                case 'n':
                case 'N':
                    printf("Manual motor backward\n");
                    motor_backward();
                    break;

                case 'x':
                case 'X':
                    printf("Manual motor stop\n");
                    motor_stop();
                    break;

                // Stepper motor control
                case 'a':
                case 'A':
                    printf("Stepper forward\n");
                    stepper_set_direction(STEPPER_FORWARD);
                    break;

                case 'z':
                case 'Z':
                    printf("Stepper backward\n");
                    stepper_set_direction(STEPPER_BACKWARD);
                    break;

                case 'e':
                case 'E':
                    printf("Stepper stop (steps: %lu)\n", stepper_get_step_count());
                    stepper_stop();
                    break;

                case 'q':
                case 'Q':
                    printf("Shutting down dispenser...\n");
                    motor_stop();
                    return 0;
                    
                default:
                    break;
            }
        }
        
        // Advance stepper motor one half-step if due
        stepper_task();

        // Small delay to prevent excessive CPU usage
        sleep_ms(1);
    }

    return 0;
}