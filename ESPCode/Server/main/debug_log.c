#include "debug_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* DEBUG_LOG_BUF_SIZE (in debug_log.h) is plenty of room for several minutes
 * of normal logging, comfortably within the ESP32-S3's available RAM.
 * Oldest lines are dropped to make room for new ones once full, see
 * debug_log_append(). */
static char s_buf[DEBUG_LOG_BUF_SIZE];
static size_t s_len = 0;
static SemaphoreHandle_t s_mutex;
static vprintf_like_t s_original_vprintf;

/* Appends data to s_buf, dropping the oldest bytes first if it would
 * overflow, so the buffer always holds the most recent DEBUG_LOG_BUF_SIZE
 * bytes of log output, oldest-to-newest, no wraparound bookkeeping needed
 * on the read side. */
static void debug_log_append(const char *data, size_t len)
{
	if (len >= DEBUG_LOG_BUF_SIZE) {
		data += (len - DEBUG_LOG_BUF_SIZE + 1);
		len = DEBUG_LOG_BUF_SIZE - 1;
	}
	if (s_len + len > DEBUG_LOG_BUF_SIZE) {
		size_t drop = s_len + len - DEBUG_LOG_BUF_SIZE;

		if (drop >= s_len) {
			s_len = 0;
		} else {
			memmove(s_buf, s_buf + drop, s_len - drop);
			s_len -= drop;
		}
	}
	memcpy(s_buf + s_len, data, len);
	s_len += len;
}

static int debug_log_vprintf(const char *fmt, va_list args)
{
	char line[256];
	int len;
	va_list args_copy;

	va_copy(args_copy, args);
	len = vsnprintf(line, sizeof(line), fmt, args_copy);
	va_end(args_copy);

	if (len > 0) {
		size_t write_len = (size_t)len >= sizeof(line) ? sizeof(line) - 1 : (size_t)len;

		/* Never block waiting for this, a log call happening while the
		 * snapshot reader briefly holds the mutex should never stall
		 * whatever's doing the logging. Just skip capturing that one
		 * line if it can't get the lock immediately. */
		if (s_mutex != NULL && xSemaphoreTake(s_mutex, 0) == pdTRUE) {
			debug_log_append(line, write_len);
			xSemaphoreGive(s_mutex);
		}
	}

	/* Still print to the real USB serial console exactly as before, this
	 * only ever adds a second destination for the same log line. */
	return s_original_vprintf != NULL ? s_original_vprintf(fmt, args) : len;
}

void debug_log_init(void)
{
	s_mutex = xSemaphoreCreateMutex();
	s_original_vprintf = esp_log_set_vprintf(debug_log_vprintf);
}

size_t debug_log_snapshot(char *out, size_t out_size)
{
	size_t copied;

	if (out == NULL || out_size == 0) {
		return 0;
	}
	if (s_mutex == NULL || xSemaphoreTake(s_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
		out[0] = '\0';
		return 0;
	}

	copied = s_len < out_size - 1 ? s_len : out_size - 1;
	memcpy(out, s_buf, copied);
	out[copied] = '\0';

	xSemaphoreGive(s_mutex);
	return copied;
}
