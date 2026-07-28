#include "time_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "time_server";

void time_utils_init(void)
{
	/* POSIX TZ format: STD offset DST[,rule]. EST5EDT = Eastern Standard is
	 * 5 hours behind UTC, Eastern Daylight is 1 hour ahead of that. The rule
	 * after the comma is "spring forward on the 2nd Sunday of March at 2am,
	 * fall back on the 1st Sunday of November at 2am", the same US DST
	 * rule already hardcoded in the Pico's pico_rtc.c eastern_offset(). */
	setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
	tzset();
	ESP_LOGI(TAG, "Timezone set to US Eastern (EST5EDT)");
}

bool screen_parse_hhmm(const char *token, int *minutes_out)
{
	char local[24];
	char *p;
	char *end;
	char *endptr;
	long hour;
	long minute;
	bool has_am = false;
	bool has_pm = false;

	if (token == NULL || minutes_out == NULL) {
		return false;
	}

	bridge_copy_string(local, sizeof(local), token);
	p = local;
	while (*p == ' ' || *p == '\t') {
		p++;
	}

	end = p + strlen(p);
	while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
		end--;
	}
	*end = '\0';

	if (*p == '\0') {
		return false;
	}

	for (char *it = p; *it != '\0'; it++) {
		if (*it >= 'A' && *it <= 'Z') {
			*it = (char)(*it - 'A' + 'a');
		}
		if (*it == '.') {
			*it = ':';
		}
	}

	end = p + strlen(p);
	if (end - p >= 2 && end[-2] == 'a' && end[-1] == 'm') {
		has_am = true;
		end -= 2;
	} else if (end - p >= 2 && end[-2] == 'p' && end[-1] == 'm') {
		has_pm = true;
		end -= 2;
	}
	while (end > p && (end[-1] == ' ' || end[-1] == '\t')) {
		end--;
	}
	*end = '\0';

	if (*p == '\0') {
		return false;
	}

	hour = strtol(p, &endptr, 10);
	if (endptr == p) {
		return false;
	}

	/* Accept hour-only format like "11" as 11:00. */
	if (*endptr == '\0') {
		minute = 0;
	} else if (*endptr != ':') {
		return false;
	} else {
		char *minute_start = endptr + 1;
		minute = strtol(minute_start, &endptr, 10);
		if (endptr == minute_start) {
			return false;
		}
		if (*endptr != '\0') {
			return false;
		}
	}

	if (minute < 0 || minute > 59) {
		return false;
	}

	if (has_am || has_pm) {
		if (hour < 1 || hour > 12) {
			return false;
		}
		if (has_am && hour == 12) {
			hour = 0;
		} else if (has_pm && hour != 12) {
			hour += 12;
		}
	} else if (hour < 0 || hour > 23) {
		return false;
	}

	*minutes_out = (int)(hour * 60 + minute);
	return true;
}

void screen_get_next_dispense_string(const pico_bridge_state_t *snapshot, char *out, size_t out_len)
{
	time_t now;
	struct tm ti;
	int current_minutes;
	int best_delta = (24 * 60) + 1;
	int best_minutes = -1;
	bool best_is_tomorrow = false;
	int best_slots[PILL_SLOT_COUNT];
	int best_count = 0;

	if (out == NULL || out_len == 0) {
		return;
	}

	out[0] = '\0';
	now = time(NULL);
	if (now <= 1700000000 || localtime_r(&now, &ti) == NULL) {
		snprintf(out, out_len, "SYNC TIME");
		return;
	}

	current_minutes = (ti.tm_hour * 60) + ti.tm_min;
	for (int si = 0; si < PILL_SLOT_COUNT; si++) {
		const pill_slot_state_t *slot = &snapshot->slots[si];
		char schedule_copy[40];
		char *saveptr = NULL;
		char *token;

		if (slot->schedule[0] == '\0' || strcmp(slot->schedule, "none") == 0) {
			continue;
		}

		bridge_copy_string(schedule_copy, sizeof(schedule_copy), slot->schedule);
		token = strtok_r(schedule_copy, ",", &saveptr);
		while (token != NULL) {
			int event_minutes;

			if (screen_parse_hhmm(token, &event_minutes)) {
				int delta = event_minutes - current_minutes;
				if (delta < 0) {
					delta += (24 * 60);
				}
				if (delta < best_delta) {
					best_delta = delta;
					best_minutes = event_minutes;
					best_is_tomorrow = (event_minutes < current_minutes);
					best_count = 1;
					best_slots[0] = si;
				} else if (delta == best_delta && best_count < PILL_SLOT_COUNT) {
					bool already_present = false;
					for (int bi = 0; bi < best_count; bi++) {
						if (best_slots[bi] == si) {
							already_present = true;
							break;
						}
					}
					if (!already_present) {
						best_slots[best_count++] = si;
					}
				}
			}

			token = strtok_r(NULL, ",", &saveptr);
		}
	}

	if (best_minutes < 0) {
		snprintf(out, out_len, "NO SCHEDULE");
	} else {
		char slots_buf[24] = {0};
		size_t used = 0;

		for (int i = 0; i < best_count; i++) {
			int written = snprintf(slots_buf + used,
					       sizeof(slots_buf) - used,
					       "%sS%d",
					       i == 0 ? "" : (best_count == 2 ? "&" : ","),
					       best_slots[i]);
			if (written < 0 || (size_t)written >= (sizeof(slots_buf) - used)) {
				break;
			}
			used += (size_t)written;
		}

		{
			int hour24 = best_minutes / 60;
			int minute = best_minutes % 60;
			int hour12 = hour24 % 12;
			const char *ampm = (hour24 < 12) ? "AM" : "PM";

			if (hour12 == 0) {
				hour12 = 12;
			}

			if (best_is_tomorrow) {
				snprintf(out, out_len, "%d:%02d %s TMRW FOR %s", hour12, minute, ampm, slots_buf);
			} else {
				snprintf(out, out_len, "%d:%02d %s FOR %s", hour12, minute, ampm, slots_buf);
			}
		}
	}
	return;
}

void get_device_time_string(char *out, size_t out_len)
{
	time_t now = time(NULL);
	struct tm timeinfo;

	if (localtime_r(&now, &timeinfo) != NULL &&
		strftime(out, out_len, "%Y-%m-%d %H:%M:%S", &timeinfo) > 0) {
		return;
	}

	int64_t uptime_seconds = esp_timer_get_time() / 1000000;
	snprintf(out, out_len, "Time not set (uptime %llds)", (long long)uptime_seconds);
}

bool bridge_is_time_valid(void)
{
	time_t now = time(NULL);

	return now > 1700000000;
}

bool bridge_set_local_time(time_t epoch)
{
	struct timeval now = {
		.tv_sec = epoch,
		.tv_usec = 0,
	};

	if (epoch <= 0) {
		return false;
	}

	if (settimeofday(&now, NULL) != 0) {
		ESP_LOGE(TAG, "Failed to set local time from epoch %lld", (long long)epoch);
		return false;
	}

	ESP_LOGI(TAG, "Local ESP time updated to epoch %lld", (long long)epoch);
	return true;
}
