/* ============================================================================
 * WIFI_AP.H - Wireless Networking
 * ----------------------------------------------------------------------------
 * Brings up the ESP32-S3's Wi-Fi radio in dual AP+STA mode: it always hosts
 * its own access point for direct/first-time setup, and optionally also
 * joins a home network as a client for internet access (NTP time sync,
 * push notifications). Also owns mDNS so the dashboard is reachable at
 * portapill.local, and the AP client-tracking used to clean up a device's
 * dashboard connections if it drops off Wi-Fi without closing them first.
 * ============================================================================ */

#ifndef WIFI_AP_H
#define WIFI_AP_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up the ESP32-S3 as a local Wi-Fi access point (SSID "PortaPill" at
 * 192.168.4.1). Falls back to an open network if the configured password
 * is too short for WPA/WPA2.
 *
 * If main/wifi_credentials.h has been filled in with a real network (see
 * wifi_credentials.example.h), this also joins that network as a Wi-Fi
 * client in the background (APSTA mode) purely to fetch the correct time
 * via NTP on boot and periodically afterward, and relays it to the Pico,
 * no phone or manual "Sync Clock" click required. The AP keeps serving the
 * dashboard exactly as before either way. */
void start_wifi_ap(void);

/* True once the ESP has joined a home Wi-Fi network and has an IP, i.e.
 * it's safe to reach the internet (NTP, push notifications). False if
 * STA mode isn't enabled, or if the link is currently down. */
bool wifi_sta_is_connected(void);

#ifdef __cplusplus
}
#endif

#endif
