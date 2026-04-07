/**
 * Sensor Interrupt Management Implementation
 */

#include "sensor_interrupts.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include <stdio.h>

// === PIEZO SENSOR STATE ===
static volatile bool piezo_enabled = false;
static volatile uint32_t piezo_count = 0;
static volatile uint32_t piezo_last_trigger = 0;
static piezo_callback_t piezo_callback = NULL;

// === HALL EFFECT SENSOR STATE ===
static volatile bool hall_effect_enabled = false;
static volatile uint32_t hall_effect_count = 0;
static volatile uint32_t hall_effect_last_trigger = 0;
static hall_effect_callback_t hall_effect_callback = NULL;

// === IR SENSOR STATE ===
static volatile bool ir_enabled = false;
static volatile uint32_t ir_count = 0;
static volatile uint32_t ir_last_trigger = 0;
static ir_callback_t ir_callback = NULL;

// === INTERRUPT HANDLERS ===

/**
 * Piezo sensor interrupt handler
 * Triggered when piezo detects vibration/impact (pill dropping)
 */
void piezo_interrupt_handler(uint gpio, uint32_t events) {
    // Get current time for debouncing
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Check if this is our pin and if sensor is enabled
    if (gpio != PIEZO_PIN) {
        printf("PIEZO DEBUG: Wrong pin (%d vs %d)\n", gpio, PIEZO_PIN);
        return;
    }
    
    if (!piezo_enabled) {
        printf("PIEZO DEBUG: Interrupt triggered but sensor disabled! Enable with 'P' command.\n");
        return;
    }
    
    // Debounce check
    if ((current_time - piezo_last_trigger) < PIEZO_DEBOUNCE_MS) {
        printf("PIEZO DEBUG: Debounced (too soon: %lu ms)\n", 
               current_time - piezo_last_trigger);
        return; // Ignore if too soon (vibration settling)
    }
    
    // Detect on both edges (vibration start/stop)
    if (events & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
        piezo_last_trigger = current_time;
        piezo_count++;
        
        bool gpio_state = gpio_get(PIEZO_PIN);
        const char* edge_type = (events & GPIO_IRQ_EDGE_RISE) ? "RISING" : "FALLING";
        
        printf(">>> PIEZO INTERRUPT! Edge: %s, GPIO State: %s, Count: %d, Time: %lu ms <<<\n", 
               edge_type, gpio_state ? "HIGH" : "LOW", piezo_count, current_time);
        
        // Call user callback if set
        if (piezo_callback != NULL) {
            piezo_callback();
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
 * IR sensor interrupt handler
 * Triggered when IR beam is broken or restored
 */
void ir_interrupt_handler(uint gpio, uint32_t events) {
    // Get current time for debouncing
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Check if this is our pin and if sensor is enabled
    if (gpio != IR_PIN || !ir_enabled) {
        return;
    }
    
    // Debounce check
    if ((current_time - ir_last_trigger) < IR_DEBOUNCE_MS) {
        return; // Ignore if too soon (noise filtering)
    }
    
    // Detect on both edges (beam broken/restored)
    if (events & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL)) {
        ir_last_trigger = current_time;
        ir_count++;
        
        bool beam_broken = !gpio_get(IR_PIN); // Assuming active low (beam broken = LOW)
        printf("IR SENSOR: Beam %s! Count: %d at %lu ms\n", 
               beam_broken ? "BROKEN" : "RESTORED", ir_count, current_time);
        
        // Call user callback if set
        if (ir_callback != NULL) {
            ir_callback();
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
        case PIEZO_PIN:
            piezo_interrupt_handler(gpio, events);
            break;
        case HALL_EFFECT_PIN:
            hall_effect_interrupt_handler(gpio, events);
            break;
        case IR_PIN:
            ir_interrupt_handler(gpio, events);
            break;
        default:
            printf("UNKNOWN GPIO INTERRUPT: pin %d, events 0x%x\n", gpio, events);
            break;
    }
}

// === INITIALIZATION ===

void sensor_interrupts_init(void) {
    // Initialize piezo sensor pin
    gpio_init(PIEZO_PIN);
    gpio_set_dir(PIEZO_PIN, GPIO_IN);
    gpio_pull_down(PIEZO_PIN);  // Pull down - but piezo naturally rests at ~1V
    
    // Initialize hall effect sensor pin
    gpio_init(HALL_EFFECT_PIN);
    gpio_set_dir(HALL_EFFECT_PIN, GPIO_IN);
    gpio_pull_down(HALL_EFFECT_PIN);  // Changed to pull down for 5V sensor
    
    // Initialize IR sensor pin
    gpio_init(IR_PIN);
    gpio_set_dir(IR_PIN, GPIO_IN);
    gpio_pull_up(IR_PIN);  // Pull up - IR sensor typically active low (beam broken = LOW)
    
    // Set up unified GPIO interrupt callback - this is the key fix!
    // The Pico SDK only allows one global GPIO interrupt handler
    gpio_set_irq_enabled_with_callback(PIEZO_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &gpio_unified_interrupt_handler);
    gpio_set_irq_enabled(HALL_EFFECT_PIN, GPIO_IRQ_EDGE_FALL, true);
    gpio_set_irq_enabled(IR_PIN, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
    
    // Initialize state
    piezo_enabled = false;
    hall_effect_enabled = false;
    ir_enabled = false;
    piezo_count = 0;
    hall_effect_count = 0;
    ir_count = 0;
    piezo_callback = NULL;
    hall_effect_callback = NULL;
    ir_callback = NULL;
    
    printf("Sensor interrupts initialized:\n");
    printf("  Piezo sensor: GPIO %d\n", PIEZO_PIN);
    printf("  Hall effect sensor: GPIO %d\n", HALL_EFFECT_PIN);
    printf("  IR sensor: GPIO %d\n", IR_PIN);
}

// === PIEZO SENSOR FUNCTIONS ===

void piezo_set_callback(piezo_callback_t callback) {
    piezo_callback = callback;
}

void piezo_enable(void) {
    piezo_enabled = true;
    printf("Piezo sensor ENABLED\n");
}

void piezo_disable(void) {
    piezo_enabled = false;
    printf("Piezo sensor DISABLED\n");
}

bool piezo_is_enabled(void) {
    return piezo_enabled;
}

uint32_t piezo_get_count(void) {
    return piezo_count;
}

void piezo_reset_count(void) {
    piezo_count = 0;
    printf("Piezo count reset\n");
}

uint32_t piezo_get_last_trigger_time(void) {
    return piezo_last_trigger;
}

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

// === IR SENSOR FUNCTIONS ===

void ir_set_callback(ir_callback_t callback) {
    ir_callback = callback;
}

void ir_enable(void) {
    ir_enabled = true;
    printf("IR sensor ENABLED\n");
}

void ir_disable(void) {
    ir_enabled = false;
    printf("IR sensor DISABLED\n");
}

bool ir_is_enabled(void) {
    return ir_enabled;
}

uint32_t ir_get_count(void) {
    return ir_count;
}

void ir_reset_count(void) {
    ir_count = 0;
    printf("IR sensor count reset\n");
}

uint32_t ir_get_last_trigger_time(void) {
    return ir_last_trigger;
}

bool ir_is_beam_broken(void) {
    return !gpio_get(IR_PIN); // Assuming active low (beam broken = LOW)
}

// === UTILITY FUNCTIONS ===

void sensor_interrupts_status(void) {
    printf("\n=== SENSOR STATUS ===\n");
    
    printf("Piezo Sensor (GPIO %d):\n", PIEZO_PIN);
    printf("  Status: %s\n", piezo_enabled ? "ENABLED" : "DISABLED");
    printf("  Count: %d triggers\n", piezo_count);
    printf("  Last trigger: %lu ms ago\n", 
           piezo_last_trigger > 0 ? (to_ms_since_boot(get_absolute_time()) - piezo_last_trigger) : 0);
    
    printf("Hall Effect Sensor (GPIO %d):\n", HALL_EFFECT_PIN);
    printf("  Status: %s\n", hall_effect_enabled ? "ENABLED" : "DISABLED");
    printf("  Count: %d triggers\n", hall_effect_count);
    printf("  Last trigger: %lu ms ago\n", 
           hall_effect_last_trigger > 0 ? (to_ms_since_boot(get_absolute_time()) - hall_effect_last_trigger) : 0);
    
    printf("IR Sensor (GPIO %d):\n", IR_PIN);
    printf("  Status: %s\n", ir_enabled ? "ENABLED" : "DISABLED");
    printf("  Count: %d triggers\n", ir_count);
    printf("  Beam state: %s\n", ir_is_beam_broken() ? "BROKEN" : "CLEAR");
    printf("  Last trigger: %lu ms ago\n", 
           ir_last_trigger > 0 ? (to_ms_since_boot(get_absolute_time()) - ir_last_trigger) : 0);
    
    printf("====================\n\n");
}

void sensor_interrupts_reset_all(void) {
    piezo_reset_count();
    hall_effect_reset_count();
    ir_reset_count();
    printf("All sensor counts reset\n");
}

void sensor_interrupts_disable_all(void) {
    piezo_disable();
    hall_effect_disable();
    ir_disable();
    printf("All sensors DISABLED\n");
}

void sensor_interrupts_enable_all(void) {
    piezo_enable();
    hall_effect_enable();
    ir_enable();
    printf("All sensors ENABLED\n");
}

void sensor_interrupts_test_gpio_states(void) {
    printf("\n=== GPIO STATE TEST ===\n");
    
    // Read raw GPIO states
    bool piezo_state = gpio_get(PIEZO_PIN);
    bool hall_state = gpio_get(HALL_EFFECT_PIN);
    bool ir_state = gpio_get(IR_PIN);
    
    printf("Raw GPIO readings:\n");
    printf("  GPIO %d (Piezo): %s (%.1fV expected)\n", 
           PIEZO_PIN, piezo_state ? "HIGH" : "LOW", piezo_state ? 3.3 : 0.0);
    printf("  GPIO %d (Hall Effect): %s (%.1fV expected)\n", 
           HALL_EFFECT_PIN, hall_state ? "HIGH" : "LOW", hall_state ? 3.3 : 0.0);
    printf("  GPIO %d (IR): %s (%.1fV expected)\n", 
           IR_PIN, ir_state ? "HIGH" : "LOW", ir_state ? 3.3 : 0.0);
    
    printf("\nInterrupt enable status:\n");
    printf("  Piezo enabled: %s\n", piezo_enabled ? "YES" : "NO");
    printf("  Hall Effect enabled: %s\n", hall_effect_enabled ? "YES" : "NO");  
    printf("  IR enabled: %s\n", ir_enabled ? "YES" : "NO");
    
    if (hall_state && !hall_effect_enabled) {
        printf("\nWARNING: Hall Effect reads HIGH but interrupts disabled!\n");
        printf("Try: Press 'H' to enable hall effect interrupts\n");
    }
    
    if (hall_state) {
        printf("\nNOTE: 5V on 3.3V GPIO detected!\n");
        printf("- This may damage the GPIO or cause unreliable operation\n");
        printf("- Consider using a voltage divider (3.3V = 5V * (3.3/(3.3+1.7)))\n");
        printf("- Or use a 3.3V hall effect sensor instead\n");
    }
}

/**
 * Debug function to check interrupt configuration
 */
void sensor_interrupts_debug_config(void) {
    printf("\n=== INTERRUPT CONFIGURATION DEBUG ===\n");
    
    // Check GPIO directions
    printf("GPIO Directions:\n");
    printf("  GPIO %d (Piezo): %s\n", PIEZO_PIN, gpio_is_dir_out(PIEZO_PIN) ? "OUTPUT" : "INPUT");
    printf("  GPIO %d (Hall): %s\n", HALL_EFFECT_PIN, gpio_is_dir_out(HALL_EFFECT_PIN) ? "OUTPUT" : "INPUT");
    printf("  GPIO %d (IR): %s\n", IR_PIN, gpio_is_dir_out(IR_PIN) ? "OUTPUT" : "INPUT");
    
    // Check pull resistors (this is harder to read back, but we'll show our config)
    printf("\nPull Resistor Configuration (from initialization):\n");
    printf("  GPIO %d (Piezo): PULL_DOWN\n", PIEZO_PIN);
    printf("  GPIO %d (Hall): PULL_DOWN\n", HALL_EFFECT_PIN);  
    printf("  GPIO %d (IR): PULL_UP\n", IR_PIN);
    
    // Check interrupt enabled status
    printf("\nSensor Enable Status:\n");
    printf("  Piezo enabled: %s\n", piezo_enabled ? "YES" : "NO");
    printf("  Hall Effect enabled: %s\n", hall_effect_enabled ? "YES" : "NO");
    printf("  IR enabled: %s\n", ir_enabled ? "YES" : "NO");
    
    // Current GPIO states
    printf("\nCurrent GPIO States:\n");
    printf("  GPIO %d (Piezo): %s\n", PIEZO_PIN, gpio_get(PIEZO_PIN) ? "HIGH" : "LOW");
    printf("  GPIO %d (Hall): %s\n", HALL_EFFECT_PIN, gpio_get(HALL_EFFECT_PIN) ? "HIGH" : "LOW");
    printf("  GPIO %d (IR): %s\n", IR_PIN, gpio_get(IR_PIN) ? "HIGH" : "LOW");
    
    printf("\nTo test interrupts:\n");
    printf("  1. Press 'P' to enable piezo interrupts\n");
    printf("  2. Press 'H' to enable hall effect interrupts  \n");
    printf("  3. Press 'I' to enable IR interrupts\n");
    printf("  4. Trigger sensors and watch for interrupt messages\n");
    printf("=====================================\n\n");
    
    printf("=====================\n\n");
}