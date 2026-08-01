/* ============================================================================
 * WIFI_AP.C - Wireless Networking Implementation
 * ----------------------------------------------------------------------------
 * Configures and starts the Wi-Fi radio in AP+STA mode, handles every
 * Wi-Fi/IP event that matters to the rest of the firmware, and owns mDNS.
 * See wifi_ap.h for the module overview.
 * ============================================================================ */

#include "wifi_ap.h"

#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "mdns.h"

#include "uart_bridge.h"
#include "web_server.h"
#include "wifi_credentials.h"

static const char *TAG = "time_server";

#define AP_SSID "PortaPill"
#define AP_PASS "123456789"
#define AP_MAX_CONN 4

/* True if AP_PASS is long enough for WPA/WPA2 (8+ characters); if not, the
 * AP falls back to an open (unencrypted) network rather than failing to
 * start. */
static bool ap_uses_password(void)
{
	return strlen(AP_PASS) >= 8;
}

/* Remembers which IP each currently-connected AP client was assigned, keyed
 * by MAC, so that when WIFI_EVENT_AP_STADISCONNECTED fires (which only gives
 * a MAC, not an IP) the matching IP can be looked up and passed to
 * web_server_close_sockets_for_ip() to clean up any socket that device left
 * open by dropping off Wi-Fi abruptly instead of closing its browser tab. */
typedef struct {
	uint8_t mac[6];
	esp_ip4_addr_t ip;
	bool valid;
} ap_client_entry_t;
static ap_client_entry_t s_ap_clients[AP_MAX_CONN];

static void ap_client_table_remember(const uint8_t mac[6], esp_ip4_addr_t ip)
{
	for (int i = 0; i < AP_MAX_CONN; i++) {
		if (s_ap_clients[i].valid && memcmp(s_ap_clients[i].mac, mac, 6) == 0) {
			s_ap_clients[i].ip = ip;
			return;
		}
	}
	for (int i = 0; i < AP_MAX_CONN; i++) {
		if (!s_ap_clients[i].valid) {
			memcpy(s_ap_clients[i].mac, mac, 6);
			s_ap_clients[i].ip = ip;
			s_ap_clients[i].valid = true;
			return;
		}
	}
}

/* Looks up and forgets (in one step) the IP last assigned to this MAC, and
 * asks the web server to close any socket still open from it. Safe to call
 * even if this MAC was never actually seen with an IP (e.g. it disconnected
 * before DHCP finished), does nothing in that case. */
static void ap_client_table_forget_and_close(const uint8_t mac[6])
{
	for (int i = 0; i < AP_MAX_CONN; i++) {
		if (s_ap_clients[i].valid && memcmp(s_ap_clients[i].mac, mac, 6) == 0) {
			char ip_str[16];

			esp_ip4addr_ntoa(&s_ap_clients[i].ip, ip_str, sizeof(ip_str));
			web_server_close_sockets_for_ip(ip_str);
			s_ap_clients[i].valid = false;
			return;
		}
	}
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

/* Applies one candidate's SSID/password to the STA interface and starts a
 * connection attempt. Doesn't block, the result (success or failure)
 * arrives later as a WIFI_EVENT/IP_EVENT handled in
 * wifi_sta_event_handler(). */
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

/* ----------------------------------------------------------------------------
 * wifi_sta_event_handler()
 * ----------------------------------------------------------------------------
 * Single handler for every Wi-Fi/IP event this firmware cares about, both
 * STA-side (joining the home network) and AP-side (other devices joining
 * or leaving the ESP's own hotspot), since both are registered under the
 * same WIFI_EVENT base with ESP_EVENT_ANY_ID:
 *
 *   STA_START          - kicks off the first connection attempt.
 *   STA_DISCONNECTED   - advances to the next candidate network and retries.
 *   AP_STACONNECTED    - logs a device joining the hotspot.
 *   AP_STADISCONNECTED - logs the departure and cleans up any dashboard
 *                         socket that device left open (see
 *                         ap_client_table_forget_and_close above).
 *   ASSIGNED_IP_TO_CLIENT - records an AP client's MAC/IP pairing.
 *   STA_GOT_IP          - marks the home-network link up and re-announces
 *                          mDNS on that interface.
 *
 * The AP connect/disconnect logging exists specifically so connection
 * issues can be diagnosed after the fact from /api/debug-log, without
 * needing a USB cable plugged in.
 * ---------------------------------------------------------------------------- */
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
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
		wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
		ESP_LOGI(TAG, "[NET] Device joined PortaPill hotspot: MAC=" MACSTR ", AID=%d, free heap=%lu bytes",
			 MAC2STR(event->mac), event->aid, (unsigned long)esp_get_free_heap_size());
	} else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
		wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
		ESP_LOGI(TAG, "[NET] Device left PortaPill hotspot: MAC=" MACSTR ", AID=%d, reason=%d, free heap=%lu bytes",
			 MAC2STR(event->mac), event->aid, event->reason, (unsigned long)esp_get_free_heap_size());
		/* This is the abrupt-disconnect case (radio dropped, walked out of
		 * range) as opposed to a clean browser-tab close, the Wi-Fi link is
		 * gone before any TCP reset can happen, so the web server would
		 * otherwise never learn this socket is dead. Proactively close
		 * whatever this device had open instead of leaving it to linger. */
		ap_client_table_forget_and_close(event->mac);
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_ASSIGNED_IP_TO_CLIENT) {
		ip_event_assigned_ip_to_client_t *event = (ip_event_assigned_ip_to_client_t *)event_data;

		ap_client_table_remember(event->mac, event->ip);
	} else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
		ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
		s_sta_connected = true;
		ESP_LOGI(TAG, "Joined \"%s\", got IP: " IPSTR,
			 HOME_WIFI_CANDIDATES[s_sta_candidate_idx].ssid, IP2STR(&event->ip_info.ip));
		ESP_LOGI(TAG, "Dashboard also reachable on this network at http://" IPSTR "/ or http://portapill.local/",
			 IP2STR(&event->ip_info.ip));

		/* Re-set the hostname now that the STA interface actually has an IP.
		 * mdns_init() ran earlier (right after the radio started, before this
		 * IP existed), and re-calling mdns_hostname_set() with the same name
		 * forces the mDNS component to re-probe and re-announce on every
		 * active interface, including this one, right now. Without this, a
		 * device that has never resolved portapill.local before could hit a
		 * brief window where the responder hasn't fully settled onto the new
		 * interface yet and its first query goes unanswered, while a device
		 * with an already-cached answer from a previous visit never notices. */
		if (mdns_hostname_set("portapill") == ESP_OK) {
			ESP_LOGI(TAG, "mDNS re-announced on the home network interface");
		}
	}
}

/* ----------------------------------------------------------------------------
 * start_wifi_ap()
 * ----------------------------------------------------------------------------
 * Brings up the Wi-Fi radio: creates the AP (and STA, if credentials are
 * configured) network interfaces, registers the event handler, starts the
 * radio in AP or AP+STA mode, kicks off SNTP if STA is enabled, and starts
 * mDNS so the dashboard is reachable at portapill.local. Call once at
 * boot.
 * ---------------------------------------------------------------------------- */
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

	/* Registered unconditionally (not just when sta_enabled) so AP-side
	 * connect/disconnect logging (see wifi_sta_event_handler) always works,
	 * even on a build with no home Wi-Fi configured yet. */
	ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
							      &wifi_sta_event_handler, NULL, NULL));
	/* Also unconditional: needed to learn each AP client's IP so an abrupt
	 * disconnect can find and close its lingering socket, has nothing to do
	 * with whether home Wi-Fi (STA) is configured. */
	ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_ASSIGNED_IP_TO_CLIENT,
							      &wifi_sta_event_handler, NULL, NULL));

	if (sta_enabled) {
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
