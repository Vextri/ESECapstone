/* ============================================================================
 * WEB_SERVER.C - HTTP Dashboard and JSON API Implementation
 * ----------------------------------------------------------------------------
 * Three groups of routes, registered in start_webserver() at the bottom of
 * this file:
 *
 *   1. Static/asset routes (root_get_handler, logo_get_handler, and the
 *      captive-portal probe handlers) - root_get_handler is the big one,
 *      it serves the entire dashboard as one self-contained HTML page with
 *      inline CSS and JS, no separate front-end build step or file server
 *      needed.
 *   2. JSON API routes (status/profile/dispense/time-sync/feedback-test/
 *      simulate-dispense/edit-mode) - read and mutate bridge_state, the
 *      same shared state the LCD and UART bridge use, guarded by
 *      bridge_state_mutex.
 *   3. Socket lifecycle tracking (http_socket_open_cb/close_cb and
 *      web_server_close_sockets_for_ip) - logs every connection's open/
 *      close for remote diagnosis and lets wifi_ap.c proactively close a
 *      socket left behind by a device that dropped off Wi-Fi abruptly.
 * ============================================================================ */

#include "web_server.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

#include "audio_feedback.h"
#include "bridge_state.h"
#include "debug_log.h"
#include "led_feedback.h"
#include "notify.h"
#include "time_utils.h"
#include "uart_bridge.h"

static const char *TAG = "time_server";

#define HTTP_BODY_BUFFER_SIZE 512
#define STATUS_RESPONSE_BUFFER_SIZE 8192
#define STATUS_SLOTS_BUFFER_SIZE    3072
#define HISTORY_JSON_BUFFER_SIZE    3300

/* Sends a bare 302 redirect to "/". Used by the captive-portal probe
 * handlers below, whose job is just to get the OS to open a real browser
 * at the dashboard. */
static esp_err_t redirect_to_root(httpd_req_t *req)
{
	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", "/");
	return httpd_resp_send(req, NULL, 0);
}

/* Answers Apple's captive-portal probe (and the similarly-registered
 * Android/Windows probe URLs further down) with a trivial page, matching
 * the pattern hotel/airport Wi-Fi portals use to trigger the OS's
 * "sign in to this network" prompt automatically. */
static esp_err_t apple_captive_handler(httpd_req_t *req)
{
	const char *response = "<html><body>Login</body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

/* Embedded via EMBED_FILES in main/CMakeLists.txt from Assets/PortaPill_logo_web.png.
 * ESP-IDF's embed mechanism names the generated symbols after the file's
 * basename only (directories are stripped), not the full relative path. */
extern const uint8_t logo_png_start[] asm("_binary_PortaPill_logo_web_png_start");
extern const uint8_t logo_png_end[]   asm("_binary_PortaPill_logo_web_png_end");

/* Serves the dashboard's logo image straight out of flash, embedded into
 * the firmware binary at build time (see the extern declarations above),
 * no filesystem needed. */
static esp_err_t logo_get_handler(httpd_req_t *req)
{
	httpd_resp_set_type(req, "image/png");
	httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=86400");
	return httpd_resp_send(req, (const char *)logo_png_start, logo_png_end - logo_png_start);
}

/* Plain-text recent log dump, readable over WiFi from any browser (e.g.
 * http://portapill.local/api/debug-log), for exactly the situation where
 * there's no physical room to plug a USB cable into the ESP once it's
 * inside the assembled device. Reload the page to get a fresh snapshot,
 * this isn't a live stream, just a point-in-time copy of the recent
 * in-RAM log buffer (see debug_log.c). */
static esp_err_t debug_log_get_handler(httpd_req_t *req)
{
	char *buf;
	size_t len;
	esp_err_t result;

	buf = malloc(DEBUG_LOG_BUF_SIZE + 1);
	if (buf == NULL) {
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
	}

	len = debug_log_snapshot(buf, DEBUG_LOG_BUF_SIZE + 1);

	httpd_resp_set_type(req, "text/plain");
	httpd_resp_set_hdr(req, "Cache-Control", "no-store");
	result = httpd_resp_send(req, buf, (ssize_t)len);
	free(buf);
	return result;
}

/* Renders a C bool as the JSON literal "true"/"false" for building JSON
 * responses by hand with snprintf, no JSON library is used anywhere in
 * this file. */
static const char *json_bool(bool value)
{
	return value ? "true" : "false";
}

/* Reads an HTTP request body into a caller-supplied buffer, bounded by
 * both the buffer size and the request's own declared Content-Length.
 * Every POST handler in this file starts by calling this. */
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

/* Extracts one field's value from a URL-encoded form body ("key=value&...")
 * and decodes it in place (%XX escapes and '+' for space), the standard
 * application/x-www-form-urlencoded format the dashboard's JS sends POST
 * bodies as. Returns false if the key isn't present. */
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

/* Same as http_body_get_string(), but parses the result as an integer. */
static bool http_body_get_int(const char *body, const char *key, int *value)
{
	char temp[16];

	if (!http_body_get_string(body, key, temp, sizeof(temp))) {
		return false;
	}

	*value = atoi(temp);
	return true;
}

/* Rejects a medication name containing '|', '\n', or '\r', since those are
 * the field/line delimiters used by the UART protocol to the Pico, letting
 * one through as-is would corrupt whatever LOAD_PROFILE line it ends up
 * embedded in. */
static bool bridge_validate_medication_name(const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return false;
	}

	return strchr(name, '|') == NULL && strchr(name, '\n') == NULL && strchr(name, '\r') == NULL;
}

/* Sends body as a JSON response with the given HTTP status line, e.g.
 * send_json_response(req, "200 OK", "{\"ok\":true}"). */
static esp_err_t send_json_response(httpd_req_t *req, const char *status, const char *body)
{
	httpd_resp_set_status(req, status);
	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

/* POST /api/profile - saves a slot's medication name, pill counts, and
 * dose timing/schedule, both to bridge_state (and NVS) and pushed down to
 * the Pico via bridge_send_load_profile_for_slot() so both sides agree on
 * what's configured. */
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
	/* Always reset to the new total. LOAD_PROFILE resets the Pico count to
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

/* POST /api/dispense - queues one or more slots for a manual dispense
 * ("slot" for one, or "slots" as a comma-separated list for several at
 * once), then kicks off the first one if nothing is already in flight.
 * Always enqueued through bridge_enqueue_dispense_slot_locked() rather
 * than sent immediately, so this plays correctly with the schedule
 * checker and other manual requests instead of racing them. */
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
			    !bridge_enqueue_dispense_slot_locked(&bridge_state, requested_slot, false)) {
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
		    !bridge_enqueue_dispense_slot_locked(&bridge_state, slot_number, false)) {
			xSemaphoreGive(bridge_state_mutex);
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot number or full queue");
		}
	}

	bridge_start_next_dispense_locked(&bridge_state);
	bridge_state_save_to_nvs(&bridge_state);
	xSemaphoreGive(bridge_state_mutex);

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

/* POST /api/time-sync - the dashboard's manual "Sync Clock" button. If the
 * request includes an epoch (the browser's own clock, useful as a fallback
 * when the ESP has no internet access for NTP), applies it to the ESP's
 * clock first. Either way, relays the ESP's current time to the Pico
 * afterward. */
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

/* POST /api/test-feedback - the dashboard's "Test Success/Failure Alert"
 * buttons. Fires the LED/audio feedback (and, for success, a real test
 * push notification) without needing an actual dispense, useful for
 * verifying those subsystems work independently of the Pico. */
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
		notify_send_test();
	} else if (strcmp(result, "fail") == 0 || strcmp(result, "failure") == 0) {
		audio_enqueue_event(AUDIO_EVENT_FAILURE);
		led_enqueue_event(LED_EVENT_FAILURE, -1);
	} else {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid result value");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

/* Test-only: simulates a Pico ACKing a dispense as "ok" for `slot`, without
 * any real hardware attached. Arms the exact same awaiting_drawer_open
 * state and escalating reminder timers (NOTIFY_REMINDER_STAGE_COUNT stages,
 * NOTIFY_REMINDER_DELAYS_MIN in notify.h) a real dispense
 * ACK would, so the full pickup-confirmation / "pickup not confirmed"
 * notification pipeline can be exercised end to end before the Pico and
 * drawer sensor are wired up. Wired to the dashboard's "Simulate Dispense"
 * button. */
static esp_err_t simulate_dispense_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	int slot_number;
	char med_name[32];

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_int(body, "slot", &slot_number) ||
	    bridge_slot_index_from_number(slot_number) < 0) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return send_json_response(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"bridge_busy\"}");
	}

	bridge_copy_string(med_name, sizeof(med_name), bridge_state.slots[slot_number].medication_name);
	bridge_state.awaiting_drawer_open = true;
	bridge_state.drawer_open_slot = slot_number;
	bridge_state.active_profile_slot = slot_number;
	bridge_copy_string(bridge_state.slots[slot_number].last_dispense_result,
			   sizeof(bridge_state.slots[slot_number].last_dispense_result), "pending");
	bridge_copy_string(bridge_state.slots[slot_number].last_event,
			   sizeof(bridge_state.slots[slot_number].last_event),
			   "Simulated dispense (test). Waiting for drawer-open confirmation.");
	bridge_state_save_to_nvs(&bridge_state);
	xSemaphoreGive(bridge_state_mutex);

	notify_schedule_dispense_reminder(slot_number, med_name, time(NULL));

	ESP_LOGI(TAG, "Simulated a successful dispense for slot %d (test only, no real hardware involved)", slot_number);

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

/* POST /api/edit-mode - toggles the edit-mode LED indicator for a slot
 * while its profile form is open in the dashboard, so the same slot LED
 * used for dispense feedback also shows "someone is currently editing
 * this one" on the physical device. */
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

/* printf-style append into a buffer, tracking how much of it is used so
 * far. Used throughout status_get_handler() to build up the slots/history
 * JSON arrays piece by piece without needing a JSON library. Stops
 * (returns false) rather than overflowing if the buffer fills up. */
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

/* ----------------------------------------------------------------------------
 * status_get_handler()
 * ----------------------------------------------------------------------------
 * GET /api/status - the JSON the dashboard's JS polls every 1.5s to refresh
 * the whole page. Takes one consistent snapshot of bridge_state under its
 * mutex, then builds the response outside the lock: every slot's full
 * profile/status, the dispense history (newest first), computed alert
 * flags (any slot failed, any slot low on pills), and the next-dispense
 * summary string. Buffers are heap-allocated rather than stack, this
 * response is large enough that stack allocation would be risky on a task
 * with a modest stack size.
 * ---------------------------------------------------------------------------- */
static esp_err_t status_get_handler(httpd_req_t *req)
{
	char time_buf[64];
	char next_dispense[32];
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

	screen_get_next_dispense_string(snapshot, next_dispense, sizeof(next_dispense));

	snprintf(response,
			 STATUS_RESPONSE_BUFFER_SIZE,
			 "{"
			 "\"device_time\":\"%s\","
			 "\"bridge_connected\":%s,"
			 "\"bridge_status\":\"waiting_for_pico\","
			 "\"controller_name\":\"Raspberry Pi Pico 2\","
			 "\"controller_transport\":\"%s\","
			 "\"next_dispense\":\"%s\","
			 "\"active_profile_slot\":%d,"
			 "\"slot_count\":%d,"
			 "\"history_count\":%d,"
			 "\"last_ack_action\":\"%s\","
			 "\"last_ack_result\":\"%s\","
			 "\"awaiting_dispense_ack\":%s,"
			 "\"awaiting_drawer_open\":%s,"
			 "\"drawer_open_slot\":%d,"
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
			 "\"ntfy_topic\":\"%s\","
			 "\"ntfy_subscribe_url\":\"%s\","
			 "\"history\":%s,"
			 "\"slots\":%s"
			 "}",
			 time_buf,
			 json_bool(snapshot->connected),
			 snapshot->controller_transport,
			 next_dispense,
			 snapshot->active_profile_slot,
			 PILL_SLOT_COUNT,
			 snapshot->history_count,
			 snapshot->last_ack_action,
			 snapshot->last_ack_result,
			 json_bool(snapshot->awaiting_dispense_ack),
			 json_bool(snapshot->awaiting_drawer_open),
			 snapshot->drawer_open_slot,
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
			 notify_get_topic(),
			 notify_get_subscribe_url(),
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

/* ----------------------------------------------------------------------------
 * root_get_handler()
 * ----------------------------------------------------------------------------
 * GET / - serves the entire dashboard as one self-contained HTML page: CSS
 * inlined in a <style> block, JS inlined in a <script> block, no separate
 * assets or build step beyond the logo image. Kept in one C string
 * literal on purpose, so the whole front-end ships as part of the
 * firmware binary itself.
 *
 * Broad structure of the page, for orientation:
 *   - <style>: CSS custom properties for the color palette up top, then
 *     the layout/component rules.
 *   - Body markup: a header with the logo and live status badge, a tab
 *     strip (Slots / History / Settings), and per-tab content panels.
 *   - <script>: fetches /api/status on a 1.5s interval and re-renders the
 *     slot cards, history list, and header badge from the response;
 *     posts to /api/profile, /api/dispense, /api/time-sync,
 *     /api/test-feedback, and /api/edit-mode in response to user actions.
 *
 * The C-level logic in this function is just building and sending one
 * big string, all the actual application behavior for this page lives in
 * the embedded JS, and all the data it displays comes from
 * status_get_handler() above.
 * ---------------------------------------------------------------------------- */
static esp_err_t root_get_handler(httpd_req_t *req)
{
	const char *response =
		"<!doctype html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>PortaPill</title>"
		"<link rel=\"icon\" href=\"/logo.png\">"
		"<style>"
		":root{color-scheme:light;--bg:#eef2f4;--ink:#16232b;--muted:#5b6b74;--panel:#ffffff;--line:rgba(15,35,45,.11);--shadow:0 10px 26px rgba(15,35,45,.07);--teal:#0c6b66;--teal-dark:#0a4f4c;--teal-soft:#dcefec;--amber:#9a6510;--amber-soft:#f6ecd9;--red:#b3261e;--red-soft:#fbe6e4;}"
		"*{box-sizing:border-box;}"
		"body{margin:0;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,Helvetica,Arial,sans-serif;color:var(--ink);background:var(--bg);min-height:100vh;-webkit-font-smoothing:antialiased;}"
		".shell{max-width:1040px;margin:0 auto;padding:24px 18px 40px;}"
		".hero{position:relative;overflow:hidden;background:linear-gradient(155deg,#0a3d3b 0,#0c6b66 100%);color:#f4faf9;border-radius:20px;padding:22px 24px;box-shadow:0 14px 32px rgba(10,61,59,.22);margin-bottom:14px;}"
		".hero:after{content:\"\";position:absolute;inset:auto -60px -80px auto;width:220px;height:220px;border-radius:50%;background:rgba(255,255,255,.05);pointer-events:none;}"
		".hero-top{display:flex;align-items:center;gap:20px;flex-wrap:wrap;position:relative;z-index:1;}"
		".logo-badge{flex-shrink:0;display:inline-flex;align-items:center;justify-content:center;background:rgba(255,253,249,.96);border-radius:18px;padding:8px 14px;box-shadow:0 12px 30px rgba(0,0,0,.2);}"
		".logo{height:48px;width:auto;display:block;}"
		".hero-text{flex:1 1 220px;min-width:0;}"
		".eyebrow{letter-spacing:.16em;text-transform:uppercase;font-size:.68rem;opacity:.78;margin-bottom:4px;}"
		"h1{font-size:clamp(1.5rem,4.2vw,2.1rem);font-weight:800;letter-spacing:-.01em;line-height:1;margin:0;}"
		".lede{max-width:42rem;margin:8px 0 0;font-size:.94rem;line-height:1.5;color:rgba(244,250,249,.82);}"
		".status-pill{display:inline-flex;align-items:center;gap:8px;padding:10px 16px;border-radius:999px;font-weight:700;background:rgba(255,255,255,.14);color:#fff0d8;border:1px solid rgba(255,255,255,.22);flex-shrink:0;transition:background .25s ease,color .25s ease;}"
		".status-pill.online{background:rgba(216,241,238,.22);color:#e6fbf8;border-color:rgba(216,241,238,.32);}"
		".dot{width:10px;height:10px;border-radius:50%;background:currentColor;box-shadow:0 0 0 5px rgba(255,255,255,.16);}"
		".status-pill.online .dot{animation:pulse 2s ease-in-out infinite;}"
		"@keyframes pulse{0%,100%{box-shadow:0 0 0 5px rgba(230,251,248,.16);}50%{box-shadow:0 0 0 9px rgba(230,251,248,.28);}}"
		".hero-meta{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:16px;position:relative;z-index:1;}"
		".hero-meta .mini{background:rgba(255,255,255,.08);border:1px solid rgba(255,255,255,.14);border-radius:16px;padding:10px 14px;}"
		".hero-meta .mini .label{font-size:.68rem;letter-spacing:.1em;text-transform:uppercase;opacity:.68;margin-bottom:4px;}"
		".hero-meta .mini .value{font-size:1rem;font-weight:700;font-variant-numeric:tabular-nums;}"
		".hero-caption{margin:10px 2px 0;font-size:.84rem;color:rgba(244,250,249,.75);position:relative;z-index:1;}"
		".tabbar{display:flex;gap:2px;overflow-x:auto;padding:4px;background:#fff;border:1px solid var(--line);border-radius:14px;margin-bottom:16px;position:sticky;top:10px;z-index:5;box-shadow:var(--shadow);}"
		".tab-btn{flex:0 0 auto;border:none;background:none;color:var(--muted);font:inherit;font-weight:600;font-size:.86rem;padding:10px 16px;border-radius:10px;cursor:pointer;white-space:nowrap;transition:background .15s ease,color .15s ease;}"
		".tab-btn:hover{color:var(--ink);background:#f2f5f5;}"
		".tab-btn.active{background:var(--teal);color:#f4faf9;}"
		".tab-panel{display:none;animation:fadein .18s ease;}"
		".tab-panel.active{display:block;}"
		"@keyframes fadein{from{opacity:0;}to{opacity:1;}}"
		".stack{display:grid;gap:14px;}"
		".panel{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:18px;box-shadow:var(--shadow);}"
		".panel-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:16px;}"
		".panel-title{margin:0;font-size:1rem;letter-spacing:.03em;text-transform:uppercase;color:var(--muted);font-weight:700;}"
		".slot-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:14px;}"
		".slot-card{padding:18px;border-radius:14px;border:1px solid var(--line);background:#fbfcfc;transition:border-color .15s ease,box-shadow .15s ease,background .15s ease;}"
		".slot-card:hover{box-shadow:var(--shadow);}"
		".slot-card.result-ok{border-color:var(--teal);background:var(--teal-soft);}"
		".slot-card.result-fail{border-color:var(--red);background:var(--red-soft);}"
		".slot-card.result-pending{border-color:var(--amber);background:var(--amber-soft);}"
		".slot-card.result-ok .badge-ok{background:var(--teal);color:#fff;}"
		".slot-card.result-fail .badge-fail{background:var(--red);color:#fff;}"
		".slot-card.result-pending .badge-pending{background:var(--amber);color:#fff;}"
		".slot-card.result-ok .slot-pills-label,.slot-card.result-fail .slot-pills-label,.slot-card.result-pending .slot-pills-label,.slot-card.result-ok .stat-label,.slot-card.result-fail .stat-label,.slot-card.result-pending .stat-label{color:var(--ink);opacity:.62;}"
		".slot-card.result-ok .slot-stats-row,.slot-card.result-fail .slot-stats-row,.slot-card.result-pending .slot-stats-row,.slot-card.result-ok .slot-last,.slot-card.result-fail .slot-last,.slot-card.result-pending .slot-last{border-top-color:rgba(22,35,43,.14);}"
		".slot-card.active{box-shadow:0 0 0 2px var(--ink) inset,var(--shadow);}"
		".slot-label{display:flex;align-items:center;gap:7px;font-size:.76rem;letter-spacing:.1em;text-transform:uppercase;color:var(--muted);margin-bottom:8px;font-weight:600;}"
		".slot-icon{flex-shrink:0;}"
		".slot-med{font-size:1.25rem;font-weight:700;line-height:1.2;margin-bottom:12px;min-height:1.2em;}"
		".slot-pills-label{font-size:.75rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);}"
		".slot-pills{font-size:2.2rem;font-weight:700;line-height:1;margin:2px 0 10px;font-variant-numeric:tabular-nums;}"
		".slot-stats-row{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px;padding-top:10px;border-top:1px solid var(--line);}"
		".stat{display:flex;flex-direction:column;gap:3px;min-width:0;}"
		".stat-label{font-size:.65rem;letter-spacing:.08em;text-transform:uppercase;color:var(--muted);}"
		".stat-value{font-size:.94rem;font-weight:700;font-variant-numeric:tabular-nums;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}"
		".badge{display:inline-flex;align-items:center;padding:4px 12px;border-radius:999px;font-size:.78rem;font-weight:700;}"
		".badge-ok{background:var(--teal-soft);color:#0e5d58;}"
		".badge-fail{background:var(--red-soft);color:var(--red);}"
		".badge-pending{background:var(--amber-soft);color:#8a5410;}"
		".badge-muted{background:#eef0ee;color:var(--muted);}"
		".slot-last{display:flex;gap:7px;align-items:flex-start;margin-top:12px;padding-top:10px;border-top:1px solid var(--line);font-size:.82rem;color:var(--muted);line-height:1.4;}"
		".slot-last:before{content:\"\\21bb\";flex-shrink:0;color:var(--muted);opacity:.55;font-size:.9rem;line-height:1.5;}"
		".slot-empty-hint{display:none;font-size:.92rem;color:var(--muted);line-height:1.5;}"
		".footer{display:flex;flex-wrap:wrap;gap:10px;margin-top:4px;color:var(--muted);font-size:.92rem;}"
		".controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;}"
		".edit-panel{display:none;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:14px;}"
		".schedule-block{grid-column:1/-1;}"
		".schedule-label{font-size:.9rem;color:var(--muted);margin-bottom:8px;}"
		".schedule-times{display:flex;flex-wrap:wrap;gap:10px;margin-bottom:10px;}"
		".time-row{display:flex;align-items:center;gap:6px;}"
		".time-row input[type=time]{width:auto;}"
		".time-remove{background:none;border:1px solid var(--line);color:var(--muted);border-radius:10px;width:34px;height:34px;padding:0;font-size:1.1rem;line-height:1;cursor:pointer;}"
		"label{display:grid;gap:6px;font-size:.9rem;color:var(--muted);}"
		"input,select,button{font:inherit;border-radius:14px;border:1px solid var(--line);padding:10px 12px;background:#fffdf9;color:var(--ink);}"
		"input:focus,select:focus,button:focus{outline:2px solid rgba(15,118,110,.35);outline-offset:1px;}"
		"button{cursor:pointer;background:var(--teal-dark);color:#f7fbfb;border:none;transition:transform .15s ease,filter .15s ease;}"
		"button:hover{filter:brightness(1.08);}"
		"button.alt{background:#e7efe7;color:var(--teal-dark);border:1px solid var(--line);}"
		".btn-primary{width:100%;margin-top:16px;padding:18px;font-size:1.15rem;font-weight:700;border-radius:18px;background:var(--teal);letter-spacing:.01em;}"
		".btn-primary:active{transform:scale(.985);}"
		".btn-row{display:flex;flex-wrap:wrap;gap:10px;margin-top:12px;}"
		".btn-row button{flex:1 1 160px;}"
		".sync-row{display:flex;flex-wrap:wrap;gap:10px;margin-top:14px;align-items:center;}"
		".sync-row input{flex:1 1 200px;}"
		".sync-row button{flex:0 0 auto;}"
		".test-row{display:flex;flex-wrap:wrap;gap:10px;margin-top:16px;padding-top:14px;border-top:1px solid var(--line);}"
		".btn-ghost{background:none;border:1px solid var(--line);color:var(--muted);font-size:.82rem;padding:8px 14px;}"
		".footer-card{padding:12px 14px;border-radius:12px;background:#f8fafb;border:1px solid var(--line);color:var(--muted);}"
		".about-copy{font-size:.98rem;line-height:1.65;color:var(--ink);margin:0;}"
		".about-signature{margin-top:12px;font-weight:600;color:var(--muted);}"
		".ntfy-row{display:flex;flex-wrap:wrap;gap:12px;align-items:center;margin-top:12px;}"
		".ntfy-topic-chip{padding:10px 14px;border-radius:14px;background:#fffdf9;border:1px solid var(--line);font-family:monospace;font-size:.9rem;color:var(--ink);word-break:break-all;}"
		".ntfy-link{display:inline-block;padding:10px 16px;border-radius:14px;background:var(--teal-dark);color:#f7fbfb;text-decoration:none;font-weight:700;font-size:.92rem;}"
		".alert-banner{display:none;background:#fef2f2;border:1.5px solid #fca5a5;border-radius:16px;padding:14px 18px;color:#b91c1c;font-weight:700;margin-bottom:14px;}"
		".alert-banner.visible{display:block;}"
		".warn-banner{display:none;background:#fffbeb;border:1.5px solid #fcd34d;border-radius:16px;padding:14px 18px;color:#92400e;font-weight:700;margin-bottom:14px;}"
		".warn-banner.visible{display:block;}"
		".hist-table{width:100%;border-collapse:collapse;font-size:.9rem;}"
		".hist-table th{text-align:left;color:var(--muted);font-size:.78rem;letter-spacing:.1em;text-transform:uppercase;padding:6px 4px;border-bottom:1px solid var(--line);}"
		".hist-table td{padding:8px 4px;border-bottom:1px solid var(--line);}"
		".hist-table tr:last-child td{border-bottom:none;}"
		".toast-wrap{position:fixed;left:0;right:0;bottom:18px;display:flex;justify-content:center;pointer-events:none;z-index:50;}"
		".status-copy{pointer-events:auto;max-width:92vw;background:var(--teal-dark);color:#f7fbfb;font-size:.88rem;font-weight:600;padding:12px 20px;border-radius:999px;box-shadow:0 14px 34px rgba(17,32,39,.32);opacity:0;transform:translateY(10px) scale(.98);transition:opacity .25s ease,transform .25s ease;}"
		".status-copy.show{opacity:1;transform:translateY(0) scale(1);}"
		"@media (max-width:760px){.shell{padding:14px 14px 90px;}.hero{padding:18px;}.logo{height:38px;}}"
		"</style></head><body>"
		"<main class=\"shell\">"
		"<section class=\"hero\">"
		"<div class=\"hero-top\">"
		"<div class=\"logo-badge\"><img class=\"logo\" src=\"/logo.png\" alt=\"PortaPill logo\"></div>"
		"<div class=\"hero-text\">"
		"<div class=\"eyebrow\">Smart Medication Management</div>"
		"<h1>PortaPill</h1>"
		"<p class=\"lede\">Never wonder if today's dose was taken. PortaPill keeps every slot stocked and dispensing right on schedule.</p>"
		"</div>"
		"<div class=\"status-pill\" id=\"bridge-pill\"><span class=\"dot\"></span><span id=\"bridge-label\">Connecting...</span></div>"
		"</div>"
		"<div class=\"hero-meta\">"
		"<div class=\"mini\"><div class=\"label\">Status</div><div class=\"value\" id=\"controller-transport\">Waiting for connection</div></div>"
		"<div class=\"mini\"><div class=\"label\">Next Dispense</div><div class=\"value\" id=\"next-dispense\">Loading...</div></div>"
		"<div class=\"mini\"><div class=\"label\">Device Time</div><div class=\"value\" id=\"device-time\">Loading...</div></div>"
		"</div>"
		"<p class=\"hero-caption\" id=\"bridge-copy\">Dashboard is running. Waiting for the dispenser to connect.</p>"
		"</section>"
		"<div id=\"fail-banner\" class=\"alert-banner\">&#9888; Dispense failure detected. Check the dispenser.</div>"
		"<div id=\"low-pill-banner\" class=\"warn-banner\">&#9888; Low pill count. One or more slots need refilling soon.</div>"
		"<nav class=\"tabbar\" id=\"tabbar\">"
		"<button class=\"tab-btn active\" type=\"button\" data-tab=\"dashboard\">Dashboard</button>"
		"<button class=\"tab-btn\" type=\"button\" data-tab=\"actions\">Schedule &amp; Actions</button>"
		"<button class=\"tab-btn\" type=\"button\" data-tab=\"history\">History</button>"
		"<button class=\"tab-btn\" type=\"button\" data-tab=\"notify\">Notifications</button>"
		"<button class=\"tab-btn\" type=\"button\" data-tab=\"about\">About</button>"
		"</nav>"
		"<div class=\"stack\">"
		"<section class=\"tab-panel active\" data-panel=\"dashboard\">"
		"<div class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Medication Slots</p><div>The highlighted card is the currently active slot.</div></div></div>"
		"<div class=\"slot-grid\">"
		"<article class=\"slot-card\" id=\"slot-card-0\"><div class=\"slot-label\"><svg class=\"slot-icon\" viewBox=\"0 0 24 24\" width=\"14\" height=\"14\" xmlns=\"http://www.w3.org/2000/svg\"><defs><linearGradient id=\"capGrad0\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"49%\" stop-color=\"#0c6b66\"/><stop offset=\"50%\" stop-color=\"#ffffff\"/></linearGradient></defs><rect x=\"3\" y=\"9\" width=\"18\" height=\"6\" rx=\"3\" transform=\"rotate(-45 12 12)\" fill=\"url(#capGrad0)\" stroke=\"#0c6b66\" stroke-width=\"1.2\"/></svg>Slot 1</div><div class=\"slot-med\" id=\"slot-medication-0\">Not set up yet</div>"
		"<div class=\"slot-details\" id=\"slot-details-0\"><div class=\"slot-pills-label\">Pills left</div><div class=\"slot-pills\" id=\"slot-left-0\">--</div><div class=\"slot-stats-row\"><div class=\"stat\"><span class=\"stat-label\">Dose</span><span class=\"stat-value\" id=\"slot-dose-0\">--</span></div><div class=\"stat\"><span class=\"stat-label\">Schedule</span><span class=\"stat-value\" id=\"slot-schedule-0\">None</span></div></div><span class=\"badge badge-muted\" id=\"slot-result-0\">No data yet</span><div class=\"slot-last\" id=\"slot-last-0\">No dispenses yet</div></div>"
		"<div class=\"slot-empty-hint\" id=\"slot-empty-0\">Tap &ldquo;Edit Slot Settings&rdquo; below to set up this slot.</div>"
		"</article>"
		"<article class=\"slot-card\" id=\"slot-card-1\"><div class=\"slot-label\"><svg class=\"slot-icon\" viewBox=\"0 0 24 24\" width=\"14\" height=\"14\" xmlns=\"http://www.w3.org/2000/svg\"><defs><linearGradient id=\"capGrad1\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"49%\" stop-color=\"#9a6510\"/><stop offset=\"50%\" stop-color=\"#ffffff\"/></linearGradient></defs><rect x=\"3\" y=\"9\" width=\"18\" height=\"6\" rx=\"3\" transform=\"rotate(-45 12 12)\" fill=\"url(#capGrad1)\" stroke=\"#9a6510\" stroke-width=\"1.2\"/></svg>Slot 2</div><div class=\"slot-med\" id=\"slot-medication-1\">Not set up yet</div>"
		"<div class=\"slot-details\" id=\"slot-details-1\"><div class=\"slot-pills-label\">Pills left</div><div class=\"slot-pills\" id=\"slot-left-1\">--</div><div class=\"slot-stats-row\"><div class=\"stat\"><span class=\"stat-label\">Dose</span><span class=\"stat-value\" id=\"slot-dose-1\">--</span></div><div class=\"stat\"><span class=\"stat-label\">Schedule</span><span class=\"stat-value\" id=\"slot-schedule-1\">None</span></div></div><span class=\"badge badge-muted\" id=\"slot-result-1\">No data yet</span><div class=\"slot-last\" id=\"slot-last-1\">No dispenses yet</div></div>"
		"<div class=\"slot-empty-hint\" id=\"slot-empty-1\">Tap &ldquo;Edit Slot Settings&rdquo; below to set up this slot.</div>"
		"</article>"
		"<article class=\"slot-card\" id=\"slot-card-2\"><div class=\"slot-label\"><svg class=\"slot-icon\" viewBox=\"0 0 24 24\" width=\"14\" height=\"14\" xmlns=\"http://www.w3.org/2000/svg\"><defs><linearGradient id=\"capGrad2\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\"><stop offset=\"49%\" stop-color=\"#0c6b66\"/><stop offset=\"50%\" stop-color=\"#ffffff\"/></linearGradient></defs><rect x=\"3\" y=\"9\" width=\"18\" height=\"6\" rx=\"3\" transform=\"rotate(-45 12 12)\" fill=\"url(#capGrad2)\" stroke=\"#0c6b66\" stroke-width=\"1.2\"/></svg>Slot 3</div><div class=\"slot-med\" id=\"slot-medication-2\">Not set up yet</div>"
		"<div class=\"slot-details\" id=\"slot-details-2\"><div class=\"slot-pills-label\">Pills left</div><div class=\"slot-pills\" id=\"slot-left-2\">--</div><div class=\"slot-stats-row\"><div class=\"stat\"><span class=\"stat-label\">Dose</span><span class=\"stat-value\" id=\"slot-dose-2\">--</span></div><div class=\"stat\"><span class=\"stat-label\">Schedule</span><span class=\"stat-value\" id=\"slot-schedule-2\">None</span></div></div><span class=\"badge badge-muted\" id=\"slot-result-2\">No data yet</span><div class=\"slot-last\" id=\"slot-last-2\">No dispenses yet</div></div>"
		"<div class=\"slot-empty-hint\" id=\"slot-empty-2\">Tap &ldquo;Edit Slot Settings&rdquo; below to set up this slot.</div>"
		"</article>"
		"</div>"
		"</div>"
		"</section>"
		"<section class=\"tab-panel\" data-panel=\"actions\">"
		"<div class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Actions</p><div>Choose a slot, then dispense now or update its settings.</div></div></div>"
		"<div class=\"controls\">"
		"<label>Slot<select id=\"control-slot\"><option value=\"0\">Slot 1</option><option value=\"1\">Slot 2</option><option value=\"2\">Slot 3</option></select></label>"
		"</div>"
		"<button id=\"dispense-slot\" type=\"button\" class=\"btn-primary\">Dispense Now</button>"
		"<div class=\"btn-row\">"
		"<button id=\"edit-start\" type=\"button\" class=\"alt\">Edit Slot Settings</button>"
		"<button id=\"save-profile\" type=\"button\" style=\"display:none\">Save Slot Settings</button>"
		"</div>"
		"<div class=\"edit-panel\" id=\"edit-panel\">"
		"<label>Medication Name<input id=\"control-med\" maxlength=\"31\" placeholder=\"Aspirin\"></label>"
		"<label>Pills in Slot<input id=\"control-total\" type=\"number\" min=\"1\" step=\"1\" value=\"20\"></label>"
		"<label>Pills per Dose<input id=\"control-dose\" type=\"number\" min=\"1\" step=\"1\" value=\"1\"></label>"
		"<div class=\"schedule-block\">"
		"<div class=\"schedule-label\">Daily Schedule</div>"
		"<div id=\"schedule-times\" class=\"schedule-times\"></div>"
		"<button id=\"add-time\" type=\"button\" class=\"btn-ghost\">+ Add another time</button>"
		"</div>"
		"</div>"
		"<div class=\"sync-row\">"
		"<input id=\"control-manual-time\" type=\"datetime-local\">"
		"<button id=\"sync-time\" type=\"button\" class=\"alt\">Sync Clock</button>"
		"</div>"
		"<div class=\"test-row\">"
		"<button id=\"test-success\" type=\"button\" class=\"btn-ghost\">Test success alert</button>"
		"<button id=\"test-fail\" type=\"button\" class=\"btn-ghost\">Test failure alert</button>"
		"<button id=\"simulate-dispense\" type=\"button\" class=\"btn-ghost\">Simulate dispense (test reminder)</button>"
		"</div>"
		"</div>"
		"</section>"
		"<section class=\"tab-panel\" data-panel=\"history\">"
		"<div class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Dispense Log</p><div>Most recent dispense events, newest first.</div></div></div>"
		"<table class=\"hist-table\"><thead><tr><th>#</th><th>Slot</th><th>Medication</th><th>Result</th><th>Pills After</th><th>Time</th></tr></thead>"
		"<tbody id=\"history-body\"><tr><td colspan=\"6\">No dispenses recorded yet. This fills in automatically once a dose is dispensed.</td></tr></tbody></table>"
		"</div>"
		"</section>"
		"<section class=\"tab-panel\" data-panel=\"notify\">"
		"<div class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Caretaker Notifications</p><div>Get a push alert if a dispensed dose isn't picked up in time.</div></div></div>"
		"<p class=\"about-copy\">Subscribe to this device's notification topic in the free <strong>ntfy</strong> app (iOS/Android), or open the link below in a browser and leave the tab open. Anyone subscribed, whether that's you, a family member, or a caretaker, gets alerted if a dose isn't confirmed as picked up.</p>"
		"<div class=\"ntfy-row\">"
		"<span class=\"ntfy-topic-chip\">Topic: <span id=\"ntfy-topic\">Loading...</span></span>"
		"<a id=\"ntfy-link\" href=\"#\" target=\"_blank\" rel=\"noopener\" class=\"ntfy-link\">Open subscribe link</a>"
		"</div>"
		"</div>"
		"</section>"
		"<section class=\"tab-panel\" data-panel=\"about\">"
		"<div class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">About This Project</p></div></div>"
		"<p class=\"about-copy\">We're Electronic Systems Engineering students, and PortaPill grew out of a problem we kept hearing about from patients and caregivers. Managing daily medication is harder than it should be. People forget a dose. Pillboxes are stiff and annoying to open every single day. Pill bottles are small and easy to lose track of or misplace. PortaPill is our attempt at a simple, reliable, and affordable answer to that: a device that dispenses the right pill at the right time, so that's one less thing to worry about each day.</p>"
		"<p class=\"about-signature\">Built by Alan Hosseinpour and Blaise Swan.</p>"
		"</div>"
		"<div class=\"footer\">"
		"<div class=\"footer-card\">Wi-Fi SSID: PortaPill</div>"
		"<div class=\"footer-card\">Portal URL: http://192.168.4.1</div>"
		"</div>"
		"</section>"
		"</div>"
		"</main>"
		"<div class=\"toast-wrap\"><div class=\"status-copy\" id=\"control-status\">Choose an action to begin.</div></div>"
		"<script>"
		"const displayNumber=v=>typeof v==='number'&&v>=0?String(v):'--';"
		"const control=(id)=>document.getElementById(id);"
		"const controlIds=['control-slot','control-med','control-total','control-dose','control-manual-time'];"
		"const DEFAULT_DISPENSE_TIME_MS='800';"
		"const scheduleTimesEl=()=>document.getElementById('schedule-times');"
		"const addTimeRow=(value)=>{const el=scheduleTimesEl();if(!el)return;const row=document.createElement('div');row.className='time-row';const input=document.createElement('input');input.type='time';if(value)input.value=value;const removeBtn=document.createElement('button');removeBtn.type='button';removeBtn.className='time-remove';removeBtn.setAttribute('aria-label','Remove this time');removeBtn.textContent='\\u00d7';removeBtn.addEventListener('click',()=>row.remove());row.appendChild(input);row.appendChild(removeBtn);el.appendChild(row);};"
		"const setScheduleFromString=str=>{const el=scheduleTimesEl();if(!el)return;el.innerHTML='';const tokens=(str&&str!=='none')?str.split(',').map(s=>s.trim()).filter(Boolean):[];if(tokens.length===0){addTimeRow();}else{tokens.forEach(t=>addTimeRow(t));}};"
		"const getScheduleValue=()=>{const inputs=document.querySelectorAll('#schedule-times input[type=time]');const values=Array.from(inputs).map(i=>i.value).filter(Boolean);return values.length?values.join(','):'none';};"
		"let latestSlots=[];"
		"let webEditActive=false;"
		"let webEditSlot='0';"
		"const text=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value;};"
		"const toLocalDateTimeValue=date=>{const pad=v=>String(v).padStart(2,'0');return date.getFullYear()+'-'+pad(date.getMonth()+1)+'-'+pad(date.getDate())+'T'+pad(date.getHours())+':'+pad(date.getMinutes());};"
		"const resultBadge=value=>{const v=value||'unknown';if(v==='ok')return{text:'Taken',cls:'badge-ok'};if(v==='fail')return{text:'Missed',cls:'badge-fail'};if(v==='timeout')return{text:'No response',cls:'badge-fail'};if(v==='pending')return{text:'Awaiting Pickup',cls:'badge-pending'};return{text:'No data yet',cls:'badge-muted'};};"
		"const resultCardClass=value=>{const v=value||'unknown';if(v==='ok')return'result-ok';if(v==='fail'||v==='timeout')return'result-fail';if(v==='pending')return'result-pending';return'';};"
		"const setActiveSlotCard=slot=>{for(let n=0;n<3;n+=1){const card=document.getElementById('slot-card-'+n);if(card)card.classList.toggle('active',n===slot);}};"
		"let toastTimer=null;"
		"const setStatusCopy=msg=>{const el=document.getElementById('control-status');if(!el)return;el.textContent=msg;el.classList.add('show');clearTimeout(toastTimer);toastTimer=setTimeout(()=>el.classList.remove('show'),3200);};"
		"const PLACEHOLDER_EVENTS=['Waiting for live data','ESP dashboard ready. Waiting for Pico 2 data link.'];"
		"const setSlotCard=slot=>{if(!slot||slot.slot==null)return;const hasData=Boolean(slot.has_data);const details=document.getElementById('slot-details-'+slot.slot);const emptyHint=document.getElementById('slot-empty-'+slot.slot);const card=document.getElementById('slot-card-'+slot.slot);if(details)details.style.display=hasData?'':'none';if(emptyHint)emptyHint.style.display=hasData?'none':'block';"
		"text('slot-medication-'+slot.slot,hasData&&slot.medication_name?slot.medication_name:'Not set up yet');"
		"if(card){card.classList.remove('result-ok','result-fail','result-pending');const rc=hasData?resultCardClass(slot.last_dispense_result):'';if(rc)card.classList.add(rc);}"
		"if(!hasData)return;"
		"text('slot-left-'+slot.slot,displayNumber(slot.pills_left));text('slot-dose-'+slot.slot,slot.pills_per_dose>0?slot.pills_per_dose+'x':'--');text('slot-schedule-'+slot.slot,slot.schedule&&slot.schedule!=='none'?slot.schedule:'None');const badge=resultBadge(slot.last_dispense_result);const el=document.getElementById('slot-result-'+slot.slot);if(el){el.textContent=badge.text;el.className='badge '+badge.cls;}"
		"const event=slot.last_event&&!PLACEHOLDER_EVENTS.includes(slot.last_event)?slot.last_event:'';const dispensed=slot.last_dispensed&&slot.last_dispensed!=='No confirmed dispense yet'?slot.last_dispensed:'';text('slot-last-'+slot.slot,event?(event+(dispensed?', '+dispensed:'')):'No previous dispense for this slot.');};"
		"const isEditingControls=()=>{const active=document.activeElement;if(!active)return false;if(controlIds.includes(active.id))return true;const panel=document.getElementById('edit-panel');return Boolean(panel&&panel.contains(active));};"
		"const getSlotByNumber=slotNumber=>latestSlots.find(slot=>slot&&slot.slot===slotNumber);"
		"const fillControlsFromSlot=slot=>{if(!slot)return;control('control-slot').value=String(slot.slot);control('control-med').value=slot.medication_name&&slot.medication_name!=='Waiting for data'?slot.medication_name:'';control('control-total').value=slot.total_pills>0?slot.total_pills:20;control('control-dose').value=slot.pills_per_dose>0?slot.pills_per_dose:1;setScheduleFromString(slot.schedule);};"
		"const postForm=async(url,data)=>{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw new Error(await r.text());return r.json();};"
		"const setEditPanel=(open)=>{const panel=document.getElementById('edit-panel');const startBtn=control('edit-start');const saveBtn=control('save-profile');if(panel)panel.style.display=open?'grid':'none';if(startBtn)startBtn.style.display=open?'none':'';if(saveBtn)saveBtn.style.display=open?'':'none';};"
		"const setWebEditMode=async(on)=>{const slot=control('control-slot').value;await postForm('/api/edit-mode',{slot,state:on?'on':'off'});webEditActive=on;webEditSlot=slot;};"
		"const saveProfile=async()=>{const data={slot:control('control-slot').value,med:control('control-med').value,total:control('control-total').value,dose:control('control-dose').value,time:DEFAULT_DISPENSE_TIME_MS,schedule:getScheduleValue()};await postForm('/api/profile',data);setStatusCopy('Slot settings saved.');await refreshStatus();if(webEditActive){await setWebEditMode(false);setEditPanel(false);}};"
		"const dispenseSelected=async()=>{await postForm('/api/dispense',{slot:control('control-slot').value});setStatusCopy('Dispense started. Waiting for confirmation...');await refreshStatus();};"
		"let manualTimeDirty=false;"
		"const syncTime=async()=>{let epoch;if(manualTimeDirty){const raw=control('control-manual-time').value;if(!raw)throw new Error('Pick a date/time first, or leave it alone to sync to right now.');epoch=Math.floor(new Date(raw+'Z').getTime()/1000);if(!Number.isFinite(epoch)||epoch<=0)throw new Error('That date/time is not valid.');}else{epoch=Math.floor(Date.now()/1000);}await postForm('/api/time-sync',{epoch:String(epoch)});manualTimeDirty=false;control('control-manual-time').value=toLocalDateTimeValue(new Date());setStatusCopy('Clock synced to '+new Date(epoch*1000).toLocaleString()+'.');};"
		"const testFeedback=async(result)=>{await postForm('/api/test-feedback',{result});setStatusCopy(result==='success'?'Success alert test sent.':'Failure alert test sent.');};"
		/* Keep this wording in sync with NOTIFY_REMINDER_DELAYS_MIN in notify.h. It's a
		 * display-only echo, not read from the firmware, since this is one string inside
		 * a C literal with no live link to that array. */
		"const simulateDispense=async()=>{const slot=control('control-slot').value;await postForm('/api/simulate-dispense',{slot});setStatusCopy('Simulated a dispense for the selected slot. If the drawer stays closed, reminder notifications fire at 5, 10, 15, and 20 minutes.');await refreshStatus();};"
		"const setBridgeState=connected=>{const pill=document.getElementById('bridge-pill');text('bridge-label',connected?'Device Ready':'Waiting for Device');text('bridge-copy',connected?'Your dispenser is connected and sending live updates.':'Dashboard is running. Waiting for the dispenser to connect.');if(pill)pill.className=connected?'status-pill online':'status-pill';text('controller-transport',connected?'Device is ready':'Waiting for connection');};"
		"async function refreshStatus(){"
		"try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('bad-response');const d=await r.json();"
		"text('device-time',d.device_time||'Unavailable');"
		"const formatNextDispense=str=>str?str.replace(/S(\\d+)/g,(_,n)=>'Slot '+(Number(n)+1)).replace('TMRW','tomorrow').replace(' FOR ',' \\u00b7 '):str;"
		"text('next-dispense',formatNextDispense(d.next_dispense)||'No schedule set');"
		"text('ntfy-topic',d.ntfy_topic||'unknown');"
		"{const ntfyLink=document.getElementById('ntfy-link');if(ntfyLink&&d.ntfy_subscribe_url)ntfyLink.href=d.ntfy_subscribe_url;}"
		"const slots=Array.isArray(d.slots)?d.slots:[];latestSlots=slots;slots.forEach(setSlotCard);"
		"if(!webEditActive&&!isEditingControls()){const selected=Number(control('control-slot').value);fillControlsFromSlot(getSlotByNumber(selected) || slots.find(slot=>slot&&slot.slot===Number(d.active_profile_slot)) || slots[0]);}"
		"setActiveSlotCard(Number(d.active_profile_slot)||0);"
		"if(d.awaiting_drawer_open){const waitStation=typeof d.drawer_open_slot==='number'&&d.drawer_open_slot>=0?d.drawer_open_slot+1:(Number(d.active_profile_slot)||0)+1;setStatusCopy('Dispense done. Waiting for the drawer to open on Slot '+waitStation+' to confirm pickup.');}else if(d.awaiting_dispense_ack){setStatusCopy('Waiting for dispenser confirmation...');}"
		"setBridgeState(Boolean(d.bridge_connected));"
		"const failBanner=document.getElementById('fail-banner');if(failBanner)failBanner.className='alert-banner'+(d.dispense_fail?' visible':'');"
		"const lowBanner=document.getElementById('low-pill-banner');if(lowBanner)lowBanner.className='warn-banner'+(d.low_pill_warn?' visible':'');"
		"const history=Array.isArray(d.history)?d.history:[];"
		"const histBody=document.getElementById('history-body');if(histBody&&history.length>0){histBody.innerHTML=history.map((h,i)=>{const t=h.time>0?new Date(h.time*1000).toLocaleTimeString():'--';const res=h.result==='ok'?'Taken':h.result==='fail'?'Missed':h.result==='timeout'?'No response':h.result||'--';return '<tr><td>'+(i+1)+'</td><td>'+(h.slot+1)+'</td><td>'+(h.medication||'--')+'</td><td>'+res+'</td><td>'+displayNumber(h.pills_left_after)+'</td><td>'+t+'</td></tr>';}).join('');}"
		"}catch(e){text('device-time','Disconnected');text('next-dispense','--');setBridgeState(false);}"
		"}"
		"document.querySelectorAll('.tab-btn').forEach(btn=>{btn.addEventListener('click',()=>{document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));document.querySelectorAll('.tab-panel').forEach(p=>p.classList.remove('active'));btn.classList.add('active');const panel=document.querySelector('.tab-panel[data-panel=\"'+btn.dataset.tab+'\"]');if(panel)panel.classList.add('active');});});"
		"control('edit-start').addEventListener('click',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));setWebEditMode(true).then(()=>{setEditPanel(true);setStatusCopy('Edit mode on for selected slot.');}).catch(e=>setStatusCopy('Could not start edit mode: '+(e.message||'unknown error')));});"
		"control('add-time').addEventListener('click',()=>{addTimeRow();});"
		"control('save-profile').addEventListener('click',()=>{saveProfile().catch(e=>setStatusCopy('Could not save slot settings: '+(e.message||'unknown error')));});"
		"control('dispense-slot').addEventListener('click',()=>{dispenseSelected().catch(e=>setStatusCopy('Could not start dispense: '+(e.message||'unknown error')));});"
		"control('control-manual-time').addEventListener('input',()=>{manualTimeDirty=true;});"
		"control('sync-time').addEventListener('click',()=>{syncTime().catch(e=>setStatusCopy(e.message||'Could not sync the clock.'));});"
		"control('test-success').addEventListener('click',()=>{testFeedback('success').catch(e=>setStatusCopy('Could not run success alert test: '+(e.message||'unknown error')));});"
		"control('test-fail').addEventListener('click',()=>{testFeedback('fail').catch(e=>setStatusCopy('Could not run failure alert test: '+(e.message||'unknown error')));});"
		"control('simulate-dispense').addEventListener('click',()=>{simulateDispense().catch(e=>setStatusCopy('Could not simulate dispense: '+(e.message||'unknown error')));});"
		"if(!control('control-manual-time').value){control('control-manual-time').value=toLocalDateTimeValue(new Date());}"
		"control('control-slot').addEventListener('change',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));if(webEditActive){setWebEditMode(true).catch(()=>{});}});"
		/* Best-effort: if the page closes or refreshes while edit mode is on,
		 * tell the ESP to clear the edit LED so it doesn't stay lit forever
		 * waiting for a Save click that's never coming. */
		"window.addEventListener('beforeunload',()=>{if(webEditActive){const body=new URLSearchParams({slot:webEditSlot||control('control-slot').value,state:'off'}).toString();navigator.sendBeacon('/api/edit-mode',new Blob([body],{type:'application/x-www-form-urlencoded'}));}});"
		"setEditPanel(false);"
		"refreshStatus();setInterval(refreshStatus,1500);"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

/* Running count of currently-open HTTP sockets, purely for the [NET] log
 * lines below, not used for any control-flow decisions. */
static volatile int s_open_socket_count = 0;

/* Handle to the running server, kept so web_server_close_sockets_for_ip()
 * (called from wifi_ap.c on an abrupt Wi-Fi disconnect) can reach it from
 * outside start_webserver(). NULL until the server has actually started. */
static httpd_handle_t s_server = NULL;

/* Tracks which IP address each currently-open socket belongs to, so a
 * device that drops off Wi-Fi abruptly (see web_server_close_sockets_for_ip)
 * can have its lingering socket(s) found and closed by IP, since the AP
 * disconnect event only gives a MAC/IP, not a socket fd. Sized above
 * max_open_sockets so it can never fill up first. */
#define MAX_TRACKED_SOCKETS 12
typedef struct {
	int fd; /* -1 = empty slot */
	char ip[48];
} tracked_socket_t;
static tracked_socket_t s_tracked_sockets[MAX_TRACKED_SOCKETS];

/* Called by esp_http_server the instant it accepts a new TCP connection,
 * before any request has actually been read. Logging here, plus the
 * matching close_fn below, gives a real-time trace of every connection's
 * full lifetime and the client IP that opened it, specifically so
 * connection/socket-exhaustion issues (like the ENFILE case this was added
 * for) can be diagnosed after the fact from /api/debug-log. */
static esp_err_t http_socket_open_cb(httpd_handle_t hd, int sockfd)
{
	struct sockaddr_in6 addr;
	socklen_t addr_len = sizeof(addr);
	char ip_str[48] = "unknown";

	(void)hd;

	if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_len) == 0) {
		if (addr.sin6_family == AF_INET) {
			inet_ntop(AF_INET, &((struct sockaddr_in *)&addr)->sin_addr, ip_str, sizeof(ip_str));
		} else {
			inet_ntop(AF_INET6, &addr.sin6_addr, ip_str, sizeof(ip_str));
		}
	}

	for (int i = 0; i < MAX_TRACKED_SOCKETS; i++) {
		if (s_tracked_sockets[i].fd == -1) {
			s_tracked_sockets[i].fd = sockfd;
			snprintf(s_tracked_sockets[i].ip, sizeof(s_tracked_sockets[i].ip), "%s", ip_str);
			break;
		}
	}

	s_open_socket_count++;
	ESP_LOGI(TAG, "[NET] HTTP socket OPEN  fd=%d from %s (now %d open, free heap=%lu bytes)",
		 sockfd, ip_str, s_open_socket_count, (unsigned long)esp_get_free_heap_size());
	return ESP_OK;
}

/* Mirrors http_socket_open_cb() above, called the instant a connection
 * closes for any reason (client disconnect, timeout, handler returning an
 * error, LRU eviction, or web_server_close_sockets_for_ip() below). */
static void http_socket_close_cb(httpd_handle_t hd, int sockfd)
{
	(void)hd;

	for (int i = 0; i < MAX_TRACKED_SOCKETS; i++) {
		if (s_tracked_sockets[i].fd == sockfd) {
			s_tracked_sockets[i].fd = -1;
			s_tracked_sockets[i].ip[0] = '\0';
			break;
		}
	}

	s_open_socket_count--;
	ESP_LOGI(TAG, "[NET] HTTP socket CLOSE fd=%d (now %d open, free heap=%lu bytes)",
		 sockfd, s_open_socket_count, (unsigned long)esp_get_free_heap_size());
	close(sockfd);
}

void web_server_close_sockets_for_ip(const char *ip_str)
{
	size_t ip_len;
	int closed_count = 0;

	if (s_server == NULL || ip_str == NULL) {
		return;
	}

	ip_len = strlen(ip_str);

	for (int i = 0; i < MAX_TRACKED_SOCKETS; i++) {
		int fd = s_tracked_sockets[i].fd;
		size_t stored_len;

		if (fd == -1) {
			continue;
		}

		/* Stored IPs are often in IPv4-mapped IPv6 form, "::FFFF:192.168.4.2",
		 * since this is a dual-stack socket, while callers pass a plain
		 * dotted-quad. A suffix match handles both without needing to parse
		 * or normalize either side. */
		stored_len = strlen(s_tracked_sockets[i].ip);
		if (stored_len >= ip_len &&
		    strcmp(s_tracked_sockets[i].ip + (stored_len - ip_len), ip_str) == 0) {
			ESP_LOGI(TAG, "[NET] Closing lingering HTTP socket fd=%d for %s (device left Wi-Fi abruptly)",
				 fd, ip_str);
			httpd_sess_trigger_close(s_server, fd);
			closed_count++;
		}
	}

	if (closed_count == 0) {
		ESP_LOGD(TAG, "[NET] No lingering HTTP sockets found for %s", ip_str);
	}
}

/* ----------------------------------------------------------------------------
 * start_webserver()
 * ----------------------------------------------------------------------------
 * Configures and starts the HTTP server, then registers every route: the
 * dashboard and its assets, the JSON API, and the captive-portal probe
 * URLs used by Android/Apple/Windows to detect this is a login-required
 * network. Call once at boot.
 * ---------------------------------------------------------------------------- */
void start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;

	for (int i = 0; i < MAX_TRACKED_SOCKETS; i++) {
		s_tracked_sockets[i].fd = -1;
	}

	config.max_uri_handlers = 20; /* currently 19 registered below; leaves headroom for new routes */
	config.stack_size = 10240;
	config.uri_match_fn = httpd_uri_match_wildcard;
	/* Total lwIP socket budget is 16 (CONFIG_LWIP_MAX_SOCKETS, raised from
	 * the original 10). Running AP+STA together, plus mDNS, SNTP, DHCP
	 * housekeeping, and the captive DNS responder, consumes more of that
	 * budget in the background than it looks like on paper, real testing
	 * showed the system running out of sockets entirely (accept() failing
	 * with ENFILE) within seconds of a second device joining, when the web
	 * server alone was given 7 of a 10-socket total. Raising the total pool
	 * instead of just reshuffling a too-small one leaves real headroom for
	 * both the web server and everything running alongside it. */
	config.max_open_sockets = 10;
	/* Without this, once every socket is occupied, even by an idle/stale
	 * connection a browser or phone left open without properly closing it,
	 * the server flatly refuses every new connection instead of reclaiming
	 * the least-recently-used one. That's the real cause of "worked once,
	 * then nothing connects at all until the ESP is rebooted": sockets pile
	 * up as idle over repeated visits and are never freed. This makes the
	 * server evict the oldest idle connection to make room for a new one
	 * instead of just rejecting it. */
	config.lru_purge_enable = true;
	config.open_fn = http_socket_open_cb;
	config.close_fn = http_socket_close_cb;

	if (httpd_start(&server, &config) == ESP_OK) {
		s_server = server;
		httpd_uri_t root = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = root_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t logo = {
			.uri = "/logo.png",
			.method = HTTP_GET,
			.handler = logo_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t status = {
			.uri = "/api/status",
			.method = HTTP_GET,
			.handler = status_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t debug_log = {
			.uri = "/api/debug-log",
			.method = HTTP_GET,
			.handler = debug_log_get_handler,
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
		httpd_uri_t simulate_dispense = {
			.uri = "/api/simulate-dispense",
			.method = HTTP_POST,
			.handler = simulate_dispense_post_handler,
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
		httpd_register_uri_handler(server, &logo);
		httpd_register_uri_handler(server, &status);
		httpd_register_uri_handler(server, &debug_log);
		httpd_register_uri_handler(server, &profile);
		httpd_register_uri_handler(server, &dispense);
		httpd_register_uri_handler(server, &time_sync);
		httpd_register_uri_handler(server, &feedback_test);
		httpd_register_uri_handler(server, &edit_mode);
		httpd_register_uri_handler(server, &simulate_dispense);
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
