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

// Pin definitions
#define PIEZO_PIN 5             // GPIO pin for piezo sensor
#define HALL_EFFECT_PIN 6       // GPIO pin for hall effect sensor
#define IR_PIN 7                // GPIO pin for IR sensor

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
 * Get current state of IR beam
 * @return true if beam is broken, false if beam is clear
 */
bool ir_is_beam_broken(void);

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