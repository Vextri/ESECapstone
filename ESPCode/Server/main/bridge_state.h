/* ============================================================================
 * BRIDGE_STATE.H - Shared Dispenser State
 * ----------------------------------------------------------------------------
 * Defines pico_bridge_state_t, the single in-RAM record of everything the
 * ESP knows about the dispenser: each slot's medication and pill count, the
 * status of the currently in-flight dispense, and recent history. Every
 * other module (uart_bridge, web_server, lcd_display, notify, drawer_sensor)
 * reads and writes through this one shared struct, so it is the source of
 * truth for the whole system. Access must always be guarded by
 * bridge_state_mutex, this header does not enforce that, callers are
 * responsible for taking/releasing the lock around any read or write.
 * ============================================================================ */

#ifndef BRIDGE_STATE_H
#define BRIDGE_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Matches the dispenser's 3 physical carousel positions (see the Pico's
 * MAX_PROFILES in pill_dispenser.h, which must stay in sync with this). */
#define PILL_SLOT_COUNT 3
#define DISPENSE_HISTORY_COUNT 16
#define LOW_PILL_THRESHOLD 5

/* ----------------------------------------------------------------------------
 * pill_slot_state_t
 * ----------------------------------------------------------------------------
 * One dispenser slot's full profile: what medication is loaded, how many
 * pills are left, the dose schedule, and the outcome of its most recent
 * dispense attempt. has_data distinguishes an empty slot from a real one;
 * is_active marks whether the slot is currently enabled for scheduling.
 * ---------------------------------------------------------------------------- */
typedef struct {
	int pills_left;
	int pills_per_dose;
	int doses_remaining;
	int total_pills;
	int slot_number;
	uint32_t time_ms;
	char medication_name[32];
	char schedule[40];
	char last_dispensed[64];
	char last_event[96];
	char notes[96];
	char last_dispense_result[16];
	bool has_data;
	bool is_active;
} pill_slot_state_t;

/* ----------------------------------------------------------------------------
 * dispense_history_entry_t
 * ----------------------------------------------------------------------------
 * One row of the dispense log shown on the dashboard's history tab. A
 * fixed-size ring of DISPENSE_HISTORY_COUNT of these is kept per device,
 * oldest entries drop off once it fills.
 * ---------------------------------------------------------------------------- */
typedef struct {
	int slot_number;
	int pills_left_after;
	int64_t esp_timestamp;
	char medication_name[32];
	char result[16];
	char event[96];
} dispense_history_entry_t;

/* ----------------------------------------------------------------------------
 * pico_bridge_state_t
 * ----------------------------------------------------------------------------
 * The full shared state record. Beyond the per-slot data, this tracks the
 * live status of the UART link to the Pico (connected, last ACK) and the
 * state machine for a dispense currently in progress: which slot is
 * running, whether it is still waiting on an ACK from the Pico, and
 * whether it is still waiting on the drawer to be opened for pickup
 * confirmation. A small FIFO queue (dispense_queue) lets more than one
 * slot's dispense be requested back to back without losing a request.
 * ---------------------------------------------------------------------------- */
typedef struct {
	bool connected;
	int active_profile_slot;
	char controller_transport[32];
	pill_slot_state_t slots[PILL_SLOT_COUNT];
	dispense_history_entry_t history[DISPENSE_HISTORY_COUNT];
	int history_count;
	char last_ack_action[32];
	char last_ack_result[32];
	bool awaiting_dispense_ack;
	/* Set once the Pico ACKs a dispense as "ok". Pickup isn't considered
	 * confirmed until the drawer's hall sensor triggers (see
	 * drawer_sensor.c) and calls bridge_mark_dispense_taken_locked(), which
	 * clears this. */
	bool awaiting_drawer_open;
	int drawer_open_slot;
	int64_t dispense_ack_deadline_us;
	int64_t last_update_us;
	int dispense_queue[PILL_SLOT_COUNT];
	/* Parallel to dispense_queue: whether each queued slot was queued by the
	 * schedule checker (true) or a manual trigger, web "Dispense Now" or the
	 * LCD (false). Read at the moment each entry is popped and started, see
	 * active_dispense_is_scheduled. */
	bool dispense_queue_is_scheduled[PILL_SLOT_COUNT];
	int dispense_queue_count;
	/* Whether the dispense currently awaiting_dispense_ack was started by the
	 * schedule checker rather than a manual trigger. Only meaningful while
	 * awaiting_dispense_ack is true; used at ACK time to decide whether to
	 * send the "dose dispensed" notification (scheduled doses only, so
	 * routine manual testing doesn't spam a caretaker's phone). */
	bool active_dispense_is_scheduled;
} pico_bridge_state_t;

/* Shared bridge state, defined in bridge_state.c. Guard all access with
 * bridge_state_mutex (except during startup before the scheduler/other
 * tasks are running). */
extern SemaphoreHandle_t bridge_state_mutex;
extern pico_bridge_state_t bridge_state;

void bridge_copy_string(char *dest, size_t dest_size, const char *src);
int bridge_slot_index_from_number(int slot_number);
const pill_slot_state_t *bridge_get_active_slot_const(const pico_bridge_state_t *state);

bool bridge_state_save_to_nvs(const pico_bridge_state_t *state);
bool bridge_state_load_from_nvs(pico_bridge_state_t *state);
void bridge_state_reset_defaults(pico_bridge_state_t *state);

/* Appends a dispense-history entry for slot_state. Caller must hold
 * bridge_state_mutex. */
void bridge_log_status_locked(pico_bridge_state_t *state, const pill_slot_state_t *slot_state);

/* Confirms every currently-pending dispense as picked up (drawer opened),
 * called from drawer_sensor.c when the hall sensor triggers while
 * awaiting_drawer_open is true. One physical drawer serves every station,
 * so this clears *all* slots whose last_dispense_result is "pending", not
 * just one, marks each "ok", clears the awaiting_drawer_open wait, logs
 * history, and plays success feedback once. Caller must hold
 * bridge_state_mutex. */
void bridge_mark_dispense_taken_locked(pico_bridge_state_t *state);

/* Refreshes bridge_state.connected based on last_update_us, and resolves a
 * timed-out dispense ack if the deadline has passed. Caller must hold
 * bridge_state_mutex. */
void bridge_update_connected_flag_locked(void);

#ifdef __cplusplus
}
#endif

#endif
