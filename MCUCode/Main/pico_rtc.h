/**
 * Software RTC for Pico2 (RP2350)
 *
 * Maintains a Unix epoch using the Pico's 64-bit microsecond hardware timer.
 * Time is lost on power-off; send TIME_REQ to ESP on boot to re-sync.
 *
 * Usage:
 *   rtc_init_module() once at startup.
 *   rtc_set_epoch(unix_epoch) to set time (from ESP SET_TIME or terminal).
 *   rtc_get_epoch() / rtc_get_minutes_of_day() anywhere you need current time.
 */

#ifndef PICO_RTC_H
#define PICO_RTC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Initialize the software RTC (clears any previously set time).
 * Call once at startup before using any other rtc_ functions.
 */
void rtc_init_module(void);

/**
 * Returns true after rtc_set_epoch() has been called at least once
 * (persists until power-cycle).
 */
bool rtc_is_set(void);

/**
 * Set the current time as a Unix epoch (seconds since 1970-01-01 00:00:00 UTC).
 */
void rtc_set_epoch(uint32_t epoch);

/**
 * Get the current Unix epoch, accounting for elapsed time since it was set.
 * Returns 0 if time has not been set.
 */
uint32_t rtc_get_epoch(void);

/**
 * Get minutes elapsed since midnight today (0–1439).
 * Returns 0 if time has not been set.
 */
uint16_t rtc_get_minutes_of_day(void);

/**
 * Fill buf with a human-readable timestamp: "YYYY-MM-DD HH:MM:SS UTC"
 * or "(time not set)" if rtc_is_set() is false.
 */
void rtc_get_time_str(char *buf, size_t len);

/**
 * Parse a "YYYY-MM-DD HH:MM:SS" string into a Unix epoch.
 * Returns 0 on failure.
 */
uint32_t rtc_parse_datetime_str(const char *str);

/**
 * Send a TIME_REQ message to the ESP over UART, asking it to respond with
 * CMD|action=SET_TIME|epoch=<n>.
 */
void rtc_request_from_esp(void);

/**
 * Calls rtc_request_from_esp() once, then automatically retries every 15
 * seconds until rtc_is_set() becomes true, at which point it stops sending
 * anything. Self-throttling -- safe and cheap to call every main loop
 * iteration.
 */
void rtc_request_from_esp_if_needed(void);

#endif // PICO_RTC_H
