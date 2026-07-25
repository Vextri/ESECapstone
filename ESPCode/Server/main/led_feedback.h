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
} led_event_t;

void led_enqueue_event(led_event_t event, int slot);
void start_led_feedback(void);

#ifdef __cplusplus
}
#endif

#endif
