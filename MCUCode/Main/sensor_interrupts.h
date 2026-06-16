/**
 * Sensor Interrupt Management for Pill Dispenser
 * 
 * Handles two types of sensors:
 * - Piezo sensor: Detects vibration/impact when pills drop
 * - Hall effect sensor: Detects magnetic field changes (motor position/rotation)
 */

#ifndef SENSOR_INTERRUPTS_H
#define SENSOR_INTERRUPTS_H

#include "pico/stdlib.h"
#include <stdbool.h>

// Number of dispenser slots with piezo + IR sensors
#define SENSOR_SLOT_COUNT 3

// Per-slot piezo pin definitions
#define PIEZO_PIN_SLOT0 5       // GPIO pin for slot 0 piezo sensor
#define PIEZO_PIN_SLOT1 16      // GPIO pin for slot 1 piezo sensor
#define PIEZO_PIN_SLOT2 18      // GPIO pin for slot 2 piezo sensor

// Per-slot IR pin definitions
#define IR_PIN_SLOT0 7          // GPIO pin for slot 0 IR sensor
#define IR_PIN_SLOT1 17         // GPIO pin for slot 1 IR sensor
#define IR_PIN_SLOT2 19         // GPIO pin for slot 2 IR sensor

// Legacy single-slot aliases (slot 0) — kept for backward compatibility
#define PIEZO_PIN       PIEZO_PIN_SLOT0
#define IR_PIN          IR_PIN_SLOT0

// Hall effect sensor (shared across all slots)
#define HALL_EFFECT_PIN 6       // GPIO pin for hall effect sensor

// Debounce settings
#define PIEZO_DEBOUNCE_MS 100   // Debounce time for piezo (vibration settling)
#define HALL_DEBOUNCE_MS 10     // Debounce time for hall effect (fast response)
#define IR_DEBOUNCE_MS 50       // Debounce time for IR sensor (beam break detection)

// Callback function types
typedef void (*piezo_callback_t)(void);
typedef void (*hall_effect_callback_t)(void);
typedef void (*ir_callback_t)(void);

/**
 * Initialize both sensor interrupt systems
 */
void sensor_interrupts_init(void);

// === PIEZO SENSOR FUNCTIONS ===

/**
 * Set callback function for piezo sensor interrupts
 * @param callback Function to call when piezo detects vibration/impact
 */
void piezo_set_callback(piezo_callback_t callback);

/**
 * Enable piezo sensor interrupts
 */
void piezo_enable(void);

/**
 * Disable piezo sensor interrupts
 */
void piezo_disable(void);

/**
 * Check if piezo sensor is enabled
 * @return true if enabled, false if disabled
 */
bool piezo_is_enabled(void);

/**
 * Get total number of piezo triggers since last reset
 * @return Number of piezo triggers detected
 */
uint32_t piezo_get_count(void);

/**
 * Reset piezo trigger counter to zero
 */
void piezo_reset_count(void);

/**
 * Get time of last piezo trigger
 * @return Timestamp in milliseconds of last trigger
 */
uint32_t piezo_get_last_trigger_time(void);

// === HALL EFFECT SENSOR FUNCTIONS ===

/**
 * Set callback function for hall effect sensor interrupts
 * @param callback Function to call when hall effect sensor triggers
 */
void hall_effect_set_callback(hall_effect_callback_t callback);

/**
 * Enable hall effect sensor interrupts
 */
void hall_effect_enable(void);

/**
 * Disable hall effect sensor interrupts
 */
void hall_effect_disable(void);

/**
 * Check if hall effect sensor is enabled
 * @return true if enabled, false if disabled
 */
bool hall_effect_is_enabled(void);

/**
 * Get total number of hall effect triggers since last reset
 * @return Number of hall effect triggers detected
 */
uint32_t hall_effect_get_count(void);

/**
 * Reset hall effect trigger counter to zero
 */
void hall_effect_reset_count(void);

/**
 * Get time of last hall effect trigger
 * @return Timestamp in milliseconds of last trigger
 */
uint32_t hall_effect_get_last_trigger_time(void);

// === IR SENSOR FUNCTIONS ===

/**
 * Set callback function for IR sensor interrupts
 * @param callback Function to call when IR sensor beam is broken/restored
 */
void ir_set_callback(ir_callback_t callback);

/**
 * Enable IR sensor interrupts
 */
void ir_enable(void);

/**
 * Disable IR sensor interrupts
 */
void ir_disable(void);

/**
 * Check if IR sensor is enabled
 * @return true if enabled, false if disabled
 */
bool ir_is_enabled(void);

/**
 * Get total number of IR sensor triggers since last reset
 * @return Number of IR sensor triggers detected
 */
uint32_t ir_get_count(void);

/**
 * Reset IR sensor trigger counter to zero
 */
void ir_reset_count(void);

/**
 * Get time of last IR sensor trigger
 * @return Timestamp in milliseconds of last trigger
 */
uint32_t ir_get_last_trigger_time(void);

/**
 * Get current state of IR beam (slot 0)
 * @return true if beam is broken, false if beam is clear
 */
bool ir_is_beam_broken(void);

// === SLOT-INDEXED PIEZO FUNCTIONS (slots 0-2) ===

void piezo_set_callback_slot(uint8_t slot, piezo_callback_t callback);
void piezo_enable_slot(uint8_t slot);
void piezo_disable_slot(uint8_t slot);
bool piezo_is_enabled_slot(uint8_t slot);
uint32_t piezo_get_count_slot(uint8_t slot);
void piezo_reset_count_slot(uint8_t slot);
uint32_t piezo_get_last_trigger_time_slot(uint8_t slot);

// === SLOT-INDEXED IR FUNCTIONS (slots 0-2) ===

void ir_set_callback_slot(uint8_t slot, ir_callback_t callback);
void ir_enable_slot(uint8_t slot);
void ir_disable_slot(uint8_t slot);
bool ir_is_enabled_slot(uint8_t slot);
uint32_t ir_get_count_slot(uint8_t slot);
void ir_reset_count_slot(uint8_t slot);
bool ir_is_beam_broken_slot(uint8_t slot);

// === UTILITY FUNCTIONS ===

/**
 * Display status of both sensors
 */
void sensor_interrupts_status(void);

/**
 * Reset both sensor counters
 */
void sensor_interrupts_reset_all(void);

/**
 * Disable all sensor interrupts
 */
void sensor_interrupts_disable_all(void);

/**
 * Enable all sensor interrupts
 */
void sensor_interrupts_enable_all(void);

/**
 * Test GPIO states and interrupt functionality
 */
void sensor_interrupts_test_gpio_states(void);

/**
 * Debug interrupt configuration and GPIO setup
 */
void sensor_interrupts_debug_config(void);

#endif // SENSOR_INTERRUPTS_H