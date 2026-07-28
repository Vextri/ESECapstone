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

void led_enqueue_event(led_event_t event, int slot);
void start_led_feedback(void);

#ifdef __cplusplus
}
#endif

#endif
