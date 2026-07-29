/**
 * Sensor Interrupt Management Implementation
 */

#include "sensor_interrupts.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdio.h>

// Per-slot pin lookup arrays
static const uint piezo_slot_pins[SENSOR_SLOT_COUNT] = {PIEZO_PIN_SLOT0, PIEZO_PIN_SLOT1, PIEZO_PIN_SLOT2};
static const uint ir_slot_pins[SENSOR_SLOT_COUNT]    = {IR_PIN_SLOT0,    IR_PIN_SLOT1,    IR_PIN_SLOT2};

// === PIEZO SENSOR STATE (per slot) ===
static volatile bool     piezo_enabled[SENSOR_SLOT_COUNT]      = {false, false, false};
static volatile uint32_t piezo_count[SENSOR_SLOT_COUNT]        = {0, 0, 0};
static volatile uint32_t piezo_last_trigger[SENSOR_SLOT_COUNT] = {0, 0, 0};
static piezo_callback_t  piezo_callback[SENSOR_SLOT_COUNT]     = {NULL, NULL, NULL};

// === HALL EFFECT SENSOR STATE ===
static volatile bool hall_effect_enabled = false;
static volatile uint32_t hall_effect_count = 0;
static volatile uint32_t hall_effect_last_trigger = 0;
static hall_effect_callback_t hall_effect_callback = NULL;

// === IR SENSOR STATE (per slot) ===
static volatile bool     ir_enabled[SENSOR_SLOT_COUNT]      = {false, false, false};
static volatile uint32_t ir_count[SENSOR_SLOT_COUNT]        = {0, 0, 0};
static volatile uint32_t ir_last_trigger[SENSOR_SLOT_COUNT] = {0, 0, 0};
static ir_callback_t     ir_callback[SENSOR_SLOT_COUNT]     = {NULL, NULL, NULL};

// === INTERRUPT HANDLERS ===

/**
 * Piezo sensor interrupt handler (slot-aware)
 * Triggered when piezo detects vibration/impact (pill dropping)
 */
static void piezo_interrupt_handler(uint gpio, uint32_t events, uint8_t slot) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (!piezo_enabled[slot]) {
        printf("PIEZO DEBUG: Interrupt on slot %d but sensor disabled!\n", slot);
        return;
    }

    if ((current_time - piezo_last_trigger[slot]) < PIEZO_DEBOUNCE_MS) {
        printf("PIEZO DEBUG: Debounced slot %d (too soon: %lu ms)\n",
               slot, current_time - piezo_last_trigger[slot]);
        return;
    }

    if (events & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
        piezo_last_trigger[slot] = current_time;
        piezo_count[slot]++;

        bool gpio_state = gpio_get(gpio);
        const char* edge_type = (events & GPIO_IRQ_EDGE_RISE) ? "RISING" : "FALLING";

        printf(">>> PIEZO INTERRUPT! Slot %d, Edge: %s, GPIO State: %s, Count: %lu, Time: %lu ms <<<\n",
               slot, edge_type, gpio_state ? "HIGH" : "LOW",
               (unsigned long)piezo_count[slot], (unsigned long)current_time);

        if (piezo_callback[slot] != NULL) {
            piezo_callback[slot]();
        }
    }
}

/**
 * Hall effect sensor interrupt handler
 * Triggered when hall effect sensor detects magnetic field change
 */
void hall_effect_interrupt_handler(uint gpio, uint32_t events) {
    // Get current time for debouncing
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Check if this is our pin and if sensor is enabled
    if (gpio != HALL_EFFECT_PIN) {
        printf("HALL DEBUG: Wrong pin (%d vs %d)\n", gpio, HALL_EFFECT_PIN);
        return;
    }
    
    if (!hall_effect_enabled) {
        printf("HALL DEBUG: Interrupt triggered but sensor disabled! Enable with 'H' command.\n");
        return;
    }
    
    // Debounce check
    if ((current_time - hall_effect_last_trigger) < HALL_DEBOUNCE_MS) {
        printf("HALL DEBUG: Debounced (too soon: %lu ms)\n", 
               current_time - hall_effect_last_trigger);
        return; // Ignore if too soon (switch bounce)
    }
    
    // Detect on falling edge only (HIGH -> LOW transition)
    if (events & GPIO_IRQ_EDGE_FALL) {
        hall_effect_last_trigger = current_time;
        hall_effect_count++;
        
        printf("HALL EFFECT: FALLING edge (HIGH->LOW), Count: %d at %lu ms\n",
               hall_effect_count, current_time);
        
        // Call user callback if set
        if (hall_effect_callback != NULL) {
            hall_effect_callback();
        }
    }
}

/**
 * IR sensor interrupt handler (slot-aware)
 * Triggered when IR beam is broken or restored
 */
static void ir_interrupt_handler(uint gpio, uint32_t events, uint8_t slot) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (!ir_enabled[slot]) {
        return;
    }

    if ((current_time - ir_last_trigger[slot]) < IR_DEBOUNCE_MS) {
        return;
    }

    if (events & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
        ir_last_trigger[slot] = current_time;
        ir_count[slot]++;

        bool beam_broken = !gpio_get(gpio); // Active low (beam broken = LOW)
        printf("IR SENSOR: Slot %d Beam %s! Count: %lu at %lu ms\n",
               slot, beam_broken ? "BROKEN" : "RESTORED",
               (unsigned long)ir_count[slot], (unsigned long)current_time);

        if (ir_callback[slot] != NULL) {
            ir_callback[slot]();
        }
    }
}

/**
 * Unified GPIO interrupt handler
 * Routes interrupts to the appropriate sensor handler
 * This fixes the issue where only one GPIO callback can be registered globally
 */
void gpio_unified_interrupt_handler(uint gpio, uint32_t events) {
    switch (gpio) {
        case PIEZO_PIN_SLOT0:  piezo_interrupt_handler(gpio, events, 0); break;
        case PIEZO_PIN_SLOT1:  piezo_interrupt_handler(gpio, events, 1); break;
        case PIEZO_PIN_SLOT2:  piezo_interrupt_handler(gpio, events, 2); break;
        case IR_PIN_SLOT0:     ir_interrupt_handler(gpio, events, 0); break;
        case IR_PIN_SLOT1:     ir_interrupt_handler(gpio, events, 1); break;
        case IR_PIN_SLOT2:     ir_interrupt_handler(gpio, events, 2); break;
        default:
            printf("UNKNOWN GPIO INTERRUPT: pin %d, events 0x%x\n", gpio, events);
            break;
    }
}

// === INITIALIZATION ===

void sensor_interrupts_init(void) {
    // Initialize per-slot piezo and IR pins
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        gpio_init(piezo_slot_pins[slot]);
        gpio_set_dir(piezo_slot_pins[slot], GPIO_IN);
        gpio_pull_down(piezo_slot_pins[slot]);  // Pull down - but piezo naturally rests at ~1V

        gpio_init(ir_slot_pins[slot]);
        gpio_set_dir(ir_slot_pins[slot], GPIO_IN);
        gpio_pull_up(ir_slot_pins[slot]);  // IR sensor active low (beam broken = LOW)
    }

    // Hall effect sensor on GPIO6 is retired: that pin now belongs to the
    // Motor 2 (Slot 2) stepper driver, see stepper_control.c. Not claimed
    // here anymore so it's free for stepper_init() to own as an output.

    // Register unified callback (SDK only allows one global GPIO handler)
    gpio_set_irq_enabled_with_callback(PIEZO_PIN_SLOT0,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_unified_interrupt_handler);

    // Enable IRQs for remaining pins (callback already registered above)
    gpio_set_irq_enabled(PIEZO_PIN_SLOT1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(PIEZO_PIN_SLOT2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(IR_PIN_SLOT0, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(IR_PIN_SLOT1, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(IR_PIN_SLOT2, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);

    // Initialize per-slot state
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        piezo_enabled[slot]      = false;
        piezo_count[slot]        = 0;
        piezo_last_trigger[slot] = 0;
        piezo_callback[slot]     = NULL;
        ir_enabled[slot]         = false;
        ir_count[slot]           = 0;
        ir_last_trigger[slot]    = 0;
        ir_callback[slot]        = NULL;
    }

    // Initialize hall effect state
    hall_effect_enabled      = false;
    hall_effect_count        = 0;
    hall_effect_last_trigger = 0;
    hall_effect_callback     = NULL;

    printf("Sensor interrupts initialized:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("  Slot %d: Piezo GPIO %d, IR GPIO %d\n",
               slot, piezo_slot_pins[slot], ir_slot_pins[slot]);
    }
    printf("  Hall effect sensor: retired (GPIO %d now used by Motor 2 stepper)\n", HALL_EFFECT_PIN);
}

// === SLOT-INDEXED PIEZO FUNCTIONS ===

void piezo_set_callback_slot(uint8_t slot, piezo_callback_t callback) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    piezo_callback[slot] = callback;
}

void piezo_enable_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    piezo_enabled[slot] = true;
    printf("Piezo sensor slot %d ENABLED (GPIO %d)\n", slot, piezo_slot_pins[slot]);
}

void piezo_disable_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    piezo_enabled[slot] = false;
    printf("Piezo sensor slot %d DISABLED\n", slot);
}

bool piezo_is_enabled_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return false;
    return piezo_enabled[slot];
}

uint32_t piezo_get_count_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return 0;
    return piezo_count[slot];
}

void piezo_reset_count_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    piezo_count[slot] = 0;
}

uint32_t piezo_get_last_trigger_time_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return 0;
    return piezo_last_trigger[slot];
}

// === LEGACY SLOT-0 WRAPPERS (backward compatibility) ===

void piezo_set_callback(piezo_callback_t callback) { piezo_set_callback_slot(0, callback); }
void piezo_enable(void)                            { piezo_enable_slot(0); }
void piezo_disable(void)                           { piezo_disable_slot(0); }
bool piezo_is_enabled(void)                        { return piezo_is_enabled_slot(0); }
uint32_t piezo_get_count(void)                     { return piezo_get_count_slot(0); }
void piezo_reset_count(void)                       { piezo_reset_count_slot(0); }
uint32_t piezo_get_last_trigger_time(void)          { return piezo_get_last_trigger_time_slot(0); }

// === HALL EFFECT SENSOR FUNCTIONS ===

void hall_effect_set_callback(hall_effect_callback_t callback) {
    hall_effect_callback = callback;
}

void hall_effect_enable(void) {
    hall_effect_enabled = true;
    printf("Hall effect sensor ENABLED\n");
}

void hall_effect_disable(void) {
    hall_effect_enabled = false;
    printf("Hall effect sensor DISABLED\n");
}

bool hall_effect_is_enabled(void) {
    return hall_effect_enabled;
}

uint32_t hall_effect_get_count(void) {
    return hall_effect_count;
}

void hall_effect_reset_count(void) {
    hall_effect_count = 0;
    printf("Hall effect count reset\n");
}

uint32_t hall_effect_get_last_trigger_time(void) {
    return hall_effect_last_trigger;
}

// === SLOT-INDEXED IR FUNCTIONS ===

void ir_set_callback_slot(uint8_t slot, ir_callback_t callback) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    ir_callback[slot] = callback;
}

void ir_enable_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    ir_enabled[slot] = true;
    printf("IR sensor slot %d ENABLED (GPIO %d)\n", slot, ir_slot_pins[slot]);
}

void ir_disable_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    ir_enabled[slot] = false;
    printf("IR sensor slot %d DISABLED\n", slot);
}

bool ir_is_enabled_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return false;
    return ir_enabled[slot];
}

uint32_t ir_get_count_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return 0;
    return ir_count[slot];
}

void ir_reset_count_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return;
    ir_count[slot] = 0;
}

bool ir_is_beam_broken_slot(uint8_t slot) {
    if (slot >= SENSOR_SLOT_COUNT) return false;
    return !gpio_get(ir_slot_pins[slot]); // Active low (beam broken = LOW)
}

// === LEGACY SLOT-0 WRAPPERS (backward compatibility) ===

void ir_set_callback(ir_callback_t callback) { ir_set_callback_slot(0, callback); }
void ir_enable(void)                          { ir_enable_slot(0); }
void ir_disable(void)                         { ir_disable_slot(0); }
bool ir_is_enabled(void)                      { return ir_is_enabled_slot(0); }
uint32_t ir_get_count(void)                   { return ir_get_count_slot(0); }
void ir_reset_count(void)                     { ir_reset_count_slot(0); }
bool ir_is_beam_broken(void)                  { return ir_is_beam_broken_slot(0); }

uint32_t ir_get_last_trigger_time(void) {
    return ir_last_trigger[0];
}

// === UTILITY FUNCTIONS ===

void sensor_interrupts_status(void) {
    printf("\n=== SENSOR STATUS ===\n");
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("Slot %d:\n", slot);
        printf("  Piezo (GPIO %d): %s | Count: %lu | Last: %lu ms ago\n",
               piezo_slot_pins[slot],
               piezo_enabled[slot] ? "ENABLED" : "DISABLED",
               (unsigned long)piezo_count[slot],
               piezo_last_trigger[slot] > 0 ? (unsigned long)(now - piezo_last_trigger[slot]) : 0UL);
        printf("  IR    (GPIO %d): %s | Count: %lu | Beam: %s | Last: %lu ms ago\n",
               ir_slot_pins[slot],
               ir_enabled[slot] ? "ENABLED" : "DISABLED",
               (unsigned long)ir_count[slot],
               ir_is_beam_broken_slot(slot) ? "BROKEN" : "CLEAR",
               ir_last_trigger[slot] > 0 ? (unsigned long)(now - ir_last_trigger[slot]) : 0UL);
    }

    printf("Hall Effect (GPIO %d): %s | Count: %lu | Last: %lu ms ago\n",
           HALL_EFFECT_PIN,
           hall_effect_enabled ? "ENABLED" : "DISABLED",
           (unsigned long)hall_effect_count,
           hall_effect_last_trigger > 0 ? (unsigned long)(now - hall_effect_last_trigger) : 0UL);
    printf("====================\n\n");
}

void sensor_interrupts_reset_all(void) {
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        piezo_count[slot] = 0;
        ir_count[slot] = 0;
    }
    hall_effect_count = 0;
    printf("All sensor counts reset\n");
}

void sensor_interrupts_disable_all(void) {
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        piezo_disable_slot(slot);
        ir_disable_slot(slot);
    }
    hall_effect_disable();
    printf("All sensors DISABLED\n");
}

void sensor_interrupts_enable_all(void) {
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        piezo_enable_slot(slot);
        ir_enable_slot(slot);
    }
    hall_effect_enable();
    printf("All sensors ENABLED\n");
}

void sensor_interrupts_test_gpio_states(void) {
    printf("\n=== GPIO STATE TEST ===\n");
    printf("Raw GPIO readings:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        bool pstate = gpio_get(piezo_slot_pins[slot]);
        bool istate = gpio_get(ir_slot_pins[slot]);
        printf("  Slot %d Piezo (GPIO %d): %s | IR (GPIO %d): %s\n",
               slot, piezo_slot_pins[slot], pstate ? "HIGH" : "LOW",
               ir_slot_pins[slot], istate ? "HIGH" : "LOW");
    }
    bool hall_state = gpio_get(HALL_EFFECT_PIN);
    printf("  Hall Effect (GPIO %d): %s\n",
           HALL_EFFECT_PIN, hall_state ? "HIGH" : "LOW");

    printf("\nInterrupt enable status:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("  Slot %d: Piezo=%s | IR=%s\n",
               slot,
               piezo_enabled[slot] ? "YES" : "NO",
               ir_enabled[slot]    ? "YES" : "NO");
    }
    printf("  Hall Effect: %s\n", hall_effect_enabled ? "YES" : "NO");

    if (hall_state && !hall_effect_enabled) {
        printf("\nWARNING: Hall Effect reads HIGH but interrupts disabled!\n");
    }
    if (hall_state) {
        printf("\nNOTE: 5V on 3.3V GPIO detected!\n");
        printf("- Consider using a voltage divider or 3.3V sensor\n");
    }
}

void sensor_interrupts_debug_config(void) {
    printf("\n=== INTERRUPT CONFIGURATION DEBUG ===\n");

    printf("GPIO Directions:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("  Slot %d Piezo (GPIO %d): %s\n",
               slot, piezo_slot_pins[slot],
               gpio_is_dir_out(piezo_slot_pins[slot]) ? "OUTPUT" : "INPUT");
        printf("  Slot %d IR    (GPIO %d): %s\n",
               slot, ir_slot_pins[slot],
               gpio_is_dir_out(ir_slot_pins[slot]) ? "OUTPUT" : "INPUT");
    }
    printf("  Hall (GPIO %d): %s\n",
           HALL_EFFECT_PIN, gpio_is_dir_out(HALL_EFFECT_PIN) ? "OUTPUT" : "INPUT");

    printf("\nPull Resistor Configuration:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("  Slot %d Piezo (GPIO %d): PULL_DOWN\n", slot, piezo_slot_pins[slot]);
        printf("  Slot %d IR    (GPIO %d): PULL_UP\n",   slot, ir_slot_pins[slot]);
    }
    printf("  Hall (GPIO %d): PULL_DOWN\n", HALL_EFFECT_PIN);

    printf("\nSensor Enable Status:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("  Slot %d: Piezo=%s | IR=%s\n",
               slot,
               piezo_enabled[slot] ? "YES" : "NO",
               ir_enabled[slot]    ? "YES" : "NO");
    }
    printf("  Hall Effect: %s\n", hall_effect_enabled ? "YES" : "NO");

    printf("\nCurrent GPIO States:\n");
    for (uint8_t slot = 0; slot < SENSOR_SLOT_COUNT; slot++) {
        printf("  Slot %d Piezo (GPIO %d): %s | IR (GPIO %d): %s\n",
               slot, piezo_slot_pins[slot],
               gpio_get(piezo_slot_pins[slot]) ? "HIGH" : "LOW",
               ir_slot_pins[slot],
               gpio_get(ir_slot_pins[slot]) ? "HIGH" : "LOW");
    }
    printf("  Hall (GPIO %d): %s\n",
           HALL_EFFECT_PIN, gpio_get(HALL_EFFECT_PIN) ? "HIGH" : "LOW");

    printf("\nTo test interrupts:\n");
    printf("  1. Press 'P' to enable slot-0 piezo\n");
    printf("  2. Press 'H' to enable hall effect\n");
    printf("  3. Press 'I' to enable slot-0 IR\n");
    printf("  4. Trigger sensors and watch for interrupt messages\n");
    printf("=====================================\n\n");
}