/* PortaPill Caretaker Notification System. See notify.h for an overview. */

#include "notify.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "bridge_state.h"
#include "ntfy_credentials.h"
#include "wifi_ap.h"

static const char *TAG = "time_server";

/* One one-shot timer per slot, created lazily on first use. A slot's timer
 * is simply restarted (not queued) if a new dispense happens before the
 * previous reminder fired, see notify_schedule_dispense_reminder(). */
static esp_timer_handle_t s_reminder_timer[PILL_SLOT_COUNT];
static char s_pending_medication[PILL_SLOT_COUNT][32];
static time_t s_pending_dispensed_at[PILL_SLOT_COUNT];

static esp_err_t notify_http_event_handler(esp_http_client_event_t *evt)
{
	switch (evt->event_id) {
	case HTTP_EVENT_ERROR:
		ESP_LOGW(TAG, "ntfy: HTTP error");
		break;
	case HTTP_EVENT_DISCONNECTED:
		ESP_LOGI(TAG, "ntfy: HTTP disconnected");
		break;
	default:
		break;
	}
	return ESP_OK;
}

static void notify_send(const char *title, const char *body)
{
	char url[96];
	esp_http_client_handle_t client;
	esp_http_client_config_t config = {
		.url = url,
		.method = HTTP_METHOD_POST,
		.crt_bundle_attach = esp_crt_bundle_attach,
		.event_handler = notify_http_event_handler,
		.timeout_ms = 10000,
	};

	if (!wifi_sta_is_connected()) {
		ESP_LOGW(TAG, "ntfy: no internet connection, skipping notification");
		return;
	}

	snprintf(url, sizeof(url), "https://ntfy.sh/%s", NTFY_TOPIC);

	client = esp_http_client_init(&config);
	if (client == NULL) {
		ESP_LOGE(TAG, "ntfy: failed to init HTTP client");
		return;
	}

	esp_http_client_set_header(client, "Title", title);
	esp_http_client_set_header(client, "Content-Type", "text/plain");
	esp_http_client_set_post_field(client, body, (int)strlen(body));

	esp_err_t err = esp_http_client_perform(client);
	if (err == ESP_OK) {
		ESP_LOGI(TAG, "ntfy: notification sent, HTTP status %d",
			 esp_http_client_get_status_code(client));
	} else {
		ESP_LOGW(TAG, "ntfy: POST failed: %s", esp_err_to_name(err));
	}

	esp_http_client_cleanup(client);
}

static void reminder_timer_callback(void *arg)
{
	int slot = (int)(intptr_t)arg;
	struct tm dispensed_tm;
	char time_str[16];
	char body[192];
	bool still_awaiting_pickup = false;

	/* The drawer sensor may have already confirmed this pill was picked up
	 * (bridge_mark_dispense_taken_locked() clears awaiting_drawer_open) any
	 * time between when this timer was armed and now. Check the live state
	 * right before sending rather than trusting a stale assumption, so a
	 * confirmed pickup never gets a "did you take this?" reminder. */
	if (bridge_state_mutex != NULL && xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
		still_awaiting_pickup = bridge_state.awaiting_drawer_open && bridge_state.drawer_open_slot == slot;
		xSemaphoreGive(bridge_state_mutex);
	}

	if (!still_awaiting_pickup) {
		ESP_LOGI(TAG, "Slot %d already confirmed picked up, skipping reminder", slot);
		return;
	}

	localtime_r(&s_pending_dispensed_at[slot], &dispensed_tm);
	strftime(time_str, sizeof(time_str), "%I:%M %p", &dispensed_tm);

	snprintf(body, sizeof(body),
		 "%s was dispensed from Container %d at %s and the drawer hasn't been opened yet. "
		 "Please check on this.",
		 s_pending_medication[slot], slot + 1, time_str);

	notify_send("PortaPill: Pickup Not Confirmed", body);
}

void notify_send_test(void)
{
	notify_send("PortaPill Test", "This is a test notification from your pill dispenser. If you can read this, push notifications are working.");
}

const char *notify_get_topic(void)
{
	return NTFY_TOPIC;
}

const char *notify_get_subscribe_url(void)
{
	static char url[96] = {0};

	if (url[0] == '\0') {
		snprintf(url, sizeof(url), "https://ntfy.sh/%s", NTFY_TOPIC);
	}

	return url;
}

void notify_init(void)
{
	for (int slot = 0; slot < PILL_SLOT_COUNT; slot++) {
		esp_timer_create_args_t timer_args = {
			.callback = &reminder_timer_callback,
			.arg = (void *)(intptr_t)slot,
			.name = "dispense_reminder",
		};

		if (esp_timer_create(&timer_args, &s_reminder_timer[slot]) != ESP_OK) {
			ESP_LOGE(TAG, "Failed to create reminder timer for slot %d", slot);
		}
	}

	ESP_LOGI(TAG, "Dispense reminders ready (%d min after a successful dispense)",
		 NOTIFY_REMINDER_DELAY_MIN);
}

void notify_schedule_dispense_reminder(int slot, const char *medication_name, time_t dispensed_at)
{
	if (slot < 0 || slot >= PILL_SLOT_COUNT || s_reminder_timer[slot] == NULL) {
		return;
	}

	/* Restart rather than queue, if this slot dispenses again before the
	 * previous reminder fires, only the newest one should still be pending. */
	esp_timer_stop(s_reminder_timer[slot]); /* no-op (returns an error, ignored) if not running */

	snprintf(s_pending_medication[slot], sizeof(s_pending_medication[slot]), "%s",
		 medication_name != NULL ? medication_name : "your medication");
	s_pending_dispensed_at[slot] = dispensed_at;

	esp_timer_start_once(s_reminder_timer[slot],
			      (uint64_t)NOTIFY_REMINDER_DELAY_MIN * 60ULL * 1000000ULL);
}
