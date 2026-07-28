#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Size of the in-RAM ring, and the recommended buffer size to pass to
 * debug_log_snapshot() to retrieve the whole thing. */
#define DEBUG_LOG_BUF_SIZE 12288

/* Captures every ESP_LOGx line into an in-RAM buffer, in addition to still
 * printing it to the USB serial console exactly as before, so the same
 * logs are readable over WiFi (see web_server.c's /api/debug-log route)
 * when there's no room to physically plug a cable into the ESP once it's
 * inside the assembled device. Call once at boot, as early as possible so
 * nothing gets missed. */
void debug_log_init(void);

/* Copies the most recent buffered log text into out (NUL-terminated,
 * truncated to fit if needed). Returns the number of bytes written,
 * excluding the terminator. */
size_t debug_log_snapshot(char *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif
