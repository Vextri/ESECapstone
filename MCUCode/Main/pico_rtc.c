/**
 * Software RTC Implementation for Pico2 (RP2350)
 *
 * Uses time_us_64() to track elapsed microseconds from a stored base epoch,
 * so the current Unix timestamp is always:
 *   current = base_epoch + (time_us_64() - base_us) / 1_000_000
 */

#include "pico_rtc.h"
#include "pill_dispenser.h"   // ESP_UART define
#include "pico/stdlib.h"      // time_us_64()
#include "hardware/uart.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static bool     s_time_set   = false;
static uint32_t s_base_epoch = 0;   // Unix epoch snapshot when time was set
static uint64_t s_base_us    = 0;   // time_us_64() snapshot when time was set

static int32_t eastern_offset(uint32_t epoch);  // forward declaration

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void rtc_init_module(void) {
    s_time_set   = false;
    s_base_epoch = 0;
    s_base_us    = 0;
}

bool rtc_is_set(void) {
    return s_time_set;
}

void rtc_set_epoch(uint32_t epoch) {
    s_base_us    = time_us_64();
    s_base_epoch = epoch;
    s_time_set   = true;
    printf("[RTC] Time set: epoch=%lu\n", (unsigned long)epoch);
}

uint32_t rtc_get_epoch(void) {
    if (!s_time_set) return 0;
    uint64_t elapsed_us = time_us_64() - s_base_us;
    return s_base_epoch + (uint32_t)(elapsed_us / 1000000ULL);
}

uint16_t rtc_get_minutes_of_day(void) {
    if (!s_time_set) return 0;
    uint32_t utc = rtc_get_epoch();
    uint32_t local = (uint32_t)((int64_t)utc + (int64_t)eastern_offset(utc));
    return (uint16_t)((local / 60U) % 1440U);
}

// ---------------------------------------------------------------------------
// Epoch ↔ datetime helpers
// ---------------------------------------------------------------------------

static const uint8_t s_days_in_month[12] = {31,28,31,30,31,30,31,31,30,31,30,31};

static bool is_leap(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static void epoch_to_datetime(uint32_t epoch,
                               int *year, int *month, int *day,
                               int *hour, int *min,  int *sec) {
    *sec   = (int)(epoch % 60); epoch /= 60;
    *min   = (int)(epoch % 60); epoch /= 60;
    *hour  = (int)(epoch % 24); epoch /= 24;

    uint32_t days = epoch;

    *year = 1970;
    while (true) {
        uint32_t diy = is_leap(*year) ? 366u : 365u;
        if (days < diy) break;
        days -= diy;
        (*year)++;
    }

    for (*month = 1; *month <= 12; (*month)++) {
        uint32_t dim = s_days_in_month[*month - 1];
        if (*month == 2 && is_leap(*year)) dim = 29;
        if (days < dim) break;
        days -= dim;
    }
    *day = (int)(days + 1);
}

// ---------------------------------------------------------------------------
// US Eastern time (EST = UTC-5, EDT = UTC-4)
// EDT runs from the second Sunday in March at 07:00 UTC
//                  to the first Sunday in November at 06:00 UTC.
// ---------------------------------------------------------------------------

static uint32_t days_since_epoch(int year, int month, int day) {
    uint32_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap(y) ? 366u : 365u;
    }
    for (int m = 1; m < month; m++) {
        uint32_t dim = s_days_in_month[m - 1];
        if (m == 2 && is_leap(year)) dim = 29;
        days += dim;
    }
    days += (uint32_t)(day - 1);
    return days;
}

// Returns -14400 (EDT) or -18000 (EST)
static int32_t eastern_offset(uint32_t epoch) {
    int year, month, day, hour, min, sec;
    epoch_to_datetime(epoch, &year, &month, &day, &hour, &min, &sec);

    // Second Sunday in March, 07:00 UTC
    uint32_t mar1_days = days_since_epoch(year, 3, 1);
    int mar1_dow = (int)((mar1_days + 4) % 7); // 0=Sun
    int first_sun_mar = (mar1_dow == 0) ? 1 : (8 - mar1_dow);
    uint32_t edt_start = days_since_epoch(year, 3, first_sun_mar + 7) * 86400U + 7U * 3600U;

    // First Sunday in November, 06:00 UTC
    uint32_t nov1_days = days_since_epoch(year, 11, 1);
    int nov1_dow = (int)((nov1_days + 4) % 7);
    int first_sun_nov = (nov1_dow == 0) ? 1 : (8 - nov1_dow);
    uint32_t edt_end = days_since_epoch(year, 11, first_sun_nov) * 86400U + 6U * 3600U;

    return (epoch >= edt_start && epoch < edt_end) ? -14400 : -18000;
}

// ---------------------------------------------------------------------------

void rtc_get_time_str(char *buf, size_t len) {
    if (!s_time_set) {
        snprintf(buf, len, "(time not set)");
        return;
    }
    uint32_t utc = rtc_get_epoch();
    int32_t  off = eastern_offset(utc);
    uint32_t local = (uint32_t)((int64_t)utc + (int64_t)off);
    int year, month, day, hour, min, sec;
    epoch_to_datetime(local, &year, &month, &day, &hour, &min, &sec);
    snprintf(buf, len, "%04d-%02d-%02d %02d:%02d:%02d %s",
             year, month, day, hour, min, sec,
             (off == -14400) ? "EDT" : "EST");
}

uint32_t rtc_parse_datetime_str(const char *str) {
    int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
    // Accept "YYYY-MM-DD HH:MM:SS" (space or T separator)
    int n = sscanf(str, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec);
    if (n < 3) {
        // Try with T separator (ISO 8601)
        n = sscanf(str, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &min, &sec);
    }
    if (n < 3 || year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) {
        return 0;
    }

    // Count days from 1970-01-01
    uint32_t days = 0;
    for (int y = 1970; y < year; y++) {
        days += is_leap(y) ? 366u : 365u;
    }
    for (int m = 1; m < month; m++) {
        uint32_t dim = s_days_in_month[m - 1];
        if (m == 2 && is_leap(year)) dim = 29;
        days += dim;
    }
    days += (uint32_t)(day - 1);

    return days * 86400UL + (uint32_t)hour * 3600UL + (uint32_t)min * 60UL + (uint32_t)sec;
}

// ---------------------------------------------------------------------------

void rtc_request_from_esp(void) {
    const char *req = "TIME_REQ\n";
    uart_write_blocking(ESP_UART, (const uint8_t *)req, strlen(req));
    printf("[RTC] TIME_REQ sent to ESP — waiting for SET_TIME response\n");
}
