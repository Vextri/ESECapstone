#include "stepper_control.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdint.h>

// Full-step sequence for bipolar stepper via DRV8833 dual H-bridge.
// Both coils are always energized for maximum torque.
// Columns: AIN1, AIN2, BIN1, BIN2
static const uint8_t full_step_seq[4][4] = {
    {1, 0, 1, 0},   // A+, B+
    {0, 1, 1, 0},   // A-, B+
    {0, 1, 0, 1},   // A-, B-
    {1, 0, 0, 1},   // A+, B-
};

static const uint stepper_pins[4] = {
    STEPPER_AIN1_PIN,
    STEPPER_AIN2_PIN,
    STEPPER_BIN1_PIN,
    STEPPER_BIN2_PIN,
};

static int8_t              s_step_index  = 0;
static stepper_direction_t s_direction   = STEPPER_IDLE;
static uint32_t            s_step_count  = 0;
static absolute_time_t     s_next_step_time;

static void apply_step(int8_t index) {
    for (int i = 0; i < 4; i++) {
        gpio_put(stepper_pins[i], full_step_seq[index][i]);
    }
}

static void deenergize(void) {
    for (int i = 0; i < 4; i++) {
        gpio_put(stepper_pins[i], 0);
    }
}

void stepper_init(void) {
    for (int i = 0; i < 4; i++) {
        gpio_init(stepper_pins[i]);
        gpio_set_dir(stepper_pins[i], GPIO_OUT);
        gpio_put(stepper_pins[i], 0);
    }
    s_step_index     = 0;
    s_direction      = STEPPER_IDLE;
    s_step_count     = 0;
    s_next_step_time = get_absolute_time();
}

void stepper_set_direction(stepper_direction_t direction) {
    s_direction      = direction;
    s_next_step_time = get_absolute_time(); // step immediately on next task() call
}

void stepper_stop(void) {
    s_direction = STEPPER_IDLE;
    deenergize();
}

void stepper_task(void) {
    if (s_direction == STEPPER_IDLE) {
        return;
    }

    if (absolute_time_diff_us(get_absolute_time(), s_next_step_time) > 0) {
        return; // not yet time for next step
    }

    if (s_direction == STEPPER_FORWARD) {
        s_step_index = (s_step_index + 1) & 3;
    } else {
        s_step_index = (s_step_index + 3) & 3; // subtract 1 mod 4
    }

    apply_step(s_step_index);
    s_step_count++;
    s_next_step_time = delayed_by_us(get_absolute_time(), STEPPER_STEP_DELAY_US);
}

stepper_direction_t stepper_get_direction(void) {
    return s_direction;
}

uint32_t stepper_get_step_count(void) {
    return s_step_count;
}

void stepper_reset_count(void) {
    s_step_count = 0;
}
