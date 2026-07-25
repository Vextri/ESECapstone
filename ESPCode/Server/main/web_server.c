#include "web_server.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "audio_feedback.h"
#include "bridge_state.h"
#include "led_feedback.h"
#include "time_utils.h"
#include "uart_bridge.h"

static const char *TAG = "time_server";

#define HTTP_BODY_BUFFER_SIZE 512
#define STATUS_RESPONSE_BUFFER_SIZE 8192
#define STATUS_SLOTS_BUFFER_SIZE    3072
#define HISTORY_JSON_BUFFER_SIZE    3300

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

static const char *json_bool(bool value)
{
	return value ? "true" : "false";
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
	char *src;
	char *dst;

	if (httpd_query_key_value(body, key, value, value_size) == ESP_OK) {
		/* Decode application/x-www-form-urlencoded values in-place. */
		src = value;
		dst = value;
		while (*src != '\0') {
			if (*src == '+') {
				*dst++ = ' ';
				src++;
			} else if (src[0] == '%' && src[1] != '\0' && src[2] != '\0') {
				char hi = src[1];
				char lo = src[2];
				int hi_val = (hi >= '0' && hi <= '9') ? (hi - '0') :
					     (hi >= 'A' && hi <= 'F') ? (hi - 'A' + 10) :
					     (hi >= 'a' && hi <= 'f') ? (hi - 'a' + 10) : -1;
				int lo_val = (lo >= '0' && lo <= '9') ? (lo - '0') :
					     (lo >= 'A' && lo <= 'F') ? (lo - 'A' + 10) :
					     (lo >= 'a' && lo <= 'f') ? (lo - 'a' + 10) : -1;
				if (hi_val >= 0 && lo_val >= 0) {
					*dst++ = (char)((hi_val << 4) | lo_val);
					src += 3;
				} else {
					*dst++ = *src++;
				}
			} else {
				*dst++ = *src++;
			}
		}
		*dst = '\0';
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
	/* Always reset to the new total — LOAD_PROFILE resets the Pico count to
	 * total= anyway, so the ESP cache must always match after a profile save. */
	slot_state->pills_left = total_pills;
	slot_state->doses_remaining = dose > 0 ? total_pills / dose : -1;
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
	char slots_csv[64];
	bool has_slots_csv;
	char slots_copy[64];
	char *saveptr = NULL;
	char *token;
	int slot_number;
	int enqueued_count = 0;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	has_slots_csv = http_body_get_string(body, "slots", slots_csv, sizeof(slots_csv)) && slots_csv[0] != '\0';

	if (!http_body_get_int(body, "slot", &slot_number)) {
		slot_number = -1;
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return send_json_response(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"bridge_busy\"}");
	}

	bridge_update_connected_flag_locked();
	if (has_slots_csv) {
		bridge_copy_string(slots_copy, sizeof(slots_copy), slots_csv);
		token = strtok_r(slots_copy, ",", &saveptr);
		while (token != NULL) {
			int requested_slot = atoi(token);
			if (bridge_slot_index_from_number(requested_slot) < 0 ||
			    !bridge_enqueue_dispense_slot_locked(&bridge_state, requested_slot)) {
				xSemaphoreGive(bridge_state_mutex);
				return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or full dispense queue");
			}
			enqueued_count++;
			token = strtok_r(NULL, ",", &saveptr);
		}
		if (enqueued_count == 0) {
			xSemaphoreGive(bridge_state_mutex);
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No valid slots in slots list");
		}
	} else {
		if (slot_number < 0) {
			slot_number = bridge_state.active_profile_slot;
		}

		if (bridge_slot_index_from_number(slot_number) < 0 ||
		    !bridge_enqueue_dispense_slot_locked(&bridge_state, slot_number)) {
			xSemaphoreGive(bridge_state_mutex);
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot number or full queue");
		}
	}

	bridge_start_next_dispense_locked(&bridge_state);
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

static esp_err_t feedback_test_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char result[16];

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_string(body, "result", result, sizeof(result))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing result field");
	}

	if (strcmp(result, "success") == 0) {
		audio_enqueue_event(AUDIO_EVENT_SUCCESS);
		led_enqueue_event(LED_EVENT_SUCCESS, -1);
	} else if (strcmp(result, "fail") == 0 || strcmp(result, "failure") == 0) {
		audio_enqueue_event(AUDIO_EVENT_FAILURE);
		led_enqueue_event(LED_EVENT_FAILURE, -1);
	} else {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid result value");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t edit_mode_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char state[16];
	int slot_number;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_int(body, "slot", &slot_number) ||
	    bridge_slot_index_from_number(slot_number) < 0) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
	}

	if (!http_body_get_string(body, "state", state, sizeof(state))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing state field");
	}

	if (strcmp(state, "on") == 0 || strcmp(state, "start") == 0) {
		led_enqueue_event(LED_EVENT_EDIT_BEGIN, slot_number);
	} else if (strcmp(state, "off") == 0 || strcmp(state, "end") == 0) {
		led_enqueue_event(LED_EVENT_EDIT_END, slot_number);
	} else {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid state value");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
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

static esp_err_t status_get_handler(httpd_req_t *req)
{
	char time_buf[64];
	char *response;
	char *slots_json;
	char *history_json;
	pico_bridge_state_t *snapshot;
	size_t slots_used = 0;
	size_t hist_used  = 0;
	bool has_fail = false;
	bool has_low  = false;
	const pill_slot_state_t *active_slot;
	int slot_index;
	esp_err_t result;

	response     = malloc(STATUS_RESPONSE_BUFFER_SIZE);
	slots_json   = malloc(STATUS_SLOTS_BUFFER_SIZE);
	history_json = malloc(HISTORY_JSON_BUFFER_SIZE);
	snapshot     = malloc(sizeof(*snapshot));
	if (response == NULL || slots_json == NULL || history_json == NULL || snapshot == NULL) {
		free(response);
		free(slots_json);
		free(history_json);
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

	/* Compute alert flags and build history JSON array (newest first) */
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; slot_index++) {
		const pill_slot_state_t *s = &snapshot->slots[slot_index];
		if (!s->is_active) continue;
		if (strcmp(s->last_dispense_result, "fail") == 0 ||
		    strcmp(s->last_dispense_result, "timeout") == 0) has_fail = true;
		if (s->pills_left >= 0 && s->pills_left < LOW_PILL_THRESHOLD) has_low = true;
	}
	bridge_append_text(history_json, HISTORY_JSON_BUFFER_SIZE, &hist_used, "[");
	for (slot_index = snapshot->history_count - 1; slot_index >= 0; slot_index--) {
		const dispense_history_entry_t *h = &snapshot->history[slot_index];
		bridge_append_text(history_json, HISTORY_JSON_BUFFER_SIZE, &hist_used,
				   "%s{\"slot\":%d,\"medication\":\"%s\",\"result\":\"%s\","
				   "\"pills_left_after\":%d,\"event\":\"%s\",\"time\":%lld}",
				   slot_index == snapshot->history_count - 1 ? "" : ",",
				   h->slot_number, h->medication_name, h->result,
				   h->pills_left_after, h->event, (long long)h->esp_timestamp);
	}
	bridge_append_text(history_json, HISTORY_JSON_BUFFER_SIZE, &hist_used, "]");

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
			 "\"dispense_fail\":%s,"
			 "\"low_pill_warn\":%s,"
			 "\"history\":%s,"
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
			 json_bool(has_fail),
			 json_bool(has_low),
			 history_json,
			 slots_json);

	httpd_resp_set_type(req, "application/json");
	result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
	free(response);
	free(slots_json);
	free(history_json);
	free(snapshot);
	return result;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
	const char *response =
		"<!doctype html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>Pill Dispenser Home</title>"
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
		".edit-panel{display:none;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:10px;}"
		"label{display:grid;gap:6px;font-size:.9rem;color:var(--muted);}"
		"input,select,button{font:inherit;border-radius:14px;border:1px solid var(--line);padding:10px 12px;background:#fffdf9;color:var(--ink);}"
		"button{cursor:pointer;background:#12333b;color:#f7fbfb;border:none;}"
		"button.alt{background:#e7efe7;color:#12333b;border:1px solid var(--line);}"
		".control-actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}"
		".status-copy{font-size:.92rem;color:var(--muted);margin-top:10px;}"
		".footer-card{padding:12px 14px;border-radius:16px;background:rgba(255,255,255,.58);border:1px solid rgba(255,255,255,.7);}"
		".alert-banner{display:none;background:#fef2f2;border:1.5px solid #fca5a5;border-radius:16px;padding:14px 18px;color:#b91c1c;font-weight:700;margin-bottom:18px;}"
		".alert-banner.visible{display:block;}"
		".warn-banner{display:none;background:#fffbeb;border:1.5px solid #fcd34d;border-radius:16px;padding:14px 18px;color:#92400e;font-weight:700;margin-bottom:18px;}"
		".warn-banner.visible{display:block;}"
		".hist-table{width:100%;border-collapse:collapse;font-size:.9rem;}"
		".hist-table th{text-align:left;color:var(--muted);font-size:.78rem;letter-spacing:.1em;text-transform:uppercase;padding:6px 4px;border-bottom:1px solid var(--line);}"
		".hist-table td{padding:8px 4px;border-bottom:1px solid var(--line);}"
		".hist-table tr:last-child td{border-bottom:none;}"
		"@media (max-width:760px){.shell{padding:14px 14px 28px;}.hero{padding:20px;}}"
		"</style></head><body>"
		"<main class=\"shell\">"
		"<section class=\"hero\">"
		"<div class=\"eyebrow\">ESP32 access point dashboard</div>"
		"<h1>Pill Dispenser Home</h1>"
		"<p class=\"lede\">Use this page to check connection status, review each slot, save medication settings, and run a dispense when needed.</p>"
		"</section>"
		"<div class=\"stack\">"
		"<div id=\"fail-banner\" class=\"alert-banner\">&#9888; Dispense failure detected &mdash; check the dispenser.</div>"
		"<div id=\"low-pill-banner\" class=\"warn-banner\">&#9888; Low pill count &mdash; one or more slots need refilling soon.</div>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\">"
		"<div><p class=\"panel-title\">System Status</p><div id=\"bridge-copy\">Dashboard is running. Waiting for dispenser connection.</div></div>"
		"<div class=\"status-pill\" id=\"bridge-pill\"><span class=\"dot\"></span><span id=\"bridge-label\">Connecting...</span></div>"
		"</div>"
		"<div class=\"connection-grid\">"
		"<article class=\"summary accent-teal\"><div class=\"label\">Controller</div><div class=\"value\" id=\"controller-name\">Raspberry Pi Pico 2</div><div class=\"hint\">This board controls the motors and confirms dispense events.</div></article>"
		"<article class=\"summary accent-amber\"><div class=\"label\">Connection</div><div class=\"value\" id=\"controller-transport\">Waiting for dispenser link</div><div class=\"hint\">Shows whether live updates are arriving from the dispenser controller.</div></article>"
		"<article class=\"summary accent-teal\"><div class=\"label\">Device Time</div><div class=\"value\" id=\"device-time\">Loading...</div><div class=\"hint\">Current time reported by the ESP dashboard host.</div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Medication Slots</p><div>All five slots are shown below. The highlighted card is the currently active slot.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-0\"><div class=\"label\">Slot 0</div><div class=\"value\" id=\"slot-medication-0\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-0\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-0\">--</span> | Doses remaining: <span id=\"slot-doses-0\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-0\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-0\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-1\"><div class=\"label\">Slot 1</div><div class=\"value\" id=\"slot-medication-1\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-1\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-1\">--</span> | Doses remaining: <span id=\"slot-doses-1\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-1\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-1\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-2\"><div class=\"label\">Slot 2</div><div class=\"value\" id=\"slot-medication-2\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-2\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-2\">--</span> | Doses remaining: <span id=\"slot-doses-2\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-2\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-2\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-3\"><div class=\"label\">Slot 3</div><div class=\"value\" id=\"slot-medication-3\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-3\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-3\">--</span> | Doses remaining: <span id=\"slot-doses-3\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-3\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-3\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-4\"><div class=\"label\">Slot 4</div><div class=\"value\" id=\"slot-medication-4\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-4\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-4\">--</span> | Doses remaining: <span id=\"slot-doses-4\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-4\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-4\">unknown</span></div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Selected Slot Details</p><div>Latest medication and dispense details for the active slot.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal\"><div class=\"label\">Medication</div><div class=\"value\" id=\"medication-name\">Waiting for data</div><div class=\"hint\">Active profile slot <span id=\"profile-slot\">0</span>.</div></article>"
		"<article class=\"metric accent-amber\"><div class=\"label\">Pills Left</div><div class=\"value\" id=\"pills-left\">--</div><div class=\"hint\"><span id=\"doses-remaining\">--</span> full doses remaining at <span id=\"pills-per-dose\">--</span> pills per dose.</div></article>"
		"</div>"
		"<div class=\"list\">"
		"<div class=\"row\"><span class=\"k\">Last Dispense</span><span class=\"v\" id=\"last-dispensed\">No confirmed dispense yet</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Event</span><span class=\"v\" id=\"last-event\">Waiting for live data</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Result</span><span class=\"v\" id=\"last-result\">Waiting</span></div>"
		"<div class=\"row\"><span class=\"k\">Notes</span><span class=\"v\" id=\"dashboard-notes\">Waiting for live pill slot data.</span></div>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Actions</p><div>Pick a slot, update its settings, run a dispense, or test lights and sounds.</div></div></div>"
		"<div class=\"controls\">"
		"<label>Slot<select id=\"control-slot\"><option value=\"0\">Slot 0</option><option value=\"1\">Slot 1</option><option value=\"2\">Slot 2</option><option value=\"3\">Slot 3</option><option value=\"4\">Slot 4</option></select></label>"
		"<label>Manual Time<input id=\"control-manual-time\" type=\"datetime-local\"></label>"
		"</div>"
		"<div class=\"edit-panel\" id=\"edit-panel\">"
		"<label>Medication Name<input id=\"control-med\" maxlength=\"31\" placeholder=\"Aspirin\"></label>"
		"<label>Pills in Slot<input id=\"control-total\" type=\"number\" min=\"1\" step=\"1\" value=\"20\"></label>"
		"<label>Pills per Dose<input id=\"control-dose\" type=\"number\" min=\"1\" step=\"1\" value=\"1\"></label>"
		"<label>Dispense Time (ms)<input id=\"control-time\" type=\"number\" min=\"100\" step=\"50\" value=\"800\"></label>"
		"<label>Daily Schedule<input id=\"control-schedule\" placeholder=\"Example: 08:00,20:00 (or none)\"></label>"
		"</div>"
		"<div class=\"control-actions\">"
		"<button id=\"edit-start\" type=\"button\">Edit Slot Settings</button>"
		"<button id=\"save-profile\" type=\"button\">Save Slot Settings</button>"
		"<button id=\"dispense-slot\" type=\"button\">Dispense Now</button>"
		"<button id=\"sync-time\" type=\"button\" class=\"alt\">Sync Clock</button>"
		"<button id=\"test-success\" type=\"button\" class=\"alt\">Test Success Alert</button>"
		"<button id=\"test-fail\" type=\"button\" class=\"alt\">Test Failure Alert</button>"
		"</div>"
		"<div class=\"status-copy\" id=\"control-status\">Choose an action to begin.</div>"
		"</section>"
		"<section class=\"panel\" id=\"history-panel\" style=\"display:none\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Dispense History</p><div>Most recent dispense events, newest first.</div></div></div>"
		"<table class=\"hist-table\"><thead><tr><th>#</th><th>Slot</th><th>Medication</th><th>Result</th><th>Pills After</th><th>Time</th></tr></thead>"
		"<tbody id=\"history-body\"><tr><td colspan=\"6\">No history yet.</td></tr></tbody></table>"
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
		"let webEditActive=false;"
		"let webEditSlot='0';"
		"const text=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value;};"
		"const toLocalDateTimeValue=date=>{const pad=v=>String(v).padStart(2,'0');return date.getFullYear()+'-'+pad(date.getMonth()+1)+'-'+pad(date.getDate())+'T'+pad(date.getHours())+':'+pad(date.getMinutes());};"
		"const setResult=(id,value)=>{const el=document.getElementById(id);if(!el)return;const normalized=value||'unknown';el.textContent=normalized==='ok'?'Success':normalized==='fail'?'Missed':normalized==='timeout'?'No Response':normalized;el.className=(id==='last-result'?'v ':'')+(normalized==='ok'?'result-ok':normalized==='fail'||normalized==='timeout'?'result-fail':'');};"
		"const setActiveSlotCard=slot=>{for(let n=0;n<5;n+=1){const card=document.getElementById('slot-card-'+n);if(card)card.className='metric '+(n%2===0?'accent-teal ':'accent-amber ')+'slot-card'+(n===slot?' active':'');}};"
		"const setStatusCopy=msg=>text('control-status',msg);"
		"const setSlotCard=slot=>{if(!slot||slot.slot==null)return;text('slot-medication-'+slot.slot,slot.medication_name||'Waiting for data');text('slot-left-'+slot.slot,displayNumber(slot.pills_left));text('slot-dose-'+slot.slot,displayNumber(slot.pills_per_dose));text('slot-doses-'+slot.slot,displayNumber(slot.doses_remaining));text('slot-schedule-'+slot.slot,slot.schedule||'none');setResult('slot-result-'+slot.slot,slot.last_dispense_result||'unknown');};"
		"const isEditingControls=()=>{const active=document.activeElement;return Boolean(active&&controlIds.includes(active.id));};"
		"const getSlotByNumber=slotNumber=>latestSlots.find(slot=>slot&&slot.slot===slotNumber);"
		"const fillControlsFromSlot=slot=>{if(!slot)return;control('control-slot').value=String(slot.slot);control('control-med').value=slot.medication_name&&slot.medication_name!=='Waiting for data'?slot.medication_name:'';control('control-total').value=slot.total_pills>0?slot.total_pills:20;control('control-dose').value=slot.pills_per_dose>0?slot.pills_per_dose:1;control('control-time').value=slot.time_ms>0?slot.time_ms:800;control('control-schedule').value=slot.schedule&&slot.schedule!=='none'?slot.schedule:'';};"
		"const postForm=async(url,data)=>{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw new Error(await r.text());return r.json();};"
		"const setEditPanel=(open)=>{const panel=document.getElementById('edit-panel');const startBtn=control('edit-start');const saveBtn=control('save-profile');if(panel)panel.style.display=open?'grid':'none';if(startBtn)startBtn.style.display=open?'none':'';if(saveBtn)saveBtn.style.display=open?'':'none';};"
		"const setWebEditMode=async(on)=>{const slot=control('control-slot').value;await postForm('/api/edit-mode',{slot,state:on?'on':'off'});webEditActive=on;webEditSlot=slot;};"
		"const saveProfile=async()=>{const data={slot:control('control-slot').value,med:control('control-med').value,total:control('control-total').value,dose:control('control-dose').value,time:control('control-time').value,schedule:control('control-schedule').value||'none'};await postForm('/api/profile',data);setStatusCopy('Slot settings saved.');await refreshStatus();if(webEditActive){await setWebEditMode(false);setEditPanel(false);}};"
		"const dispenseSelected=async()=>{await postForm('/api/dispense',{slot:control('control-slot').value});setStatusCopy('Dispense started. Waiting for confirmation...');await refreshStatus();};"
		"const syncTime=async()=>{const raw=control('control-manual-time').value;if(!raw)throw new Error('missing-time');const epoch=Math.floor(new Date(raw+'Z').getTime()/1000);if(!Number.isFinite(epoch)||epoch<=0)throw new Error('invalid-time');await postForm('/api/time-sync',{epoch:String(epoch)});setStatusCopy('Clock sync sent.');};"
		"const testFeedback=async(result)=>{await postForm('/api/test-feedback',{result});setStatusCopy(result==='success'?'Success alert test sent.':'Failure alert test sent.');};"
		"const setBridgeState=(connected,status)=>{const pill=document.getElementById('bridge-pill');text('bridge-label',connected?'Connected':'Connecting...');text('bridge-copy',connected?'Live updates are coming in from the dispenser controller.':'Dashboard is running. Waiting for dispenser connection.');if(pill)pill.className=connected?'status-pill online':'status-pill';if(status){text('controller-transport',status);} };"
		"async function refreshStatus(){"
		"try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('bad-response');const d=await r.json();"
		"text('device-time',d.device_time||'Unavailable');"
		"text('controller-name',d.controller_name||'Raspberry Pi Pico 2');"
		"text('controller-transport',d.controller_transport||'Waiting for dispenser link');"
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
		"if(d.awaiting_dispense_ack){setStatusCopy('Waiting for dispenser confirmation...');}"
		"setBridgeState(Boolean(d.bridge_connected),d.controller_transport);"
		"const failBanner=document.getElementById('fail-banner');if(failBanner)failBanner.className='alert-banner'+(d.dispense_fail?' visible':'');"
		"const lowBanner=document.getElementById('low-pill-banner');if(lowBanner)lowBanner.className='warn-banner'+(d.low_pill_warn?' visible':'');"
		"const history=Array.isArray(d.history)?d.history:[];"
		"const histPanel=document.getElementById('history-panel');if(histPanel)histPanel.style.display=history.length>0?'':'none';"
		"const histBody=document.getElementById('history-body');if(histBody&&history.length>0){histBody.innerHTML=history.map((h,i)=>{const t=h.time>0?new Date(h.time*1000).toLocaleTimeString():'--';const res=h.result==='ok'?'Success':h.result==='fail'?'Missed':h.result==='timeout'?'No Response':h.result||'--';return '<tr><td>'+(i+1)+'</td><td>'+h.slot+'</td><td>'+(h.medication||'--')+'</td><td>'+res+'</td><td>'+displayNumber(h.pills_left_after)+'</td><td>'+t+'</td></tr>';}).join('');}"
		"}catch(e){text('device-time','Disconnected');text('last-event','ESP status endpoint is unavailable.');setBridgeState(false,'ESP status unavailable');}"
		"}"
		"control('edit-start').addEventListener('click',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));setWebEditMode(true).then(()=>{setEditPanel(true);setStatusCopy('Edit mode on for selected slot.');}).catch(()=>setStatusCopy('Could not start edit mode.'));});"
		"control('save-profile').addEventListener('click',()=>{saveProfile().catch(()=>setStatusCopy('Could not save slot settings.'));});"
		"control('dispense-slot').addEventListener('click',()=>{dispenseSelected().catch(()=>setStatusCopy('Could not start dispense.'));});"
		"control('sync-time').addEventListener('click',()=>{syncTime().catch(()=>setStatusCopy('Pick a valid date/time first.'));});"
		"control('test-success').addEventListener('click',()=>{testFeedback('success').catch(()=>setStatusCopy('Could not run success alert test.'));});"
		"control('test-fail').addEventListener('click',()=>{testFeedback('fail').catch(()=>setStatusCopy('Could not run failure alert test.'));});"
		"if(!control('control-manual-time').value){control('control-manual-time').value=toLocalDateTimeValue(new Date());}"
		"control('control-slot').addEventListener('change',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));if(webEditActive){setWebEditMode(true).catch(()=>{});}});"
		"setEditPanel(false);"
		"refreshStatus();setInterval(refreshStatus,1500);"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

void start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	config.max_uri_handlers = 16;
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
		httpd_uri_t feedback_test = {
			.uri = "/api/test-feedback",
			.method = HTTP_POST,
			.handler = feedback_test_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t edit_mode = {
			.uri = "/api/edit-mode",
			.method = HTTP_POST,
			.handler = edit_mode_post_handler,
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
		httpd_register_uri_handler(server, &feedback_test);
		httpd_register_uri_handler(server, &edit_mode);
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
