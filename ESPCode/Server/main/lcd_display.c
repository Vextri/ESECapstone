/* ============================================================================
 * LCD_DISPLAY.C - On-Device Touchscreen UI Implementation
 * ----------------------------------------------------------------------------
 * Three layers, top to bottom:
 *
 *   1. Raw ST7796 display driver (lcd_send_cmd/lcd_send_data/lcd_fill_rect/
 *      lcd_draw_string, etc.) - talks SPI directly to the panel, including a
 *      hand-rolled 5x7 bitmap font, no graphics library involved.
 *   2. Screen renderers (the screen_draw_* functions) - each draws one full
 *      screen of the UI (status view, slot menu, edit forms, feedback
 *      cards) from the current ui_state_t and the live bridge_state
 *      snapshot.
 *   3. Two FreeRTOS tasks that tie it together: button_task() polls the
 *      5-way pad and turns raw pin edges into UI actions (moving the
 *      cursor, entering edit mode, triggering a dispense), and
 *      screen_task() owns ui_state and periodically re-renders whichever
 *      screen is currently active.
 *
 * ui_state_t is the single source of truth for what the LCD is currently
 * showing and any in-progress edit, only screen_task()'s thread ever
 * touches it, button_task() communicates with it exclusively through
 * btn_queue.
 * ============================================================================ */

#include "lcd_display.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "audio_feedback.h"
#include "bridge_state.h"
#include "led_feedback.h"
#include "time_utils.h"
#include "uart_bridge.h"

static const char *TAG = "time_server";

/* ── ST7796 LCD display ───────────────────────────────────────────────────── *
 * GPIO 11 and 12 are taken by the UART bridge (RX/TX to Pico), so MOSI and  *
 * CLK must use different pins. All other original pins are kept as-is.       */
#define LCD_MOSI  13
#define LCD_CLK   14
#define LCD_CS    15
#define LCD_DC     2
#define LCD_RST    4
#define LCD_BL     5

/* ── Physical buttons (active-low, internal pull-up), 5-way pad ───────────── */
#define BTN_LEFT_PIN  36
#define BTN_RIGHT_PIN 37
#define BTN_UP_PIN    38
#define BTN_DOWN_PIN  39
#define BTN_OK_PIN    40
#define BTN_LONG_MS  700
#define BTN_DEBOUNCE_MS 35
#define BTN_DOWN_DEBOUNCE_MS 70
#define UI_INACTIVITY_TIMEOUT_MS 20000
#define UI_VISIBLE_SLOT_COUNT 3
#define UI_SAVE_FEEDBACK_MS 1400

#define SCREEN_W  480
#define SCREEN_H  320

#define LCD_BLACK   0x0000
#define LCD_WHITE   0xFFFF
#define LCD_CYAN    0x07FF
#define LCD_YELLOW  0xFFE0
#define LCD_GREEN   0x07E0
#define LCD_RED     0xF800
#define LCD_GREY    0x8410
#define LCD_DKGREY  0x4208

static spi_device_handle_t lcd_spi;
static uint8_t lcd_row_buf[SCREEN_W * 2];

/* ── Button event + UI state types ────────────────────────────────────────── */
typedef enum { BTN_EVT_UP, BTN_EVT_DOWN, BTN_EVT_LEFT, BTN_EVT_RIGHT, BTN_EVT_SELECT, BTN_EVT_BACK } btn_event_t;
typedef enum {
	UI_STATUS,
	UI_SLOT_MENU,
	UI_ACTION_MENU,
	UI_CONFIRM_DISPENSE,
	UI_EDIT_FIELD,
	UI_EDIT_NAME,
	UI_EDIT_SCHEDULE,
	UI_NOTICE_DISPENSING,
	UI_NOTICE_DISPENSE_SUCCESS,
	UI_NOTICE_DISPENSE_FAILURE,
	UI_NOTICE_SAVED,
} ui_screen_t;

/* Fields: NAME, TOTAL PILLS, DOSE PER DISPENSE, SCHEDULE, CONFIRM. */
#define EDIT_FIELD_COUNT      5
#define EDIT_NAME_MAX_LEN     20  /* keeps the on-screen keyboard readable at scale 2; medication_name buffer is 32 */

/* Must stay in sync with MAX_DOSES_PER_DAY in MCUCode/Main/pill_dispenser.h --
 * separate codebases, so this can't be shared directly. */
#define LCD_SCHED_MAX_TIMES 4
/* Per schedule entry: 4 digit positions (hour tens/ones, minute tens/ones)
 * plus 1 on/off toggle position. */
#define LCD_SCHED_FIELDS_PER_ENTRY 5
#define LCD_SCHED_CURSOR_COUNT (LCD_SCHED_MAX_TIMES * LCD_SCHED_FIELDS_PER_ENTRY)

/* On-screen keyboard for the medication name: arrow keys hover between
 * tiles, OK types the highlighted one. Last row holds SPACE/DEL/DONE. */
#define KB_ROWS 4
static const char *const kb_row0[] = { "A","B","C","D","E","F","G","H","I","J","K","L","M" };
static const char *const kb_row1[] = { "N","O","P","Q","R","S","T","U","V","W","X","Y","Z" };
static const char *const kb_row2[] = { "0","1","2","3","4","5","6","7","8","9","-","." };
static const char *const kb_row3[] = { "SPACE","DEL","DONE" };
static const char *const *const kb_rows[KB_ROWS] = { kb_row0, kb_row1, kb_row2, kb_row3 };
static const int kb_row_len[KB_ROWS] = {
	(int)(sizeof(kb_row0) / sizeof(kb_row0[0])),
	(int)(sizeof(kb_row1) / sizeof(kb_row1[0])),
	(int)(sizeof(kb_row2) / sizeof(kb_row2[0])),
	(int)(sizeof(kb_row3) / sizeof(kb_row3[0])),
};

typedef struct {
	uint8_t hour;   /* 0-23 */
	uint8_t minute; /* 0-59 */
	bool    active; /* included in the saved schedule when true */
} edit_sched_entry_t;

typedef struct {
	ui_screen_t screen;
	int         cursor;
	int         edit_slot;
	int         dispense_slot;
	int         edit_total;
	int         edit_dose;
	char        edit_name[32];
	int         edit_name_len;
	int         kb_row;
	int         kb_col;
	edit_sched_entry_t edit_sched[LCD_SCHED_MAX_TIMES];
	int         edit_sched_cursor;
	TickType_t  feedback_deadline_tick;
} ui_state_t;

static QueueHandle_t btn_queue;
static ui_state_t    ui_state;

/* ----------------------------------------------------------------------------
 * Raw ST7796 display driver
 * ----------------------------------------------------------------------------
 * The panel is controlled over SPI with a separate D/C (data/command) GPIO
 * line: pulling D/C low before a transfer means "this byte is a command",
 * pulling it high means "this is pixel/parameter data". Everything in this
 * section is built on that one primitive.
 * ---------------------------------------------------------------------------- */
static void lcd_send_cmd(uint8_t cmd)
{
	gpio_set_level(LCD_DC, 0);
	spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
	spi_device_transmit(lcd_spi, &t);
}

static void lcd_send_data(const uint8_t *data, int len)
{
	if (len == 0) {
		return;
	}
	gpio_set_level(LCD_DC, 1);
	spi_transaction_t t = { .length = (size_t)len * 8, .tx_buffer = data };
	spi_device_transmit(lcd_spi, &t);
}

static void lcd_send_byte(uint8_t b)
{
	lcd_send_data(&b, 1);
}

/* Hardware reset pulse followed by the panel's standard init sequence:
 * software reset, sleep-out, 16-bit color mode, then the memory access
 * control byte that sets orientation (landscape, rotated 180 degrees to
 * match how the panel is mounted) and RGB/BGR pixel order. */
static void lcd_st7796_init(void)
{
	gpio_set_level(LCD_RST, 0);
	vTaskDelay(pdMS_TO_TICKS(10));
	gpio_set_level(LCD_RST, 1);
	vTaskDelay(pdMS_TO_TICKS(120));
	lcd_send_cmd(0x01);
	vTaskDelay(pdMS_TO_TICKS(120));
	lcd_send_cmd(0x11);
	vTaskDelay(pdMS_TO_TICKS(120));
	lcd_send_cmd(0x3A); lcd_send_byte(0x55);
	lcd_send_cmd(0x36); lcd_send_byte(0xE8); /* landscape 180°: MY=1, MX=1, MV=1, BGR=1 */
	lcd_send_cmd(0x29);
}

/* Sets the panel's active drawing rectangle (column/row address window),
 * every pixel sent after this lands inside that box, wrapping row by row.
 * All higher-level drawing (fill_rect, characters) goes through this. */
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t col[] = { x0 >> 8, x0, x1 >> 8, x1 };
	uint8_t row[] = { y0 >> 8, y0, y1 >> 8, y1 };
	lcd_send_cmd(0x2A); lcd_send_data(col, 4);
	lcd_send_cmd(0x2B); lcd_send_data(row, 4);
	lcd_send_cmd(0x2C);
	gpio_set_level(LCD_DC, 1);
}

/* Fills a rectangle with a solid color, one row at a time from a
 * pre-filled line buffer, clamped to stay on-screen if x/y/w/h would
 * otherwise run past the panel edge. The one workhorse every other shape
 * (lines, characters, filled boxes) is built from. */
static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	int row_bytes;
	int i;
	uint32_t x_end;
	uint32_t y_end;

	if (w == 0 || h == 0) {
		return;
	}
	if (x >= SCREEN_W || y >= SCREEN_H) {
		return;
	}

	x_end = (uint32_t)x + (uint32_t)w;
	y_end = (uint32_t)y + (uint32_t)h;
	if (x_end > SCREEN_W) {
		w = (uint16_t)(SCREEN_W - x);
	}
	if (y_end > SCREEN_H) {
		h = (uint16_t)(SCREEN_H - y);
	}
	if (w == 0 || h == 0) {
		return;
	}

	row_bytes = (int)w * 2;
	if (row_bytes > (int)sizeof(lcd_row_buf)) {
		row_bytes = (int)sizeof(lcd_row_buf);
	}
	for (i = 0; i < row_bytes; i += 2) {
		lcd_row_buf[i]     = (uint8_t)(color & 0xFF);
		lcd_row_buf[i + 1] = (uint8_t)(color >> 8);
	}
	lcd_set_window(x, y, (uint16_t)(x + w - 1), (uint16_t)(y + h - 1));
	for (i = 0; i < (int)h; i++) {
		lcd_send_data(lcd_row_buf, row_bytes);
	}
}

static void lcd_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
	lcd_fill_rect(x, y, w, 2, color);
}

/* Bitmap font, 5x7 pixels per glyph, one column of bits per byte. Covers
 * space through uppercase Z (ASCII 32-90), enough for every screen in this
 * UI since text is upper-cased before drawing (see lcd_draw_string). */
static const uint8_t lcd_font5x7[][5] = {
	{0x00,0x00,0x00,0x00,0x00}, /* space */
	{0x00,0x00,0x5F,0x00,0x00}, /* !     */
	{0x00,0x07,0x00,0x07,0x00}, /* "     */
	{0x14,0x7F,0x14,0x7F,0x14}, /* #     */
	{0x24,0x2A,0x7F,0x2A,0x12}, /* $     */
	{0x23,0x13,0x08,0x64,0x62}, /* %     */
	{0x36,0x49,0x55,0x22,0x50}, /* &     */
	{0x00,0x05,0x03,0x00,0x00}, /* '     */
	{0x00,0x1C,0x22,0x41,0x00}, /* (     */
	{0x00,0x41,0x22,0x1C,0x00}, /* )     */
	{0x08,0x2A,0x1C,0x2A,0x08}, /* *     */
	{0x08,0x08,0x3E,0x08,0x08}, /* +     */
	{0x00,0x50,0x30,0x00,0x00}, /* ,     */
	{0x08,0x08,0x08,0x08,0x08}, /* -     */
	{0x00,0x60,0x60,0x00,0x00}, /* .     */
	{0x20,0x10,0x08,0x04,0x02}, /* /     */
	{0x3E,0x51,0x49,0x45,0x3E}, /* 0     */
	{0x00,0x42,0x7F,0x40,0x00}, /* 1     */
	{0x42,0x61,0x51,0x49,0x46}, /* 2     */
	{0x21,0x41,0x45,0x4B,0x31}, /* 3     */
	{0x18,0x14,0x12,0x7F,0x10}, /* 4     */
	{0x27,0x45,0x45,0x45,0x39}, /* 5     */
	{0x3C,0x4A,0x49,0x49,0x30}, /* 6     */
	{0x01,0x71,0x09,0x05,0x03}, /* 7     */
	{0x36,0x49,0x49,0x49,0x36}, /* 8     */
	{0x06,0x49,0x49,0x29,0x1E}, /* 9     */
	{0x00,0x36,0x36,0x00,0x00}, /* :     */
	{0x00,0x56,0x36,0x00,0x00}, /* ;     */
	{0x00,0x08,0x14,0x22,0x41}, /* <     */
	{0x14,0x14,0x14,0x14,0x14}, /* =     */
	{0x41,0x22,0x14,0x08,0x00}, /* >     */
	{0x02,0x01,0x51,0x09,0x06}, /* ?     */
	{0x32,0x49,0x79,0x41,0x3E}, /* @     */
	{0x7E,0x11,0x11,0x11,0x7E}, /* A     */
	{0x7F,0x49,0x49,0x49,0x36}, /* B     */
	{0x3E,0x41,0x41,0x41,0x22}, /* C     */
	{0x7F,0x41,0x41,0x22,0x1C}, /* D     */
	{0x7F,0x49,0x49,0x49,0x41}, /* E     */
	{0x7F,0x09,0x09,0x09,0x01}, /* F     */
	{0x3E,0x41,0x49,0x49,0x7A}, /* G     */
	{0x7F,0x08,0x08,0x08,0x7F}, /* H     */
	{0x00,0x41,0x7F,0x41,0x00}, /* I     */
	{0x20,0x40,0x41,0x3F,0x01}, /* J     */
	{0x7F,0x08,0x14,0x22,0x41}, /* K     */
	{0x7F,0x40,0x40,0x40,0x40}, /* L     */
	{0x7F,0x02,0x04,0x02,0x7F}, /* M     */
	{0x7F,0x04,0x08,0x10,0x7F}, /* N     */
	{0x3E,0x41,0x41,0x41,0x3E}, /* O     */
	{0x7F,0x09,0x09,0x09,0x06}, /* P     */
	{0x3E,0x41,0x51,0x21,0x5E}, /* Q     */
	{0x7F,0x09,0x19,0x29,0x46}, /* R     */
	{0x46,0x49,0x49,0x49,0x31}, /* S     */
	{0x01,0x01,0x7F,0x01,0x01}, /* T     */
	{0x3F,0x40,0x40,0x40,0x3F}, /* U     */
	{0x1F,0x20,0x40,0x20,0x1F}, /* V     */
	{0x3F,0x40,0x38,0x40,0x3F}, /* W     */
	{0x63,0x14,0x08,0x14,0x63}, /* X     */
	{0x07,0x08,0x70,0x08,0x07}, /* Y     */
	{0x61,0x51,0x49,0x45,0x43}, /* Z     */
};

/* Draws one character by rendering each bit of its 5x7 bitmap as a small
 * filled square, scale controls how big each "pixel" of the font is on
 * screen. */
static void lcd_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, int scale)
{
	const uint8_t *bmp;
	int col, row;

	if (c < 32 || c > 90) {
		c = 32;
	}
	bmp = lcd_font5x7[(uint8_t)c - 32];
	for (col = 0; col < 5; col++) {
		for (row = 0; row < 7; row++) {
			uint16_t px = (bmp[col] >> row) & 1 ? color : bg;
			lcd_fill_rect((uint16_t)(x + col * scale),
				      (uint16_t)(y + row * scale),
				      (uint16_t)scale, (uint16_t)scale, px);
		}
	}
}

/* Draws a full string left to right, upper-casing letters (the font only
 * has uppercase glyphs) and stopping early rather than wrapping if the
 * text would run off the right or bottom edge of the screen. */
static void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, int scale)
{
	char c;
	uint16_t step;
	uint16_t char_h;

	if (str == NULL || scale <= 0) {
		return;
	}
	if (y >= SCREEN_H) {
		return;
	}

	step = (uint16_t)((5 + 1) * scale);
	char_h = (uint16_t)(7 * scale);
	if (y + char_h > SCREEN_H) {
		return;
	}

	while (*str) {
		if (x + (uint16_t)(5 * scale) > SCREEN_W) {
			break;
		}
		c = *str;
		if (c >= 'a' && c <= 'z') {
			c = (char)(c - 32);
		}
		lcd_draw_char(x, y, c, color, bg, scale);
		x = (uint16_t)(x + step);
		str++;
	}
}

/* screen_get_next_dispense_string() returns a compact machine-readable
 * string like "9:30 AM TMRW FOR S2", meant for the website's own JS to
 * re-word into "Station 3" etc, the LCD has no such transform layer and
 * was just drawing that raw "S2" token straight to the screen. This scans
 * for any 'S' immediately followed by digits and replaces it with
 * "SLOT <1-indexed number>" in place, leaving everything else (the time,
 * TMRW, FOR, & / , separators for tied slots) untouched. */
static void screen_format_next_dispense(const char *raw, char *out, size_t out_size)
{
	size_t out_len = 0;
	const char *p = raw;

	if (out == NULL || out_size == 0) {
		return;
	}
	if (raw == NULL) {
		out[0] = '\0';
		return;
	}

	while (*p != '\0' && out_len + 1 < out_size) {
		if (*p == 'S' && p[1] >= '0' && p[1] <= '9') {
			int slot_num = 0;
			const char *digits = p + 1;
			int written;

			while (*digits >= '0' && *digits <= '9') {
				slot_num = slot_num * 10 + (*digits - '0');
				digits++;
			}
			written = snprintf(out + out_len, out_size - out_len, "SLOT %d", slot_num + 1);
			if (written < 0 || (size_t)written >= out_size - out_len) {
				out_len = out_size - 1;
				break;
			}
			out_len += (size_t)written;
			p = digits;
		} else {
			out[out_len++] = *p++;
		}
	}
	out[out_len] = '\0';
}

/* ----------------------------------------------------------------------------
 * screen_draw_ui()
 * ----------------------------------------------------------------------------
 * The main status screen, and the busiest render function in this file:
 * title bar with clock and a connection/alert badge, a 3-row table (one
 * per slot) of medication/pills-left/dose/status, and a banner showing
 * when the next scheduled dispense is due. Redrawn on a timer by
 * screen_task() while this screen is active. Alert conditions (a failed
 * dispense, a low pill count) are scanned for up front so the header badge
 * can reflect the worst one at a glance before the per-row detail is drawn.
 * ---------------------------------------------------------------------------- */
static void screen_draw_ui(const pico_bridge_state_t *snapshot)
{
	char buf[32];
	char next_dispense_raw[32];
	char next_dispense[48];
	int slot_index;
	int si;
	time_t now;
	struct tm ti;
	bool alert_fail;
	bool alert_low;
	uint16_t hdr_bg;
	const char *badge_str;
	uint16_t badge_color;

	/* Scan slots for alert conditions before drawing */
	alert_fail = false;
	alert_low  = false;
	for (si = 0; si < UI_VISIBLE_SLOT_COUNT; si++) {
		const pill_slot_state_t *s = &snapshot->slots[si];
		if (!s->is_active) continue;
		if (strcmp(s->last_dispense_result, "fail") == 0 ||
		    strcmp(s->last_dispense_result, "timeout") == 0) alert_fail = true;
		if (s->pills_left >= 0 && s->pills_left < LOW_PILL_THRESHOLD) alert_low = true;
	}

	/* ── Title bar ───────────────────────────────────────────────── */
	hdr_bg = alert_fail ? LCD_RED : LCD_DKGREY;
	lcd_fill_rect(0, 0, SCREEN_W, 44, hdr_bg);
	lcd_draw_string(8, 14, "PILL DISPENSER", LCD_CYAN, hdr_bg, 2);

	/* HH:MM clock (top-right) */
	now = time(NULL);
	if (now > 1700000000 && localtime_r(&now, &ti) != NULL) {
		snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
	} else {
		snprintf(buf, sizeof(buf), "--:--");
	}
	lcd_draw_string(410, 14, buf, LCD_YELLOW, hdr_bg, 2);

	/* Connection / alert badge */
	if (alert_fail) {
		badge_str   = "FAIL!";
		badge_color = LCD_WHITE;
	} else if (!snapshot->connected) {
		badge_str   = "WAIT";
		badge_color = LCD_RED;
	} else if (alert_low) {
		badge_str   = "LOW";
		badge_color = LCD_YELLOW;
	} else {
		badge_str   = "LIVE";
		badge_color = LCD_GREEN;
	}
	lcd_draw_string(310, 14, badge_str, badge_color, hdr_bg, 2);

	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	/* ── Column headers ──────────────────────────────────────────── */
	lcd_fill_rect(0, 46, SCREEN_W, 26, LCD_DKGREY);
	lcd_draw_string(8,   52, "SLOT",   LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_string(76,  52, "MED",    LCD_WHITE,  LCD_DKGREY, 2);
	lcd_draw_string(253, 52, "LEFT",   LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_string(317, 52, "DOSE",   LCD_WHITE,  LCD_DKGREY, 2);
	lcd_draw_string(375, 52, "STATUS", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_hline(0, 72, SCREEN_W, LCD_WHITE);

	/* ── One row per visible slot (0..2) ─────────────────────────── */
	for (slot_index = 0; slot_index < UI_VISIBLE_SLOT_COUNT; slot_index++) {
		const pill_slot_state_t *slot = &snapshot->slots[slot_index];
		uint16_t row_y      = (uint16_t)(74 + slot_index * 62);
		uint16_t slot_col   = LCD_CYAN;
		const char *status_str;
		uint16_t status_color;
		int i;

		lcd_fill_rect(0, row_y, SCREEN_W, 60, LCD_BLACK);

		/* Slot label: S1..S3, 1-indexed to match the header above and the
		 * rest of the LCD; kept short rather than the full word "SLOT"
		 * since this column only has ~68px before the MED column starts. */
		buf[0] = 'S';
		buf[1] = (char)('1' + slot_index);
		buf[2] = '\0';
		lcd_draw_string(8, (uint16_t)(row_y + 20), buf, slot_col, LCD_BLACK, 2);

		/* Medication name, uppercase, capped at 16 chars */
		if (slot->has_data) {
			char med[17];
			strncpy(med, slot->medication_name, 16);
			med[16] = '\0';
			for (i = 0; med[i]; i++) {
				if (med[i] >= 'a' && med[i] <= 'z') {
					med[i] = (char)(med[i] - 32);
				}
			}
			lcd_draw_string(76, (uint16_t)(row_y + 20), med, LCD_WHITE, LCD_BLACK, 2);
		} else {
			lcd_draw_string(76, (uint16_t)(row_y + 20), "NO DATA", LCD_GREY, LCD_BLACK, 2);
		}

		/* Pills left, color-coded: red at 0, yellow near empty */
		{
			uint16_t pill_color = LCD_WHITE;
			if (slot->pills_left >= 0) {
				snprintf(buf, sizeof(buf), "%d", slot->pills_left);
				if (slot->pills_left == 0) {
					pill_color = LCD_RED;
				} else if (slot->pills_left < LOW_PILL_THRESHOLD) {
					pill_color = LCD_YELLOW;
				}
			} else {
				snprintf(buf, sizeof(buf), "--");
			}
			lcd_draw_string(253, (uint16_t)(row_y + 20), buf, pill_color, LCD_BLACK, 2);
		}

		/* Pills per dose */
		if (slot->pills_per_dose > 0) {
			snprintf(buf, sizeof(buf), "%dX", slot->pills_per_dose);
		} else {
			snprintf(buf, sizeof(buf), "--");
		}
		lcd_draw_string(317, (uint16_t)(row_y + 20), buf, LCD_WHITE, LCD_BLACK, 2);

		/* Dispense status */
		if (!slot->is_active) {
			status_str   = "INACTIVE";
			status_color = LCD_GREY;
		} else if (strcmp(slot->last_dispense_result, "ok") == 0) {
			status_str   = "TAKEN";
			status_color = LCD_GREEN;
		} else if (strcmp(slot->last_dispense_result, "fail") == 0 ||
		           strcmp(slot->last_dispense_result, "timeout") == 0) {
			status_str   = slot->last_dispense_result[0] == 't' ? "NO-ACK" : "MISSED";
			status_color = LCD_RED;
		} else if (strcmp(slot->last_dispense_result, "pending") == 0) {
			status_str   = "PENDING";
			status_color = LCD_YELLOW;
		} else {
			status_str   = "WAITING";
			status_color = LCD_YELLOW;
		}
		lcd_draw_string(375, (uint16_t)(row_y + 20), status_str, status_color, LCD_BLACK, 2);

		lcd_draw_hline(0, (uint16_t)(row_y + 60), SCREEN_W, LCD_DKGREY);
	}

	/* Next dispense banner uses free space from reducing rows to 0..2. */
	screen_get_next_dispense_string(snapshot, next_dispense_raw, sizeof(next_dispense_raw));
	screen_format_next_dispense(next_dispense_raw, next_dispense, sizeof(next_dispense));
	lcd_fill_rect(0, 262, SCREEN_W, 58, LCD_DKGREY);
	lcd_draw_hline(0, 262, SCREEN_W, LCD_WHITE);
	lcd_draw_string(8, 278, "NEXT DISPENSE AT", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_string(210, 278, next_dispense, LCD_WHITE, LCD_DKGREY, 2);
}

/* The strip along the bottom of every menu screen reminding the user what
 * each button does on that particular screen, wording changes per screen
 * since not every screen supports the same actions. */
static void screen_draw_help_bar(const char *text)
{
	lcd_fill_rect(0, 300, SCREEN_W, 20, LCD_DKGREY);
	lcd_draw_hline(0, 300, SCREEN_W, LCD_WHITE);
	lcd_draw_string(8, 306, text, LCD_WHITE, LCD_DKGREY, 1);
}

/* Small "SLOT X OF 3" label in the title bar's top-right corner, right-
 * aligned so it doesn't collide with the screen title on the left. */
static void screen_draw_slot_position_label(int slot_index)
{
	char pos[32];
	int x;

	snprintf(pos, sizeof(pos), "SLOT %d OF %d", slot_index + 1, UI_VISIBLE_SLOT_COUNT);
	x = (int)SCREEN_W - 8 - ((int)strlen(pos) * 8);
	if (x < 180) {
		x = 180;
	}
	lcd_draw_string((uint16_t)x, 14, pos, LCD_YELLOW, LCD_DKGREY, 1);
}

/* ── Menu draw functions ───────────────────────────────────────────────────── */

/* Scrollable list of the 3 slots, highlighting whichever one the cursor is
 * on. Selecting one here moves into screen_draw_action_menu() for that
 * slot. */
static void screen_draw_slot_menu(const pico_bridge_state_t *snap, int cursor)
{
	char buf[20];
	int i;
	int j;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "SLOTS", LCD_CYAN, LCD_DKGREY, 2);
	screen_draw_slot_position_label(cursor);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (i = 0; i < UI_VISIBLE_SLOT_COUNT; i++) {
		uint16_t y = (uint16_t)(46 + i * 54);
		bool sel = (i == cursor);
		uint16_t bg = sel ? LCD_CYAN : LCD_BLACK;
		uint16_t fg = sel ? LCD_BLACK : LCD_WHITE;
		const pill_slot_state_t *s = &snap->slots[i];
		char med[17];

		lcd_fill_rect(0, y, SCREEN_W, 52, bg);
		if (sel) {
			lcd_fill_rect(0, y, SCREEN_W, 2, LCD_WHITE);
			lcd_fill_rect(0, (uint16_t)(y + 50), SCREEN_W, 2, LCD_WHITE);
			lcd_fill_rect(0, y, 3, 52, LCD_WHITE);
			lcd_fill_rect((uint16_t)(SCREEN_W - 3), y, 3, 52, LCD_WHITE);
			lcd_draw_string(2, (uint16_t)(y + 18), ">", fg, bg, 2);
		}
		/* "S1:".."S3:", the colon keeps this from visually running into the
		 * medication name right next to it (e.g. "S1ASPIRIN"). */
		buf[0] = 'S'; buf[1] = (char)('1' + i); buf[2] = ':'; buf[3] = '\0';
		lcd_draw_string(8, (uint16_t)(y + 18), buf, fg, bg, 2);

		if (s->has_data) {
			strncpy(med, s->medication_name, 16);
			med[16] = '\0';
			for (j = 0; med[j]; j++) {
				if (med[j] >= 'a' && med[j] <= 'z') {
					med[j] = (char)(med[j] - 32);
				}
			}
			lcd_draw_string(60, (uint16_t)(y + 18), med, fg, bg, 2);
		} else {
			lcd_draw_string(60, (uint16_t)(y + 18), "NO DATA", fg, bg, 2);
		}
		lcd_draw_hline(0, (uint16_t)(y + 52), SCREEN_W, LCD_DKGREY);
	}

	screen_draw_help_bar("UP/DOWN=MOVE   OK=SELECT   HOLD OK=HOME");
}

/* The 3-option menu (Dispense Now / Edit Profile / Back) shown after a slot
 * is picked from the slot menu. */
static void screen_draw_action_menu(int slot, int cursor)
{
	static const char *const actions[3]   = { "DISPENSE NOW", "EDIT PROFILE", "BACK" };
	static const uint16_t    act_fg[3]    = { LCD_GREEN, LCD_CYAN, LCD_CYAN };
	char title[32];
	int i;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	snprintf(title, sizeof(title), "SLOT %d", slot + 1);
	lcd_draw_string(8, 14, title, LCD_YELLOW, LCD_DKGREY, 2);
	screen_draw_slot_position_label(slot);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (i = 0; i < 3; i++) {
		uint16_t y = (uint16_t)(58 + i * 84);
		bool sel = (i == cursor);
		uint16_t bg = sel ? act_fg[i] : LCD_DKGREY;
		uint16_t fg = sel ? LCD_BLACK : act_fg[i];

		lcd_fill_rect(16, y, SCREEN_W - 32, 64, bg);
		lcd_draw_string(32, (uint16_t)(y + 24), actions[i], fg, bg, 2);
	}

	screen_draw_help_bar("UP/DOWN=MOVE   OK=SELECT   HOLD OK=BACK");
}

/* Yes/No confirmation shown before a manual dispense actually fires, a
 * deliberate extra step so a stray button press can't dispense pills. */
static void screen_draw_dispense_confirm(int slot, int cursor)
{
	bool yes_sel = (cursor == 0);
	bool no_sel = (cursor == 1);

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "CONFIRM DISPENSE", LCD_YELLOW, LCD_DKGREY, 2);
	screen_draw_slot_position_label(slot);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	lcd_draw_string(36, 86, "DISPENSE PILLS NOW?", LCD_WHITE, LCD_BLACK, 2);

	lcd_fill_rect(36, 156, 188, 74, yes_sel ? LCD_GREEN : LCD_DKGREY);
	lcd_draw_string(72, 184, "YES", yes_sel ? LCD_BLACK : LCD_GREEN, yes_sel ? LCD_GREEN : LCD_DKGREY, 2);

	lcd_fill_rect(256, 156, 188, 74, no_sel ? LCD_RED : LCD_DKGREY);
	lcd_draw_string(308, 184, "NO", no_sel ? LCD_BLACK : LCD_RED, no_sel ? LCD_RED : LCD_DKGREY, 2);

	screen_draw_help_bar("UP/DOWN=CHOOSE   OK=CONFIRM   HOLD OK=CANCEL");
}

/* Generic centered message card (title bar + boxed message), the shared
 * layout used by every waiting/success/failure notice screen below, each
 * of those is just this with different text and accent color. */
static void screen_draw_feedback_card(const char *title, const char *message, uint16_t accent)
{
	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, title, accent, LCD_DKGREY, 2);
	lcd_draw_hline(0, 44, SCREEN_W, accent);

	lcd_fill_rect(40, 94, SCREEN_W - 80, 132, LCD_DKGREY);
	lcd_fill_rect(40, 94, SCREEN_W - 80, 4, accent);
	lcd_fill_rect(40, 222, SCREEN_W - 80, 4, accent);
	lcd_draw_string(84, 146, message, LCD_WHITE, LCD_DKGREY, 2);

	screen_draw_help_bar("PLEASE WAIT...");
}

/* Shown the moment a dispense command is sent, while waiting on the
 * Pico's ACK. */
static void screen_draw_dispense_waiting(void)
{
	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "DISPENSE", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	lcd_fill_rect(40, 94, SCREEN_W - 80, 132, LCD_DKGREY);
	lcd_fill_rect(40, 94, SCREEN_W - 80, 4, LCD_CYAN);
	lcd_fill_rect(40, 222, SCREEN_W - 80, 4, LCD_CYAN);
	lcd_draw_string(84, 136, "DISPENSING PILLS...", LCD_WHITE, LCD_DKGREY, 2);
	lcd_draw_string(76, 178, "WAITING FOR CONTROLLER", LCD_YELLOW, LCD_DKGREY, 1);

	screen_draw_help_bar("WAITING FOR RESULT...");
}

/* Shown after a successful dispense, while still waiting on the drawer to
 * be opened for pickup confirmation. Pressing OK dismisses it early
 * without needing to actually open the drawer right that second. */
static void screen_draw_dispense_success_wait(void)
{
	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "DISPENSE", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	lcd_fill_rect(40, 94, SCREEN_W - 80, 132, LCD_DKGREY);
	lcd_fill_rect(40, 94, SCREEN_W - 80, 4, LCD_CYAN);
	lcd_fill_rect(40, 222, SCREEN_W - 80, 4, LCD_CYAN);
	lcd_draw_string(66, 130, "PILLS DISPENSED", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_string(66, 164, "OPEN TRAY :)", LCD_WHITE, LCD_DKGREY, 2);
	lcd_draw_string(66, 198, "OR PRESS OK TO CONTINUE", LCD_YELLOW, LCD_DKGREY, 1);

	screen_draw_help_bar("OPEN TRAY OR PRESS OK");
}

/* Shown when a dispense fails or times out, prompting the user to check
 * for a physical jam on the affected slot. */
static void screen_draw_dispense_failure_wait(int slot)
{
	char msg[48];

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "DISPENSE", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	lcd_fill_rect(40, 94, SCREEN_W - 80, 132, LCD_DKGREY);
	lcd_fill_rect(40, 94, SCREEN_W - 80, 4, LCD_CYAN);
	lcd_fill_rect(40, 222, SCREEN_W - 80, 4, LCD_CYAN);
	lcd_draw_string(52, 126, "PILLS FAILED", LCD_CYAN, LCD_DKGREY, 2);
	snprintf(msg, sizeof(msg), "CHECK FOR JAM ON SLOT %d", slot + 1);
	lcd_draw_string(52, 160, msg, LCD_WHITE, LCD_DKGREY, 2);
	lcd_draw_string(52, 194, "PRESS OK TO CONTINUE", LCD_YELLOW, LCD_DKGREY, 2);

	screen_draw_help_bar("PRESS OK TO CONTINUE");
}

/* Parses a "HH:MM,HH:MM" schedule string into up to LCD_SCHED_MAX_TIMES
 * entries. Unparsed slots are left inactive at 00:00 so the editor always
 * has a full, well-defined set of rows to show. */
static void lcd_parse_schedule_into_edit(const char *sched_str, edit_sched_entry_t *out)
{
	char copy[40];
	char *saveptr = NULL;
	char *token;
	int count = 0;
	int i;

	for (i = 0; i < LCD_SCHED_MAX_TIMES; i++) {
		out[i].hour = 0;
		out[i].minute = 0;
		out[i].active = false;
	}

	if (sched_str == NULL || sched_str[0] == '\0' || strcmp(sched_str, "none") == 0) {
		return;
	}

	bridge_copy_string(copy, sizeof(copy), sched_str);
	token = strtok_r(copy, ",", &saveptr);
	while (token != NULL && count < LCD_SCHED_MAX_TIMES) {
		int h = 0, m = 0;

		if (sscanf(token, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60) {
			out[count].hour = (uint8_t)h;
			out[count].minute = (uint8_t)m;
			out[count].active = true;
			count++;
		}
		token = strtok_r(NULL, ",", &saveptr);
	}
}

/* Builds a compact one-line summary of the active schedule entries, e.g.
 * "08:00, 14:00" or "NOT SET" if none are active. Used by the field list. */
static void schedule_summary(const ui_state_t *st, char *out, size_t out_size)
{
	size_t used = 0;
	int i;
	bool any = false;

	out[0] = '\0';
	for (i = 0; i < LCD_SCHED_MAX_TIMES; i++) {
		int written;

		if (!st->edit_sched[i].active) {
			continue;
		}
		written = snprintf(out + used, out_size - used, "%s%02d:%02d",
				    any ? "," : "", (int)st->edit_sched[i].hour, (int)st->edit_sched[i].minute);
		if (written < 0 || (size_t)written >= out_size - used) {
			break;
		}
		used += (size_t)written;
		any = true;
	}
	if (!any) {
		snprintf(out, out_size, "NOT SET");
	}
}

/* The per-slot profile editor: a vertical list of fields (name, total
 * pills, dose size, schedule, then a final confirm/save step). Pressing OK
 * on a field either edits it in place (numeric fields) or opens a
 * dedicated sub-screen (name uses the on-screen keyboard, schedule uses
 * its own digit editor below). */
static void screen_draw_edit(const ui_state_t *st)
{
	static const char *const field_labels[EDIT_FIELD_COUNT] = {
		"MEDICATION NAME", "TOTAL PILLS", "DOSE PER DISPENSE", "SCHEDULE", "CONFIRM?",
	};
	char val[40];
	char hint[42];
	char title[32];
	int i;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	snprintf(title, sizeof(title), "EDIT SLOT %d", st->edit_slot + 1);
	lcd_draw_string(8, 14, title, LCD_YELLOW, LCD_DKGREY, 2);
	screen_draw_slot_position_label(st->edit_slot);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (i = 0; i < EDIT_FIELD_COUNT; i++) {
		uint16_t y = (uint16_t)(52 + i * 44);
		bool sel = (i == st->cursor);

		lcd_draw_string(8, y, field_labels[i], sel ? LCD_CYAN : LCD_GREY, LCD_BLACK, 1);

		if (i == 0) {
			snprintf(val, sizeof(val), "%s", st->edit_name[0] ? st->edit_name : "(BLANK)");
		} else if (i == 1) {
			snprintf(val, sizeof(val), "%d", st->edit_total);
		} else if (i == 2) {
			snprintf(val, sizeof(val), "%d", st->edit_dose);
		} else if (i == 3) {
			schedule_summary(st, val, sizeof(val));
		} else {
			snprintf(val, sizeof(val), "PRESS OK");
		}

		lcd_draw_string(8, (uint16_t)(y + 10), val, sel ? LCD_WHITE : LCD_GREY, LCD_BLACK, 2);
		if (sel) {
			lcd_draw_hline(8, (uint16_t)(y + 26), 240, LCD_CYAN);
		}
	}

	if (st->cursor == 0) {
		snprintf(hint, sizeof(hint), "OK TO TYPE THE MEDICATION NAME");
	} else if (st->cursor == 1) {
		snprintf(hint, sizeof(hint), "SET TOTAL PILLS IN THIS SLOT");
	} else if (st->cursor == 2) {
		snprintf(hint, sizeof(hint), "SET PILLS TAKEN EACH TIME");
	} else if (st->cursor == 3) {
		snprintf(hint, sizeof(hint), "OK TO SET THE DAILY TIME PLAN");
	} else {
		snprintf(hint, sizeof(hint), "PRESS OK TO SAVE PROFILE");
	}
	lcd_draw_string(8, 274, hint, LCD_GREY, LCD_BLACK, 1);

	screen_draw_help_bar("UP/DOWN CHANGE  OK NEXT/OPEN  HOLD OK=CANCEL");
}

/* On-screen keyboard for the medication name. Arrow keys hover between
 * tiles (kb_row/kb_col), OK types the highlighted one; DEL backspaces and
 * DONE (or holding BACK) finishes and advances to the next field. */
static void screen_draw_edit_name(const ui_state_t *st)
{
	int r;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "MEDICATION NAME", LCD_YELLOW, LCD_DKGREY, 2);
	screen_draw_slot_position_label(st->edit_slot);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	/* Typed-so-far preview */
	lcd_fill_rect(0, 48, SCREEN_W, 28, LCD_BLACK);
	lcd_draw_string(8, 54, st->edit_name[0] ? st->edit_name : "(BLANK)", LCD_WHITE, LCD_BLACK, 2);
	lcd_draw_hline(0, 78, SCREEN_W, LCD_DKGREY);

	/* Keyboard grid: hover with arrows, OK types the highlighted tile. */
	for (r = 0; r < KB_ROWS; r++) {
		int row_len = kb_row_len[r];
		int cell_w = (SCREEN_W - 16) / row_len;
		uint16_t y = (uint16_t)(84 + r * 42);
		int c;

		for (c = 0; c < row_len; c++) {
			bool sel = (r == st->kb_row && c == st->kb_col);
			uint16_t x = (uint16_t)(8 + c * cell_w);
			uint16_t bg = sel ? LCD_CYAN : LCD_DKGREY;
			uint16_t fg = sel ? LCD_BLACK : LCD_WHITE;

			lcd_fill_rect(x, y, (uint16_t)(cell_w - 2), 36, bg);
			lcd_draw_string((uint16_t)(x + 4), (uint16_t)(y + 10), kb_rows[r][c], fg, bg, 2);
		}
	}

	screen_draw_help_bar("ARROWS=MOVE  OK=TYPE  HOLD OK=DONE");
}

/* Digit-by-digit schedule editor. Each of LCD_SCHED_MAX_TIMES entries shows
 * as "TIME n: [ON/OFF] HH:MM"; LEFT/RIGHT move across the 4 time digits plus
 * the on/off toggle for every entry, UP/DOWN adjust whichever is selected,
 * and OK toggles that entry on/off from anywhere within it. */
static void screen_draw_edit_schedule(const ui_state_t *st)
{
	int entry;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "DAILY SCHEDULE", LCD_YELLOW, LCD_DKGREY, 2);
	screen_draw_slot_position_label(st->edit_slot);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (entry = 0; entry < LCD_SCHED_MAX_TIMES; entry++) {
		const edit_sched_entry_t *e = &st->edit_sched[entry];
		int base_cursor = entry * LCD_SCHED_FIELDS_PER_ENTRY;
		uint16_t y = (uint16_t)(56 + entry * 58);
		bool entry_sel = (st->edit_sched_cursor / LCD_SCHED_FIELDS_PER_ENTRY) == entry;
		char label[8];
		uint16_t digit_x[4];
		char digit_ch[4];
		int i;

		lcd_fill_rect(0, y, SCREEN_W, 54, entry_sel ? LCD_DKGREY : LCD_BLACK);

		snprintf(label, sizeof(label), "TIME %d", entry + 1);
		lcd_draw_string(8, (uint16_t)(y + 18), label, LCD_GREY, entry_sel ? LCD_DKGREY : LCD_BLACK, 1);

		{
			bool toggle_sel = (st->edit_sched_cursor == base_cursor + 4);
			uint16_t bg = toggle_sel ? LCD_CYAN : (e->active ? LCD_GREEN : LCD_DKGREY);
			uint16_t fg = toggle_sel ? LCD_BLACK : (e->active ? LCD_BLACK : LCD_GREY);

			lcd_fill_rect(70, (uint16_t)(y + 6), 60, 26, bg);
			lcd_draw_string(80, (uint16_t)(y + 12), e->active ? "ON" : "OFF", fg, bg, 1);
		}

		digit_ch[0] = (char)('0' + (e->hour / 10));
		digit_ch[1] = (char)('0' + (e->hour % 10));
		digit_ch[2] = (char)('0' + (e->minute / 10));
		digit_ch[3] = (char)('0' + (e->minute % 10));
		digit_x[0] = 160; digit_x[1] = 178; digit_x[2] = 206; digit_x[3] = 224;

		lcd_draw_string(196, (uint16_t)(y + 10), ":", LCD_WHITE, entry_sel ? LCD_DKGREY : LCD_BLACK, 2);

		for (i = 0; i < 4; i++) {
			bool digit_sel = (st->edit_sched_cursor == base_cursor + i);
			char ch[2] = { digit_ch[i], '\0' };
			uint16_t bg = digit_sel ? LCD_CYAN : (entry_sel ? LCD_DKGREY : LCD_BLACK);
			uint16_t fg = digit_sel ? LCD_BLACK : LCD_WHITE;

			if (digit_sel) {
				lcd_fill_rect(digit_x[i], (uint16_t)(y + 6), 16, 26, bg);
			}
			lcd_draw_string(digit_x[i], (uint16_t)(y + 10), ch, fg, bg, 2);
		}
	}

	lcd_draw_string(8, 288, "OK TOGGLES THIS TIME ON/OFF", LCD_GREY, LCD_BLACK, 1);
	screen_draw_help_bar("LEFT/RIGHT MOVE  UP/DOWN CHANGE  HOLD OK=DONE");
}

/* Trims trailing blanks, then returns to the field list with the cursor
 * advanced past NAME so OK/DONE/hold-BACK always move you forward instead
 * of bouncing back into the same field. */
static void lcd_finish_name_edit(void)
{
	while (ui_state.edit_name_len > 0 &&
	       ui_state.edit_name[ui_state.edit_name_len - 1] == ' ') {
		ui_state.edit_name_len--;
	}
	ui_state.edit_name[ui_state.edit_name_len] = '\0';
	ui_state.screen = UI_EDIT_FIELD;
	ui_state.cursor = 1;
}

/* ── Button polling task ───────────────────────────────────────────────────── */

/* ----------------------------------------------------------------------------
 * button_task()
 * ----------------------------------------------------------------------------
 * Polls the 5 button GPIOs (active-low) on a short interval, debounces
 * each one independently, and turns clean press/release edges into
 * btn_event_t values pushed onto btn_queue for screen_task() to consume.
 * The OK button is handled specially: a short press posts BTN_EVT_SELECT,
 * but if it's held past BTN_LONG_MS it posts BTN_EVT_BACK instead, giving
 * the 5-way pad a "back/home" action without a dedicated 6th button. Runs
 * forever once started.
 * ---------------------------------------------------------------------------- */
static void button_task(void *arg)
{
	bool prev_left = true;
	bool prev_right = true;
	bool prev_up  = true;
	bool prev_dn  = true;
	bool prev_sel = true;
	TickType_t sel_press_tick = 0;
	TickType_t last_left_evt_tick = 0;
	TickType_t last_right_evt_tick = 0;
	TickType_t last_up_evt_tick = 0;
	TickType_t last_dn_evt_tick = 0;
	TickType_t last_sel_evt_tick = 0;
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << BTN_LEFT_PIN) | (1ULL << BTN_RIGHT_PIN) |
				(1ULL << BTN_UP_PIN) | (1ULL << BTN_DOWN_PIN) | (1ULL << BTN_OK_PIN),
		.mode         = GPIO_MODE_INPUT,
		.pull_up_en   = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type    = GPIO_INTR_DISABLE,
	};

	(void)arg;
	gpio_config(&cfg);

	while (1) {
		TickType_t now_tick = xTaskGetTickCount();
		bool cur_left  = gpio_get_level(BTN_LEFT_PIN) != 0;
		bool cur_right = gpio_get_level(BTN_RIGHT_PIN) != 0;
		bool cur_up  = gpio_get_level(BTN_UP_PIN)  != 0;
		bool cur_dn  = gpio_get_level(BTN_DOWN_PIN) != 0;
		bool cur_sel = gpio_get_level(BTN_OK_PIN)  != 0;

		if (!cur_left && prev_left &&
		    (now_tick - last_left_evt_tick) >= pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
			ESP_LOGI(TAG, "BTN: LEFT pressed");
			btn_event_t e = BTN_EVT_LEFT;
			xQueueSend(btn_queue, &e, 0);
			last_left_evt_tick = now_tick;
		}
		if (!cur_right && prev_right &&
		    (now_tick - last_right_evt_tick) >= pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
			ESP_LOGI(TAG, "BTN: RIGHT pressed");
			btn_event_t e = BTN_EVT_RIGHT;
			xQueueSend(btn_queue, &e, 0);
			last_right_evt_tick = now_tick;
		}
		if (!cur_up && prev_up &&
		    (now_tick - last_up_evt_tick) >= pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
			ESP_LOGI(TAG, "BTN: UP pressed");
			btn_event_t e = BTN_EVT_UP;
			xQueueSend(btn_queue, &e, 0);
			last_up_evt_tick = now_tick;
		}
		if (!cur_dn && prev_dn &&
		    (now_tick - last_dn_evt_tick) >= pdMS_TO_TICKS(BTN_DOWN_DEBOUNCE_MS)) {
			ESP_LOGI(TAG, "BTN: DOWN pressed");
			btn_event_t e = BTN_EVT_DOWN;
			xQueueSend(btn_queue, &e, 0);
			last_dn_evt_tick = now_tick;
		}
		if (!cur_sel && prev_sel) {
			sel_press_tick = xTaskGetTickCount();
		}
		if (cur_sel && !prev_sel &&
		    (now_tick - last_sel_evt_tick) >= pdMS_TO_TICKS(BTN_DEBOUNCE_MS)) {
			TickType_t held = xTaskGetTickCount() - sel_press_tick;
			btn_event_t e = (held >= pdMS_TO_TICKS(BTN_LONG_MS)) ? BTN_EVT_BACK : BTN_EVT_SELECT;
			if (e == BTN_EVT_BACK)
				ESP_LOGI(TAG, "BTN: OK long-press (BACK)");
			else
				ESP_LOGI(TAG, "BTN: OK short-press (SELECT)");
			xQueueSend(btn_queue, &e, 0);
			last_sel_evt_tick = now_tick;
		}

		prev_left = cur_left;
		prev_right = cur_right;
		prev_up  = cur_up;
		prev_dn  = cur_dn;
		prev_sel = cur_sel;
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

/* ----------------------------------------------------------------------------
 * screen_task()
 * ----------------------------------------------------------------------------
 * The other half of the UI: owns ui_state (the only task that ever writes
 * to it) and drives the whole menu system. Each loop iteration:
 *
 *   1. Drains btn_queue and applies each event to ui_state according to
 *      whichever screen is currently active, moving the cursor, entering
 *      a sub-screen, saving an edit, or sending a dispense/profile update
 *      over the UART bridge.
 *   2. Falls back to the status screen after UI_INACTIVITY_TIMEOUT_MS of
 *      no input, so the device doesn't get stuck in a menu indefinitely.
 *   3. Takes a snapshot of bridge_state under its mutex and re-renders
 *      whichever screen is active, calling the matching screen_draw_*
 *      function from earlier in this file.
 *
 * Runs forever once started.
 * ---------------------------------------------------------------------------- */
static void screen_task(void *arg)
{
	pico_bridge_state_t *snapshot;
	TickType_t last_input_tick;
	ui_screen_t last_drawn_screen = (ui_screen_t)(-1);
	int last_drawn_failure_slot = -1;
	/* Lets the status screen skip repainting when nothing it shows has
	 * actually changed, instead of wiping and redrawing everything on
	 * every idle tick (was causing a visible flicker every ~2s). */
	static pico_bridge_state_t last_status_snapshot;
	bool have_last_status_snapshot = false;
	int last_status_minute = -1;

	(void)arg;

	snapshot = malloc(sizeof(*snapshot));
	if (snapshot == NULL) {
		ESP_LOGE(TAG, "screen_task: malloc failed");
		vTaskDelete(NULL);
		return;
	}

	ui_state.screen = UI_STATUS;
	ui_state.cursor = 0;
	ui_state.dispense_slot = -1;
	ui_state.feedback_deadline_tick = 0;
	last_input_tick = xTaskGetTickCount();
	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	memset(snapshot, 0, sizeof(*snapshot));

	while (1) {
		TickType_t wait = (ui_state.screen == UI_STATUS)
		                  ? pdMS_TO_TICKS(2000)
		                  : pdMS_TO_TICKS(100);
		btn_event_t evt;
		bool got_event = xQueueReceive(btn_queue, &evt, wait) == pdTRUE;

		if (got_event) {
			last_input_tick = xTaskGetTickCount();
			switch (ui_state.screen) {
			case UI_STATUS:
				ui_state.screen = UI_SLOT_MENU;
				ui_state.cursor = 0;
				led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.cursor);
				break;

			case UI_SLOT_MENU:
				if (evt == BTN_EVT_UP) {
					ui_state.cursor = (ui_state.cursor + UI_VISIBLE_SLOT_COUNT - 1) % UI_VISIBLE_SLOT_COUNT;
					led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.cursor);
				} else if (evt == BTN_EVT_DOWN) {
					ui_state.cursor = (ui_state.cursor + 1) % UI_VISIBLE_SLOT_COUNT;
					led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.cursor);
				} else if (evt == BTN_EVT_BACK) {
					ui_state.screen = UI_STATUS;
					led_enqueue_event(LED_EVENT_SLOT_CLEAR, -1);
				} else if (evt == BTN_EVT_SELECT) {
					ui_state.edit_slot = ui_state.cursor;
					ui_state.cursor    = 0;
					ui_state.screen    = UI_ACTION_MENU;
					led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.edit_slot);
				}
				break;

			case UI_ACTION_MENU:
				if (evt == BTN_EVT_UP) {
					ui_state.cursor = (ui_state.cursor + 2) % 3;
				} else if (evt == BTN_EVT_DOWN) {
					ui_state.cursor = (ui_state.cursor + 1) % 3;
				} else if (evt == BTN_EVT_BACK) {
					ui_state.screen = UI_SLOT_MENU;
					ui_state.cursor = ui_state.edit_slot;
					led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.cursor);
				} else if (evt == BTN_EVT_SELECT) {
					if (ui_state.cursor == 0) {
						ui_state.screen = UI_CONFIRM_DISPENSE;
						ui_state.cursor = 0;
					} else if (ui_state.cursor == 1) {
						/* Edit Profile: seed edit buffer from current slot */
						if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
							const pill_slot_state_t *s = &bridge_state.slots[ui_state.edit_slot];
							ui_state.edit_total = (s->total_pills   > 0) ? s->total_pills   : 20;
							ui_state.edit_dose  = (s->pills_per_dose > 0) ? s->pills_per_dose : 1;
							bridge_copy_string(ui_state.edit_name, sizeof(ui_state.edit_name),
									   (s->has_data && s->medication_name[0]) ? s->medication_name : "");
							ui_state.edit_name_len = (int)strlen(ui_state.edit_name);
							ui_state.kb_row = 0;
							ui_state.kb_col = 0;
							lcd_parse_schedule_into_edit(s->schedule, ui_state.edit_sched);
							ui_state.edit_sched_cursor = 0;
							xSemaphoreGive(bridge_state_mutex);
						}
						ui_state.cursor = 0;
						ui_state.screen = UI_EDIT_FIELD;
						audio_enqueue_event(AUDIO_EVENT_EDIT_BEGIN);
						led_enqueue_event(LED_EVENT_SLOT_CLEAR, -1);
						led_enqueue_event(LED_EVENT_EDIT_BEGIN, ui_state.edit_slot);
					} else {
						/* Back */
						ui_state.screen = UI_SLOT_MENU;
						ui_state.cursor = ui_state.edit_slot;
						led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.cursor);
					}
				}
				break;

			case UI_CONFIRM_DISPENSE:
				if (evt == BTN_EVT_UP || evt == BTN_EVT_DOWN) {
					ui_state.cursor = (ui_state.cursor == 0) ? 1 : 0;
				} else if (evt == BTN_EVT_BACK) {
					ui_state.screen = UI_ACTION_MENU;
					ui_state.cursor = 0;
				} else if (evt == BTN_EVT_SELECT) {
					if (ui_state.cursor == 0) {
						/* Yes -> Dispense now */
						if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
							bridge_send_dispense_for_slot(ui_state.edit_slot);
							bridge_state.active_profile_slot    = ui_state.edit_slot;
							bridge_state.awaiting_dispense_ack  = true;
							bridge_state.active_dispense_is_scheduled = false;
							bridge_state.awaiting_drawer_open   = false;
							bridge_state.drawer_open_slot       = -1;
							bridge_state.dispense_ack_deadline_us =
								esp_timer_get_time() + DISPENSE_ACK_TIMEOUT_US;
							bridge_copy_string(bridge_state.last_ack_action,
							                   sizeof(bridge_state.last_ack_action), "DISPENSE");
							bridge_copy_string(bridge_state.last_ack_result,
							                   sizeof(bridge_state.last_ack_result), "pending");
							bridge_state_save_to_nvs(&bridge_state);
							xSemaphoreGive(bridge_state_mutex);
						}
						ui_state.screen = UI_NOTICE_DISPENSING;
						ui_state.dispense_slot = ui_state.edit_slot;
						ui_state.feedback_deadline_tick = 0;
						led_enqueue_event(LED_EVENT_SLOT_SELECT, ui_state.edit_slot);
					} else {
						/* No -> back to action menu */
						ui_state.screen = UI_ACTION_MENU;
						ui_state.cursor = 0;
					}
				}
				break;

			case UI_NOTICE_DISPENSING:
				/* Wait screen is driven by ACK/drawer status updates below. */
				break;

			case UI_NOTICE_DISPENSE_SUCCESS:
				if (evt == BTN_EVT_SELECT) {
					led_enqueue_event(LED_EVENT_SLOT_CLEAR, -1);
					ui_state.screen = UI_STATUS;
					ui_state.cursor = 0;
					ui_state.dispense_slot = -1;
				}
				break;

			case UI_NOTICE_DISPENSE_FAILURE:
				if (evt == BTN_EVT_SELECT) {
					led_enqueue_event(LED_EVENT_SLOT_CLEAR, -1);
					ui_state.screen = UI_STATUS;
					ui_state.cursor = 0;
					ui_state.dispense_slot = -1;
				}
				break;

			case UI_EDIT_FIELD:
				if (evt == BTN_EVT_BACK) {
					led_enqueue_event(LED_EVENT_EDIT_END, ui_state.edit_slot);
					ui_state.screen = UI_ACTION_MENU;
					ui_state.cursor = 1;
					break;
				}
				if (evt == BTN_EVT_SELECT) {
					if (ui_state.cursor == 0) {
						ui_state.screen = UI_EDIT_NAME;
					} else if (ui_state.cursor == 3) {
						ui_state.screen = UI_EDIT_SCHEDULE;
					} else if (ui_state.cursor < EDIT_FIELD_COUNT - 1) {
						ui_state.cursor++;
					} else {
						/* Confirm: save and send to Pico */
						if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
							pill_slot_state_t *s = &bridge_state.slots[ui_state.edit_slot];
							char sched_buf[40];

							s->slot_number     = ui_state.edit_slot;
							s->total_pills     = ui_state.edit_total;
							s->pills_left      = ui_state.edit_total;
							s->pills_per_dose  = ui_state.edit_dose;
							s->doses_remaining = (ui_state.edit_dose > 0)
							                     ? (ui_state.edit_total / ui_state.edit_dose)
							                     : -1;
							s->is_active = true;
							s->has_data  = true;
							bridge_copy_string(s->medication_name, sizeof(s->medication_name),
							                   ui_state.edit_name[0] ? ui_state.edit_name : "Unnamed");
							schedule_summary(&ui_state, sched_buf, sizeof(sched_buf));
							bridge_copy_string(s->schedule, sizeof(s->schedule),
							                   strcmp(sched_buf, "NOT SET") == 0 ? "none" : sched_buf);
							bridge_copy_string(s->notes, sizeof(s->notes),
							                   "Profile saved via display.");
							bridge_state.active_profile_slot = ui_state.edit_slot;
							bridge_state_save_to_nvs(&bridge_state);
							bridge_send_load_profile_for_slot(s);
							xSemaphoreGive(bridge_state_mutex);
						}
						led_enqueue_event(LED_EVENT_EDIT_END, ui_state.edit_slot);
						ui_state.screen = UI_NOTICE_SAVED;
						ui_state.feedback_deadline_tick =
							xTaskGetTickCount() + pdMS_TO_TICKS(UI_SAVE_FEEDBACK_MS);
					}
					break;
				}
				/* UP/DOWN modify the active field's value (name and schedule are
				 * edited in their own sub-screens instead, entered via SELECT). */
				if (ui_state.cursor == 1) {
					if (evt == BTN_EVT_UP)
						ui_state.edit_total = (ui_state.edit_total < 999) ? ui_state.edit_total + 1 : 1;
					else if (evt == BTN_EVT_DOWN)
						ui_state.edit_total = (ui_state.edit_total > 1) ? ui_state.edit_total - 1 : 999;
				} else if (ui_state.cursor == 2) {
					if (evt == BTN_EVT_UP)
						ui_state.edit_dose = (ui_state.edit_dose < 10) ? ui_state.edit_dose + 1 : 1;
					else if (evt == BTN_EVT_DOWN)
						ui_state.edit_dose = (ui_state.edit_dose > 1) ? ui_state.edit_dose - 1 : 10;
				}
				break;

			case UI_EDIT_NAME:
				if (evt == BTN_EVT_BACK) {
					/* Hold BACK = same as hovering DONE and pressing OK. */
					lcd_finish_name_edit();
					break;
				}
				if (evt == BTN_EVT_LEFT) {
					if (ui_state.kb_col > 0) {
						ui_state.kb_col--;
					}
				} else if (evt == BTN_EVT_RIGHT) {
					if (ui_state.kb_col < kb_row_len[ui_state.kb_row] - 1) {
						ui_state.kb_col++;
					}
				} else if (evt == BTN_EVT_UP) {
					ui_state.kb_row = (ui_state.kb_row + KB_ROWS - 1) % KB_ROWS;
					if (ui_state.kb_col >= kb_row_len[ui_state.kb_row]) {
						ui_state.kb_col = kb_row_len[ui_state.kb_row] - 1;
					}
				} else if (evt == BTN_EVT_DOWN) {
					ui_state.kb_row = (ui_state.kb_row + 1) % KB_ROWS;
					if (ui_state.kb_col >= kb_row_len[ui_state.kb_row]) {
						ui_state.kb_col = kb_row_len[ui_state.kb_row] - 1;
					}
				} else if (evt == BTN_EVT_SELECT) {
					const char *key = kb_rows[ui_state.kb_row][ui_state.kb_col];

					if (strcmp(key, "DONE") == 0) {
						lcd_finish_name_edit();
					} else if (strcmp(key, "DEL") == 0) {
						if (ui_state.edit_name_len > 0) {
							ui_state.edit_name_len--;
							ui_state.edit_name[ui_state.edit_name_len] = '\0';
						}
					} else if (ui_state.edit_name_len < EDIT_NAME_MAX_LEN) {
						char ch = (strcmp(key, "SPACE") == 0) ? ' ' : key[0];

						ui_state.edit_name[ui_state.edit_name_len] = ch;
						ui_state.edit_name_len++;
						ui_state.edit_name[ui_state.edit_name_len] = '\0';
					}
				}
				break;

			case UI_EDIT_SCHEDULE:
				if (evt == BTN_EVT_BACK) {
					ui_state.screen = UI_EDIT_FIELD;
					ui_state.cursor = 4;
					break;
				}
				if (evt == BTN_EVT_LEFT) {
					ui_state.edit_sched_cursor = (ui_state.edit_sched_cursor + LCD_SCHED_CURSOR_COUNT - 1)
					                              % LCD_SCHED_CURSOR_COUNT;
				} else if (evt == BTN_EVT_RIGHT) {
					ui_state.edit_sched_cursor = (ui_state.edit_sched_cursor + 1) % LCD_SCHED_CURSOR_COUNT;
				} else if (evt == BTN_EVT_SELECT) {
					int entry = ui_state.edit_sched_cursor / LCD_SCHED_FIELDS_PER_ENTRY;
					ui_state.edit_sched[entry].active = !ui_state.edit_sched[entry].active;
				} else if (evt == BTN_EVT_UP || evt == BTN_EVT_DOWN) {
					int entry = ui_state.edit_sched_cursor / LCD_SCHED_FIELDS_PER_ENTRY;
					int field = ui_state.edit_sched_cursor % LCD_SCHED_FIELDS_PER_ENTRY;
					edit_sched_entry_t *e = &ui_state.edit_sched[entry];
					int dir = (evt == BTN_EVT_UP) ? 1 : -1;

					if (field == 4) {
						e->active = !e->active;
					} else if (field == 0) {
						e->hour = (uint8_t)(((int)e->hour + 10 * dir + 240) % 24);
					} else if (field == 1) {
						e->hour = (uint8_t)(((int)e->hour + dir + 24) % 24);
					} else if (field == 2) {
						e->minute = (uint8_t)(((int)e->minute + 10 * dir + 600) % 60);
					} else if (field == 3) {
						e->minute = (uint8_t)(((int)e->minute + dir + 60) % 60);
					}
				}
				break;

			default:
				break;
			}
		}

		if (ui_state.screen == UI_NOTICE_SAVED &&
		    ui_state.feedback_deadline_tick != 0 &&
		    xTaskGetTickCount() >= ui_state.feedback_deadline_tick) {
			ui_state.screen = UI_STATUS;
			ui_state.cursor = 0;
			ui_state.feedback_deadline_tick = 0;
		}

		/* Redraw: always on button event; also on 2s timeout in status mode */
		if (!got_event && ui_state.screen != UI_STATUS &&
		    ui_state.screen != UI_NOTICE_DISPENSING &&
		    ui_state.screen != UI_NOTICE_DISPENSE_SUCCESS &&
		    ui_state.screen != UI_NOTICE_DISPENSE_FAILURE) {
			if ((xTaskGetTickCount() - last_input_tick) >= pdMS_TO_TICKS(UI_INACTIVITY_TIMEOUT_MS)) {
				if (ui_state.screen == UI_EDIT_FIELD || ui_state.screen == UI_EDIT_NAME ||
				    ui_state.screen == UI_EDIT_SCHEDULE) {
					led_enqueue_event(LED_EVENT_EDIT_END, ui_state.edit_slot);
				}
				led_enqueue_event(LED_EVENT_SLOT_CLEAR, -1);
				ui_state.screen = UI_STATUS;
				ui_state.cursor = 0;
			} else {
				continue;
			}
		}

		if (bridge_state_mutex != NULL &&
		    xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
			*snapshot = bridge_state;
			xSemaphoreGive(bridge_state_mutex);
		}

		if (ui_state.screen == UI_NOTICE_DISPENSING) {
			int dslot = ui_state.dispense_slot;
			bool slot_valid = (dslot >= 0 && dslot < PILL_SLOT_COUNT);
			const char *slot_result = slot_valid ? snapshot->slots[dslot].last_dispense_result : "";

			if (snapshot->awaiting_dispense_ack) {
				/* Keep waiting while ACK is pending. */
			} else if (snapshot->awaiting_drawer_open &&
			           ((snapshot->drawer_open_slot == dslot) ||
			            strcmp(snapshot->last_ack_result, "ok") == 0)) {
				/* Audio/LED for this already fired once from bridge_handle_ack_line()
				 * the instant the ACK came back "ok" -- this only switches the screen. */
				ui_state.screen = UI_NOTICE_DISPENSE_SUCCESS;
			} else if ((strcmp(snapshot->last_ack_action, "DISPENSE") == 0) &&
			           (strcmp(snapshot->last_ack_result, "fail") == 0 ||
			            strcmp(snapshot->last_ack_result, "timeout") == 0)) {
				ui_state.screen = UI_NOTICE_DISPENSE_FAILURE;
			} else if (slot_valid &&
			           (strcmp(slot_result, "fail") == 0 || strcmp(slot_result, "timeout") == 0)) {
				ui_state.screen = UI_NOTICE_DISPENSE_FAILURE;
			} else if (slot_valid && strcmp(slot_result, "ok") == 0) {
				/* Dispense finished very quickly and slot already reported success.
				 * Audio/LED already fired from bridge_handle_ack_line(); just switch screens. */
				ui_state.screen = UI_NOTICE_DISPENSE_SUCCESS;
			} else if (strcmp(snapshot->last_ack_result, "fail") == 0 ||
			           strcmp(snapshot->last_ack_result, "timeout") == 0) {
				ui_state.screen = UI_NOTICE_DISPENSE_FAILURE;
			} else {
				/* No terminal result yet; stay on wait screen instead of jumping to menu. */
			}
		} else if (ui_state.screen == UI_NOTICE_DISPENSE_SUCCESS) {
			if (!snapshot->awaiting_drawer_open) {
				led_enqueue_event(LED_EVENT_SLOT_CLEAR, -1);
				ui_state.screen = UI_STATUS;
				ui_state.dispense_slot = -1;
			}
		}

		if (!got_event &&
		    (ui_state.screen == UI_NOTICE_DISPENSING ||
		     ui_state.screen == UI_NOTICE_DISPENSE_SUCCESS ||
		     ui_state.screen == UI_NOTICE_DISPENSE_FAILURE) &&
		    ui_state.screen == last_drawn_screen &&
		    (ui_state.screen != UI_NOTICE_DISPENSE_FAILURE ||
		     snapshot->active_profile_slot == last_drawn_failure_slot)) {
			continue;
		}

		if (!got_event && ui_state.screen == UI_STATUS &&
		    ui_state.screen == last_drawn_screen && have_last_status_snapshot) {
			time_t now = time(NULL);
			int cur_minute = (now > 1700000000) ? (int)(now / 60) : -1;

			if (cur_minute == last_status_minute &&
			    memcmp(&last_status_snapshot, snapshot, sizeof(*snapshot)) == 0) {
				continue;
			}
		}

		if (ui_state.screen == UI_STATUS) {
			screen_draw_ui(snapshot);
		} else if (ui_state.screen == UI_SLOT_MENU) {
			screen_draw_slot_menu(snapshot, ui_state.cursor);
		} else if (ui_state.screen == UI_ACTION_MENU) {
			screen_draw_action_menu(ui_state.edit_slot, ui_state.cursor);
		} else if (ui_state.screen == UI_CONFIRM_DISPENSE) {
			screen_draw_dispense_confirm(ui_state.edit_slot, ui_state.cursor);
		} else if (ui_state.screen == UI_EDIT_FIELD) {
			screen_draw_edit(&ui_state);
		} else if (ui_state.screen == UI_EDIT_NAME) {
			screen_draw_edit_name(&ui_state);
		} else if (ui_state.screen == UI_EDIT_SCHEDULE) {
			screen_draw_edit_schedule(&ui_state);
		} else if (ui_state.screen == UI_NOTICE_DISPENSING) {
			screen_draw_dispense_waiting();
		} else if (ui_state.screen == UI_NOTICE_DISPENSE_SUCCESS) {
			screen_draw_dispense_success_wait();
		} else if (ui_state.screen == UI_NOTICE_DISPENSE_FAILURE) {
			screen_draw_dispense_failure_wait(snapshot->active_profile_slot);
		} else if (ui_state.screen == UI_NOTICE_SAVED) {
			screen_draw_feedback_card("PROFILE", "PILLS SAVED :)", LCD_CYAN);
		}

		last_drawn_screen = ui_state.screen;
		if (ui_state.screen == UI_NOTICE_DISPENSE_FAILURE) {
			last_drawn_failure_slot = snapshot->active_profile_slot;
		}
		if (ui_state.screen == UI_STATUS) {
			time_t now = time(NULL);

			last_status_minute = (now > 1700000000) ? (int)(now / 60) : -1;
			last_status_snapshot = *snapshot;
			have_last_status_snapshot = true;
		}
	}

	free(snapshot); /* unreachable */
}

/* Sets up the SPI bus and the LCD's control pins (backlight, D/C, reset),
 * runs the panel init sequence, then starts button_task() and
 * screen_task(). Call once at boot. */
void start_lcd_display(void)
{
	spi_bus_config_t bus = {
		.mosi_io_num     = LCD_MOSI,
		.miso_io_num     = -1,
		.sclk_io_num     = LCD_CLK,
		.quadwp_io_num   = -1,
		.quadhd_io_num   = -1,
		.max_transfer_sz = SCREEN_W * 2,
	};
	spi_device_interface_config_t dev = {
		.clock_speed_hz = 40 * 1000 * 1000,
		.mode           = 0,
		.spics_io_num   = LCD_CS,
		.queue_size     = 7,
	};

	gpio_set_direction(LCD_BL,  GPIO_MODE_OUTPUT);
	gpio_set_level(LCD_BL, 1);
	gpio_set_direction(LCD_DC,  GPIO_MODE_OUTPUT);
	gpio_set_direction(LCD_RST, GPIO_MODE_OUTPUT);
	gpio_set_level(LCD_RST, 1);

	ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
	ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &lcd_spi));

	lcd_st7796_init();
	btn_queue = xQueueCreate(8, sizeof(btn_event_t));
	xTaskCreate(button_task, "button_task", 2048, NULL, 3, NULL);
	xTaskCreate(screen_task, "screen_task", 12288, NULL, 2, NULL);
	ESP_LOGI(TAG, "LCD display started on SPI3");
}
