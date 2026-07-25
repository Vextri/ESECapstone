#include "wifi_ap.h"

#include <stdbool.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "time_server";

#define AP_SSID "ESP-Time-Server"
#define AP_PASS "123456789"
#define AP_MAX_CONN 4

static bool ap_uses_password(void)
{
	return strlen(AP_PASS) >= 8;
}

void start_wifi_ap(void)
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
