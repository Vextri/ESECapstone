/**
 * ESP UART Command Receiver Implementation
 */

#include "esp_uart.h"
#include "pill_dispenser.h"
#include "pico_rtc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define RX_BUF_SIZE 256

static char rx_buf[RX_BUF_SIZE];
static int  rx_pos = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * Extracts the value for a given key from a pipe-delimited line.
 * e.g. get_field("CMD|slot=2|med=Aspirin", "med", out, 32) -> "Aspirin"
 * Returns true if the key was found, false otherwise.
 */
static bool get_field(const char *line, const char *key,
                      char *out, size_t out_size) {
    char search[48];
    snprintf(search, sizeof(search), "%s=", key);

    const char *p = strstr(line, search);
    if (!p) return false;
    p += strlen(search);

    size_t i = 0;
    while (*p && *p != '|' && *p != '\r' && *p != '\n' && i < out_size - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return true;
}

/**
 * Parses a schedule string like "08:00,20:00" into a uint16_t array of
 * minutes-since-midnight values. Returns the number of entries parsed.
 */
static uint8_t parse_schedule(const char *sched_str,
                               uint16_t *out, uint8_t max_count) {
    uint8_t count = 0;
    const char *p = sched_str;
    while (*p && count < max_count) {
        int hours = 0, mins = 0;
        // Parse HH
        while (*p >= '0' && *p <= '9') hours = hours * 10 + (*p++ - '0');
        if (*p == ':') p++;
        // Parse MM
        while (*p >= '0' && *p <= '9') mins  = mins  * 10 + (*p++ - '0');
        if (hours < 24 && mins < 60) {
            out[count++] = (uint16_t)(hours * 60 + mins);
        }
        if (*p == ',') p++;
    }
    return count;
}

static void send_ack(const char *action, uint8_t slot, const char *result) {
    char buf[96];
    int len = snprintf(buf, sizeof(buf),
                       "ACK|action=%s|slot=%d|result=%s\n",
                       action, (int)slot, result);
    if (len > 0) {
        uart_write_blocking(ESP_UART, (const uint8_t *)buf, (size_t)len);
        printf("[ESP TX] %s", buf);
    }
}

// ---------------------------------------------------------------------------
// Command handlers
// ---------------------------------------------------------------------------

static void handle_load_profile(const char *line) {
    char slot_str[4]  = {0};
    char med[32]      = {0};
    char total_str[8] = {0};
    char dose_str[8]  = {0};
    char time_str[16] = {0};

    bool ok = get_field(line, "slot",  slot_str,  sizeof(slot_str))
           && get_field(line, "med",   med,        sizeof(med))
           && get_field(line, "total", total_str,  sizeof(total_str))
           && get_field(line, "dose",  dose_str,   sizeof(dose_str))
           && get_field(line, "time",  time_str,   sizeof(time_str));

    if (!ok) {
        printf("[ESP CMD] LOAD_PROFILE: missing field(s) in: %s\n", line);
        send_ack("LOAD_PROFILE", 0, "err_missing_fields");
        return;
    }

    uint8_t  slot    = (uint8_t)atoi(slot_str);
    uint8_t  total   = (uint8_t)atoi(total_str);
    uint8_t  dose    = (uint8_t)atoi(dose_str);
    uint32_t time_ms = (uint32_t)atoi(time_str);

    if (slot >= MAX_PROFILES) {
        printf("[ESP CMD] LOAD_PROFILE: invalid slot %d (max %d)\n",
               slot, MAX_PROFILES - 1);
        send_ack("LOAD_PROFILE", slot, "err_invalid_slot");
        return;
    }

    dispenser_load_profile_to_slot(slot, med, total, dose, time_ms);

    // Optional schedule field: "08:00,20:00" etc.
    char sched_str[48] = {0};
    if (get_field(line, "schedule", sched_str, sizeof(sched_str)) && sched_str[0] != '\0') {
        uint16_t times[MAX_DOSES_PER_DAY];
        uint8_t count = parse_schedule(sched_str, times, MAX_DOSES_PER_DAY);
        if (count > 0) {
            dispenser_set_profile_schedule(slot, times, count);
        }
    }

    send_ack("LOAD_PROFILE", slot, "ok");
}

static void handle_set_time(const char *line) {
    char epoch_str[16] = {0};
    if (!get_field(line, "epoch", epoch_str, sizeof(epoch_str))) {
        printf("[ESP CMD] SET_TIME: missing epoch field in: %s\n", line);
        send_ack("SET_TIME", 0, "err_missing_epoch");
        return;
    }
    uint32_t epoch = (uint32_t)strtoul(epoch_str, NULL, 10);
    if (epoch == 0) {
        printf("[ESP CMD] SET_TIME: invalid epoch value\n");
        send_ack("SET_TIME", 0, "err_invalid_epoch");
        return;
    }
    rtc_set_epoch(epoch);
    send_ack("SET_TIME", 0, "ok");
}

static void handle_dispense(const char *line) {
    char slot_str[4] = {0};
    if (get_field(line, "slot", slot_str, sizeof(slot_str))) {
        uint8_t slot = (uint8_t)atoi(slot_str);
        if (slot >= MAX_PROFILES) {
            printf("[ESP CMD] DISPENSE: invalid slot %d (max %d)\n",
                   slot, MAX_PROFILES - 1);
            send_ack("DISPENSE", slot, "err_invalid_slot");
            return;
        }
        if (!dispenser_switch_to_profile(slot)) {
            printf("[ESP CMD] DISPENSE: slot %d is not active\n", slot);
            send_ack("DISPENSE", slot, "err_slot_not_active");
            return;
        }
    }

    bool ok = dispenser_execute_dose_sensor_based();
    uint8_t current = (uint8_t)dispenser_get_current_profile_slot();
    send_ack("DISPENSE", current, ok ? "ok" : "fail");
}

// ---------------------------------------------------------------------------
// Dispatcher
// ---------------------------------------------------------------------------

static void dispatch_command(char *line) {
    printf("[ESP RX] %s\n", line);

    if (strncmp(line, "CMD|", 4) != 0) {
        printf("[ESP CMD] Ignored (not CMD prefix): %s\n", line);
        return;
    }

    char action[32] = {0};
    if (!get_field(line, "action", action, sizeof(action))) {
        printf("[ESP CMD] No action field in: %s\n", line);
        return;
    }

    if (strcmp(action, "LOAD_PROFILE") == 0) {
        handle_load_profile(line);
    } else if (strcmp(action, "SET_TIME") == 0) {
        handle_set_time(line);
    } else if (strcmp(action, "DISPENSE") == 0) {
        handle_dispense(line);
    } else {
        printf("[ESP CMD] Unhandled action: %s\n", action);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void esp_uart_inject_line(char *line) {
    dispatch_command(line);
}

void esp_uart_poll(void) {
    while (uart_is_readable(ESP_UART)) {
        char c = (char)uart_getc(ESP_UART);

        if (c == '\n') {
            rx_buf[rx_pos] = '\0';
            // Strip trailing \r (Windows-style line endings)
            if (rx_pos > 0 && rx_buf[rx_pos - 1] == '\r') {
                rx_buf[--rx_pos] = '\0';
            }
            if (rx_pos > 0) {
                dispatch_command(rx_buf);
            }
            rx_pos = 0;
        } else if (rx_pos < RX_BUF_SIZE - 1) {
            rx_buf[rx_pos++] = c;
        } else {
            // Line too long — discard and reset
            printf("[ESP RX] Line buffer overflow, discarding\n");
            rx_pos = 0;
        }
    }
}
