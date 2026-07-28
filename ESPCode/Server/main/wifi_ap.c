#include "wifi_ap.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "uart_bridge.h"
#include "wifi_credentials.h"

static const char *TAG = "time_server";

#define AP_SSID "PortaPill"
#define AP_PASS "123456789"
#define AP_MAX_CONN 4

static bool ap_uses_password(void)
{
	return strlen(AP_PASS) >= 8;
}

/* Fires whenever SNTP (re)synchronizes the system clock, the first sync
 * after boot, and any periodic re-sync afterward. Relays the corrected time
 * to the Pico so its software RTC never drifts far from real time, exactly
 * like a manual "Sync Clock" click from the dashboard would. */
static void on_sntp_time_synced(struct timeval *tv)
{
	(void)tv;
	ESP_LOGI(TAG, "SNTP time sync received, relaying to Pico");
	bridge_send_set_time();
}

/* Index into HOME_WIFI_CANDIDATES of the network currently being tried.
 * Advances (with wraparound) on every failed connection attempt, so the ESP
 * cycles through the whole list, e.g. home Wi-Fi, then a phone hotspot,
 * until one of them actually connects. */
static size_t s_sta_candidate_idx = 0;
static volatile bool s_sta_connected = false;

bool wifi_sta_is_connected(void)
{
	return s_sta_connected;
}

static void wifi_sta_try_candidate(size_t idx)
{
	const wifi_credential_t *candidate = &HOME_WIFI_CANDIDATES[idx];
	wifi_config_t sta_config = {
		.sta = {
			.threshold.authmode = WIFI_AUTH_WPA2_PSK,
		},
	};

	strncpy((char *)sta_config.sta.ssid, candidate->ssid, sizeof(sta_config.sta.ssid) - 1);
	strncpy((char *)sta_config.sta.password, candidate->password, sizeof(sta_config.sta.password) - 1);

	ESP_LOGI(TAG, "Connecting to Wi-Fi \"%s\" for time sync (network %u of %u)...",
		 candidate->ssid, (unsigned)(idx + 1), (unsigned)HOME_WIFI_CANDIDATE_COUNT);
	esp_wifi_set_config(WIFI_IF_STA, &sta_config);
	esp_wifi_connect();
}

static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base,
				    int32_t event_id, void *event_data)
{
	(void)arg;

	if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
		s_sta_candidate_idx = 0;
		wifi_sta_try_candidate(s_sta_candidate_idx);
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
		s_sta_connected = false;
		s_sta_candidate_idx = (s_sta_candidate_idx + 1) % HOME_WIFI_CANDIDATE_COUNT;
		ESP_LOGW(TAG, "Wi-Fi link lost or unreachable, trying next network in the list...");
		wifi_sta_try_candidate(s_sta_candidate_idx);
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		s_sta_connected = true;
		ESP_LOGI(TAG, "Joined \"%s\", got IP: " IPSTR,
			 HOME_WIFI_CANDIDATES[s_sta_candidate_idx].ssid, IP2STR(&event->ip_info.ip));
		ESP_LOGI(TAG, "Dashboard also reachable on this network at http://" IPSTR "/ or http://portapill.local/",
			 IP2STR(&event->ip_info.ip));
	}
}

void start_wifi_ap(void)
{
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	bool use_password = ap_uses_password();
	/* Home-network join (for NTP) only turns on once wifi_credentials.h has
	 * been filled in, until then this silently stays AP-only, identical
	 * to the original behavior. */
	bool sta_enabled = (HOME_WIFI_CANDIDATE_COUNT > 0 &&
			     strcmp(HOME_WIFI_CANDIDATES[0].ssid, "YOUR_WIFI_NAME") != 0);
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

	if (sta_enabled) {
		esp_netif_create_default_wifi_sta();
	} else {
		ESP_LOGW(TAG,
				 "wifi_credentials.h still has placeholder values, skipping home "
				 "Wi-Fi join and automatic NTP time sync. Edit main/wifi_credentials.h "
				 "with your real network and rebuild to enable it.");
	}

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));

	if (sta_enabled) {
		ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
								      &wifi_sta_event_handler, NULL, NULL));
		ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
								      &wifi_sta_event_handler, NULL, NULL));
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
		/* STA config (which candidate SSID/password to try) is set
		 * dynamically in wifi_sta_try_candidate(), triggered by the
		 * WIFI_EVENT_STA_START handler once esp_wifi_start() runs below. */
	} else {
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	}
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	esp_wifi_set_max_tx_power(84); /* 84 = 21 dBm, maximum */

	if (sta_enabled) {
		esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
		sntp_config.sync_cb = on_sntp_time_synced;
		ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp_config));
	}

	/* mDNS lets the dashboard be reached at a fixed, memorable address,
	 * http://portapill.local/, on whichever network the ESP is on,
	 * instead of hunting down a DHCP-assigned IP that can change between
	 * boots. Works over both the AP and the home Wi-Fi (STA) connection. */
	if (mdns_init() == ESP_OK) {
		mdns_hostname_set("portapill");
		mdns_instance_name_set("PortaPill Dispenser");
		ESP_LOGI(TAG, "mDNS ready, dashboard also reachable at http://portapill.local/");
	} else {
		ESP_LOGW(TAG, "mDNS init failed, dashboard still reachable by IP address");
	}

	ESP_LOGI(TAG, "Wi-Fi AP started. SSID: %s, Password: %s", AP_SSID, use_password ? AP_PASS : "<open>");
	ESP_LOGI(TAG, "Open portal: http://192.168.4.1/");
}
