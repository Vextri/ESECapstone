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

#ifdef __cplusplus
}
#endif

#endif
