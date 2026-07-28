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
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bridge_state.h"
#include "ntfy_credentials.h"
#include "wifi_ap.h"

static const char *TAG = "time_server";

/* NOTIFY_REMINDER_STAGE_COUNT one-shot timers per slot, one per escalation
 * stage, all created up front at boot. A slot's whole set is simply
 * restarted (not queued) if a new dispense happens before the previous
 * reminders fired, see notify_schedule_dispense_reminder(). */
static esp_timer_handle_t s_reminder_timer[PILL_SLOT_COUNT][NOTIFY_REMINDER_STAGE_COUNT];
static char s_pending_medication[PILL_SLOT_COUNT][32];
static time_t s_pending_dispensed_at[PILL_SLOT_COUNT];
static const int s_reminder_delay_min[NOTIFY_REMINDER_STAGE_COUNT] = NOTIFY_REMINDER_DELAYS_MIN;

/* notify_send() only ever enqueues here, the actual blocking HTTPS POST to
 * ntfy.sh happens on notify_task(), off of whatever caller's stack/lock it
 * was invoked from. Several call sites (a STATUS line updating a pill count,
 * a DISPENSE ack) run while bridge_state_mutex is held, and that mutex is
 * also needed by the dashboard, the LCD, and UART line processing, so a
 * multi-second network call must never happen inline there, it would freeze
 * the whole device for as long as the HTTP request takes. */
typedef struct {
	char title[48];
	char body[192];
} notify_msg_t;

static QueueHandle_t s_notify_queue;

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

/* The actual blocking HTTPS POST to ntfy.sh. Only ever called from
 * notify_task(), never directly, see the notify_msg_t comment above. */
static void notify_send_blocking(const char *title, const char *body)
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

static void notify_task(void *arg)
{
	notify_msg_t msg;

	(void)arg;

	while (1) {
		if (xQueueReceive(s_notify_queue, &msg, portMAX_DELAY) == pdTRUE) {
			notify_send_blocking(msg.title, msg.body);
		}
	}
}

/* Enqueues a notification for notify_task() to actually send. Safe to call
 * from anywhere, including while holding bridge_state_mutex, it never
 * blocks (drops and logs a warning if the queue is momentarily full rather
 * than waiting). */
static void notify_send(const char *title, const char *body)
{
	notify_msg_t msg;

	if (s_notify_queue == NULL) {
		return;
	}

	snprintf(msg.title, sizeof(msg.title), "%s", title != NULL ? title : "");
	snprintf(msg.body, sizeof(msg.body), "%s", body != NULL ? body : "");

	if (xQueueSend(s_notify_queue, &msg, 0) != pdTRUE) {
		ESP_LOGW(TAG, "notify queue full, dropping notification: %s", title);
	}
}

static void reminder_timer_callback(void *arg)
{
	int packed = (int)(intptr_t)arg;
	int slot = packed / NOTIFY_REMINDER_STAGE_COUNT;
	int stage = packed % NOTIFY_REMINDER_STAGE_COUNT;
	struct tm dispensed_tm;
	char time_str[16];
	char body[192];
	bool still_awaiting_pickup = false;

	/* The drawer sensor may have already confirmed this pill was picked up
	 * (bridge_mark_dispense_taken_locked() sets this slot's result to "ok")
	 * any time between when this timer was armed and now. Check the live
	 * per-slot state right before sending rather than trusting a stale
	 * assumption, so a confirmed pickup never gets a "did you take this?"
	 * reminder. Deliberately checks this slot's own last_dispense_result
	 * rather than bridge_state.awaiting_drawer_open/drawer_open_slot, which
	 * only ever remember the single most-recently-dispensed slot, if a
	 * second station dispensed after this one and before this timer fired,
	 * those shared fields would already point at the other slot and this
	 * reminder would be wrongly skipped even though this pill was never
	 * picked up. */
	if (bridge_state_mutex != NULL && xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
		still_awaiting_pickup = strcmp(bridge_state.slots[slot].last_dispense_result, "pending") == 0;
		xSemaphoreGive(bridge_state_mutex);
	}

	if (!still_awaiting_pickup) {
		ESP_LOGI(TAG, "Slot %d already confirmed picked up, skipping stage %d reminder", slot, stage);
		return;
	}

	localtime_r(&s_pending_dispensed_at[slot], &dispensed_tm);
	strftime(time_str, sizeof(time_str), "%I:%M %p", &dispensed_tm);

	if (stage == 0) {
		snprintf(body, sizeof(body),
			 "%s was dispensed from Slot %d at %s and the drawer hasn't been opened yet. "
			 "Please check on this.",
			 s_pending_medication[slot], slot + 1, time_str);
	} else if (stage < NOTIFY_REMINDER_STAGE_COUNT - 1) {
		snprintf(body, sizeof(body),
			 "%s from Slot %d, dispensed at %s, still hasn't been picked up after %d minutes. "
			 "Please check on this soon.",
			 s_pending_medication[slot], slot + 1, time_str, s_reminder_delay_min[stage]);
	} else {
		snprintf(body, sizeof(body),
			 "%s from Slot %d, dispensed at %s, has NOT been picked up after %d minutes. "
			 "Please check on this as soon as possible.",
			 s_pending_medication[slot], slot + 1, time_str, s_reminder_delay_min[stage]);
	}

	notify_send("PortaPill: Pickup Not Confirmed", body);
}

void notify_send_test(void)
{
	notify_send("PortaPill Test", "This is a test notification from your pill dispenser. If you can read this, push notifications are working.");
}

void notify_check_pill_level(int slot, const char *medication_name, int old_left, int new_left)
{
	char body[160];
	const char *name = (medication_name != NULL && medication_name[0]) ? medication_name : "Medication";

	if (slot < 0 || slot >= PILL_SLOT_COUNT) {
		return;
	}

	if (old_left > 0 && new_left == 0) {
		snprintf(body, sizeof(body),
			 "%s in Slot %d has run out. Please refill this slot.",
			 name, slot + 1);
		notify_send("PortaPill: Out of Pills", body);
	} else if (old_left > 1 && new_left == 1) {
		snprintf(body, sizeof(body),
			 "%s in Slot %d is down to its last pill. Please refill soon.",
			 name, slot + 1);
		notify_send("PortaPill: Low on Pills", body);
	}
}

void notify_send_dispensed(int slot, const char *medication_name)
{
	char body[128];
	const char *name = (medication_name != NULL && medication_name[0]) ? medication_name : "A dose";

	if (slot < 0 || slot >= PILL_SLOT_COUNT) {
		return;
	}

	snprintf(body, sizeof(body), "%s was dispensed from Slot %d as scheduled.", name, slot + 1);
	notify_send("PortaPill: Dose Dispensed", body);
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
		for (int stage = 0; stage < NOTIFY_REMINDER_STAGE_COUNT; stage++) {
			esp_timer_create_args_t timer_args = {
				.callback = &reminder_timer_callback,
				.arg = (void *)(intptr_t)(slot * NOTIFY_REMINDER_STAGE_COUNT + stage),
				.name = "dispense_reminder",
			};

			if (esp_timer_create(&timer_args, &s_reminder_timer[slot][stage]) != ESP_OK) {
				ESP_LOGE(TAG, "Failed to create reminder timer for slot %d stage %d", slot, stage);
			}
		}
	}

	s_notify_queue = xQueueCreate(4, sizeof(notify_msg_t));
	if (s_notify_queue == NULL) {
		ESP_LOGE(TAG, "Failed to create notify queue");
	} else if (xTaskCreate(notify_task, "notify_task", 8192, NULL, 3, NULL) != pdPASS) {
		ESP_LOGE(TAG, "Failed to create notify task");
	}

	ESP_LOGI(TAG, "Dispense reminders ready (%d escalating stages, first at %d min after a successful dispense)",
		 NOTIFY_REMINDER_STAGE_COUNT, s_reminder_delay_min[0]);
}

void notify_schedule_dispense_reminder(int slot, const char *medication_name, time_t dispensed_at)
{
	if (slot < 0 || slot >= PILL_SLOT_COUNT) {
		return;
	}

	snprintf(s_pending_medication[slot], sizeof(s_pending_medication[slot]), "%s",
		 medication_name != NULL ? medication_name : "your medication");
	s_pending_dispensed_at[slot] = dispensed_at;

	for (int stage = 0; stage < NOTIFY_REMINDER_STAGE_COUNT; stage++) {
		if (s_reminder_timer[slot][stage] == NULL) {
			continue;
		}

		/* Restart rather than queue, if this slot dispenses again before
		 * the previous reminders fire, only the newest dispense's
		 * reminders should still be pending. */
		esp_timer_stop(s_reminder_timer[slot][stage]); /* no-op (returns an error, ignored) if not running */
		esp_timer_start_once(s_reminder_timer[slot][stage],
				      (uint64_t)s_reminder_delay_min[stage] * 60ULL * 1000000ULL);
	}
}
