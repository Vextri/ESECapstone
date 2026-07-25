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

/* ── Physical buttons (active-low, internal pull-up) ──────────────────────── */
#define BTN_UP_PIN   38
#define BTN_DOWN_PIN 39
#define BTN_SEL_PIN  40
#define BTN_LONG_MS  700
#define UI_INACTIVITY_TIMEOUT_MS 20000
#define UI_VISIBLE_SLOT_COUNT 3

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
typedef enum { BTN_EVT_UP, BTN_EVT_DOWN, BTN_EVT_SELECT, BTN_EVT_BACK } btn_event_t;
typedef enum { UI_STATUS, UI_SLOT_MENU, UI_ACTION_MENU, UI_EDIT_FIELD } ui_screen_t;

#define EDIT_FIELD_COUNT      4
#define SCHEDULE_PRESET_COUNT 6

static const char *const schedule_presets[SCHEDULE_PRESET_COUNT] = {
	"none", "08:00", "12:00", "20:00", "08:00,20:00", "08:00,14:00,20:00",
};
static const char *const schedule_labels[SCHEDULE_PRESET_COUNT] = {
	"NONE", "08:00", "12:00", "20:00", "08+20", "8+14+20",
};

typedef struct {
	ui_screen_t screen;
	int         cursor;
	int         edit_slot;
	int         edit_total;
	int         edit_dose;
	int         edit_sched_idx;
} ui_state_t;

static QueueHandle_t btn_queue;
static ui_state_t    ui_state;

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

static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
	uint8_t col[] = { x0 >> 8, x0, x1 >> 8, x1 };
	uint8_t row[] = { y0 >> 8, y0, y1 >> 8, y1 };
	lcd_send_cmd(0x2A); lcd_send_data(col, 4);
	lcd_send_cmd(0x2B); lcd_send_data(row, 4);
	lcd_send_cmd(0x2C);
	gpio_set_level(LCD_DC, 1);
}

static void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
	int row_bytes;
	int i;

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

static void lcd_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, int scale)
{
	char c;

	while (*str) {
		c = *str;
		if (c >= 'a' && c <= 'z') {
			c = (char)(c - 32);
		}
		lcd_draw_char(x, y, c, color, bg, scale);
		x = (uint16_t)(x + (5 + 1) * scale);
		str++;
	}
}

/* Renders the live pill slot data onto the LCD. Called from screen_task. */
static void screen_draw_ui(const pico_bridge_state_t *snapshot)
{
	char buf[32];
	char next_dispense[24];
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

		/* Slot label: S0 .. S4 */
		buf[0] = 'S';
		buf[1] = (char)('0' + slot_index);
		buf[2] = '\0';
		lcd_draw_string(8, (uint16_t)(row_y + 20), buf, slot_col, LCD_BLACK, 2);

		/* Medication name — uppercase, capped at 16 chars */
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

		/* Pills left — color-coded: red at 0, yellow near empty */
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
	screen_get_next_dispense_string(snapshot, next_dispense, sizeof(next_dispense));
	lcd_fill_rect(0, 262, SCREEN_W, 58, LCD_DKGREY);
	lcd_draw_hline(0, 262, SCREEN_W, LCD_WHITE);
	lcd_draw_string(8, 278, "NEXT DISPENSE AT", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_string(210, 278, next_dispense, LCD_WHITE, LCD_DKGREY, 2);
}

/* ── Menu draw functions ───────────────────────────────────────────────────── */

static void screen_draw_slot_menu(const pico_bridge_state_t *snap, int cursor)
{
	char buf[20];
	int i;
	int j;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	lcd_draw_string(8, 14, "SELECT SLOT", LCD_CYAN, LCD_DKGREY, 2);
	lcd_draw_string(332, 14, "HOLD=BACK", LCD_GREY, LCD_DKGREY, 1);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (i = 0; i < PILL_SLOT_COUNT; i++) {
		uint16_t y = (uint16_t)(46 + i * 54);
		bool sel = (i == cursor);
		uint16_t bg = sel ? LCD_CYAN : LCD_BLACK;
		uint16_t fg = sel ? LCD_BLACK : LCD_WHITE;
		const pill_slot_state_t *s = &snap->slots[i];
		char med[17];

		lcd_fill_rect(0, y, SCREEN_W, 52, bg);
		buf[0] = 'S'; buf[1] = (char)('0' + i); buf[2] = '\0';
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
}

static void screen_draw_action_menu(int slot, int cursor)
{
	static const char *const actions[3]   = { "DISPENSE NOW", "EDIT PROFILE", "BACK" };
	static const uint16_t    act_fg[3]    = { LCD_GREEN, LCD_CYAN, LCD_GREY };
	char title[20];
	int i;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	snprintf(title, sizeof(title), "SLOT %d", slot);
	lcd_draw_string(8, 14, title, LCD_YELLOW, LCD_DKGREY, 2);
	lcd_draw_string(308, 14, "HOLD=BACK", LCD_GREY, LCD_DKGREY, 1);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (i = 0; i < 3; i++) {
		uint16_t y = (uint16_t)(58 + i * 84);
		bool sel = (i == cursor);
		uint16_t bg = sel ? act_fg[i] : LCD_DKGREY;
		uint16_t fg = sel ? LCD_BLACK : act_fg[i];

		lcd_fill_rect(16, y, SCREEN_W - 32, 64, bg);
		lcd_draw_string(32, (uint16_t)(y + 24), actions[i], fg, bg, 2);
	}
}

static void screen_draw_edit(const ui_state_t *st)
{
	static const char *const field_labels[EDIT_FIELD_COUNT] = {
		"TOTAL PILLS", "DOSE PER DISPENSE", "SCHEDULE", "CONFIRM?",
	};
	char val[24];
	char title[20];
	int i;

	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);
	lcd_fill_rect(0, 0, SCREEN_W, 44, LCD_DKGREY);
	snprintf(title, sizeof(title), "EDIT SLOT %d", st->edit_slot);
	lcd_draw_string(8, 14, title, LCD_YELLOW, LCD_DKGREY, 2);
	lcd_draw_hline(0, 44, SCREEN_W, LCD_CYAN);

	for (i = 0; i < EDIT_FIELD_COUNT; i++) {
		uint16_t y = (uint16_t)(52 + i * 62);
		bool sel = (i == st->cursor);

		lcd_draw_string(8, y, field_labels[i], sel ? LCD_CYAN : LCD_GREY, LCD_BLACK, 1);

		if (i == 0) {
			snprintf(val, sizeof(val), "%d", st->edit_total);
		} else if (i == 1) {
			snprintf(val, sizeof(val), "%d", st->edit_dose);
		} else if (i == 2) {
			snprintf(val, sizeof(val), "%s", schedule_labels[st->edit_sched_idx]);
		} else {
			snprintf(val, sizeof(val), "PRESS SELECT");
		}

		lcd_draw_string(8, (uint16_t)(y + 10), val, sel ? LCD_WHITE : LCD_GREY, LCD_BLACK, 2);
		if (sel) {
			lcd_draw_hline(8, (uint16_t)(y + 28), 200, LCD_CYAN);
		}
	}

	lcd_draw_string(8, 305, "UP/DN=CHANGE  SEL=NEXT  HOLD=CANCEL", LCD_GREY, LCD_BLACK, 1);
}

/* ── Button polling task ───────────────────────────────────────────────────── */

static void button_task(void *arg)
{
	bool prev_up  = true;
	bool prev_dn  = true;
	bool prev_sel = true;
	TickType_t sel_press_tick = 0;
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << BTN_UP_PIN) | (1ULL << BTN_DOWN_PIN) | (1ULL << BTN_SEL_PIN),
		.mode         = GPIO_MODE_INPUT,
		.pull_up_en   = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type    = GPIO_INTR_DISABLE,
	};

	(void)arg;
	gpio_config(&cfg);

	while (1) {
		bool cur_up  = gpio_get_level(BTN_UP_PIN)  != 0;
		bool cur_dn  = gpio_get_level(BTN_DOWN_PIN) != 0;
		bool cur_sel = gpio_get_level(BTN_SEL_PIN)  != 0;

		if (!cur_up && prev_up) {
			ESP_LOGI(TAG, "BTN: UP pressed");
			btn_event_t e = BTN_EVT_UP;
			xQueueSend(btn_queue, &e, 0);
		}
		if (!cur_dn && prev_dn) {
			ESP_LOGI(TAG, "BTN: DOWN pressed");
			btn_event_t e = BTN_EVT_DOWN;
			xQueueSend(btn_queue, &e, 0);
		}
		if (!cur_sel && prev_sel) {
			sel_press_tick = xTaskGetTickCount();
		}
		if (cur_sel && !prev_sel) {
			TickType_t held = xTaskGetTickCount() - sel_press_tick;
			btn_event_t e = (held >= pdMS_TO_TICKS(BTN_LONG_MS)) ? BTN_EVT_BACK : BTN_EVT_SELECT;
			if (e == BTN_EVT_BACK)
				ESP_LOGI(TAG, "BTN: SEL long-press (BACK)");
			else
				ESP_LOGI(TAG, "BTN: SEL short-press (SELECT)");
			xQueueSend(btn_queue, &e, 0);
		}

		prev_up  = cur_up;
		prev_dn  = cur_dn;
		prev_sel = cur_sel;
		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

static void screen_task(void *arg)
{
	pico_bridge_state_t *snapshot;
	TickType_t last_input_tick;

	(void)arg;

	snapshot = malloc(sizeof(*snapshot));
	if (snapshot == NULL) {
		ESP_LOGE(TAG, "screen_task: malloc failed");
		vTaskDelete(NULL);
		return;
	}

	ui_state.screen = UI_STATUS;
	ui_state.cursor = 0;
	last_input_tick = xTaskGetTickCount();
	lcd_fill_rect(0, 0, SCREEN_W, SCREEN_H, LCD_BLACK);

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
				break;

			case UI_SLOT_MENU:
				if (evt == BTN_EVT_UP) {
					ui_state.cursor = (ui_state.cursor + PILL_SLOT_COUNT - 1) % PILL_SLOT_COUNT;
				} else if (evt == BTN_EVT_DOWN) {
					ui_state.cursor = (ui_state.cursor + 1) % PILL_SLOT_COUNT;
				} else if (evt == BTN_EVT_BACK) {
					ui_state.screen = UI_STATUS;
				} else if (evt == BTN_EVT_SELECT) {
					ui_state.edit_slot = ui_state.cursor;
					ui_state.cursor    = 0;
					ui_state.screen    = UI_ACTION_MENU;
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
				} else if (evt == BTN_EVT_SELECT) {
					if (ui_state.cursor == 0) {
						/* Dispense Now */
						if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
							bridge_send_dispense_for_slot(ui_state.edit_slot);
							bridge_state.active_profile_slot    = ui_state.edit_slot;
							bridge_state.awaiting_dispense_ack  = true;
							bridge_state.dispense_ack_deadline_us =
								esp_timer_get_time() + DISPENSE_ACK_TIMEOUT_US;
							bridge_copy_string(bridge_state.last_ack_action,
							                   sizeof(bridge_state.last_ack_action), "DISPENSE");
							bridge_copy_string(bridge_state.last_ack_result,
							                   sizeof(bridge_state.last_ack_result), "pending");
							bridge_state_save_to_nvs(&bridge_state);
							xSemaphoreGive(bridge_state_mutex);
						}
						ui_state.screen = UI_STATUS;
					} else if (ui_state.cursor == 1) {
						/* Edit Profile — seed edit buffer from current slot */
						if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
							const pill_slot_state_t *s = &bridge_state.slots[ui_state.edit_slot];
							int pi;
							ui_state.edit_total      = (s->total_pills   > 0) ? s->total_pills   : 20;
							ui_state.edit_dose       = (s->pills_per_dose > 0) ? s->pills_per_dose : 1;
							ui_state.edit_sched_idx  = 0;
							for (pi = 0; pi < SCHEDULE_PRESET_COUNT; pi++) {
								if (strcmp(s->schedule, schedule_presets[pi]) == 0) {
									ui_state.edit_sched_idx = pi;
									break;
								}
							}
							xSemaphoreGive(bridge_state_mutex);
						}
						ui_state.cursor = 0;
						ui_state.screen = UI_EDIT_FIELD;
						audio_enqueue_event(AUDIO_EVENT_EDIT_BEGIN);
						led_enqueue_event(LED_EVENT_EDIT_BEGIN, ui_state.edit_slot);
					} else {
						/* Back */
						ui_state.screen = UI_SLOT_MENU;
						ui_state.cursor = ui_state.edit_slot;
					}
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
					if (ui_state.cursor < EDIT_FIELD_COUNT - 1) {
						ui_state.cursor++;
					} else {
						/* Confirm — save and send to Pico */
						if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(200)) == pdTRUE) {
							pill_slot_state_t *s = &bridge_state.slots[ui_state.edit_slot];
							s->slot_number     = ui_state.edit_slot;
							s->total_pills     = ui_state.edit_total;
							s->pills_left      = ui_state.edit_total;
							s->pills_per_dose  = ui_state.edit_dose;
							s->doses_remaining = (ui_state.edit_dose > 0)
							                     ? (ui_state.edit_total / ui_state.edit_dose)
							                     : -1;
							s->is_active = true;
							s->has_data  = true;
							bridge_copy_string(s->schedule, sizeof(s->schedule),
							                   schedule_presets[ui_state.edit_sched_idx]);
							bridge_copy_string(s->notes, sizeof(s->notes),
							                   "Profile saved via display.");
							bridge_state.active_profile_slot = ui_state.edit_slot;
							bridge_state_save_to_nvs(&bridge_state);
							bridge_send_load_profile_for_slot(s);
							xSemaphoreGive(bridge_state_mutex);
						}
						led_enqueue_event(LED_EVENT_EDIT_END, ui_state.edit_slot);
						ui_state.screen = UI_STATUS;
					}
					break;
				}
				/* UP/DOWN modify the active field's value */
				if (ui_state.cursor == 0) {
					if (evt == BTN_EVT_UP)
						ui_state.edit_total = (ui_state.edit_total < 999) ? ui_state.edit_total + 1 : 1;
					else if (evt == BTN_EVT_DOWN)
						ui_state.edit_total = (ui_state.edit_total > 1) ? ui_state.edit_total - 1 : 999;
				} else if (ui_state.cursor == 1) {
					if (evt == BTN_EVT_UP)
						ui_state.edit_dose = (ui_state.edit_dose < 10) ? ui_state.edit_dose + 1 : 1;
					else if (evt == BTN_EVT_DOWN)
						ui_state.edit_dose = (ui_state.edit_dose > 1) ? ui_state.edit_dose - 1 : 10;
				} else if (ui_state.cursor == 2) {
					if (evt == BTN_EVT_UP)
						ui_state.edit_sched_idx = (ui_state.edit_sched_idx + 1) % SCHEDULE_PRESET_COUNT;
					else if (evt == BTN_EVT_DOWN)
						ui_state.edit_sched_idx = (ui_state.edit_sched_idx + SCHEDULE_PRESET_COUNT - 1)
						                           % SCHEDULE_PRESET_COUNT;
				}
				break;

			default:
				break;
			}
		}

		/* Redraw: always on button event; also on 2s timeout in status mode */
		if (!got_event && ui_state.screen != UI_STATUS) {
			if ((xTaskGetTickCount() - last_input_tick) >= pdMS_TO_TICKS(UI_INACTIVITY_TIMEOUT_MS)) {
				if (ui_state.screen == UI_EDIT_FIELD) {
					led_enqueue_event(LED_EVENT_EDIT_END, ui_state.edit_slot);
				}
				ui_state.screen = UI_STATUS;
				ui_state.cursor = 0;
			} else {
				continue;
			}
		}

		memset(snapshot, 0, sizeof(*snapshot));
		if (bridge_state_mutex != NULL &&
		    xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
			*snapshot = bridge_state;
			xSemaphoreGive(bridge_state_mutex);
		}

		if (ui_state.screen == UI_STATUS) {
			screen_draw_ui(snapshot);
		} else if (ui_state.screen == UI_SLOT_MENU) {
			screen_draw_slot_menu(snapshot, ui_state.cursor);
		} else if (ui_state.screen == UI_ACTION_MENU) {
			screen_draw_action_menu(ui_state.edit_slot, ui_state.cursor);
		} else if (ui_state.screen == UI_EDIT_FIELD) {
			screen_draw_edit(&ui_state);
		}
	}

	free(snapshot); /* unreachable */
}

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
