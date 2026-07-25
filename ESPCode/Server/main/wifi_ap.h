#ifndef WIFI_AP_H
#define WIFI_AP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up the ESP32-S3 as a local Wi-Fi access point (SSID "ESP-Time-Server"
 * at 192.168.4.1). Falls back to an open network if the configured password
 * is too short for WPA/WPA2. */
void start_wifi_ap(void);

#ifdef __cplusplus
}
#endif

#endif
