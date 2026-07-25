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

#define PILL_SLOT_COUNT 5
#define DISPENSE_HISTORY_COUNT 16
#define LOW_PILL_THRESHOLD 5

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

typedef struct {
	int slot_number;
	int pills_left_after;
	int64_t esp_timestamp;
	char medication_name[32];
	char result[16];
	char event[96];
} dispense_history_entry_t;

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
	int64_t dispense_ack_deadline_us;
	int64_t last_update_us;
	int dispense_queue[PILL_SLOT_COUNT];
	int dispense_queue_count;
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

/* Refreshes bridge_state.connected based on last_update_us, and resolves a
 * timed-out dispense ack if the deadline has passed. Caller must hold
 * bridge_state_mutex. */
void bridge_update_connected_flag_locked(void);

#ifdef __cplusplus
}
#endif

#endif
