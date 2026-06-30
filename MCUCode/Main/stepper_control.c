#include "stepper_control.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdint.h>

// Full-step sequence for bipolar stepper via DRV8833 dual H-bridge.
// Columns: AIN1, AIN2, BIN1, BIN2
static const uint8_t full_step_seq[4][4] = {
    {1, 0, 1, 0},   // A+, B+
    {0, 1, 1, 0},   // A-, B+
    {0, 1, 0, 1},   // A-, B-
    {1, 0, 0, 1},   // A+, B-
};

// Pin layout for each motor: {AIN1, AIN2, BIN1, BIN2}
typedef struct {
    uint ain1;
    uint ain2;
    uint bin1;
    uint bin2;
} stepper_pin_cfg_t;

static const stepper_pin_cfg_t pin_cfg[STEPPER_MOTOR_COUNT] = {
    { 8,  9, 10, 11},  // Motor 1: main dispenser
    { 0,  1,  2,  3},  // Motor 2: reuses old DC motor GPIOs (GPIO1=AIN1, GPIO2=AIN2)
    {12, 13, 14, 15},  // Motor 3: dispenser 3
};

// Per-motor runtime state
typedef struct {
    int8_t              step_index;
    stepper_direction_t direction;
    uint32_t            step_count;
    absolute_time_t     next_step_time;
} stepper_state_t;

static stepper_state_t motors[STEPPER_MOTOR_COUNT];

static void apply_step(uint8_t m, int8_t step) {
    gpio_put(pin_cfg[m].ain1, full_step_seq[step][0]);
    gpio_put(pin_cfg[m].ain2, full_step_seq[step][1]);
    gpio_put(pin_cfg[m].bin1, full_step_seq[step][2]);
    gpio_put(pin_cfg[m].bin2, full_step_seq[step][3]);
}

static void deenergize(uint8_t m) {
    gpio_put(pin_cfg[m].ain1, 0);
    gpio_put(pin_cfg[m].ain2, 0);
    gpio_put(pin_cfg[m].bin1, 0);
    gpio_put(pin_cfg[m].bin2, 0);
}

void stepper_init(void) {
    for (uint8_t m = 0; m < STEPPER_MOTOR_COUNT; m++) {
        uint pins[4] = {
            pin_cfg[m].ain1, pin_cfg[m].ain2,
            pin_cfg[m].bin1, pin_cfg[m].bin2
        };
        for (int p = 0; p < 4; p++) {
            gpio_init(pins[p]);
            gpio_set_dir(pins[p], GPIO_OUT);
            gpio_put(pins[p], 0);
        }
        motors[m].step_index     = 0;
        motors[m].direction      = STEPPER_IDLE;
        motors[m].step_count     = 0;
        motors[m].next_step_time = get_absolute_time();
    }
}

void stepper_set_direction(uint8_t motor_idx, stepper_direction_t direction) {
    if (motor_idx >= STEPPER_MOTOR_COUNT) return;
    motors[motor_idx].direction      = direction;
    motors[motor_idx].next_step_time = get_absolute_time();
    if (direction != STEPPER_IDLE) {
        // Re-energize coils immediately so the driver is active before stepper_task() runs
        apply_step(motor_idx, motors[motor_idx].step_index);
    }
}

void stepper_stop(uint8_t motor_idx) {
    if (motor_idx >= STEPPER_MOTOR_COUNT) return;
    motors[motor_idx].direction = STEPPER_IDLE;
    // Hold last step position — keeps coils energized so the driver stays active
    // and the motor can restart cleanly. Use stepper_release() to fully deenergize.
    apply_step(motor_idx, motors[motor_idx].step_index);
}

void stepper_release(uint8_t motor_idx) {
    if (motor_idx >= STEPPER_MOTOR_COUNT) return;
    motors[motor_idx].direction = STEPPER_IDLE;
    deenergize(motor_idx);
}

void stepper_task(void) {
    for (uint8_t m = 0; m < STEPPER_MOTOR_COUNT; m++) {
        if (motors[m].direction == STEPPER_IDLE) continue;
        if (absolute_time_diff_us(get_absolute_time(), motors[m].next_step_time) > 0) continue;

        if (motors[m].direction == STEPPER_FORWARD) {
            motors[m].step_index = (motors[m].step_index + 1) & 3;
        } else {
            motors[m].step_index = (motors[m].step_index + 3) & 3;
        }

        apply_step(m, motors[m].step_index);
        motors[m].step_count++;
        motors[m].next_step_time = delayed_by_us(get_absolute_time(), STEPPER_STEP_DELAY_US);
    }
}

stepper_direction_t stepper_get_direction(uint8_t motor_idx) {
    if (motor_idx >= STEPPER_MOTOR_COUNT) return STEPPER_IDLE;
    return motors[motor_idx].direction;
}

uint32_t stepper_get_step_count(uint8_t motor_idx) {
    if (motor_idx >= STEPPER_MOTOR_COUNT) return 0;
    return motors[motor_idx].step_count;
}

void stepper_reset_count(uint8_t motor_idx) {
    if (motor_idx >= STEPPER_MOTOR_COUNT) return;
    motors[motor_idx].step_count = 0;
}
