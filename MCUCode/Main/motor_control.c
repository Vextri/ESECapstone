/**
 * Motor Control Library Implementation
 */

#include "motor_control.h"

// Global variable to store PWM slice number
uint motor_slice_num;

void motor_init(void) {
    // Set up PWM on GPIO 0 for motor speed control
    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);
    motor_slice_num = pwm_gpio_to_slice_num(PWM_PIN);
    
    // Set PWM frequency (wrap value) - original resolution
    pwm_set_wrap(motor_slice_num, 3);  // 4-step resolution (0-3)
    
    // Initially set PWM to 0 (motor stopped)
    pwm_set_chan_level(motor_slice_num, PWM_CHAN_A, 0);
    
    // Enable PWM
    pwm_set_enabled(motor_slice_num, true);
    
    // Set up GPIO pins for direction control
    gpio_init(DIR_PIN1);
    gpio_init(DIR_PIN2);
    gpio_set_dir(DIR_PIN1, GPIO_OUT);
    gpio_set_dir(DIR_PIN2, GPIO_OUT);
    
    // Initially set both direction pins LOW (motor stopped)
    gpio_put(DIR_PIN1, 0);
    gpio_put(DIR_PIN2, 0);
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
    // Both direction pins LOW, PWM OFF
    gpio_put(DIR_PIN1, 0);
    gpio_put(DIR_PIN2, 0);
    pwm_set_chan_level(motor_slice_num, PWM_CHAN_A, 0);
}

void motor_forward(void) {
    // Forward - DIR_PIN1 HIGH, DIR_PIN2 LOW, PWM ON
    gpio_put(DIR_PIN1, 1);
    gpio_put(DIR_PIN2, 0);
    pwm_set_chan_level(motor_slice_num, PWM_CHAN_A, MOTOR_SPEED);
}

void motor_backward(void) {
    // Backward - DIR_PIN1 LOW, DIR_PIN2 HIGH, PWM ON
    gpio_put(DIR_PIN1, 0);
    gpio_put(DIR_PIN2, 1);
    pwm_set_chan_level(motor_slice_num, PWM_CHAN_A, MOTOR_SPEED);
}