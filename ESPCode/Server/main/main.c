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

/* This file only wires the subsystems together. Each subsystem lives in its
 * own module:
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
 */

void app_main(void)
{
	esp_err_t ret;

	debug_log_init(); /* first, so nothing that follows gets missed */

	ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
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
