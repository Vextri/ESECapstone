/**
 * Pill Dispenser Management System
 * 
 * Provides high-level pill dispensing functionality with profiles,
 * dosage tracking, and simulation capabilities for prototyping
 */

#ifndef PILL_DISPENSER_H
#define PILL_DISPENSER_H

#include "motor_control.h"
#include <stdint.h>
#include <stdbool.h>

// Dispense profile structure
typedef struct {
    char medication_name[32];
    uint8_t pills_remaining;
    uint8_t pills_per_dose;
    uint32_t dispense_time_ms;  // How long to run motor per pill
    bool is_active;             // Whether this profile slot is in use
} dispense_profile_t;

// Profile management constants
#define MAX_PROFILES 5
#define PROFILE_VITAMIN_D 0
#define PROFILE_ASPIRIN 1
#define PROFILE_SLOT_2 2
#define PROFILE_SLOT_3 3
#define PROFILE_SLOT_4 4

/**
 * Initialize pill dispenser system
 * Sets up motor control and dispenser state
 */
void dispenser_init(void);

/**
 * Load a medication profile into a specific slot
 * @param slot Profile slot number (0-4)
 * @param med_name Name of the medication
 * @param total_pills Total number of pills loaded
 * @param per_dose Number of pills per dose
 * @param time_per_pill Motor runtime per pill in milliseconds
 */
void dispenser_load_profile_to_slot(uint8_t slot, const char* med_name, uint8_t total_pills, uint8_t per_dose, uint32_t time_per_pill);

/**
 * Switch to a different profile slot
 * @param slot Profile slot number to switch to (0-4)
 * @return true if switch was successful, false if slot is empty
 */
bool dispenser_switch_to_profile(uint8_t slot);

/**
 * Get the currently active profile slot number
 * @return Current profile slot number, or -1 if no profile active
 */
int8_t dispenser_get_current_profile_slot(void);

/**
 * Display all available profiles and their status
 */
void dispenser_list_all_profiles(void);

/**
 * Load a medication profile into the dispenser (legacy function - uses slot 0)
 * @param med_name Name of the medication
 * @param total_pills Total number of pills loaded
 * @param per_dose Number of pills per dose
 * @param time_per_pill Motor runtime per pill in milliseconds
 */
void dispenser_load_profile(const char* med_name, uint8_t total_pills, uint8_t per_dose, uint32_t time_per_pill);

/**
 * Execute a complete dose based on current profile
 * @return true if dose was successfully dispensed, false if error
 */
bool dispenser_execute_dose(void);

/**
 * Display current dispenser status and profile information
 */
void dispenser_get_status(void);

/**
 * Get number of pills remaining in current profile
 * @return Number of pills remaining
 */
uint8_t dispenser_get_remaining_pills(void);

/**
 * Simulate a button press for testing
 * Executes a dose and provides feedback
 */
void dispenser_simulate_button_press(void);

/**
 * Display test mode help information
 */
void dispenser_test_mode(void);

#endif // PILL_DISPENSER_H