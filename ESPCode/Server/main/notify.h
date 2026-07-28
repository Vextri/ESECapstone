#ifndef NOTIFY_H
#define NOTIFY_H

/* PortaPill Caretaker Notification System
 *
 * Sends push notifications (via the free ntfy.sh service) to whoever has
 * subscribed to this device's topic, most importantly a "pickup not
 * confirmed" alert when a dose was dispensed but the drawer sensor never
 * saw it opened. See ntfy_credentials.h for the topic itself, and
 * notify_get_topic()/notify_get_subscribe_url() below for how a caretaker
 * actually subscribes. */

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Minutes after a confirmed successful dispense before the "pickup not
 * confirmed" push notification fires. One constant to tune, change this
 * and rebuild, no other code needs to change. */
#define NOTIFY_REMINDER_DELAY_MIN 5

/* Creates the per-slot reminder timers. Call once at boot. */
void notify_init(void);

/* Schedules a "pickup not confirmed" reminder for `slot`, to fire
 * NOTIFY_REMINDER_DELAY_MIN minutes after `dispensed_at` (normally
 * time(NULL) at the moment the Pico ACKs a dispense as "ok"). At fire
 * time, the timer checks the live drawer_sensor state (via
 * bridge_state.awaiting_drawer_open) and only actually sends a
 * notification if the drawer still hasn't been opened since, so a
 * confirmed pickup never triggers a spurious reminder.
 *
 * If a reminder was already pending for this slot, it's replaced, only
 * the most recent dispense's reminder fires, so back-to-back dispenses of
 * the same slot don't queue up duplicate notifications.
 *
 * Silently does nothing if there's no internet connection when the timer
 * fires (checked via wifi_sta_is_connected()), a missed reminder is far
 * better than a crash or a blocked task over a notification that isn't
 * essential to the dispenser's core job. */
void notify_schedule_dispense_reminder(int slot, const char *medication_name, time_t dispensed_at);

/* Sends a notification immediately (no 5-minute delay), for verifying the
 * ESP-to-ntfy.sh chain works, wired to the dashboard's "Test Success Alert"
 * button so this is testable without the Pico attached. */
void notify_send_test(void);

/* Returns this device's ntfy.sh topic name (from ntfy_credentials.h). To
 * receive PortaPill's push notifications, subscribe to this exact topic in
 * the ntfy app (iOS/Android), or just open notify_get_subscribe_url() in a
 * browser and leave the tab open. */
const char *notify_get_topic(void);

/* Returns the full https://ntfy.sh/<topic> URL a caretaker can open in a
 * browser, or paste into the ntfy app, to subscribe. */
const char *notify_get_subscribe_url(void);

#ifdef __cplusplus
}
#endif

#endif
