/* ============================================================================
 * NOTIFY.H - Caretaker Notification System
 * ----------------------------------------------------------------------------
 * Sends push notifications (via the free ntfy.sh service) to whoever has
 * subscribed to this device's topic: escalating "pickup not confirmed"
 * reminders, low/empty pill alerts, and scheduled-dose confirmations. See
 * ntfy_credentials.h for the topic itself, and notify_get_topic()/
 * notify_get_subscribe_url() below for how a caretaker actually subscribes.
 * ============================================================================ */

#ifndef NOTIFY_H
#define NOTIFY_H

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How many escalating "pickup not confirmed" reminders fire per dispense,
 * and how many minutes after the dispense each one fires. All are armed at
 * once when a dispense succeeds; the wording gets more urgent at each
 * later stage. This array is the one place to edit to change the
 * schedule, e.g. add a 5th stage or change the spacing, notify.c reads it
 * directly, nothing else needs to change. */
#define NOTIFY_REMINDER_STAGE_COUNT 4
#define NOTIFY_REMINDER_DELAYS_MIN { 5, 10, 15, 20 }

/* Creates the per-slot, per-stage reminder timers. Call once at boot. */
void notify_init(void);

/* Arms all NOTIFY_REMINDER_STAGE_COUNT "pickup not confirmed" reminders for
 * `slot`, at NOTIFY_REMINDER_DELAYS_MIN minutes after `dispensed_at`
 * (normally time(NULL) at the moment the Pico ACKs a dispense as "ok").
 * Each stage independently checks this slot's live state right before
 * sending, and only actually notifies if the drawer still hasn't been
 * opened by then, so a confirmed pickup never triggers a spurious
 * reminder, no matter how many stages were armed.
 *
 * If reminders were already pending for this slot, they're all replaced,
 * only the most recent dispense's reminders fire, so back-to-back
 * dispenses of the same slot don't queue up duplicate notifications.
 *
 * Silently does nothing if there's no internet connection when a timer
 * fires (checked via wifi_sta_is_connected()), a missed reminder is far
 * better than a crash or a blocked task over a notification that isn't
 * essential to the dispenser's core job. */
void notify_schedule_dispense_reminder(int slot, const char *medication_name, time_t dispensed_at);

/* Sends a notification immediately (no 5-minute delay), for verifying the
 * ESP-to-ntfy.sh chain works, wired to the dashboard's "Test Success Alert"
 * button so this is testable without the Pico attached. */
void notify_send_test(void);

/* Checks a station's pill count for a low/empty transition and sends a
 * "please refill" push notification if it just crossed into 1 pill left or
 * 0 pills left. Edge-triggered on old_left -> new_left, call this every time
 * a station's pills_left is updated with the previous and new values, it
 * only actually notifies on the instant the count crosses a threshold, not
 * every time it happens to still be low, and naturally re-arms itself after
 * a refill. */
void notify_check_pill_level(int slot, const char *medication_name, int old_left, int new_left);

/* Sends a "dose dispensed" push notification for a scheduled dispense only.
 * Call this from the DISPENSE ACK path, but only when the dispense that was
 * just acknowledged was started by the schedule checker, not a manual
 * "Dispense Now" click or LCD dispense, so routine testing doesn't spam a
 * caretaker's phone with notifications for doses that were never actually
 * due. */
void notify_send_dispensed(int slot, const char *medication_name);

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
