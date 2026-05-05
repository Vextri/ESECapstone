#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
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
#include "nvs_flash.h"

static const char *TAG = "time_server";

#define AP_SSID "ESP-Time-Server"
#define AP_PASS "1"
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
#define PILL_SLOT_COUNT 3
#define STATUS_RESPONSE_BUFFER_SIZE 4096
#define STATUS_SLOTS_BUFFER_SIZE 2048

typedef struct {
	int pills_left;
	int pills_per_dose;
	int doses_remaining;
	int slot_number;
	char medication_name[32];
	char last_dispensed[64];
	char last_event[96];
	char notes[96];
	char last_dispense_result[16];
	bool has_data;
} pill_slot_state_t;

typedef struct {
	bool connected;
	int active_profile_slot;
	char controller_transport[32];
	pill_slot_state_t slots[PILL_SLOT_COUNT];
	int64_t last_update_us;
} pico_bridge_state_t;

static SemaphoreHandle_t bridge_state_mutex;
static pico_bridge_state_t bridge_state;

static void bridge_reset_slot_defaults(pill_slot_state_t *slot_state, int slot_number)
{
	slot_state->pills_left = -1;
	slot_state->pills_per_dose = -1;
	slot_state->doses_remaining = -1;
	slot_state->slot_number = slot_number;
	strcpy(slot_state->medication_name, "Waiting for data");
	strcpy(slot_state->last_dispensed, "No confirmed dispense yet");
	strcpy(slot_state->last_event, "Waiting for live data");
	strcpy(slot_state->notes, "Waiting for live pill slot data.");
	strcpy(slot_state->last_dispense_result, "unknown");
	slot_state->has_data = false;
}

static void bridge_state_reset_defaults(pico_bridge_state_t *state)
{
	int slot_index;

	state->connected = false;
	state->active_profile_slot = 1;
	strcpy(state->controller_transport, "UART bridge pending");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		bridge_reset_slot_defaults(&state->slots[slot_index], slot_index + 1);
	}
	strcpy(state->slots[0].last_event, "ESP dashboard ready. Waiting for Pico 2 data link.");
	strcpy(state->slots[0].notes, "Send STATUS|slot=1..3|med=...|left=...|dose=...|doses=...|last=...|event=...|result=ok/fail");
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
	if (slot_number < 1 || slot_number > PILL_SLOT_COUNT) {
		return -1;
	}

	return slot_number - 1;
}

static const pill_slot_state_t *bridge_get_active_slot_const(const pico_bridge_state_t *state)
{
	int slot_index = bridge_slot_index_from_number(state->active_profile_slot);

	if (slot_index < 0) {
		slot_index = 0;
	}

	return &state->slots[slot_index];
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
	} else if (strcmp(key, "last") == 0) {
		bridge_copy_string(slot_state->last_dispensed, sizeof(slot_state->last_dispensed), value);
	} else if (strcmp(key, "event") == 0) {
		bridge_copy_string(slot_state->last_event, sizeof(slot_state->last_event), value);
	} else if (strcmp(key, "notes") == 0) {
		bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), value);
	} else if (strcmp(key, "result") == 0) {
		bridge_copy_string(slot_state->last_dispense_result, sizeof(slot_state->last_dispense_result), value);
	}
}

static void bridge_process_uart_line(char *line)
{
	char raw_line[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token = strtok_r(line, "|", &saveptr);
	pico_bridge_state_t updated_state;
	pill_slot_state_t *slot_state;
	int slot_number;
	int slot_index;

	bridge_copy_string(raw_line, sizeof(raw_line), line);

	if (token == NULL) {
		return;
	}

	if (strcmp(token, "STATUS") != 0) {
		ESP_LOGI(TAG, "UART RX: %s", line);
		return;
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
		return;
	}

	updated_state = bridge_state;
	slot_number = bridge_extract_slot_number(raw_line);
	if (slot_number < 0) {
		slot_number = updated_state.active_profile_slot;
	}

	slot_index = bridge_slot_index_from_number(slot_number);
	if (slot_index < 0) {
		ESP_LOGW(TAG, "Ignoring STATUS update with invalid slot number: %d", slot_number);
		xSemaphoreGive(bridge_state_mutex);
		return;
	}

	updated_state.active_profile_slot = slot_number;
	slot_state = &updated_state.slots[slot_index];
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');
		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		bridge_apply_field(&updated_state, slot_state, token, separator + 1);
	}

	slot_state->has_data = true;
	updated_state.last_update_us = esp_timer_get_time();
	updated_state.connected = true;
	bridge_copy_string(updated_state.controller_transport,
				   sizeof(updated_state.controller_transport),
				   "UART linked");
	bridge_state = updated_state;
	xSemaphoreGive(bridge_state_mutex);

	ESP_LOGI(TAG, "Pico status updated over UART");
}

static void uart_bridge_task(void *arg)
{
	char line_buffer[UART_BRIDGE_LINE_SIZE];
	size_t line_length = 0;
	uint8_t rx_buffer[64];

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
					line_buffer[line_length] = '\0';
					if (line_length > 0) {
						bridge_process_uart_line(line_buffer);
					}
					line_length = 0;
					continue;
				}

				if (line_length < (sizeof(line_buffer) - 1)) {
					line_buffer[line_length++] = current;
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

	bridge_state_reset_defaults(&bridge_state);

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

static esp_err_t status_get_handler(httpd_req_t *req)
{
	char time_buf[64];
	char *response;
	char *slots_json;
	size_t slots_used = 0;
	pico_bridge_state_t snapshot;
	const pill_slot_state_t *active_slot;
	int slot_index;
	esp_err_t result;

	response = malloc(STATUS_RESPONSE_BUFFER_SIZE);
	slots_json = malloc(STATUS_SLOTS_BUFFER_SIZE);
	if (response == NULL || slots_json == NULL) {
		free(response);
		free(slots_json);
		ESP_LOGE(TAG, "Failed to allocate status response buffers");
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
	}

	get_device_time_string(time_buf, sizeof(time_buf));
	memset(&snapshot, 0, sizeof(snapshot));

	if (bridge_state_mutex != NULL && xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
		bridge_update_connected_flag_locked();
		snapshot = bridge_state;
		xSemaphoreGive(bridge_state_mutex);
	} else {
		bridge_state_reset_defaults(&snapshot);
	}

	active_slot = bridge_get_active_slot_const(&snapshot);
	bridge_append_text(slots_json, STATUS_SLOTS_BUFFER_SIZE, &slots_used, "[");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		const pill_slot_state_t *slot_state = &snapshot.slots[slot_index];

		bridge_append_text(slots_json,
				   STATUS_SLOTS_BUFFER_SIZE,
				   &slots_used,
				   "%s{\"slot\":%d,\"has_data\":%s,\"medication_name\":\"%s\",\"pills_left\":%d,\"pills_per_dose\":%d,\"doses_remaining\":%d,\"last_dispensed\":\"%s\",\"last_event\":\"%s\",\"last_dispense_result\":\"%s\",\"notes\":\"%s\"}",
				   slot_index == 0 ? "" : ",",
				   slot_state->slot_number,
				   json_bool(slot_state->has_data),
				   slot_state->medication_name,
				   slot_state->pills_left,
				   slot_state->pills_per_dose,
				   slot_state->doses_remaining,
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
			 json_bool(snapshot.connected),
			 snapshot.controller_transport,
			 snapshot.active_profile_slot,
			 PILL_SLOT_COUNT,
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
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Slot Overview</p><div>Three slot records are shown below. The active slot is highlighted and mirrored in the detail panel.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-1\"><div class=\"label\">Slot 1</div><div class=\"value\" id=\"slot-medication-1\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-1\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-1\">--</span> | Doses remaining: <span id=\"slot-doses-1\">--</span></div><div class=\"hint\">Result: <span id=\"slot-result-1\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-2\"><div class=\"label\">Slot 2</div><div class=\"value\" id=\"slot-medication-2\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-2\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-2\">--</span> | Doses remaining: <span id=\"slot-doses-2\">--</span></div><div class=\"hint\">Result: <span id=\"slot-result-2\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-3\"><div class=\"label\">Slot 3</div><div class=\"value\" id=\"slot-medication-3\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-3\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-3\">--</span> | Doses remaining: <span id=\"slot-doses-3\">--</span></div><div class=\"hint\">Result: <span id=\"slot-result-3\">unknown</span></div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Active Slot Detail</p><div>Most recent details for the slot identified by the Pico in its STATUS update.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal\"><div class=\"label\">Medication</div><div class=\"value\" id=\"medication-name\">Waiting for data</div><div class=\"hint\">Active profile slot <span id=\"profile-slot\">1</span>.</div></article>"
		"<article class=\"metric accent-amber\"><div class=\"label\">Pills Left</div><div class=\"value\" id=\"pills-left\">--</div><div class=\"hint\"><span id=\"doses-remaining\">--</span> full doses remaining at <span id=\"pills-per-dose\">--</span> pills per dose.</div></article>"
		"</div>"
		"<div class=\"list\">"
		"<div class=\"row\"><span class=\"k\">Last Dispense</span><span class=\"v\" id=\"last-dispensed\">No confirmed dispense yet</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Event</span><span class=\"v\" id=\"last-event\">Waiting for live data</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Result</span><span class=\"v\" id=\"last-result\">Waiting</span></div>"
		"<div class=\"row\"><span class=\"k\">Profile Notes</span><span class=\"v\" id=\"dashboard-notes\">Waiting for live pill slot data.</span></div>"
		"</div>"
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
		"const text=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value;};"
		"const setResult=(id,value)=>{const el=document.getElementById(id);if(!el)return;const normalized=value||'unknown';el.textContent=normalized==='ok'?'Pass':normalized==='fail'?'Fail':normalized;el.className=(id==='last-result'?'v ':'')+(normalized==='ok'?'result-ok':normalized==='fail'?'result-fail':'');};"
		"const setActiveSlotCard=slot=>{for(let n=1;n<=3;n+=1){const card=document.getElementById('slot-card-'+n);if(card)card.className='metric '+(n%2===1?'accent-teal ':'accent-amber ')+'slot-card'+(n===slot?' active':'');}};"
		"const setSlotCard=slot=>{if(!slot||slot.slot==null)return;text('slot-medication-'+slot.slot,slot.medication_name||'Waiting for data');text('slot-left-'+slot.slot,displayNumber(slot.pills_left));text('slot-dose-'+slot.slot,displayNumber(slot.pills_per_dose));text('slot-doses-'+slot.slot,displayNumber(slot.doses_remaining));setResult('slot-result-'+slot.slot,slot.last_dispense_result||'unknown');};"
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
		"(Array.isArray(d.slots)?d.slots:[]).forEach(setSlotCard);"
		"setActiveSlotCard(Number(d.active_profile_slot)||1);"
		"setBridgeState(Boolean(d.bridge_connected),d.controller_transport);"
		"}catch(e){text('device-time','Disconnected');text('last-event','ESP status endpoint is unavailable.');setBridgeState(false,'ESP status unavailable');}"
		"}"
		"refreshStatus();setInterval(refreshStatus,1500);"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static void start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	config.max_uri_handlers = 11;
	config.uri_match_fn = httpd_uri_match_wildcard;

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
