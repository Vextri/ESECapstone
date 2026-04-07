/**
 * Motor Control Library Implementation
 */

#include "motor_control.h"

// PWM slice numbers for AIN1 (GPIO1, slice0 chanB) and AIN2 (GPIO2, slice1 chanA)
uint motor_slice_ain1;
uint motor_slice_ain2;

void motor_init(void) {
    // AIN1 = GPIO1: PWM slice 0, channel B
    gpio_set_function(AIN1_PIN, GPIO_FUNC_PWM);
    motor_slice_ain1 = pwm_gpio_to_slice_num(AIN1_PIN);
    pwm_set_wrap(motor_slice_ain1, 999);
    pwm_set_chan_level(motor_slice_ain1, PWM_CHAN_B, 0);
    pwm_set_enabled(motor_slice_ain1, true);

    // AIN2 = GPIO2: PWM slice 1, channel A
    gpio_set_function(AIN2_PIN, GPIO_FUNC_PWM);
    motor_slice_ain2 = pwm_gpio_to_slice_num(AIN2_PIN);
    pwm_set_wrap(motor_slice_ain2, 999);
    pwm_set_chan_level(motor_slice_ain2, PWM_CHAN_A, 0);
    pwm_set_enabled(motor_slice_ain2, true);
}

void motor_set_direction(motor_direction_t direction) {
    switch (direction) {
        case MOTOR_FORWARD:
            motor_forward();
            break;
        case MOTOR_BACKWARD:
            motor_backward();
            break;
        case MOTOR_STOP:
        default:
            motor_stop();
            break;
    }
}

void motor_stop(void) {
    // Coast: both AIN1 and AIN2 at 0 (DRV8833 coast mode)
    pwm_set_chan_level(motor_slice_ain1, PWM_CHAN_B, 0);
    pwm_set_chan_level(motor_slice_ain2, PWM_CHAN_A, 0);
}

void motor_forward(void) {
    // AIN1 = PWM, AIN2 = 0
    pwm_set_chan_level(motor_slice_ain2, PWM_CHAN_A, 0);
    pwm_set_chan_level(motor_slice_ain1, PWM_CHAN_B, MOTOR_SPEED);
}

void motor_backward(void) {
    // AIN1 = 0, AIN2 = PWM
    pwm_set_chan_level(motor_slice_ain1, PWM_CHAN_B, 0);
    pwm_set_chan_level(motor_slice_ain2, PWM_CHAN_A, MOTOR_SPEED);
}