/* ============================================================================
 * WEB_SERVER.H - HTTP Dashboard and JSON API
 * ----------------------------------------------------------------------------
 * Serves the browser-based dashboard (a single self-contained HTML/CSS/JS
 * page) and its supporting JSON API for status, profile edits, dispensing,
 * and diagnostics. This is the primary way a user interacts with the
 * device from a phone or laptop, mirroring what the LCD offers on-device.
 * ============================================================================ */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the HTTP server: dashboard at "/", JSON status at "/api/status",
 * and POST endpoints for profile edits, dispense, time sync, feedback
 * tests, and edit-mode LED signaling. Also answers captive-portal probe
 * URLs used by Android/Apple/Windows clients. */
void start_webserver(void);

/* Force-closes any HTTP socket(s) currently open from the given IPv4
 * address, e.g. "192.168.4.2". Used when a device drops off Wi-Fi abruptly
 * (walks out of range, radio disconnects) instead of closing its browser
 * tab first: in that case the ESP never gets a TCP reset to notice the
 * socket is dead, so it would otherwise sit "open" indefinitely, wasting a
 * slot in the small socket pool. Safe to call even if no matching socket is
 * currently open, does nothing in that case. */
void web_server_close_sockets_for_ip(const char *ip_str);

#ifdef __cplusplus
}
#endif

#endif
