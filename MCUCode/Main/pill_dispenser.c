/**
 * Pill Dispenser Management System Implementation
 */

#include "pill_dispenser.h"
#include "sensor_interrupts.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

static dispense_profile_t profiles[MAX_PROFILES];
static int8_t current_profile_slot = -1;

void dispenser_init(void) {
    motor_init();
    
    // Initialize all profile slots as empty
    for (int i = 0; i < MAX_PROFILES; i++) {
        memset(&profiles[i], 0, sizeof(profiles[i]));
        profiles[i].is_active = false;
    }
    current_profile_slot = -1;
    
    printf("Pill Dispenser System Initialized\n");
    printf("Profile slots available: %d\n", MAX_PROFILES);
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
    
    printf("Profile loaded to slot %d: %s\n", slot, med_name);
    printf("Total Pills: %d, Per Dose: %d, Time per Pill: %dms\n", 
           total_pills, per_dose, time_per_pill);
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
    printf("Switched to profile slot %d: %s\n", slot, profiles[slot].medication_name);
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
            if (i == current_profile_slot) {
                printf(" [ACTIVE]");
            }
            printf("\n");
        } else {
            printf("[EMPTY]\n");
        }
    }
    printf("==================\n\n");
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
        return false;
    }
    
    printf("Dispensing %d pills of %s (Slot %d)...\n", 
           profile->pills_per_dose, profile->medication_name, current_profile_slot);
    
    // Dispense each pill individually
    for (int i = 0; i < profile->pills_per_dose; i++) {
        printf("Dispensing pill %d/%d\n", i + 1, profile->pills_per_dose);
        
        motor_forward();
        sleep_ms(profile->dispense_time_ms);
        motor_stop();
        
        profile->pills_remaining--;
        
        // Small pause between pills
        sleep_ms(500);
    }
    
    printf("Dose complete! Pills remaining: %d\n", profile->pills_remaining);
    return true;
}

void dispenser_get_status(void) {
    if (current_profile_slot < 0 || !profiles[current_profile_slot].is_active) {
        printf("No active medication profile selected\n");
        dispenser_list_all_profiles();
        return;
    }
    
    dispense_profile_t* profile = &profiles[current_profile_slot];
    
    printf("\n=== CURRENT PROFILE STATUS ===\n");
    printf("Active Slot: %d\n", current_profile_slot);
    printf("Medication: %s\n", profile->medication_name);
    printf("Pills Remaining: %d\n", profile->pills_remaining);
    printf("Pills per Dose: %d\n", profile->pills_per_dose);
    printf("Possible Doses Remaining: %d\n", 
           profile->pills_remaining / profile->pills_per_dose);
    printf("=============================\n\n");
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

bool dispenser_dispense_single_pill_sensor_based(uint32_t timeout_ms) {
    printf("Starting sensor-based pill dispense (timeout: %dms)...\n", timeout_ms);

    // Ensure piezo and IR are enabled
    if (!piezo_is_enabled()) piezo_enable();
    if (!ir_is_enabled()) ir_enable();

    const int MAX_RETRIES = 3;

    for (int attempt = 1; attempt <= MAX_RETRIES; attempt++) {
        if (attempt > 1) {
            printf("--- Retry attempt %d/%d ---\n", attempt, MAX_RETRIES);
            sleep_ms(500);
        }

        // Reset counts before each attempt so only new events count
        piezo_reset_count();
        ir_reset_count();

        uint32_t start_time = to_ms_since_boot(get_absolute_time());

        // Start motor - piezo will stop it when pill impact is detected
        printf("Motor starting (attempt %d)...\n", attempt);
        motor_forward();

        // Wait for piezo to detect pill impact
        while (true) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if (now - start_time > timeout_ms) {
                printf("TIMEOUT: Piezo did not trigger within %dms (attempt %d/%d)\n",
                       timeout_ms, attempt, MAX_RETRIES);
                motor_stop();
                break;  // Treat as failed attempt, retry up to MAX_RETRIES
            }

            if (piezo_get_count() > 0) {
                motor_stop();
                printf("Pill impact detected - motor stopped.\n");
                break;
            }

            sleep_ms(5);
        }

        // Wait for IR interrupt to register - piezo and IR fire near-simultaneously
        // so we need enough time for the IRQ handler to increment the count
        sleep_ms(300);

        // IR confirms pill passed through chute
        bool ir_ok = ir_get_count() > 0;

        printf("Pill check: Piezo=TRIGGERED | IR=%s\n",
               ir_ok ? "TRIGGERED" : "NO SIGNAL");

        if (ir_ok) {
            printf("SUCCESS: Pill confirmed dispensed!\n");
            return true;
        }

        printf("WARNING: IR did not confirm pill - retrying...\n");
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
        return false;
    }
    
    printf("\n=== SENSOR-BASED DOSE DISPENSING ===\n");
    printf("Dispensing %d pills of %s (Slot %d)...\n", 
           profile->pills_per_dose, profile->medication_name, current_profile_slot);
    printf("Motor runs until piezo detects impact, IR confirms pill dispensed.\n\n");

    // Dispense each pill using sensor feedback
    bool all_pills_dispensed = true;
    for (int i = 0; i < profile->pills_per_dose; i++) {
        printf("--- Dispensing pill %d/%d ---\n", i + 1, profile->pills_per_dose);
        
        // Use sensor-based dispensing with 10 second timeout per pill
        if (dispenser_dispense_single_pill_sensor_based(10000)) {
            profile->pills_remaining--;
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
    } else {
        printf("\n⚠ DOSE INCOMPLETE! Some pills failed to dispense\n");
    }
    
    printf("====================================\n\n");
    return all_pills_dispensed;
}