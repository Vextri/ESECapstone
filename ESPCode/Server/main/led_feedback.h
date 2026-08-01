/* ============================================================================
 * LED_FEEDBACK.H - Per-Slot Status LEDs
 * ----------------------------------------------------------------------------
 * Drives a WS2812 addressable LED strip, one LED per pill slot, used both
 * as pass/fail feedback after a dispense and as an on-device cursor while
 * navigating the LCD menu (the LED under whichever slot is selected lights
 * up). Events are queued rather than applied directly, so callers on any
 * task (UART bridge, LCD, web server) can request an LED change without
 * touching the LED hardware themselves.
 * ============================================================================ */

#ifndef LED_FEEDBACK_H
#define LED_FEEDBACK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	LED_EVENT_SUCCESS = 1,
	LED_EVENT_FAILURE = 2,
	LED_EVENT_EDIT_BEGIN = 3,
	LED_EVENT_EDIT_END = 4,
	/* Highlights the slot's LED while it's the cursor target in the LCD
	 * menu (slot list, action menu, confirm-dispense), so the physical
	 * LED under a station doubles as an on-device cursor. */
	LED_EVENT_SLOT_SELECT = 5,
	LED_EVENT_SLOT_CLEAR = 6,
} led_event_t;

/* Queues an LED event for the given slot (0-based). Safe to call from any
 * task; never blocks. */
void led_enqueue_event(led_event_t event, int slot);

/* Initializes the WS2812 strip and starts the task that consumes queued
 * events. Call once at boot. */
void start_led_feedback(void);

#ifdef __cplusplus
}
#endif

#endif
