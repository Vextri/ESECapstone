/**
 * Pill Dispenser Management System Implementation
 */

#include "pill_dispenser.h"
#include "pico_storage.h"
#include "sensor_interrupts.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

static dispense_profile_t profiles[MAX_PROFILES];
static int8_t current_profile_slot = -1;

// Maps a profile slot number to its assigned stepper motor index.
// Slot 0 -> Motor 1, Slot 1 -> Motor 2, Slot 2 -> Motor 3.
// Slots beyond STEPPER_MOTOR_COUNT clamp to the last motor.
static uint8_t slot_to_motor(int8_t slot) {
    if (slot < 0) return STEPPER_MOTOR_1;
    if ((uint8_t)slot >= STEPPER_MOTOR_COUNT) return STEPPER_MOTOR_COUNT - 1;
    return (uint8_t)slot;
}

static bool any_piezo_triggered(void) {
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        if (piezo_get_count_slot(slot) > 0) {
            return true;
        }
    }

    return false;
}

static int8_t first_piezo_triggered_slot(void) {
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        if (piezo_get_count_slot(slot) > 0) {
            return (int8_t)slot;
        }
    }

    return -1;
}

static void format_status_timestamp(char *buffer, size_t buffer_size) {
    uint32_t uptime_seconds = to_ms_since_boot(get_absolute_time()) / 1000;
    uint32_t hours = uptime_seconds / 3600;
    uint32_t minutes = (uptime_seconds % 3600) / 60;
    uint32_t seconds = uptime_seconds % 60;

    snprintf(buffer, buffer_size, "uptime %02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)minutes,
             (unsigned long)seconds);
}

static void send_status_update(const dispense_profile_t *profile,
                               int8_t profile_slot,
                               const char *event,
                               const char *result,
                               const char *notes) {
    char timestamp[32];
    char line[320];
    uint32_t doses_remaining;
    int length;

    if (profile == NULL || !profile->is_active) {
        return;
    }

    format_status_timestamp(timestamp, sizeof(timestamp));
    doses_remaining = profile->pills_per_dose > 0
        ? profile->pills_remaining / profile->pills_per_dose
        : 0;

    length = snprintf(line,
                      sizeof(line),
                      "STATUS|slot=%d|med=%s|left=%d|dose=%d|doses=%lu|last=%s|event=%s|result=%s|notes=%s\n",
                      (int)profile_slot,
                      profile->medication_name,
                      profile->pills_remaining,
                      profile->pills_per_dose,
                      (unsigned long)doses_remaining,
                      timestamp,
                      event,
                      result,
                      notes);

    if (length > 0) {
        uart_write_blocking(ESP_UART, (const uint8_t *)line, (size_t)length);
        printf("[ESP TX] %s", line);
    }
}

void dispenser_init(void) {
    uart_init(ESP_UART, ESP_UART_BAUD);
    gpio_set_function(ESP_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(ESP_UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(ESP_UART, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(ESP_UART, false, false);

    stepper_init();
    
    // Initialize all profile slots as empty
    for (int i = 0; i < MAX_PROFILES; i++) {
        memset(&profiles[i], 0, sizeof(profiles[i]));
        profiles[i].is_active = false;
    }
    current_profile_slot = -1;

    // Attempt to restore slots 0-2 from flash
    if (flash_profiles_load(profiles)) {
        for (int i = 0; i < FLASH_PROFILE_SLOT_COUNT; i++) {
            if (profiles[i].is_active) {
                printf("  Restored slot %d: %s (%d pills)\n",
                       i, profiles[i].medication_name, profiles[i].pills_remaining);
                if (current_profile_slot < 0) {
                    current_profile_slot = i;
                }
            }
        }
    } else {
        printf("  No saved profiles — use 'M' command or ESP LOAD_PROFILE to add profiles\n");
    }

    printf("Pill Dispenser System Initialized\n");
    printf("Profile slots available: %d\n", MAX_PROFILES);

    // Broadcast all active slots to the ESP so it can sync its own records
    for (int i = 0; i < FLASH_PROFILE_SLOT_COUNT; i++) {
        if (profiles[i].is_active) {
            char line[320];
            uint32_t doses = profiles[i].pills_per_dose > 0
                ? profiles[i].pills_remaining / profiles[i].pills_per_dose
                : 0;
            // Build schedule string e.g. "08:00,20:00" (empty string if no schedule)
            char sched_str[48] = "none";
            if (profiles[i].schedule_count > 0) {
                int spos = 0;
                for (uint8_t s = 0; s < profiles[i].schedule_count && spos < (int)sizeof(sched_str) - 6; s++) {
                    if (s > 0) sched_str[spos++] = ',';
                    spos += snprintf(sched_str + spos, sizeof(sched_str) - spos,
                                     "%02u:%02u",
                                     profiles[i].schedule_times_mins[s] / 60,
                                     profiles[i].schedule_times_mins[s] % 60);
                }
            }
            int len = snprintf(line, sizeof(line),
                "BOOT_SYNC|slot=%d|med=%s|left=%d|dose=%d|doses=%lu|schedule=%s\n",
                i,
                profiles[i].medication_name,
                profiles[i].pills_remaining,
                profiles[i].pills_per_dose,
                (unsigned long)doses,
                sched_str);
            if (len > 0) {
                uart_write_blocking(ESP_UART, (const uint8_t *)line, (size_t)len);
                printf("[ESP TX] %s", line);
            }
        }
    }
}

void dispenser_load_profile_to_slot(uint8_t slot, const char* med_name, uint8_t total_pills, uint8_t per_dose, uint32_t time_per_pill) {
    if (slot >= MAX_PROFILES) {
        printf("ERROR: Invalid profile slot %d (max %d)\n", slot, MAX_PROFILES - 1);
        return;
    }
    
    strncpy(profiles[slot].medication_name, med_name, sizeof(profiles[slot].medication_name) - 1);
    profiles[slot].medication_name[sizeof(profiles[slot].medication_name) - 1] = '\0';
    profiles[slot].pills_remaining = total_pills;
    profiles[slot].pills_per_dose = per_dose;
    profiles[slot].dispense_time_ms = time_per_pill;
    profiles[slot].is_active = true;
    // Clear schedule on profile load; use dispenser_set_profile_schedule to set it
    profiles[slot].schedule_count = 0;
    memset(profiles[slot].schedule_times_mins, 0, sizeof(profiles[slot].schedule_times_mins));

    printf("Profile loaded to slot %d: %s\n", slot, med_name);
    printf("Total Pills: %d, Per Dose: %d, Time per Pill: %dms\n",
           total_pills, per_dose, time_per_pill);

    // Auto-select this slot for manual commands if nothing is currently selected
    if (current_profile_slot < 0) {
        current_profile_slot = slot;
        printf("Slot %d auto-selected for manual commands\n", slot);
    }

    // Persist to flash immediately
    if (slot < FLASH_PROFILE_SLOT_COUNT) {
        flash_profiles_save(profiles);
    }
}

void dispenser_set_profile_schedule(uint8_t slot, const uint16_t *times_mins, uint8_t count) {
    if (slot >= MAX_PROFILES || !profiles[slot].is_active) {
        printf("ERROR: dispenser_set_profile_schedule: invalid or inactive slot %d\n", slot);
        return;
    }
    if (count > MAX_DOSES_PER_DAY) {
        count = MAX_DOSES_PER_DAY;
    }
    profiles[slot].schedule_count = count;
    memcpy(profiles[slot].schedule_times_mins, times_mins, count * sizeof(uint16_t));

    printf("Schedule set for slot %d (%d time(s)):\n", slot, count);
    for (uint8_t i = 0; i < count; i++) {
        printf("  %02u:%02u\n",
               profiles[slot].schedule_times_mins[i] / 60,
               profiles[slot].schedule_times_mins[i] % 60);
    }

    if (slot < FLASH_PROFILE_SLOT_COUNT) {
        flash_profiles_save(profiles);
    }
}

bool dispenser_switch_to_profile(uint8_t slot) {
    if (slot >= MAX_PROFILES) {
        printf("ERROR: Invalid profile slot %d (max %d)\n", slot, MAX_PROFILES - 1);
        return false;
    }
    
    if (!profiles[slot].is_active) {
        printf("ERROR: Profile slot %d is empty\n", slot);
        return false;
    }
    
    current_profile_slot = slot;
    printf("Selected slot %d for manual commands: %s\n", slot, profiles[slot].medication_name);
    printf("Pills remaining: %d, Pills per dose: %d\n",
           profiles[slot].pills_remaining, profiles[slot].pills_per_dose);
    return true;
}

int8_t dispenser_get_current_profile_slot(void) {
    return current_profile_slot;
}

void dispenser_list_all_profiles(void) {
    printf("\n=== ALL PROFILES ===\n");
    for (int i = 0; i < MAX_PROFILES; i++) {
        printf("Slot %d: ", i);
        if (profiles[i].is_active) {
            printf("%s - %d pills, %d per dose",
                   profiles[i].medication_name,
                   profiles[i].pills_remaining,
                   profiles[i].pills_per_dose);
            if (profiles[i].schedule_count > 0) {
                printf(" [SCHEDULED: ");
                for (uint8_t s = 0; s < profiles[i].schedule_count; s++) {
                    if (s > 0) printf(",");
                    printf("%02u:%02u",
                           profiles[i].schedule_times_mins[s] / 60,
                           profiles[i].schedule_times_mins[s] % 60);
                }
                printf("]");
            } else {
                printf(" [MANUAL ONLY]");
            }
            if (i == current_profile_slot) {
                printf(" <-- selected");
            }
            printf("\n");
        } else {
            printf("[EMPTY]\n");
        }
    }
    printf("===================\n\n");
}

void dispenser_load_profile(const char* med_name, uint8_t total_pills, uint8_t per_dose, uint32_t time_per_pill) {
    // Legacy function - loads to slot 0 and switches to it
    dispenser_load_profile_to_slot(0, med_name, total_pills, per_dose, time_per_pill);
    dispenser_switch_to_profile(0);
}

bool dispenser_execute_dose(void) {
    if (current_profile_slot < 0 || !profiles[current_profile_slot].is_active) {
        printf("ERROR: No active dispense profile selected!\n");
        return false;
    }
    
    dispense_profile_t* profile = &profiles[current_profile_slot];
    
    if (profile->pills_remaining < profile->pills_per_dose) {
        printf("ERROR: Insufficient pills remaining (%d needed, %d available)\n", 
               profile->pills_per_dose, profile->pills_remaining);
        send_status_update(profile,
                           current_profile_slot,
                           "Dispense rejected",
                           "fail",
                           "Not enough pills for timed dispense");
        return false;
    }
    
    printf("Dispensing %d pills of %s (Slot %d)...\n", 
           profile->pills_per_dose, profile->medication_name, current_profile_slot);
    
    // Dispense each pill individually
    for (int i = 0; i < profile->pills_per_dose; i++) {
        printf("Dispensing pill %d/%d\n", i + 1, profile->pills_per_dose);
        
        uint8_t motor_idx = slot_to_motor(current_profile_slot);
        stepper_set_direction(motor_idx, STEPPER_FORWARD);
        uint32_t step_end = to_ms_since_boot(get_absolute_time()) + profile->dispense_time_ms;
        while (to_ms_since_boot(get_absolute_time()) < step_end) {
            stepper_task();
            sleep_ms(1);
        }
        stepper_stop(motor_idx);
        
        profile->pills_remaining--;
        
        // Small pause between pills
        sleep_ms(500);
    }
    
    printf("Dose complete! Pills remaining: %d\n", profile->pills_remaining);
    send_status_update(profile,
                       current_profile_slot,
                       "Dispense complete",
                       "ok",
                       "Timed dispense completed");
    return true;
}

void dispenser_get_status(void) {
    if (current_profile_slot < 0 || !profiles[current_profile_slot].is_active) {
        printf("No slot selected for manual commands\n");
        dispenser_list_all_profiles();
        return;
    }

    dispense_profile_t* profile = &profiles[current_profile_slot];

    printf("\n=== STATUS ===\n");
    printf("Selected Slot: %d (target for manual dispense commands)\n", current_profile_slot);
    printf("Medication: %s\n", profile->medication_name);
    printf("Pills Remaining: %d\n", profile->pills_remaining);
    printf("Pills per Dose: %d\n", profile->pills_per_dose);
    printf("Possible Doses Remaining: %d\n",
           profile->pills_remaining / profile->pills_per_dose);
    if (profile->schedule_count > 0) {
        printf("Schedule: ");
        for (uint8_t i = 0; i < profile->schedule_count; i++) {
            if (i > 0) printf(", ");
            printf("%02u:%02u",
                   profile->schedule_times_mins[i] / 60,
                   profile->schedule_times_mins[i] % 60);
        }
        printf("\n");
    } else {
        printf("Schedule: none\n");
    }
    printf("==============\n");

    // Also show summary of all loaded slots
    printf("\n--- All Slots ---\n");
    for (int i = 0; i < MAX_PROFILES; i++) {
        if (profiles[i].is_active) {
            if (profiles[i].schedule_count > 0) {
                printf("  Slot %d: %s — %d pills [SCHEDULED]",
                       i, profiles[i].medication_name, profiles[i].pills_remaining);
                printf(" ");
                for (uint8_t s = 0; s < profiles[i].schedule_count; s++) {
                    if (s > 0) printf(",");
                    printf("%02u:%02u",
                           profiles[i].schedule_times_mins[s] / 60,
                           profiles[i].schedule_times_mins[s] % 60);
                }
            } else {
                printf("  Slot %d: %s — %d pills [MANUAL ONLY]",
                       i, profiles[i].medication_name, profiles[i].pills_remaining);
            }
            if (i == current_profile_slot) printf(" <-- selected");
            printf("\n");
        }
    }
    printf("-----------------\n");
}

uint8_t dispenser_get_remaining_pills(void) {
    if (current_profile_slot < 0 || !profiles[current_profile_slot].is_active) {
        return 0;
    }
    return profiles[current_profile_slot].pills_remaining;
}

void dispenser_simulate_button_press(void) {
    printf("\n>>> BUTTON PRESS SIMULATED <<<\n");
    printf("Using sensor-based dispensing...\n");
    dispenser_execute_dose_sensor_based();
}

void dispenser_test_mode(void) {
    printf("\n=== PILL DISPENSER TEST MODE ===\n");
    printf("Commands:\n");
    printf("L - Load test profile\n");
    printf("D - Dispense dose\n");
    printf("S - Show status\n");
    printf("B - Simulate button press\n");
    printf("Q - Quit test mode\n");
    printf("===============================\n");
}

bool dispenser_dispense_single_pill_sensor_based(uint32_t timeout_ms, uint8_t motor_idx) {
    printf("Starting sensor-based pill dispense on Motor %d (timeout: %dms)...\n",
           motor_idx + 1, timeout_ms);

    // Ensure piezo and IR are enabled for this slot; hall effect is shared
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        if (!piezo_is_enabled_slot(slot)) {
            piezo_enable_slot(slot);
        }
    }
    if (!ir_is_enabled_slot(motor_idx)) ir_enable_slot(motor_idx);
    if (!hall_effect_is_enabled()) hall_effect_enable();

    const int MAX_RETRIES = 3;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (attempt > 1) {
            printf("--- Retry attempt %d/%d ---\n", attempt, MAX_RETRIES);
            printf("Reversing Motor %d for 1 second before retry...\n", motor_idx + 1);
            stepper_set_direction(motor_idx, STEPPER_BACKWARD);
            uint32_t rev_end = to_ms_since_boot(get_absolute_time()) + 1000;
            while (to_ms_since_boot(get_absolute_time()) < rev_end) {
                stepper_task();
                sleep_ms(1);
            }
            stepper_stop(motor_idx);
            sleep_ms(500);
        }

        // Reset counts before each attempt so only new events count
        sensor_interrupts_reset_all();
        ir_reset_count_slot(motor_idx);
        hall_effect_reset_count();

        uint32_t start_time = to_ms_since_boot(get_absolute_time());
        bool piezo_seen = false;
        bool ir_seen = false;

        // Start stepper - piezo will stop it when pill impact is detected
        printf("Motor %d starting (attempt %d)...\n", motor_idx + 1, attempt);
        stepper_set_direction(motor_idx, STEPPER_FORWARD);

        // Wait for both sensors to confirm the pill.
        // The pill may pass the IR beam before the piezo fires, so either order is valid.
        while (true) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (now - start_time > timeout_ms) {
                printf("TIMEOUT: Sensors did not confirm pill within %dms (attempt %d/%d)\n",
                       timeout_ms, attempt, MAX_RETRIES);
                stepper_stop(motor_idx);
                break;  // Treat as failed attempt, retry up to MAX_RETRIES
            }

            if (!piezo_seen && any_piezo_triggered()) {
                piezo_seen = true;
                stepper_stop(motor_idx);
                int8_t piezo_slot = first_piezo_triggered_slot();
                if (piezo_slot >= 0) {
                    printf("Pill impact detected by piezo slot %d - Motor %d stopped.\n",
                           piezo_slot, motor_idx + 1);
                } else {
                    printf("Pill impact detected by piezo sensor - Motor %d stopped.\n",
                           motor_idx + 1);
                }
            }

            if (!ir_seen && ir_get_count_slot(motor_idx) > 0) {
                ir_seen = true;
                printf("Pill passed IR beam on Motor %d.\n", motor_idx + 1);
            }

            if (piezo_seen && ir_seen) {
                break;
            }

            stepper_task();
            sleep_ms(1);
        }

        // Wait for IR interrupt to register - piezo and IR fire near-simultaneously
        // so we need enough time for the IRQ handler to increment the count
        sleep_ms(300);

        uint32_t hall_triggers = hall_effect_get_count();
        printf("Hall effect triggers for this pill: %lu\n", (unsigned long)hall_triggers);

         // IR confirms pill passed through chute
         bool ir_ok = ir_seen || (ir_get_count_slot(motor_idx) > 0);

         printf("Pill check: Piezo=%s | IR=%s\n",
             piezo_seen ? "TRIGGERED" : "NO SIGNAL",
             ir_ok ? "TRIGGERED" : "NO SIGNAL");

         if (piezo_seen && ir_ok) {
            printf("SUCCESS: Pill confirmed dispensed!\n");
            return true;
        }

         printf("WARNING: Pill was not confirmed by both sensors - retrying...\n");
    }

    printf("ERROR: Pill failed to dispense after %d attempts\n", MAX_RETRIES);
    return false;
}

bool dispenser_execute_dose_sensor_based(void) {
    if (current_profile_slot < 0 || !profiles[current_profile_slot].is_active) {
        printf("ERROR: No active dispense profile selected!\n");
        return false;
    }
    
    dispense_profile_t* profile = &profiles[current_profile_slot];
    
    if (profile->pills_remaining < profile->pills_per_dose) {
        printf("ERROR: Insufficient pills remaining (%d needed, %d available)\n", 
               profile->pills_per_dose, profile->pills_remaining);
        send_status_update(profile,
                           current_profile_slot,
                           "Dispense rejected",
                           "fail",
                           "Not enough pills for sensor dispense");
        return false;
    }
    
    printf("\n=== SENSOR-BASED DOSE DISPENSING ===\n");
    printf("Dispensing %d pills of %s (Slot %d)...\n", 
           profile->pills_per_dose, profile->medication_name, current_profile_slot);
    uint8_t motor_idx = slot_to_motor(current_profile_slot);
    printf("Motor %d assigned to slot %d.\n", motor_idx + 1, current_profile_slot);
    printf("Motor runs until piezo detects impact, IR confirms pill dispensed.\n\n");

    // Dispense each pill using sensor feedback
    bool all_pills_dispensed = true;
    int pills_dispensed = 0;
    for (int i = 0; i < profile->pills_per_dose; i++) {
        printf("--- Dispensing pill %d/%d ---\n", i + 1, profile->pills_per_dose);
        
        // Use sensor-based dispensing with 10 second timeout per pill
        if (dispenser_dispense_single_pill_sensor_based(10000, motor_idx)) {
            profile->pills_remaining--;
            pills_dispensed++;
            printf("Pill %d successfully dispensed! Remaining: %d\n", 
                   i + 1, profile->pills_remaining);
        } else {
            printf("ERROR: Failed to dispense pill %d\n", i + 1);
            all_pills_dispensed = false;
            break;  // Stop trying to dispense more pills
        }
        
        // Pause between pills if dispensing multiple
        if (i < profile->pills_per_dose - 1) {
            printf("Pausing between pills...\n\n");
            sleep_ms(1000);  // 1 second pause
        }
    }
    
    if (all_pills_dispensed) {
        printf("\n✓ DOSE COMPLETE! All %d pills dispensed successfully\n", profile->pills_per_dose);
        printf("Pills remaining in profile: %d\n", profile->pills_remaining);
        send_status_update(profile,
                           current_profile_slot,
                           "Dispense complete",
                           "ok",
                           "Sensor dispense completed");
    } else {
        printf("\n⚠ DOSE INCOMPLETE! Some pills failed to dispense\n");
        char failure_notes[96];

        snprintf(failure_notes,
                 sizeof(failure_notes),
                 "Sensor dispense incomplete: %d of %d pills dispensed",
                 pills_dispensed,
                 profile->pills_per_dose);
        send_status_update(profile,
                           current_profile_slot,
                           "Dispense incomplete",
                           "fail",
                           failure_notes);
    }
    
    printf("====================================\n\n");
    return all_pills_dispensed;
}