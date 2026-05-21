/**
 * Stepper Motor Control for Raspberry Pi Pico
 *
 * Half-step bipolar control via DRV8833 dual H-bridge driver.
 * Each H-bridge drives one coil of a bipolar stepper motor.
 *
 * Wiring:
 *   GPIO8  -> DRV8833 AIN1  (Coil A, terminal 1)
 *   GPIO9  -> DRV8833 AIN2  (Coil A, terminal 2)
 *   GPIO10 -> DRV8833 BIN1  (Coil B, terminal 1)
 *   GPIO11 -> DRV8833 BIN2  (Coil B, terminal 2)
 */

#ifndef STEPPER_CONTROL_H
#define STEPPER_CONTROL_H

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

// Pin definitions - using free GPIOs not occupied by DC motor or sensors
#define STEPPER_AIN1_PIN    8   // DRV8833 AIN1
#define STEPPER_AIN2_PIN    9   // DRV8833 AIN2
#define STEPPER_BIN1_PIN    10  // DRV8833 BIN1
#define STEPPER_BIN2_PIN    11  // DRV8833 BIN2

// Step timing: microseconds between each full-step
// 2000us = ~500 full-steps/sec (~4 sec/revolution on 28BYJ-48)
#define STEPPER_STEP_DELAY_US 2000

// Direction values
typedef enum {
    STEPPER_IDLE,
    STEPPER_FORWARD,
    STEPPER_BACKWARD
} stepper_direction_t;

/**
 * Initialize stepper motor GPIO pins
 */
void stepper_init(void);

/**
 * Start stepping in the given direction (non-blocking)
 * @param direction STEPPER_FORWARD, STEPPER_BACKWARD, or STEPPER_IDLE
 */
void stepper_set_direction(stepper_direction_t direction);

/**
 * Stop the stepper and de-energize all coils
 */
void stepper_stop(void);

/**
 * Non-blocking update — call this frequently in the main loop.
 * Advances one half-step when the step interval has elapsed.
 */
void stepper_task(void);

/**
 * Get current running direction
 */
stepper_direction_t stepper_get_direction(void);

/**
 * Get total half-steps taken since init (absolute, wraps at UINT32_MAX)
 */
uint32_t stepper_get_step_count(void);

/**
 * Reset the step counter to zero
 */
void stepper_reset_count(void);

#endif // STEPPER_CONTROL_H
