#include "uart_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_feedback.h"
#include "led_feedback.h"
#include "notify.h"
#include "time_utils.h"

static const char *TAG = "time_server";

#define UART_BRIDGE_PORT UART_NUM_1
#define UART_BRIDGE_BAUD 115200
#define UART_BRIDGE_TX_PIN 12
#define UART_BRIDGE_RX_PIN 11
#define UART_BRIDGE_BUFFER_SIZE 512
#define UART_BRIDGE_LINE_SIZE 256

/* Serializes actual writes to the UART port. The heartbeat (uart_bridge_task
 * itself), the LCD's button handler, and the web server's request handlers
 * are all separate FreeRTOS tasks that can each independently decide to
 * send a line at any moment, e.g. a manual "Dispense Now" click landing at
 * the same instant the 60s SET_TIME heartbeat fires. Without a lock around
 * the write, two concurrent uart_write_bytes() calls on the same port can
 * interleave on the wire and glue the tail of one line onto the head of
 * another with no newline between them, exactly what a live capture
 * caught: "CMD|action=SET_TIME|epoch=178526CMD|action=DISPENSE|slot=0"
 * arriving as a single garbled line, silently swallowing the DISPENSE
 * command entirely (the Pico's parser only ever sees the first action= in
 * the merged line). This directly explains the intermittent stuck-dispense
 * bug: it only happens when two sends race, which is far likelier right
 * after a failed dispense since that takes 30+ seconds, shifting the
 * heartbeat's timing to land right as someone retries. */
static SemaphoreHandle_t s_uart_tx_mutex;

static void uart_bridge_send_line(const char *line)
{
	if (line == NULL) {
		return;
	}

	if (s_uart_tx_mutex != NULL) {
		xSemaphoreTake(s_uart_tx_mutex, portMAX_DELAY);
	}
	uart_write_bytes(UART_BRIDGE_PORT, line, strlen(line));
	ESP_LOGI(TAG, "UART TX: %s", line);
	if (s_uart_tx_mutex != NULL) {
		xSemaphoreGive(s_uart_tx_mutex);
	}
}

bool bridge_send_set_time(void)
{
	char line[96];
	time_t now = time(NULL);

	if (!bridge_is_time_valid()) {
		ESP_LOGW(TAG, "Skipping SET_TIME because current epoch is invalid");
		return false;
	}

	snprintf(line, sizeof(line), "CMD|action=SET_TIME|epoch=%lld\n", (long long)now);
	uart_bridge_send_line(line);
	return true;
}

void bridge_send_dispense_for_slot(int slot_number)
{
	char line[64];

	if (slot_number < 0) {
		snprintf(line, sizeof(line), "CMD|action=DISPENSE\n");
	} else {
		snprintf(line, sizeof(line), "CMD|action=DISPENSE|slot=%d\n", slot_number);
	}

	uart_bridge_send_line(line);
}

static void bridge_start_dispense_locked(pico_bridge_state_t *state, int slot_number, bool is_scheduled)
{
	if (state == NULL || bridge_slot_index_from_number(slot_number) < 0) {
		return;
	}

	bridge_send_dispense_for_slot(slot_number);
	state->active_profile_slot = slot_number;
	state->awaiting_dispense_ack = true;
	state->active_dispense_is_scheduled = is_scheduled;
	/* A fresh dispense supersedes any earlier drawer-open wait (e.g. this
	 * slot's previous dispense was never picked up before a new one fired). */
	state->awaiting_drawer_open = false;
	state->drawer_open_slot = -1;
	state->dispense_ack_deadline_us = esp_timer_get_time() + DISPENSE_ACK_TIMEOUT_US;
	bridge_copy_string(state->last_ack_action, sizeof(state->last_ack_action), "DISPENSE");
	bridge_copy_string(state->last_ack_result, sizeof(state->last_ack_result), "pending");
}

bool bridge_enqueue_dispense_slot_locked(pico_bridge_state_t *state, int slot_number, bool is_scheduled)
{
	if (state == NULL || bridge_slot_index_from_number(slot_number) < 0) {
		return false;
	}

	if (state->awaiting_dispense_ack && state->active_profile_slot == slot_number) {
		return true;
	}

	for (int i = 0; i < state->dispense_queue_count; i++) {
		if (state->dispense_queue[i] == slot_number) {
			return true;
		}
	}

	if (state->dispense_queue_count >= PILL_SLOT_COUNT) {
		return false;
	}

	state->dispense_queue_is_scheduled[state->dispense_queue_count] = is_scheduled;
	state->dispense_queue[state->dispense_queue_count++] = slot_number;
	return true;
}

bool bridge_start_next_dispense_locked(pico_bridge_state_t *state)
{
	int next_slot;
	bool next_is_scheduled;

	if (state == NULL || state->awaiting_dispense_ack || state->dispense_queue_count <= 0) {
		return false;
	}

	next_slot = state->dispense_queue[0];
	next_is_scheduled = state->dispense_queue_is_scheduled[0];
	if (state->dispense_queue_count > 1) {
		memmove(&state->dispense_queue[0],
			&state->dispense_queue[1],
			(size_t)(state->dispense_queue_count - 1) * sizeof(state->dispense_queue[0]));
		memmove(&state->dispense_queue_is_scheduled[0],
			&state->dispense_queue_is_scheduled[1],
			(size_t)(state->dispense_queue_count - 1) * sizeof(state->dispense_queue_is_scheduled[0]));
	}
	state->dispense_queue_count--;
	bridge_start_dispense_locked(state, next_slot, next_is_scheduled);
	return true;
}

void bridge_send_load_profile_for_slot(const pill_slot_state_t *slot_state)
{
	char line[256];
	int total_pills;

	if (slot_state == NULL || !slot_state->is_active) {
		return;
	}

	total_pills = slot_state->pills_left >= 0 ? slot_state->pills_left : slot_state->total_pills;
	if (total_pills <= 0 || slot_state->pills_per_dose <= 0) {
		ESP_LOGW(TAG, "Skipping LOAD_PROFILE for slot %d due to incomplete data", slot_state->slot_number);
		return;
	}

	if (strcmp(slot_state->schedule, "none") == 0 || slot_state->schedule[0] == '\0') {
		snprintf(line,
			 sizeof(line),
			 "CMD|action=LOAD_PROFILE|slot=%d|med=%s|total=%d|dose=%d|time=%lu\n",
			 slot_state->slot_number,
			 slot_state->medication_name,
			 total_pills,
			 slot_state->pills_per_dose,
			 (unsigned long)slot_state->time_ms);
	} else {
		snprintf(line,
			 sizeof(line),
			 "CMD|action=LOAD_PROFILE|slot=%d|med=%s|total=%d|dose=%d|time=%lu|schedule=%s\n",
			 slot_state->slot_number,
			 slot_state->medication_name,
			 total_pills,
			 slot_state->pills_per_dose,
			 (unsigned long)slot_state->time_ms,
			 slot_state->schedule);
	}

	uart_bridge_send_line(line);
}

static int bridge_extract_slot_number(const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');

		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		if (strcmp(token, "slot") == 0) {
			return atoi(separator + 1);
		}
	}

	return -1;
}

/* Marks the bridge as connected, since any recognized line at all (BOOT_SYNC,
 * STATUS, or ACK) is proof the Pico is alive and talking, not just STATUS
 * specifically. Caller must hold bridge_state_mutex. */
static void bridge_mark_link_alive_locked(pico_bridge_state_t *state)
{
	state->last_update_us = esp_timer_get_time();
	state->connected = true;
	bridge_copy_string(state->controller_transport, sizeof(state->controller_transport), "UART linked");
}

static void bridge_handle_ack_line(pico_bridge_state_t *state, const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;
	int ack_slot = -1;
	char action[32] = "unknown";
	char result[32] = "unknown";

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');

		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		if (strcmp(token, "action") == 0) {
			bridge_copy_string(action, sizeof(action), separator + 1);
		} else if (strcmp(token, "result") == 0) {
			bridge_copy_string(result, sizeof(result), separator + 1);
		} else if (strcmp(token, "slot") == 0) {
			ack_slot = atoi(separator + 1);
		}
	}

	bridge_copy_string(state->last_ack_action, sizeof(state->last_ack_action), action);
	bridge_copy_string(state->last_ack_result, sizeof(state->last_ack_result), result);
	if (strcmp(action, "DISPENSE") == 0) {
		int dispense_slot = bridge_slot_index_from_number(ack_slot) >= 0 ? ack_slot : state->active_profile_slot;

		if (bridge_slot_index_from_number(dispense_slot) < 0) {
			dispense_slot = 0;
		}

		state->awaiting_dispense_ack = false;
		state->dispense_ack_deadline_us = 0;
		if (strcmp(result, "ok") == 0) {
			pill_slot_state_t *slot_state = &state->slots[dispense_slot];

			/* Not confirmed as taken yet. The motor ran, but nobody has
			 * necessarily opened the drawer. bridge_mark_dispense_taken_locked()
			 * (called from drawer_sensor.c) finishes this once the hall
			 * sensor confirms pickup. */
			state->awaiting_drawer_open = true;
			state->drawer_open_slot = dispense_slot;
			state->active_profile_slot = dispense_slot;
			bridge_copy_string(slot_state->last_dispense_result,
					   sizeof(slot_state->last_dispense_result), "pending");
			bridge_copy_string(slot_state->last_event, sizeof(slot_state->last_event),
					   "Dispensed. Waiting for drawer-open confirmation.");

			/* Immediate "pill dispensed" alert, the moment the Pico confirms the
			 * motor/sensors succeeded, same instant on the LCD and the dashboard.
			 * The separate "taken" alert still fires later, only once the drawer
			 * sensor confirms pickup (see bridge_mark_dispense_taken_locked()). */
			audio_enqueue_event(AUDIO_EVENT_SUCCESS);
			led_enqueue_event(LED_EVENT_SUCCESS, dispense_slot);

			notify_schedule_dispense_reminder(dispense_slot, slot_state->medication_name, time(NULL));
			if (state->active_dispense_is_scheduled) {
				notify_send_dispensed(dispense_slot, slot_state->medication_name);
			}

			ESP_LOGI(TAG, "Dispense for slot %d acknowledged. Waiting for hall trigger to mark taken.",
				 dispense_slot);
		} else if (strcmp(result, "fail") == 0 || strcmp(result, "timeout") == 0) {
			/* Deliberately does NOT touch awaiting_drawer_open/drawer_open_slot
			 * here. Those are a single shared pair, not per-slot, so clearing
			 * them on any failure would also wipe out a genuinely still-pending
			 * drawer-open wait for a completely different station that
			 * dispensed successfully earlier. This slot's own result is
			 * already correctly "fail"/"timeout" via last_ack_result above,
			 * nothing else needs clearing for it. */
			audio_enqueue_event(AUDIO_EVENT_FAILURE);
			led_enqueue_event(LED_EVENT_FAILURE, ack_slot);
		}
		bridge_start_next_dispense_locked(state);
	}
	if (ack_slot >= 0 && ack_slot < PILL_SLOT_COUNT) {
		state->active_profile_slot = ack_slot;
	}

	ESP_LOGI(TAG, "Pico ACK action=%s slot=%d result=%s", action, ack_slot, result);
	bridge_state_save_to_nvs(state);
}

static void bridge_apply_boot_sync_field(pill_slot_state_t *slot_state, const char *key, const char *value)
{
	if (strcmp(key, "med") == 0) {
		bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), value);
	} else if (strcmp(key, "left") == 0) {
		slot_state->pills_left = atoi(value);
	} else if (strcmp(key, "dose") == 0) {
		slot_state->pills_per_dose = atoi(value);
	} else if (strcmp(key, "doses") == 0) {
		slot_state->doses_remaining = atoi(value);
	} else if (strcmp(key, "schedule") == 0) {
		bridge_copy_string(slot_state->schedule, sizeof(slot_state->schedule), value);
	}
}

static void bridge_handle_boot_sync_line(pico_bridge_state_t *state, const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;
	int slot_number;
	int slot_index;
	pill_slot_state_t *slot_state;

	slot_number = bridge_extract_slot_number(line);
	slot_index = bridge_slot_index_from_number(slot_number);
	if (slot_index < 0) {
		ESP_LOGW(TAG, "Ignoring BOOT_SYNC with invalid slot number: %d", slot_number);
		return;
	}

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	slot_state = &state->slots[slot_index];
	slot_state->slot_number = slot_number;
	slot_state->is_active = true;
	slot_state->has_data = true;
	state->active_profile_slot = slot_number;
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');

		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		bridge_apply_boot_sync_field(slot_state, token, separator + 1);
	}

	if (slot_state->pills_left >= 0) {
		slot_state->total_pills = slot_state->pills_left;
	}
	bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), "State mirrored from Pico BOOT_SYNC.");
	bridge_state_save_to_nvs(state);
	ESP_LOGI(TAG, "Pico BOOT_SYNC applied for slot %d", slot_number);
}

static void bridge_apply_field(pico_bridge_state_t *state, pill_slot_state_t *slot_state, const char *key, const char *value)
{
	if (strcmp(key, "med") == 0) {
		bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), value);
	} else if (strcmp(key, "slot") == 0) {
		state->active_profile_slot = atoi(value);
	} else if (strcmp(key, "left") == 0) {
		/* Edge-triggered, not level-triggered: only fires the instant the
		 * count actually crosses into 1 or 0, comparing against whatever it
		 * was a moment ago. That way repeated STATUS lines while sitting at
		 * the same low count don't spam duplicate alerts, and a refill
		 * (pills_left jumping back up) naturally re-arms both alerts for
		 * the next time it runs low, no separate "already warned" flag to
		 * remember and reset. */
		int old_left = slot_state->pills_left;
		int new_left = atoi(value);

		slot_state->pills_left = new_left;
		notify_check_pill_level(slot_state->slot_number, slot_state->medication_name, old_left, new_left);
	} else if (strcmp(key, "dose") == 0) {
		slot_state->pills_per_dose = atoi(value);
	} else if (strcmp(key, "doses") == 0) {
		slot_state->doses_remaining = atoi(value);
	} else if (strcmp(key, "total") == 0) {
		slot_state->total_pills = atoi(value);
	} else if (strcmp(key, "time") == 0) {
		slot_state->time_ms = (uint32_t)strtoul(value, NULL, 10);
	} else if (strcmp(key, "last") == 0) {
		bridge_copy_string(slot_state->last_dispensed, sizeof(slot_state->last_dispensed), value);
	} else if (strcmp(key, "event") == 0) {
		bridge_copy_string(slot_state->last_event, sizeof(slot_state->last_event), value);
	} else if (strcmp(key, "notes") == 0) {
		bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), value);
	} else if (strcmp(key, "result") == 0) {
		/* Don't let a STATUS line's "ok" jump ahead of the drawer-open
		 * confirmation this slot is still waiting on. */
		if (state->awaiting_drawer_open && state->drawer_open_slot == slot_state->slot_number &&
		    strcmp(value, "ok") == 0) {
			bridge_copy_string(slot_state->last_dispense_result,
					   sizeof(slot_state->last_dispense_result), "pending");
		} else {
			bridge_copy_string(slot_state->last_dispense_result, sizeof(slot_state->last_dispense_result), value);
		}
	} else if (strcmp(key, "schedule") == 0) {
		bridge_copy_string(slot_state->schedule, sizeof(slot_state->schedule), value);
	}
}

static void bridge_handle_status_line(pico_bridge_state_t *state, const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;
	pill_slot_state_t *slot_state;
	int slot_number;
	int slot_index;

	slot_number = bridge_extract_slot_number(line);
	if (slot_number < 0) {
		slot_number = state->active_profile_slot;
	}

	slot_index = bridge_slot_index_from_number(slot_number);
	if (slot_index < 0) {
		ESP_LOGW(TAG, "Ignoring STATUS update with invalid slot number: %d", slot_number);
		return;
	}

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	slot_state = &state->slots[slot_index];
	state->active_profile_slot = slot_number;
	slot_state->slot_number = slot_number;
	slot_state->is_active = true;
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');
		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		bridge_apply_field(state, slot_state, token, separator + 1);
	}

	slot_state->has_data = true;
	bridge_mark_link_alive_locked(state);
	bridge_log_status_locked(state, slot_state);
	bridge_state_save_to_nvs(state);

	ESP_LOGI(TAG, "Pico status updated over UART");
}

static void bridge_process_uart_line(char *line)
{
	char raw_line[UART_BRIDGE_LINE_SIZE];
	char *type_end;

	if (line == NULL || line[0] == '\0') {
		return;
	}

	bridge_copy_string(raw_line, sizeof(raw_line), line);
	type_end = strchr(raw_line, '|');
	if (type_end != NULL) {
		*type_end = '\0';
	}

	if (strcmp(raw_line, "TIME_REQ") == 0) {
		ESP_LOGI(TAG, "Pico requested current time");
		if (!bridge_send_set_time()) {
			ESP_LOGW(TAG, "Unable to answer TIME_REQ because ESP time is not valid yet");
		}
		return;
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
		return;
	}

	if (strcmp(raw_line, "BOOT_SYNC") == 0) {
		bridge_handle_boot_sync_line(&bridge_state, line);
		bridge_mark_link_alive_locked(&bridge_state);
	} else if (strcmp(raw_line, "STATUS") == 0) {
		bridge_handle_status_line(&bridge_state, line);
	} else if (strcmp(raw_line, "ACK") == 0) {
		bridge_handle_ack_line(&bridge_state, line);
		bridge_mark_link_alive_locked(&bridge_state);
	} else {
		ESP_LOGI(TAG, "UART RX: %s", line);
	}

	xSemaphoreGive(bridge_state_mutex);
}

/* Checks every active slot's schedule against the current time and enqueues a
 * dispense if a scheduled time matches.  Must be called with the bridge state
 * mutex already held. */
static void bridge_check_schedule_locked(pico_bridge_state_t *state)
{
	/* Stores the minute-boundary timestamp of the last auto-fire per slot so
	 * we never queue the same slot twice in the same clock minute. */
	static time_t last_fired_minute[PILL_SLOT_COUNT];
	time_t now;
	struct tm ti;
	int current_minutes;
	time_t now_minute;
	int si;
	bool any_queued = false;

	now = time(NULL);
	if (now <= 1700000000 || localtime_r(&now, &ti) == NULL) {
		return; /* clock not set yet */
	}

	current_minutes = ti.tm_hour * 60 + ti.tm_min;
	now_minute = now - ti.tm_sec; /* truncate to minute boundary */

	for (si = 0; si < PILL_SLOT_COUNT; si++) {
		const pill_slot_state_t *slot = &state->slots[si];
		char schedule_copy[40];
		char *saveptr = NULL;
		char *token;

		if (!slot->is_active || slot->schedule[0] == '\0' ||
		    strcmp(slot->schedule, "none") == 0) {
			continue;
		}

		if (slot->pills_left == 0) {
			continue; /* empty, skip */
		}

		if (last_fired_minute[si] == now_minute) {
			continue; /* already queued this minute */
		}

		bridge_copy_string(schedule_copy, sizeof(schedule_copy), slot->schedule);
		token = strtok_r(schedule_copy, ",", &saveptr);
		while (token != NULL) {
			int event_minutes;

			if (screen_parse_hhmm(token, &event_minutes) &&
			    event_minutes == current_minutes) {
				if (bridge_enqueue_dispense_slot_locked(state, si, true)) {
					last_fired_minute[si] = now_minute;
					ESP_LOGI(TAG,
						 "Schedule: queued auto-dispense slot %d at %02d:%02d",
						 si, ti.tm_hour, ti.tm_min);
					any_queued = true;
				}
				break;
			}
			token = strtok_r(NULL, ",", &saveptr);
		}
	}

	if (any_queued) {
		bridge_start_next_dispense_locked(state);
		bridge_state_save_to_nvs(state);
	}
}

static void uart_bridge_task(void *arg)
{
	char line_buffer[UART_BRIDGE_LINE_SIZE];
	size_t line_length = 0;
	uint8_t rx_buffer[64];
	bool discarding_line = false;
	int64_t last_heartbeat_us = 0;
	int64_t last_status_print_us = 0;

	(void)arg;

	while (1) {
		int bytes_read = uart_read_bytes(UART_BRIDGE_PORT,
						   rx_buffer,
						   sizeof(rx_buffer),
						   pdMS_TO_TICKS(100));
		if (bytes_read > 0) {
			for (int index = 0; index < bytes_read; ++index) {
				char current = (char)rx_buffer[index];

				if (current == '\r') {
					continue;
				}

				if (current == '\n') {
					if (!discarding_line) {
						line_buffer[line_length] = '\0';
					}
					if (!discarding_line && line_length > 0) {
						bridge_process_uart_line(line_buffer);
					}
					line_length = 0;
					discarding_line = false;
					continue;
				}

				if (discarding_line) {
					continue;
				}

				if (line_length < (sizeof(line_buffer) - 1)) {
					line_buffer[line_length++] = current;
				} else {
					discarding_line = true;
					line_length = 0;
				}
			}
		}

		if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
			bridge_update_connected_flag_locked();
			bridge_check_schedule_locked(&bridge_state);
			xSemaphoreGive(bridge_state_mutex);
		}

		/* Heartbeat: re-send SET_TIME periodically so the Pico's ACK keeps
		 * "connected" accurate between real events (see
		 * UART_BRIDGE_HEARTBEAT_INTERVAL_US for why). Harmless no-op on the
		 * Pico side if the epoch hasn't meaningfully changed.
		 *
		 * Skipped entirely while a dispense is in flight (awaiting_dispense_ack).
		 * The Pico's sensor-based dispense retry loop blocks its whole superloop
		 * for up to ~34s and never services UART during that window, so anything
		 * sent while it's busy just sits in its small hardware RX FIFO until it
		 * frees up, and can get dropped outright if a later command overflows
		 * that FIFO before the Pico gets back around to reading it. last_heartbeat_us
		 * is deliberately left untouched while suppressed, so the moment the
		 * dispense finishes and awaiting_dispense_ack clears, the overdue check
		 * below fires right away instead of waiting out the rest of the interval. */
		int64_t now_us = esp_timer_get_time();
		bool dispense_in_flight = false;

		if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
			dispense_in_flight = bridge_state.awaiting_dispense_ack;
			xSemaphoreGive(bridge_state_mutex);
		}

		if (!dispense_in_flight && now_us - last_heartbeat_us >= UART_BRIDGE_HEARTBEAT_INTERVAL_US) {
			bridge_send_set_time();
			last_heartbeat_us = now_us;
		}

		/* Clear, unambiguous link status, separate from the scrolling
		 * per-message protocol logs, so it's obvious at a glance whether
		 * the Pico is actually connected right now, not just inferred from
		 * reading individual UART TX/RX lines. */
		if (now_us - last_status_print_us >= UART_BRIDGE_STATUS_PRINT_INTERVAL_US) {
			bool connected_snapshot = false;
			int64_t last_update_snapshot = 0;

			if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
				connected_snapshot = bridge_state.connected;
				last_update_snapshot = bridge_state.last_update_us;
				xSemaphoreGive(bridge_state_mutex);
			}

			if (connected_snapshot) {
				int64_t age_sec = (now_us - last_update_snapshot) / 1000000;
				ESP_LOGI(TAG,
					 "[LINK] ESP <-> Pico: CONNECTED (last heard %llds ago) | this board's UART1 pins: TX=GPIO%d RX=GPIO%d @%dbaud",
					 (long long)age_sec, UART_BRIDGE_TX_PIN, UART_BRIDGE_RX_PIN, UART_BRIDGE_BAUD);
			} else if (last_update_snapshot > 0) {
				int64_t age_sec = (now_us - last_update_snapshot) / 1000000;
				ESP_LOGW(TAG,
					 "[LINK] ESP <-> Pico: NOT CONNECTED (last heard %llds ago, times out after %llds) | this board's UART1 pins: TX=GPIO%d RX=GPIO%d @%dbaud",
					 (long long)age_sec, (long long)(UART_BRIDGE_TIMEOUT_US / 1000000),
					 UART_BRIDGE_TX_PIN, UART_BRIDGE_RX_PIN, UART_BRIDGE_BAUD);
			} else {
				ESP_LOGW(TAG,
					 "[LINK] ESP <-> Pico: NOT CONNECTED (never heard from it since boot) | this board's UART1 pins: TX=GPIO%d RX=GPIO%d @%dbaud",
					 UART_BRIDGE_TX_PIN, UART_BRIDGE_RX_PIN, UART_BRIDGE_BAUD);
			}
			last_status_print_us = now_us;
		}
	}
}

void start_uart_bridge(void)
{
	const uart_config_t uart_config = {
		.baud_rate = UART_BRIDGE_BAUD,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	bridge_state_mutex = xSemaphoreCreateMutex();
	if (bridge_state_mutex == NULL) {
		ESP_LOGE(TAG, "Failed to create UART bridge mutex");
		return;
	}

	s_uart_tx_mutex = xSemaphoreCreateMutex();
	if (s_uart_tx_mutex == NULL) {
		ESP_LOGE(TAG, "Failed to create UART TX mutex");
		return;
	}

	if (!bridge_state_load_from_nvs(&bridge_state)) {
		bridge_state_reset_defaults(&bridge_state);
		bridge_state_save_to_nvs(&bridge_state);
	} else {
		int slot_index;

		bridge_state.connected = false;
		bridge_state.last_update_us = 0;
		/* Dispense-ack state is transient. Never restore it across reboots.
		 * The old deadline_us would be stale and the timeout would never fire. */
		bridge_state.awaiting_dispense_ack = false;
		bridge_state.active_dispense_is_scheduled = false;
		bridge_state.awaiting_drawer_open = false;
		bridge_state.drawer_open_slot = -1;
		bridge_state.dispense_ack_deadline_us = 0;
		bridge_copy_string(bridge_state.controller_transport,
					   sizeof(bridge_state.controller_transport),
					   "UART bridge pending");
		for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
			bridge_state.slots[slot_index].slot_number = slot_index;
		}
		if (bridge_slot_index_from_number(bridge_state.active_profile_slot) < 0) {
			bridge_state.active_profile_slot = 0;
		}
	}

	ESP_ERROR_CHECK(uart_driver_install(UART_BRIDGE_PORT,
					     UART_BRIDGE_BUFFER_SIZE,
					     0,
					     0,
					     NULL,
					     0));
	ESP_ERROR_CHECK(uart_param_config(UART_BRIDGE_PORT, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_BRIDGE_PORT,
					  UART_BRIDGE_TX_PIN,
					  UART_BRIDGE_RX_PIN,
					  UART_PIN_NO_CHANGE,
					  UART_PIN_NO_CHANGE));

	ESP_LOGI(TAG,
		 "UART bridge ready on ESP GPIO%d(TX) and GPIO%d(RX). Waiting for Pico to push STATUS events.",
		 UART_BRIDGE_TX_PIN,
		 UART_BRIDGE_RX_PIN);
	xTaskCreate(uart_bridge_task, "uart_bridge", 4096, NULL, 5, NULL);
}
