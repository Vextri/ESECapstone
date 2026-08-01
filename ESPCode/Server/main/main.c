/* ============================================================================
 * MAIN.C - System Entry Point
 * ----------------------------------------------------------------------------
 * Top-level wiring for the ESP32-S3 firmware. This file does not contain
 * any application logic of its own, it only brings each subsystem online
 * in an order that respects their dependencies. All real behavior lives in
 * the individual modules listed below:
 *
 *   audio_feedback  - I2S tone playback for success/failure/edit events
 *   led_feedback    - WS2812 status LEDs
 *   lcd_display     - ST7796 LCD UI + physical 5-way button pad
 *   drawer_sensor   - hall-effect drawer-open detection (dispense pickup confirmation)
 *   uart_bridge     - line protocol to/from the Pico 2 controller
 *   bridge_state    - shared slot/status state + NVS persistence
 *   time_utils      - schedule parsing and clock helpers
 *   notify          - ntfy.sh push notifications (dispense reminders)
 *   wifi_ap         - Wi-Fi access point + home Wi-Fi/NTP client
 *   captive_dns     - captive-portal DNS responder
 *   web_server      - HTTP dashboard + JSON API
 *   debug_log       - in-RAM log capture, readable over WiFi at /api/debug-log
 * ============================================================================ */

#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "audio_feedback.h"
#include "captive_dns.h"
#include "debug_log.h"
#include "drawer_sensor.h"
#include "lcd_display.h"
#include "led_feedback.h"
#include "notify.h"
#include "time_utils.h"
#include "uart_bridge.h"
#include "web_server.h"
#include "wifi_ap.h"

/* ----------------------------------------------------------------------------
 * app_main()
 * ----------------------------------------------------------------------------
 * Entry point called once by the ESP-IDF startup code. Initializes NVS
 * (flash-backed key/value storage, needed by Wi-Fi and by our own saved
 * profiles), then starts every subsystem task in a fixed order:
 *
 *   1. debug_log first, so no later boot message is missed.
 *   2. time_utils / notify - no hardware dependency, safe to init early.
 *   3. audio / led / lcd - user-facing feedback, independent of networking.
 *   4. uart_bridge - creates bridge_state_mutex, must exist before
 *      drawer_sensor starts, since drawer_sensor reads/writes shared state
 *      under that same lock.
 *   5. wifi_ap / captive_dns / web_server - networking stack, started last
 *      since everything above it can run without a network connection.
 * ---------------------------------------------------------------------------- */
void app_main(void)
{
	esp_err_t ret;

	debug_log_init(); /* first, so nothing that follows gets missed */

	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		/* Flash layout changed since the last flash (or this is a first
		 * boot on unformatted flash) - wipe and reinitialize NVS. */
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	time_utils_init();
	notify_init();

	start_audio_feedback();
	start_led_feedback();
	start_lcd_display();
	start_uart_bridge();
	start_drawer_sensor(); /* after start_uart_bridge() so bridge_state_mutex already exists */
	start_wifi_ap();
	start_captive_dns();
	start_webserver();
}
