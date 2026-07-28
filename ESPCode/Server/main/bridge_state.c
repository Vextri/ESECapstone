#include "bridge_state.h"

#include <string.h>
#include <time.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "audio_feedback.h"
#include "led_feedback.h"
#include "uart_bridge.h"

static const char *TAG = "time_server";

#define BRIDGE_NVS_NAMESPACE "bridge_state"

SemaphoreHandle_t bridge_state_mutex;
pico_bridge_state_t bridge_state;

void bridge_copy_string(char *dest, size_t dest_size, const char *src)
{
	if (dest_size == 0) {
		return;
	}

	if (src == NULL) {
		dest[0] = '\0';
		return;
	}

	strncpy(dest, src, dest_size - 1);
	dest[dest_size - 1] = '\0';
}

int bridge_slot_index_from_number(int slot_number)
{
	if (slot_number < 0 || slot_number >= PILL_SLOT_COUNT) {
		return -1;
	}

	return slot_number;
}

const pill_slot_state_t *bridge_get_active_slot_const(const pico_bridge_state_t *state)
{
	int slot_index = bridge_slot_index_from_number(state->active_profile_slot);

	if (slot_index < 0) {
		slot_index = 0;
	}

	return &state->slots[slot_index];
}

bool bridge_state_save_to_nvs(const pico_bridge_state_t *state)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(BRIDGE_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
		return false;
	}

	err = nvs_set_blob(handle, "state", state, sizeof(*state));
	if (err == ESP_OK) {
		err = nvs_commit(handle);
	}
	nvs_close(handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to persist bridge state: %s", esp_err_to_name(err));
		return false;
	}

	return true;
}

bool bridge_state_load_from_nvs(pico_bridge_state_t *state)
{
	nvs_handle_t handle;
	size_t size = sizeof(*state);
	size_t min_legacy_size = offsetof(pico_bridge_state_t, dispense_queue);
	esp_err_t err = nvs_open(BRIDGE_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (state == NULL) {
		return false;
	}

	memset(state, 0, sizeof(*state));

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
		return false;
	}

	err = nvs_get_blob(handle, "state", state, &size);
	nvs_close(handle);
	if (err != ESP_OK) {
		return false;
	}

	/* Accept legacy blobs that predate dispense queue fields. */
	if (size < min_legacy_size || size > sizeof(*state)) {
		return false;
	}

	state->dispense_queue_count = 0;

	return true;
}

static void bridge_reset_slot_defaults(pill_slot_state_t *slot_state, int slot_number)
{
	slot_state->pills_left = -1;
	slot_state->pills_per_dose = -1;
	slot_state->doses_remaining = -1;
	slot_state->total_pills = -1;
	slot_state->slot_number = slot_number;
	slot_state->time_ms = 0;
	strcpy(slot_state->medication_name, "Waiting for data");
	strcpy(slot_state->schedule, "none");
	strcpy(slot_state->last_dispensed, "No confirmed dispense yet");
	strcpy(slot_state->last_event, "Waiting for live data");
	strcpy(slot_state->notes, "Waiting for live pill slot data.");
	strcpy(slot_state->last_dispense_result, "unknown");
	slot_state->has_data = false;
	slot_state->is_active = false;
}

void bridge_state_reset_defaults(pico_bridge_state_t *state)
{
	int slot_index;

	state->connected = false;
	state->active_profile_slot = 0;
	strcpy(state->controller_transport, "UART bridge pending");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		bridge_reset_slot_defaults(&state->slots[slot_index], slot_index);
	}
	state->history_count = 0;
	state->dispense_queue_count = 0;
	strcpy(state->last_ack_action, "none");
	strcpy(state->last_ack_result, "none");
	state->awaiting_dispense_ack = false;
	state->awaiting_drawer_open = false;
	state->drawer_open_slot = -1;
	state->dispense_ack_deadline_us = 0;
	state->last_update_us = 0;
}

void bridge_log_status_locked(pico_bridge_state_t *state, const pill_slot_state_t *slot_state)
{
	int write_index;

	if (state->history_count < DISPENSE_HISTORY_COUNT) {
		write_index = state->history_count;
		state->history_count++;
	} else {
		memmove(&state->history[0],
				&state->history[1],
				(sizeof(state->history[0]) * (DISPENSE_HISTORY_COUNT - 1)));
		write_index = DISPENSE_HISTORY_COUNT - 1;
	}

	state->history[write_index].slot_number = slot_state->slot_number;
	state->history[write_index].pills_left_after = slot_state->pills_left;
	state->history[write_index].esp_timestamp = (int64_t)time(NULL);
	bridge_copy_string(state->history[write_index].medication_name,
				 sizeof(state->history[write_index].medication_name),
				 slot_state->medication_name);
	bridge_copy_string(state->history[write_index].result,
				 sizeof(state->history[write_index].result),
				 slot_state->last_dispense_result);
	bridge_copy_string(state->history[write_index].event,
				 sizeof(state->history[write_index].event),
				 slot_state->last_event);
}

void bridge_mark_dispense_taken_locked(pico_bridge_state_t *state)
{
	int cleared_count = 0;
	int last_cleared_slot = -1;
	time_t now;
	struct tm timeinfo;
	char time_buf[64];
	bool have_time_str;

	if (state == NULL) {
		return;
	}

	now = time(NULL);
	have_time_str = localtime_r(&now, &timeinfo) != NULL &&
			strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &timeinfo) > 0;

	/* One physical drawer/hall sensor serves every station, so a single
	 * drawer-open event must confirm every dispense currently pending
	 * pickup, not just the most recently dispensed one. Previously this
	 * only cleared whichever single slot was last remembered in
	 * drawer_open_slot, so dispensing two stations close together left
	 * the earlier one stuck on "pending" forever, since opening the
	 * drawer again did nothing once awaiting_drawer_open had already
	 * been cleared by the first confirmation. */
	for (int i = 0; i < PILL_SLOT_COUNT; i++) {
		pill_slot_state_t *slot_state = &state->slots[i];

		if (!slot_state->is_active || strcmp(slot_state->last_dispense_result, "pending") != 0) {
			continue;
		}

		bridge_copy_string(slot_state->last_dispensed, sizeof(slot_state->last_dispensed),
				   have_time_str ? time_buf : "Drawer opened (time unavailable)");
		bridge_copy_string(slot_state->last_dispense_result, sizeof(slot_state->last_dispense_result), "ok");
		bridge_copy_string(slot_state->last_event, sizeof(slot_state->last_event),
				   "Drawer opened after dispense. Pills taken.");
		bridge_copy_string(slot_state->notes, sizeof(slot_state->notes),
				   "Hall sensor confirmed drawer open after dispense.");

		bridge_log_status_locked(state, slot_state);
		last_cleared_slot = i;
		cleared_count++;
		ESP_LOGI(TAG, "Dispense confirmed as taken for slot %d by hall trigger", i);
	}

	state->awaiting_drawer_open = false;
	state->drawer_open_slot = -1;

	if (cleared_count > 0) {
		state->active_profile_slot = last_cleared_slot;
		bridge_state_save_to_nvs(state);
		audio_enqueue_event(AUDIO_EVENT_SUCCESS);
		led_enqueue_event(LED_EVENT_SUCCESS, last_cleared_slot);
	}
}

void bridge_update_connected_flag_locked(void)
{
	int64_t age_us = esp_timer_get_time() - bridge_state.last_update_us;

	if (bridge_state.awaiting_dispense_ack &&
		bridge_state.dispense_ack_deadline_us > 0 &&
		esp_timer_get_time() > bridge_state.dispense_ack_deadline_us) {
		pill_slot_state_t *timed_out_slot = &bridge_state.slots[bridge_state.active_profile_slot];
		bridge_state.awaiting_dispense_ack = false;
		bridge_state.awaiting_drawer_open = false;
		bridge_state.drawer_open_slot = -1;
		bridge_state.dispense_ack_deadline_us = 0;
		bridge_copy_string(bridge_state.last_ack_result,
				   sizeof(bridge_state.last_ack_result),
				   "timeout");
		/* Log the timeout as a failed dispense so history shows it */
		bridge_copy_string(timed_out_slot->last_dispense_result,
				   sizeof(timed_out_slot->last_dispense_result),
				   "timeout");
		bridge_copy_string(timed_out_slot->last_event,
				   sizeof(timed_out_slot->last_event),
				   "Dispense timed out - no ACK from Pico.");
		bridge_log_status_locked(&bridge_state, timed_out_slot);
		audio_enqueue_event(AUDIO_EVENT_FAILURE);
		led_enqueue_event(LED_EVENT_FAILURE, bridge_state.active_profile_slot);
		bridge_start_next_dispense_locked(&bridge_state);
	}

	bridge_state.connected = bridge_state.last_update_us > 0 && age_us < UART_BRIDGE_TIMEOUT_US;
	if (!bridge_state.connected) {
		bridge_copy_string(bridge_state.controller_transport,
					   sizeof(bridge_state.controller_transport),
					   "UART bridge pending");
	}
}
