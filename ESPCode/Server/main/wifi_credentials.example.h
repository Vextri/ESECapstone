#ifndef WIFI_CREDENTIALS_H
#define WIFI_CREDENTIALS_H

/* Copy this file to wifi_credentials.h (same folder) and fill in real
 * networks. wifi_credentials.h is gitignored on purpose, your real
 * network names and passwords never get committed or shared. Only this
 * .example file (with placeholder values) is tracked in git.
 *
 * These are networks the ESP joins as a Wi-Fi *client* purely to fetch the
 * correct time over the internet (NTP) on boot and periodically afterward.
 * They have nothing to do with the ESP's own "ESP-Time-Server" access
 * point, which keeps working exactly as before regardless of whether any
 * of these are in range.
 *
 * List as many as you want, in priority order, the ESP tries the first
 * entry, and if that fails to connect (out of range, wrong password, etc.)
 * it automatically moves on to the next one, cycling through the whole
 * list. Handy for a home network plus a phone hotspot as backup when
 * you're away from home.
 *
 * Only simple WPA2-Personal (password-only) networks work here. Campus/
 * enterprise Wi-Fi that needs a student login (WPA2-Enterprise / EAP,
 * e.g. eduroam) is NOT supported by this list, that needs a materially
 * different, more involved setup (certificates, identity + username/
 * password via the esp_eap_client component), so just leave it out and
 * rely on a phone hotspot in that environment instead. */

typedef struct {
	const char *ssid;
	const char *password;
} wifi_credential_t;

static const wifi_credential_t HOME_WIFI_CANDIDATES[] = {
	{"YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD"},
	/* {"YOUR_PHONE_HOTSPOT_NAME", "YOUR_HOTSPOT_PASSWORD"}, */
};

#define HOME_WIFI_CANDIDATE_COUNT (sizeof(HOME_WIFI_CANDIDATES) / sizeof(HOME_WIFI_CANDIDATES[0]))

#endif
