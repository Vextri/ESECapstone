#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#include "bridge_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parses a schedule token like "08:00", "8:00am", or "20" (hour-only) into
 * minutes-since-midnight. Returns false if the token isn't a valid time. */
bool screen_parse_hhmm(const char *token, int *minutes_out);

/* Formats the next upcoming scheduled dispense (e.g. "08:00 FOR S0&S2") for
 * display on the LCD status screen. */
void screen_get_next_dispense_string(const pico_bridge_state_t *snapshot, char *out, size_t out_len);

/* Formats the current local time as "YYYY-MM-DD HH:MM:SS", or an uptime
 * fallback string if the clock has not been set yet. */
void get_device_time_string(char *out, size_t out_len);

/* True once the ESP's local clock holds a plausible (post-2023) epoch. */
bool bridge_is_time_valid(void);

/* Sets the ESP's local clock (settimeofday) from a Unix epoch. */
bool bridge_set_local_time(time_t epoch);

#ifdef __cplusplus
}
#endif

#endif
