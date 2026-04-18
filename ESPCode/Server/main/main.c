#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "time_server";

#define AP_SSID "ESP-Time-Server"
#define AP_PASS "123456789"
#define AP_MAX_CONN 4

#define DNS_PORT 53
#define AP_IP_OCTET_1 192
#define AP_IP_OCTET_2 168
#define AP_IP_OCTET_3 4
#define AP_IP_OCTET_4 1

#define UART_BRIDGE_PORT UART_NUM_1
#define UART_BRIDGE_BAUD 115200
#define UART_BRIDGE_TX_PIN 12
#define UART_BRIDGE_RX_PIN 11
#define UART_BRIDGE_BUFFER_SIZE 512
#define UART_BRIDGE_LINE_SIZE 256
#define UART_BRIDGE_TIMEOUT_US (5 * 1000000)
#define PILL_SLOT_COUNT 5
#define STATUS_RESPONSE_BUFFER_SIZE 4096
#define STATUS_SLOTS_BUFFER_SIZE 3072
#define DISPENSE_HISTORY_COUNT 16
#define BRIDGE_NVS_NAMESPACE "bridge_state"
#define HTTP_BODY_BUFFER_SIZE 512
#define DISPENSE_ACK_TIMEOUT_US (40 * 1000000LL)

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
} pico_bridge_state_t;

static SemaphoreHandle_t bridge_state_mutex;
static pico_bridge_state_t bridge_state;

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

static void bridge_state_reset_defaults(pico_bridge_state_t *state)
{
	int slot_index;

	state->connected = false;
	state->active_profile_slot = 0;
	strcpy(state->controller_transport, "UART bridge pending");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		bridge_reset_slot_defaults(&state->slots[slot_index], slot_index);
	}
	strcpy(state->slots[0].last_event, "ESP dashboard ready. Waiting for Pico 2 data link.");
	strcpy(state->slots[0].notes, "Expect TIME_REQ, BOOT_SYNC, STATUS, and ACK from Pico over UART.");
	state->history_count = 0;
	strcpy(state->last_ack_action, "none");
	strcpy(state->last_ack_result, "none");
	state->awaiting_dispense_ack = false;
	state->dispense_ack_deadline_us = 0;
	state->last_update_us = 0;
}

static void bridge_copy_string(char *dest, size_t dest_size, const char *src)
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

static int bridge_slot_index_from_number(int slot_number)
{
	if (slot_number < 0 || slot_number >= PILL_SLOT_COUNT) {
		return -1;
	}

	return slot_number;
}

static const pill_slot_state_t *bridge_get_active_slot_const(const pico_bridge_state_t *state)
{
	int slot_index = bridge_slot_index_from_number(state->active_profile_slot);

	if (slot_index < 0) {
		slot_index = 0;
	}

	return &state->slots[slot_index];
}

static bool bridge_state_save_to_nvs(const pico_bridge_state_t *state)
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

static bool bridge_state_load_from_nvs(pico_bridge_state_t *state)
{
	nvs_handle_t handle;
	size_t size = sizeof(*state);
	esp_err_t err = nvs_open(BRIDGE_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
		return false;
	}

	err = nvs_get_blob(handle, "state", state, &size);
	nvs_close(handle);
	if (err != ESP_OK || size != sizeof(*state)) {
		return false;
	}

	return true;
}

static void bridge_log_status_locked(pico_bridge_state_t *state, const pill_slot_state_t *slot_state)
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

static void uart_bridge_send_line(const char *line)
{
	if (line == NULL) {
		return;
	}

	uart_write_bytes(UART_BRIDGE_PORT, line, strlen(line));
	ESP_LOGI(TAG, "UART TX: %s", line);
}

static bool bridge_is_time_valid(void)
{
	time_t now = time(NULL);

	return now > 1700000000;
}

static bool bridge_send_set_time(void)
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

static bool bridge_set_local_time(time_t epoch)
{
	struct timeval now = {
		.tv_sec = epoch,
		.tv_usec = 0,
	};

	if (epoch <= 0) {
		return false;
	}

	if (settimeofday(&now, NULL) != 0) {
		ESP_LOGE(TAG, "Failed to set local time from epoch %lld", (long long)epoch);
		return false;
	}

	ESP_LOGI(TAG, "Local ESP time updated to epoch %lld", (long long)epoch);
	return true;
}

static void bridge_send_dispense_for_slot(int slot_number)
{
	char line[64];

	if (slot_number < 0) {
		snprintf(line, sizeof(line), "CMD|action=DISPENSE\n");
	} else {
		snprintf(line, sizeof(line), "CMD|action=DISPENSE|slot=%d\n", slot_number);
	}

	uart_bridge_send_line(line);
}

static void bridge_send_load_profile_for_slot(const pill_slot_state_t *slot_state)
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
		state->awaiting_dispense_ack = false;
		state->dispense_ack_deadline_us = 0;
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

static bool bridge_append_text(char *buffer, size_t buffer_size, size_t *used, const char *format, ...)
{
	va_list args;
	int written;

	if (*used >= buffer_size) {
		return false;
	}

	va_start(args, format);
	written = vsnprintf(buffer + *used, buffer_size - *used, format, args);
	va_end(args);

	if (written < 0) {
		return false;
	}

	if ((size_t)written >= (buffer_size - *used)) {
		*used = buffer_size - 1;
		return false;
	}

	*used += (size_t)written;
	return true;
}

static void bridge_update_connected_flag_locked(void)
{
	int64_t age_us = esp_timer_get_time() - bridge_state.last_update_us;

	if (bridge_state.awaiting_dispense_ack &&
		bridge_state.dispense_ack_deadline_us > 0 &&
		esp_timer_get_time() > bridge_state.dispense_ack_deadline_us) {
		bridge_state.awaiting_dispense_ack = false;
		bridge_state.dispense_ack_deadline_us = 0;
		bridge_copy_string(bridge_state.last_ack_result,
				   sizeof(bridge_state.last_ack_result),
				   "timeout");
	}

	bridge_state.connected = bridge_state.last_update_us > 0 && age_us < UART_BRIDGE_TIMEOUT_US;
	if (!bridge_state.connected) {
		bridge_copy_string(bridge_state.controller_transport,
					   sizeof(bridge_state.controller_transport),
					   "UART bridge pending");
	}
}

static void bridge_apply_field(pico_bridge_state_t *state, pill_slot_state_t *slot_state, const char *key, const char *value)
{
	if (strcmp(key, "med") == 0) {
		bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), value);
	} else if (strcmp(key, "slot") == 0) {
		state->active_profile_slot = atoi(value);
	} else if (strcmp(key, "left") == 0) {
		slot_state->pills_left = atoi(value);
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
		bridge_copy_string(slot_state->last_dispense_result, sizeof(slot_state->last_dispense_result), value);
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
	state->last_update_us = esp_timer_get_time();
	state->connected = true;
	bridge_copy_string(state->controller_transport,
				   sizeof(state->controller_transport),
				   "UART linked");
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
	} else if (strcmp(raw_line, "STATUS") == 0) {
		bridge_handle_status_line(&bridge_state, line);
	} else if (strcmp(raw_line, "ACK") == 0) {
		bridge_handle_ack_line(&bridge_state, line);
	} else {
		ESP_LOGI(TAG, "UART RX: %s", line);
	}

	xSemaphoreGive(bridge_state_mutex);
}

static void uart_bridge_task(void *arg)
{
	char line_buffer[UART_BRIDGE_LINE_SIZE];
	size_t line_length = 0;
	uint8_t rx_buffer[64];
	bool discarding_line = false;

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
			xSemaphoreGive(bridge_state_mutex);
		}
	}
}

static void start_uart_bridge(void)
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

	if (!bridge_state_load_from_nvs(&bridge_state)) {
		bridge_state_reset_defaults(&bridge_state);
		bridge_state_save_to_nvs(&bridge_state);
	} else {
		int slot_index;

		bridge_state.connected = false;
		bridge_state.last_update_us = 0;
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

static void dns_server_task(void *arg)
{
	int sock;
	struct sockaddr_in listen_addr;

	(void)arg;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock < 0) {
		ESP_LOGE(TAG, "DNS socket create failed");
		vTaskDelete(NULL);
		return;
	}

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons(DNS_PORT);
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
		ESP_LOGE(TAG, "DNS socket bind failed");
		close(sock);
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(TAG, "Captive DNS started on UDP 53");

	while (1) {
		uint8_t request[512];
		uint8_t response[512];
		struct sockaddr_in source_addr;
		socklen_t source_addr_len = sizeof(source_addr);
		ssize_t req_len = recvfrom(sock,
							   request,
							   sizeof(request),
							   0,
							   (struct sockaddr *)&source_addr,
							   &source_addr_len);

		if (req_len < 12) {
			continue;
		}

		if (request[2] & 0x80) {
			continue;
		}

		int index = 12;
		while (index < req_len && request[index] != 0) {
			index += request[index] + 1;
		}

		if ((index + 5) >= req_len) {
			continue;
		}

		int question_len = (index + 1) - 12 + 4;
		if ((12 + question_len) > req_len) {
			continue;
		}

		memcpy(response, request, 12 + question_len);
		response[2] = 0x81;
		response[3] = 0x80;
		response[6] = 0x00;
		response[7] = 0x01;
		response[8] = 0x00;
		response[9] = 0x00;
		response[10] = 0x00;
		response[11] = 0x00;

		int resp_len = 12 + question_len;
		response[resp_len++] = 0xC0;
		response[resp_len++] = 0x0C;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x01;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x01;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x3C;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x04;
		response[resp_len++] = AP_IP_OCTET_1;
		response[resp_len++] = AP_IP_OCTET_2;
		response[resp_len++] = AP_IP_OCTET_3;
		response[resp_len++] = AP_IP_OCTET_4;

		sendto(sock,
			   response,
			   resp_len,
			   0,
			   (struct sockaddr *)&source_addr,
			   source_addr_len);
	}
}

static void start_captive_dns(void)
{
	xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 4, NULL);
}

static esp_err_t redirect_to_root(httpd_req_t *req)
{
	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", "/");
	return httpd_resp_send(req, NULL, 0);
}

static esp_err_t apple_captive_handler(httpd_req_t *req)
{
	const char *response = "<html><body>Login</body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static void get_device_time_string(char *out, size_t out_len)
{
	time_t now = time(NULL);
	struct tm timeinfo;

	if (localtime_r(&now, &timeinfo) != NULL &&
		strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &timeinfo) > 0) {
		return;
	}

	int64_t uptime_seconds = esp_timer_get_time() / 1000000;
	snprintf(out, out_len, "Time not set (uptime %llds)", (long long)uptime_seconds);
}

static const char *json_bool(bool value)
{
	return value ? "true" : "false";
}

static bool ap_uses_password(void)
{
	return strlen(AP_PASS) >= 8;
}

static esp_err_t read_http_body(httpd_req_t *req, char *buffer, size_t buffer_size)
{
	int total_received = 0;

	if (buffer_size == 0) {
		return ESP_ERR_INVALID_SIZE;
	}

	while (total_received < req->content_len && total_received < (int)(buffer_size - 1)) {
		int received = httpd_req_recv(req,
					     buffer + total_received,
					     buffer_size - 1 - total_received);

		if (received <= 0) {
			return ESP_FAIL;
		}

		total_received += received;
	}

	buffer[total_received] = '\0';
	return total_received == req->content_len ? ESP_OK : ESP_ERR_HTTPD_RESULT_TRUNC;
}

static bool http_body_get_string(const char *body, const char *key, char *value, size_t value_size)
{
	if (httpd_query_key_value(body, key, value, value_size) == ESP_OK) {
		return true;
	}

	value[0] = '\0';
	return false;
}

static bool http_body_get_int(const char *body, const char *key, int *value)
{
	char temp[16];

	if (!http_body_get_string(body, key, temp, sizeof(temp))) {
		return false;
	}

	*value = atoi(temp);
	return true;
}

static bool bridge_validate_medication_name(const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return false;
	}

	return strchr(name, '|') == NULL && strchr(name, '\n') == NULL && strchr(name, '\r') == NULL;
}

static esp_err_t send_json_response(httpd_req_t *req, const char *status, const char *body)
{
	httpd_resp_set_status(req, status);
	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t profile_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char medication_name[32];
	char schedule[40];
	int slot_number;
	int total_pills;
	int dose;
	int time_ms;
	pill_slot_state_t *slot_state;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_int(body, "slot", &slot_number) ||
		!http_body_get_int(body, "total", &total_pills) ||
		!http_body_get_int(body, "dose", &dose) ||
		!http_body_get_int(body, "time", &time_ms) ||
		!http_body_get_string(body, "med", medication_name, sizeof(medication_name))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
	}

	if (bridge_slot_index_from_number(slot_number) < 0 || total_pills <= 0 || dose <= 0 || !bridge_validate_medication_name(medication_name)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile data");
	}

	http_body_get_string(body, "schedule", schedule, sizeof(schedule));
	if (schedule[0] == '\0') {
		strcpy(schedule, "none");
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return send_json_response(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"bridge_busy\"}");
	}

	slot_state = &bridge_state.slots[slot_number];
	slot_state->slot_number = slot_number;
	slot_state->total_pills = total_pills;
	slot_state->pills_per_dose = dose;
	slot_state->time_ms = (uint32_t)time_ms;
	slot_state->is_active = true;
	slot_state->has_data = true;
	bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), medication_name);
	bridge_copy_string(slot_state->schedule, sizeof(slot_state->schedule), schedule);
	if (slot_state->pills_left < 0 || slot_state->pills_left > total_pills) {
		slot_state->pills_left = total_pills;
	}
	slot_state->doses_remaining = dose > 0 && slot_state->pills_left >= 0 ? slot_state->pills_left / dose : -1;
	bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), "Profile saved on ESP and sent to Pico.");
	bridge_state.active_profile_slot = slot_number;
	bridge_state_save_to_nvs(&bridge_state);
	bridge_send_load_profile_for_slot(slot_state);
	xSemaphoreGive(bridge_state_mutex);

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t dispense_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	int slot_number;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_int(body, "slot", &slot_number)) {
		slot_number = -1;
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return send_json_response(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"bridge_busy\"}");
	}

	bridge_update_connected_flag_locked();
	if (bridge_state.awaiting_dispense_ack) {
		xSemaphoreGive(bridge_state_mutex);
		return send_json_response(req, "409 Conflict", "{\"ok\":false,\"error\":\"dispense_pending\"}");
	}

	if (slot_number < 0) {
		slot_number = bridge_state.active_profile_slot;
	}

	if (bridge_slot_index_from_number(slot_number) < 0 || !bridge_state.slots[slot_number].is_active) {
		xSemaphoreGive(bridge_state_mutex);
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or inactive slot");
	}

	bridge_send_dispense_for_slot(slot_number);
	bridge_state.active_profile_slot = slot_number;
	bridge_state.awaiting_dispense_ack = true;
	bridge_state.dispense_ack_deadline_us = esp_timer_get_time() + DISPENSE_ACK_TIMEOUT_US;
	bridge_copy_string(bridge_state.last_ack_action, sizeof(bridge_state.last_ack_action), "DISPENSE");
	bridge_copy_string(bridge_state.last_ack_result, sizeof(bridge_state.last_ack_result), "pending");
	bridge_state_save_to_nvs(&bridge_state);
	xSemaphoreGive(bridge_state_mutex);

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t time_sync_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	time_t epoch = 0;
	char epoch_str[24];

	if (req->content_len > 0) {
		if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
		}
		if (http_body_get_string(body, "epoch", epoch_str, sizeof(epoch_str))) {
			epoch = (time_t)strtoll(epoch_str, NULL, 10);
		}
	}

	if (epoch > 0 && !bridge_set_local_time(epoch)) {
		return send_json_response(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"settimeofday_failed\"}");
	}

	if (!bridge_send_set_time()) {
		return send_json_response(req, "409 Conflict", "{\"ok\":false,\"error\":\"time_invalid\"}");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
	char time_buf[64];
	char *response;
	char *slots_json;
	pico_bridge_state_t *snapshot;
	size_t slots_used = 0;
	const pill_slot_state_t *active_slot;
	int slot_index;
	esp_err_t result;

	response = malloc(STATUS_RESPONSE_BUFFER_SIZE);
	slots_json = malloc(STATUS_SLOTS_BUFFER_SIZE);
	snapshot = malloc(sizeof(*snapshot));
	if (response == NULL || slots_json == NULL || snapshot == NULL) {
		free(response);
		free(slots_json);
		free(snapshot);
		ESP_LOGE(TAG, "Failed to allocate status response buffers");
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
	}

	get_device_time_string(time_buf, sizeof(time_buf));
	memset(snapshot, 0, sizeof(*snapshot));

	if (bridge_state_mutex != NULL && xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
		bridge_update_connected_flag_locked();
		*snapshot = bridge_state;
		xSemaphoreGive(bridge_state_mutex);
	} else {
		bridge_state_reset_defaults(snapshot);
	}

	active_slot = bridge_get_active_slot_const(snapshot);
	bridge_append_text(slots_json, STATUS_SLOTS_BUFFER_SIZE, &slots_used, "[");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		const pill_slot_state_t *slot_state = &snapshot->slots[slot_index];

		bridge_append_text(slots_json,
				   STATUS_SLOTS_BUFFER_SIZE,
				   &slots_used,
				   "%s{\"slot\":%d,\"has_data\":%s,\"is_active\":%s,\"medication_name\":\"%s\",\"pills_left\":%d,\"pills_per_dose\":%d,\"doses_remaining\":%d,\"total_pills\":%d,\"time_ms\":%lu,\"schedule\":\"%s\",\"last_dispensed\":\"%s\",\"last_event\":\"%s\",\"last_dispense_result\":\"%s\",\"notes\":\"%s\"}",
				   slot_index == 0 ? "" : ",",
				   slot_state->slot_number,
				   json_bool(slot_state->has_data),
				   json_bool(slot_state->is_active),
				   slot_state->medication_name,
				   slot_state->pills_left,
				   slot_state->pills_per_dose,
				   slot_state->doses_remaining,
				   slot_state->total_pills,
				   (unsigned long)slot_state->time_ms,
				   slot_state->schedule,
				   slot_state->last_dispensed,
				   slot_state->last_event,
				   slot_state->last_dispense_result,
				   slot_state->notes);
	}
	bridge_append_text(slots_json, STATUS_SLOTS_BUFFER_SIZE, &slots_used, "]");

	snprintf(response,
			 STATUS_RESPONSE_BUFFER_SIZE,
			 "{"
			 "\"device_time\":\"%s\","
			 "\"bridge_connected\":%s,"
			 "\"bridge_status\":\"waiting_for_pico\","
			 "\"controller_name\":\"Raspberry Pi Pico 2\","
			 "\"controller_transport\":\"%s\","
			 "\"active_profile_slot\":%d,"
			 "\"slot_count\":%d,"
			 "\"history_count\":%d,"
			 "\"last_ack_action\":\"%s\","
			 "\"last_ack_result\":\"%s\","
			 "\"awaiting_dispense_ack\":%s,"
			 "\"medication_name\":\"%s\","
			 "\"pills_left\":%d,"
			 "\"pills_per_dose\":%d,"
			 "\"doses_remaining\":%d,"
			 "\"dispense_mode\":\"sensor_based\","
			 "\"piezo_enabled\":true,"
			 "\"hall_enabled\":true,"
			 "\"ir_enabled\":true,"
			 "\"last_dispensed\":\"%s\","
			 "\"last_event\":\"%s\","
			 "\"last_dispense_result\":\"%s\","
			 "\"notes\":\"%s\","
			 "\"slots\":%s"
			 "}",
			 time_buf,
			 json_bool(snapshot->connected),
			 snapshot->controller_transport,
			 snapshot->active_profile_slot,
			 PILL_SLOT_COUNT,
			 snapshot->history_count,
			 snapshot->last_ack_action,
			 snapshot->last_ack_result,
			 json_bool(snapshot->awaiting_dispense_ack),
			 active_slot->medication_name,
			 active_slot->pills_left,
			 active_slot->pills_per_dose,
			 active_slot->doses_remaining,
			 active_slot->last_dispensed,
			 active_slot->last_event,
			 active_slot->last_dispense_result,
			 active_slot->notes,
			 slots_json);

	httpd_resp_set_type(req, "application/json");
	result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
	free(response);
	free(slots_json);
	free(snapshot);
	return result;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
	const char *response =
		"<!doctype html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>Pill Dispenser Control Surface</title>"
		"<style>"
		":root{color-scheme:light;--bg:#f4efe7;--ink:#112027;--muted:#5d6b70;--panel:#fffaf3;--line:rgba(17,32,39,.1);--shadow:0 22px 60px rgba(17,32,39,.14);--teal:#0f766e;--teal-soft:#d8f1ee;--amber:#c77b18;--amber-soft:#fff0d8;}"
		"*{box-sizing:border-box;}"
		"body{margin:0;font-family:\"Trebuchet MS\",\"Segoe UI Variable\",sans-serif;color:var(--ink);background:radial-gradient(circle at top left,#fff8ef 0,#f4efe7 45%,#e9f3f1 100%);min-height:100vh;}"
		"body:before,body:after{content:\"\";position:fixed;border-radius:999px;filter:blur(12px);opacity:.45;pointer-events:none;}"
		"body:before{width:280px;height:280px;background:#f5d6a5;top:-90px;right:-70px;}"
		"body:after{width:220px;height:220px;background:#b7e4db;left:-60px;bottom:-40px;}"
		".shell{max-width:980px;margin:0 auto;padding:24px 18px 40px;}"
		".hero{position:relative;overflow:hidden;background:linear-gradient(135deg,#12333b 0,#184f5b 52%,#1b6d67 100%);color:#f7fbfb;border-radius:28px;padding:24px;box-shadow:var(--shadow);margin-bottom:18px;}"
		".hero:after{content:\"\";position:absolute;inset:auto -40px -70px auto;width:240px;height:240px;border-radius:50%;background:rgba(255,255,255,.08);box-shadow:-120px -70px 0 rgba(255,255,255,.06);pointer-events:none;}"
		".eyebrow{letter-spacing:.16em;text-transform:uppercase;font-size:.72rem;opacity:.78;margin-bottom:10px;}"
		"h1{font-family:Georgia,\"Times New Roman\",serif;font-size:clamp(2rem,7vw,3.8rem);line-height:.96;margin:0;max-width:8ch;}"
		".lede{max-width:40rem;margin:14px 0 0;font-size:1rem;line-height:1.5;color:rgba(247,251,251,.82);}"
		".stack{display:grid;gap:18px;}"
		".panel{background:rgba(255,250,243,.9);border:1px solid rgba(255,255,255,.6);border-radius:24px;padding:18px;box-shadow:var(--shadow);backdrop-filter:blur(10px);}"
		".panel-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:16px;}"
		".panel-title{margin:0;font-size:1.05rem;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);font-weight:700;}"
		".status-pill{display:inline-flex;align-items:center;gap:8px;padding:10px 14px;border-radius:999px;font-weight:700;background:var(--amber-soft);color:#8a5410;border:1px solid rgba(199,123,24,.16);}"
		".status-pill.online{background:var(--teal-soft);color:#0e5d58;border-color:rgba(15,118,110,.18);}"
		".dot{width:10px;height:10px;border-radius:50%;background:currentColor;box-shadow:0 0 0 6px rgba(255,255,255,.18);}"
		".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px;}"
		".metric{padding:16px;border-radius:20px;background:#fffdf9;border:1px solid var(--line);}"
		".slot-card{transition:border-color .2s ease,transform .2s ease,box-shadow .2s ease;}"
		".slot-card.active{border-color:rgba(15,118,110,.5);box-shadow:0 16px 36px rgba(15,118,110,.12);transform:translateY(-2px);}"
		".metric .label{font-size:.8rem;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin-bottom:10px;}"
		".metric .value{font-family:Georgia,\"Times New Roman\",serif;font-size:2rem;line-height:1;margin-bottom:8px;}"
		".metric .hint{font-size:.95rem;color:var(--muted);line-height:1.35;}"
		".accent-teal{background:linear-gradient(180deg,#f7fffe 0,#ecfaf8 100%);}"
		".accent-amber{background:linear-gradient(180deg,#fffaf3 0,#fff1dc 100%);}"
		".connection-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;}"
		".summary{padding:16px;border-radius:20px;background:#fffdf9;border:1px solid var(--line);min-height:124px;}"
		".summary .label{font-size:.8rem;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin-bottom:10px;}"
		".summary .value{font-size:1.15rem;font-weight:700;line-height:1.25;}"
		".summary .hint{margin-top:8px;color:var(--muted);font-size:.94rem;line-height:1.35;}"
		".list{display:grid;gap:12px;}"
		".row{display:flex;justify-content:space-between;gap:14px;padding:12px 0;border-bottom:1px solid var(--line);}"
		".row:last-child{border-bottom:none;padding-bottom:0;}"
		".row:first-child{padding-top:0;}"
		".k{color:var(--muted);}"
		".v{font-weight:700;text-align:right;}"
		".result-ok{color:#0e5d58;}"
		".result-fail{color:#b91c1c;}"
		".footer{display:flex;flex-wrap:wrap;gap:10px;margin-top:18px;color:var(--muted);font-size:.92rem;}"
		".controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;}"
		"label{display:grid;gap:6px;font-size:.9rem;color:var(--muted);}"
		"input,select,button{font:inherit;border-radius:14px;border:1px solid var(--line);padding:10px 12px;background:#fffdf9;color:var(--ink);}"
		"button{cursor:pointer;background:#12333b;color:#f7fbfb;border:none;}"
		"button.alt{background:#e7efe7;color:#12333b;border:1px solid var(--line);}"
		".control-actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}"
		".status-copy{font-size:.92rem;color:var(--muted);margin-top:10px;}"
		".footer-card{padding:12px 14px;border-radius:16px;background:rgba(255,255,255,.58);border:1px solid rgba(255,255,255,.7);}"
		"@media (max-width:760px){.shell{padding:14px 14px 28px;}.hero{padding:20px;}}"
		"</style></head><body>"
		"<main class=\"shell\">"
		"<section class=\"hero\">"
		"<div class=\"eyebrow\">ESP32 access point dashboard</div>"
		"<h1>Pill Dispenser Control Surface</h1>"
		"<p class=\"lede\">A focused view of the pill dispenser. This page only shows the connection to the Raspberry Pi Pico, the current pill profile, the current time, and the last dispense information.</p>"
		"</section>"
		"<div class=\"stack\">"
		"<section class=\"panel\">"
		"<div class=\"panel-head\">"
		"<div><p class=\"panel-title\">Connection State</p><div id=\"bridge-copy\">The ESP page is up. Waiting for live Raspberry Pi Pico data.</div></div>"
		"<div class=\"status-pill\" id=\"bridge-pill\"><span class=\"dot\"></span><span id=\"bridge-label\">Bridge Pending</span></div>"
		"</div>"
		"<div class=\"connection-grid\">"
		"<article class=\"summary accent-teal\"><div class=\"label\">Controller</div><div class=\"value\" id=\"controller-name\">Raspberry Pi Pico 2</div><div class=\"hint\">Main MCU expected to provide live pill and dispense state.</div></article>"
		"<article class=\"summary accent-amber\"><div class=\"label\">Transport</div><div class=\"value\" id=\"controller-transport\">Waiting for UART bridge</div><div class=\"hint\">Shows whether the ESP is receiving Pico-side updates.</div></article>"
		"<article class=\"summary accent-teal\"><div class=\"label\">Device Time</div><div class=\"value\" id=\"device-time\">Loading...</div><div class=\"hint\">Current time reported by the ESP dashboard host.</div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Slot Overview</p><div>Five Pico protocol slots are shown below. The selected slot is highlighted and mirrored in the detail panel.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-0\"><div class=\"label\">Slot 0</div><div class=\"value\" id=\"slot-medication-0\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-0\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-0\">--</span> | Doses remaining: <span id=\"slot-doses-0\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-0\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-0\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-1\"><div class=\"label\">Slot 1</div><div class=\"value\" id=\"slot-medication-1\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-1\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-1\">--</span> | Doses remaining: <span id=\"slot-doses-1\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-1\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-1\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-2\"><div class=\"label\">Slot 2</div><div class=\"value\" id=\"slot-medication-2\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-2\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-2\">--</span> | Doses remaining: <span id=\"slot-doses-2\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-2\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-2\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-3\"><div class=\"label\">Slot 3</div><div class=\"value\" id=\"slot-medication-3\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-3\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-3\">--</span> | Doses remaining: <span id=\"slot-doses-3\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-3\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-3\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-4\"><div class=\"label\">Slot 4</div><div class=\"value\" id=\"slot-medication-4\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-4\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-4\">--</span> | Doses remaining: <span id=\"slot-doses-4\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-4\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-4\">unknown</span></div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Active Slot Detail</p><div>Most recent details for the slot identified by the Pico in its STATUS update.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal\"><div class=\"label\">Medication</div><div class=\"value\" id=\"medication-name\">Waiting for data</div><div class=\"hint\">Active profile slot <span id=\"profile-slot\">0</span>.</div></article>"
		"<article class=\"metric accent-amber\"><div class=\"label\">Pills Left</div><div class=\"value\" id=\"pills-left\">--</div><div class=\"hint\"><span id=\"doses-remaining\">--</span> full doses remaining at <span id=\"pills-per-dose\">--</span> pills per dose.</div></article>"
		"</div>"
		"<div class=\"list\">"
		"<div class=\"row\"><span class=\"k\">Last Dispense</span><span class=\"v\" id=\"last-dispensed\">No confirmed dispense yet</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Event</span><span class=\"v\" id=\"last-event\">Waiting for live data</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Result</span><span class=\"v\" id=\"last-result\">Waiting</span></div>"
		"<div class=\"row\"><span class=\"k\">Profile Notes</span><span class=\"v\" id=\"dashboard-notes\">Waiting for live pill slot data.</span></div>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Controls</p><div>Save profiles to ESP storage, sync them to the Pico, trigger dispense, and manually resend time.</div></div></div>"
		"<div class=\"controls\">"
		"<label>Slot<select id=\"control-slot\"><option value=\"0\">Slot 0</option><option value=\"1\">Slot 1</option><option value=\"2\">Slot 2</option><option value=\"3\">Slot 3</option><option value=\"4\">Slot 4</option></select></label>"
		"<label>Medication<input id=\"control-med\" maxlength=\"31\" placeholder=\"Aspirin\"></label>"
		"<label>Total Pills<input id=\"control-total\" type=\"number\" min=\"1\" step=\"1\" value=\"20\"></label>"
		"<label>Dose<input id=\"control-dose\" type=\"number\" min=\"1\" step=\"1\" value=\"1\"></label>"
		"<label>Time ms<input id=\"control-time\" type=\"number\" min=\"100\" step=\"50\" value=\"800\"></label>"
		"<label>Schedule<input id=\"control-schedule\" placeholder=\"08:00,20:00 or none\"></label>"
		"<label>Manual Time<input id=\"control-manual-time\" type=\"datetime-local\"></label>"
		"</div>"
		"<div class=\"control-actions\">"
		"<button id=\"save-profile\" type=\"button\">Save Profile</button>"
		"<button id=\"dispense-slot\" type=\"button\">Dispense Selected Slot</button>"
		"<button id=\"sync-time\" type=\"button\" class=\"alt\">Send Time</button>"
		"</div>"
		"<div class=\"status-copy\" id=\"control-status\">No command sent yet.</div>"
		"</section>"
		"<div class=\"footer\">"
		"<div class=\"footer-card\">Wi-Fi SSID: ESP-Time-Server</div>"
		"<div class=\"footer-card\">Password: Open network</div>"
		"<div class=\"footer-card\">Portal URL: http://192.168.4.1</div>"
		"</div>"
		"</section>"
		"</div>"
		"</main>"
		"<script>"
		"const displayNumber=v=>typeof v==='number'&&v>=0?String(v):'--';"
		"const control=(id)=>document.getElementById(id);"
		"const controlIds=['control-slot','control-med','control-total','control-dose','control-time','control-schedule','control-manual-time'];"
		"let latestSlots=[];"
		"const text=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value;};"
		"const toLocalDateTimeValue=date=>{const pad=v=>String(v).padStart(2,'0');return date.getFullYear()+'-'+pad(date.getMonth()+1)+'-'+pad(date.getDate())+'T'+pad(date.getHours())+':'+pad(date.getMinutes());};"
		"const setResult=(id,value)=>{const el=document.getElementById(id);if(!el)return;const normalized=value||'unknown';el.textContent=normalized==='ok'?'Pass':normalized==='fail'?'Fail':normalized;el.className=(id==='last-result'?'v ':'')+(normalized==='ok'?'result-ok':normalized==='fail'?'result-fail':'');};"
		"const setActiveSlotCard=slot=>{for(let n=0;n<5;n+=1){const card=document.getElementById('slot-card-'+n);if(card)card.className='metric '+(n%2===0?'accent-teal ':'accent-amber ')+'slot-card'+(n===slot?' active':'');}};"
		"const setStatusCopy=msg=>text('control-status',msg);"
		"const setSlotCard=slot=>{if(!slot||slot.slot==null)return;text('slot-medication-'+slot.slot,slot.medication_name||'Waiting for data');text('slot-left-'+slot.slot,displayNumber(slot.pills_left));text('slot-dose-'+slot.slot,displayNumber(slot.pills_per_dose));text('slot-doses-'+slot.slot,displayNumber(slot.doses_remaining));text('slot-schedule-'+slot.slot,slot.schedule||'none');setResult('slot-result-'+slot.slot,slot.last_dispense_result||'unknown');};"
		"const isEditingControls=()=>{const active=document.activeElement;return Boolean(active&&controlIds.includes(active.id));};"
		"const getSlotByNumber=slotNumber=>latestSlots.find(slot=>slot&&slot.slot===slotNumber);"
		"const fillControlsFromSlot=slot=>{if(!slot)return;control('control-slot').value=String(slot.slot);control('control-med').value=slot.medication_name&&slot.medication_name!=='Waiting for data'?slot.medication_name:'';control('control-total').value=slot.total_pills>0?slot.total_pills:20;control('control-dose').value=slot.pills_per_dose>0?slot.pills_per_dose:1;control('control-time').value=slot.time_ms>0?slot.time_ms:800;control('control-schedule').value=slot.schedule&&slot.schedule!=='none'?slot.schedule:'';};"
		"const postForm=async(url,data)=>{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw new Error(await r.text());return r.json();};"
		"const saveProfile=async()=>{const data={slot:control('control-slot').value,med:control('control-med').value,total:control('control-total').value,dose:control('control-dose').value,time:control('control-time').value,schedule:control('control-schedule').value||'none'};await postForm('/api/profile',data);setStatusCopy('Profile saved and sent to Pico.');await refreshStatus();};"
		"const dispenseSelected=async()=>{await postForm('/api/dispense',{slot:control('control-slot').value});setStatusCopy('Dispense command sent. Waiting for Pico ACK.');await refreshStatus();};"
		"const syncTime=async()=>{const raw=control('control-manual-time').value;if(!raw)throw new Error('missing-time');const epoch=Math.floor(new Date(raw).getTime()/1000);if(!Number.isFinite(epoch)||epoch<=0)throw new Error('invalid-time');await postForm('/api/time-sync',{epoch:String(epoch)});setStatusCopy('Chosen time sent to Pico.');};"
		"const setBridgeState=(connected,status)=>{const pill=document.getElementById('bridge-pill');text('bridge-label',connected?'Connected to Pico':'Bridge Pending');text('bridge-copy',connected?'The ESP is receiving live Raspberry Pi Pico updates.':'The ESP page is up. Waiting for live Raspberry Pi Pico data.');if(pill)pill.className=connected?'status-pill online':'status-pill';if(status){text('controller-transport',status);} };"
		"async function refreshStatus(){"
		"try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('bad-response');const d=await r.json();"
		"text('device-time',d.device_time||'Unavailable');"
		"text('controller-name',d.controller_name||'Raspberry Pi Pico 2');"
		"text('controller-transport',d.controller_transport||'Waiting for UART bridge');"
		"text('medication-name',d.medication_name||'No active profile');"
		"text('profile-slot',d.active_profile_slot!=null?d.active_profile_slot:'-');"
		"text('pills-left',displayNumber(d.pills_left));"
		"text('pills-per-dose',displayNumber(d.pills_per_dose));"
		"text('doses-remaining',displayNumber(d.doses_remaining));"
		"text('last-dispensed',d.last_dispensed||'No confirmed dispense yet');"
		"text('last-event',d.last_event||'Waiting for live data');"
		"setResult('last-result',d.last_dispense_result||'unknown');"
		"text('dashboard-notes',d.notes||'Waiting for live pill slot data.');"
		"const slots=Array.isArray(d.slots)?d.slots:[];latestSlots=slots;slots.forEach(setSlotCard);"
		"if(!isEditingControls()){const selected=Number(control('control-slot').value);fillControlsFromSlot(getSlotByNumber(selected) || slots.find(slot=>slot&&slot.slot===Number(d.active_profile_slot)) || slots[0]);}"
		"setActiveSlotCard(Number(d.active_profile_slot)||0);"
		"if(d.awaiting_dispense_ack){setStatusCopy('Waiting for ACK: '+(d.last_ack_action||'command')+' / '+(d.last_ack_result||'pending'));}"
		"setBridgeState(Boolean(d.bridge_connected),d.controller_transport);"
		"}catch(e){text('device-time','Disconnected');text('last-event','ESP status endpoint is unavailable.');setBridgeState(false,'ESP status unavailable');}"
		"}"
		"control('save-profile').addEventListener('click',()=>{saveProfile().catch(()=>setStatusCopy('Failed to save profile.'));});"
		"control('dispense-slot').addEventListener('click',()=>{dispenseSelected().catch(()=>setStatusCopy('Failed to send dispense command.'));});"
		"control('sync-time').addEventListener('click',()=>{syncTime().catch(()=>setStatusCopy('Pick a valid manual time before sending.'));});"
		"if(!control('control-manual-time').value){control('control-manual-time').value=toLocalDateTimeValue(new Date());}"
		"control('control-slot').addEventListener('change',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));});"
		"refreshStatus();setInterval(refreshStatus,1500);"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static void start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	config.max_uri_handlers = 14;
	config.stack_size = 10240;
	config.uri_match_fn = httpd_uri_match_wildcard;
	config.max_open_sockets = 4; // Reserve sockets for DNS + lwIP internals (total lwIP sockets = 10)

	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_uri_t root = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = root_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t status = {
			.uri = "/api/status",
			.method = HTTP_GET,
			.handler = status_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t profile = {
			.uri = "/api/profile",
			.method = HTTP_POST,
			.handler = profile_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t dispense = {
			.uri = "/api/dispense",
			.method = HTTP_POST,
			.handler = dispense_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t time_sync = {
			.uri = "/api/time-sync",
			.method = HTTP_POST,
			.handler = time_sync_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t android_204 = {
			.uri = "/generate_204",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t android_gen_204 = {
			.uri = "/gen_204",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_hotspot = {
			.uri = "/hotspot-detect.html",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_library_test = {
			.uri = "/library/test/success.html",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_success_txt = {
			.uri = "/success.txt",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_mobile_status = {
			.uri = "/mobile/status.php",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t msft_ncsi = {
			.uri = "/ncsi.txt",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t msft_connect = {
			.uri = "/connecttest.txt",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t catch_all = {
			.uri = "/*",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};

		httpd_register_uri_handler(server, &root);
		httpd_register_uri_handler(server, &status);
		httpd_register_uri_handler(server, &profile);
		httpd_register_uri_handler(server, &dispense);
		httpd_register_uri_handler(server, &time_sync);
		httpd_register_uri_handler(server, &android_204);
		httpd_register_uri_handler(server, &android_gen_204);
		httpd_register_uri_handler(server, &apple_hotspot);
		httpd_register_uri_handler(server, &apple_library_test);
		httpd_register_uri_handler(server, &apple_success_txt);
		httpd_register_uri_handler(server, &apple_mobile_status);
		httpd_register_uri_handler(server, &msft_ncsi);
		httpd_register_uri_handler(server, &msft_connect);
		httpd_register_uri_handler(server, &catch_all);
		ESP_LOGI(TAG, "HTTP server started");
	} else {
		ESP_LOGE(TAG, "Failed to start HTTP server");
	}
}

static void start_wifi_ap(void)
{
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	bool use_password = ap_uses_password();
	wifi_config_t ap_config = {
		.ap = {
			.ssid = AP_SSID,
			.ssid_len = strlen(AP_SSID),
			.channel = 1,
			.password = AP_PASS,
			.max_connection = AP_MAX_CONN,
			.authmode = use_password ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN,
			.pmf_cfg = {
				.required = false,
			},
		},
	};

	if (!use_password) {
		ESP_LOGW(TAG,
				 "AP password must be at least 8 characters for WPA/WPA2. Starting open network for SSID %s.",
				 AP_SSID);
	}

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	esp_wifi_set_max_tx_power(84); /* 84 = 21 dBm, maximum */

	ESP_LOGI(TAG, "Wi-Fi AP started. SSID: %s, Password: %s", AP_SSID, use_password ? AP_PASS : "<open>");
	ESP_LOGI(TAG, "Open portal: http://192.168.4.1/");
}

void app_main(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	start_uart_bridge();
	start_wifi_ap();
	start_captive_dns();
	start_webserver();
}
