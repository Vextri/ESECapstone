/**
 * Pill Dispenser Management System Implementation
 */

#include "pill_dispenser.h"
#include "pico/stdlib.h"
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
    dispenser_execute_dose();
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