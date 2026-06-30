/**
 * Stepper Motor Control for Raspberry Pi Pico
 *
 * Full-step bipolar control via DRV8833 dual H-bridge driver.
 * Supports 3 independent stepper motors.
 *
 * Wiring:
 *   Motor 1 (main dispenser): GPIO8=AIN1,  GPIO9=AIN2,  GPIO10=BIN1, GPIO11=BIN2
 *   Motor 2 (dispenser 2):    GPIO0=AIN1,  GPIO1=AIN2,  GPIO2=BIN1,  GPIO3=BIN2
 *   Motor 3 (dispenser 3):    GPIO12=AIN1, GPIO13=AIN2, GPIO14=BIN1, GPIO15=BIN2
 */

#ifndef STEPPER_CONTROL_H
#define STEPPER_CONTROL_H

#include "pico/stdlib.h"
#include <stdbool.h>
#include <stdint.h>

// Number of stepper motors supported
#define STEPPER_MOTOR_COUNT 3

// Motor index constants
#define STEPPER_MOTOR_1  0   // Main dispenser: GPIO 8-11
#define STEPPER_MOTOR_2  1   // Dispenser 2:   GPIO 0-3 (reuses old DC motor pins)
#define STEPPER_MOTOR_3  2   // Dispenser 3:   GPIO 12-15

// Step timing: microseconds between each full-step
// 2000us = ~500 full-steps/sec
#define STEPPER_STEP_DELAY_US 2000

// Direction values
typedef enum {
    STEPPER_IDLE,
    STEPPER_FORWARD,
    STEPPER_BACKWARD
} stepper_direction_t;

/**
 * Initialize all stepper motor GPIO pins
 */
void stepper_init(void);

/**
 * Start stepping the specified motor in the given direction (non-blocking)
 * @param motor_idx Motor index (STEPPER_MOTOR_1 / _2 / _3)
 * @param direction STEPPER_FORWARD, STEPPER_BACKWARD, or STEPPER_IDLE
 */
void stepper_set_direction(uint8_t motor_idx, stepper_direction_t direction);

/**
 * Stop the specified motor and hold the last step position (coils stay energized).
 * This keeps the driver active so the motor can restart cleanly.
 * @param motor_idx Motor index
 */
void stepper_stop(uint8_t motor_idx);

/**
 * Stop the specified motor and fully de-energize its coils (coast mode).
 * Use this only when you want to cut power to the motor entirely.
 * @param motor_idx Motor index
 */
void stepper_release(uint8_t motor_idx);

/**
 * Non-blocking update — call this frequently in the main loop.
 * Advances each motor one step when its interval has elapsed.
 */
void stepper_task(void);

/**
 * Get current running direction for the specified motor
 */
stepper_direction_t stepper_get_direction(uint8_t motor_idx);

/**
 * Get total steps taken since init for the specified motor
 */
uint32_t stepper_get_step_count(uint8_t motor_idx);

/**
 * Reset the step counter to zero for the specified motor
 */
void stepper_reset_count(uint8_t motor_idx);

#endif // STEPPER_CONTROL_H
