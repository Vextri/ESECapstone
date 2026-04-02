/**
 * Motor Control Library for Raspberry Pi Pico
 * 
 * Provides PWM speed control with GPIO direction control
 * Compatible with H-bridge motor drivers
 */

#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"

// Pin definitions (DRV8833: AIN1=GPIO1, AIN2=GPIO2, no separate enable pin)
#define AIN1_PIN 1   // DRV8833 AIN1 - PWM for forward
#define AIN2_PIN 2   // DRV8833 AIN2 - PWM for backward
#define MOTOR_SPEED 900  // PWM duty cycle (0-999), 900 = 90%

// Motor direction states
typedef enum {
    MOTOR_STOP,
    MOTOR_FORWARD,
    MOTOR_BACKWARD
} motor_direction_t;

// Global variables (two slices: AIN1=slice0 chanB, AIN2=slice1 chanA)
extern uint motor_slice_ain1;
extern uint motor_slice_ain2;

/**
 * Initialize motor control system
 * Sets up PWM and GPIO pins for motor control
 */
void motor_init(void);

/**
 * Set motor direction and speed
 * @param direction Motor direction (MOTOR_STOP, MOTOR_FORWARD, MOTOR_BACKWARD)
 */
void motor_set_direction(motor_direction_t direction);

/**
 * Stop the motor
 * Sets both direction pins low and PWM to 0
 */
void motor_stop(void);

/**
 * Set motor to forward direction
 * DIR_PIN1 HIGH, DIR_PIN2 LOW, PWM ON
 */
void motor_forward(void);

/**
 * Set motor to backward direction
 * DIR_PIN1 LOW, DIR_PIN2 HIGH, PWM ON
 */
void motor_backward(void);

#endif // MOTOR_CONTROL_H