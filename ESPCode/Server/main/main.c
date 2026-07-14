#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "driver/spi_master.h"
#include "driver/i2s_std.h"
#include "driver/rmt_tx.h"

static const char *TAG = "time_server";

#define AP_SSID "ESP-Time-Server"
#define AP_PASS "123456789"
#define AP_MAX_CONN 4

#define DNS_PORT 53
#define AP_IP_OCTET_1 192
#define AP_IP_OCTET_2 168
#define AP_IP_OCTET_3 4
#define AP_IP_OCTET_4 1

#define UART_BRIDGE_PORT UART_NUM_1
#define UART_BRIDGE_BAUD 115200
#define UART_BRIDGE_TX_PIN 12
#define UART_BRIDGE_RX_PIN 11
#define UART_BRIDGE_BUFFER_SIZE 512
#define UART_BRIDGE_LINE_SIZE 256
#define UART_BRIDGE_TIMEOUT_US (5 * 1000000)
#define PILL_SLOT_COUNT 5
#define STATUS_RESPONSE_BUFFER_SIZE 8192
#define STATUS_SLOTS_BUFFER_SIZE    3072
#define HISTORY_JSON_BUFFER_SIZE    3300
#define DISPENSE_HISTORY_COUNT      16
#define LOW_PILL_THRESHOLD          5
#define BRIDGE_NVS_NAMESPACE "bridge_state"
#define HTTP_BODY_BUFFER_SIZE 512
#define DISPENSE_ACK_TIMEOUT_US (40 * 1000000LL)

#define AUDIO_I2S_BCLK_GPIO GPIO_NUM_16
#define AUDIO_I2S_WS_GPIO   GPIO_NUM_17
#define AUDIO_I2S_DOUT_GPIO GPIO_NUM_18
#define AUDIO_SAMPLE_RATE   22050
#define AUDIO_AMPLITUDE     14000
#define AUDIO_BUFFER_SAMPLES 192

#define AUDIO_NOTE_WHOLE_MS     640
#define AUDIO_NOTE_HALF_MS      320
#define AUDIO_NOTE_QUARTER_MS   160
#define AUDIO_NOTE_EIGHTH_MS     80

#define AUDIO_FREQ_REST 0.0f
#define AUDIO_FREQ_A3   220.00f
#define AUDIO_FREQ_C4   261.63f
#define AUDIO_FREQ_E4   329.63f
#define AUDIO_FREQ_G4   392.00f
#define AUDIO_FREQ_A4   440.00f
#define AUDIO_FREQ_C5   523.25f

#define LED_WS2812_GPIO GPIO_NUM_9
#define LED_COUNT 3
#define LED_FLASH_ON_MS 140
#define LED_FLASH_OFF_MS 120
#define LED_EDIT_R 140
#define LED_EDIT_G 0
#define LED_EDIT_B 180

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
	int pills_left;
	int pills_per_dose;
	int doses_remaining;
	int total_pills;
	int slot_number;
	uint32_t time_ms;
	char medication_name[32];
	char schedule[40];
	char last_dispensed[64];
	char last_event[96];
	char notes[96];
	char last_dispense_result[16];
	bool has_data;
	bool is_active;
} pill_slot_state_t;

typedef struct {
	int slot_number;
	int pills_left_after;
	int64_t esp_timestamp;
	char medication_name[32];
	char result[16];
	char event[96];
} dispense_history_entry_t;

typedef struct {
	bool connected;
	int active_profile_slot;
	char controller_transport[32];
	pill_slot_state_t slots[PILL_SLOT_COUNT];
	dispense_history_entry_t history[DISPENSE_HISTORY_COUNT];
	int history_count;
	char last_ack_action[32];
	char last_ack_result[32];
	bool awaiting_dispense_ack;
	int64_t dispense_ack_deadline_us;
	int64_t last_update_us;
	int dispense_queue[PILL_SLOT_COUNT];
	int dispense_queue_count;
} pico_bridge_state_t;

static SemaphoreHandle_t bridge_state_mutex;
static pico_bridge_state_t bridge_state;

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

typedef struct {
	float freq;
	int dur_ms;
} audio_note_t;

typedef enum {
	AUDIO_EVENT_SUCCESS = 1,
	AUDIO_EVENT_FAILURE = 2,
	AUDIO_EVENT_EDIT_BEGIN = 3,
} audio_event_t;

static QueueHandle_t audio_event_queue;
static i2s_chan_handle_t audio_i2s_tx;

typedef enum {
	LED_EVENT_SUCCESS = 1,
	LED_EVENT_FAILURE = 2,
	LED_EVENT_EDIT_BEGIN = 3,
	LED_EVENT_EDIT_END = 4,
} led_event_t;

typedef struct {
	led_event_t event;
	int slot;
} led_event_msg_t;

typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} led_pixel_t;

static QueueHandle_t led_event_queue;
static rmt_channel_handle_t led_rmt_chan;
static rmt_encoder_handle_t led_rmt_encoder;
static led_pixel_t led_pixels[LED_COUNT];

static const audio_note_t success_melody[] = {
	{AUDIO_FREQ_C4, AUDIO_NOTE_EIGHTH_MS},
	{AUDIO_FREQ_E4, AUDIO_NOTE_EIGHTH_MS},
	{AUDIO_FREQ_G4, AUDIO_NOTE_EIGHTH_MS},
	{AUDIO_FREQ_C5, AUDIO_NOTE_HALF_MS},
};

static const audio_note_t failure_melody[] = {
	{AUDIO_FREQ_A4, AUDIO_NOTE_QUARTER_MS},
	{AUDIO_FREQ_E4, AUDIO_NOTE_QUARTER_MS},
	{AUDIO_FREQ_C4, AUDIO_NOTE_HALF_MS},
	{AUDIO_FREQ_A3, AUDIO_NOTE_EIGHTH_MS},
	{AUDIO_FREQ_REST, AUDIO_NOTE_EIGHTH_MS},
	{AUDIO_FREQ_A3, AUDIO_NOTE_EIGHTH_MS},
};

static const audio_note_t edit_start_melody[] = {
	{AUDIO_FREQ_C5, AUDIO_NOTE_EIGHTH_MS},
	{AUDIO_FREQ_G4, AUDIO_NOTE_EIGHTH_MS},
};

static void audio_enqueue_event(audio_event_t event)
{
	if (audio_event_queue == NULL) {
		return;
	}

	if (xQueueSend(audio_event_queue, &event, 0) != pdTRUE) {
		ESP_LOGW(TAG, "Audio event queue full, dropping tone event=%d", (int)event);
	}
}

static void audio_play_silence(i2s_chan_handle_t tx, int dur_ms)
{
	int16_t buf[AUDIO_BUFFER_SAMPLES * 2] = {0};
	int total_samples = (int)(((int64_t)AUDIO_SAMPLE_RATE * dur_ms) / 1000);
	size_t written = 0;

	while (total_samples > 0) {
		int chunk = total_samples < AUDIO_BUFFER_SAMPLES ? total_samples : AUDIO_BUFFER_SAMPLES;
		i2s_channel_write(tx, buf, chunk * 4, &written, portMAX_DELAY);
		total_samples -= chunk;
	}
}

static void audio_play_note(i2s_chan_handle_t tx, float freq, int dur_ms)
{
	int tone_ms = dur_ms > 25 ? (dur_ms - 20) : dur_ms;
	int rest_ms = dur_ms - tone_ms;
	int total_samples = (int)(((int64_t)AUDIO_SAMPLE_RATE * tone_ms) / 1000);
	int16_t buf[AUDIO_BUFFER_SAMPLES * 2];
	uint32_t sample_pos = 0;
	size_t written = 0;

	while (total_samples > 0) {
		int chunk = total_samples < AUDIO_BUFFER_SAMPLES ? total_samples : AUDIO_BUFFER_SAMPLES;

		for (int i = 0; i < chunk; i++) {
			int16_t v = (freq > 0.0f)
				? (int16_t)(AUDIO_AMPLITUDE * sinf((2.0f * (float)M_PI * freq * sample_pos) / AUDIO_SAMPLE_RATE))
				: 0;
			buf[i * 2] = v;
			buf[i * 2 + 1] = v;
			sample_pos++;
		}

		i2s_channel_write(tx, buf, chunk * 4, &written, portMAX_DELAY);
		total_samples -= chunk;
	}

	if (rest_ms > 0) {
		audio_play_silence(tx, rest_ms);
	}
}

static void audio_play_melody(i2s_chan_handle_t tx, const audio_note_t *notes, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		audio_play_note(tx, notes[i].freq, notes[i].dur_ms);
	}
}

static void audio_task(void *arg)
{
	audio_event_t event;
	i2s_chan_handle_t tx = (i2s_chan_handle_t)arg;

	while (1) {
		if (xQueueReceive(audio_event_queue, &event, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		if (event == AUDIO_EVENT_SUCCESS) {
			audio_play_melody(tx, success_melody, sizeof(success_melody) / sizeof(success_melody[0]));
		} else if (event == AUDIO_EVENT_FAILURE) {
			audio_play_melody(tx, failure_melody, sizeof(failure_melody) / sizeof(failure_melody[0]));
		} else if (event == AUDIO_EVENT_EDIT_BEGIN) {
			audio_play_melody(tx, edit_start_melody, sizeof(edit_start_melody) / sizeof(edit_start_melody[0]));
		}
	}
}

static void start_audio_feedback(void)
{
	i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	i2s_std_config_t std_cfg;

	audio_event_queue = xQueueCreate(8, sizeof(audio_event_t));
	if (audio_event_queue == NULL) {
		ESP_LOGE(TAG, "Failed to create audio event queue");
		return;
	}

	chan_cfg.auto_clear = true;
	if (i2s_new_channel(&chan_cfg, &audio_i2s_tx, NULL) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create I2S channel for audio");
		return;
	}

	std_cfg = (i2s_std_config_t){
		.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
		.slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
		.gpio_cfg = {
			.mclk = I2S_GPIO_UNUSED,
			.bclk = AUDIO_I2S_BCLK_GPIO,
			.ws = AUDIO_I2S_WS_GPIO,
			.dout = AUDIO_I2S_DOUT_GPIO,
			.din = I2S_GPIO_UNUSED,
			.invert_flags = {
				.mclk_inv = false,
				.bclk_inv = false,
				.ws_inv = false,
			},
		},
	};

	if (i2s_channel_init_std_mode(audio_i2s_tx, &std_cfg) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to init I2S std mode for audio");
		return;
	}

	if (i2s_channel_enable(audio_i2s_tx) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to enable I2S TX for audio");
		return;
	}

	xTaskCreatePinnedToCore(audio_task, "audio_task", 4096, audio_i2s_tx, 4, NULL, 1);
	ESP_LOGI(TAG, "Audio feedback ready on I2S BCLK=%d WS=%d DOUT=%d",
		 (int)AUDIO_I2S_BCLK_GPIO, (int)AUDIO_I2S_WS_GPIO, (int)AUDIO_I2S_DOUT_GPIO);
}

static void led_ws2812_show(void)
{
	static const rmt_symbol_word_t bit0 = {
		.level0 = 1, .duration0 = 4,
		.level1 = 0, .duration1 = 9,
	};
	static const rmt_symbol_word_t bit1 = {
		.level0 = 1, .duration0 = 8,
		.level1 = 0, .duration1 = 5,
	};
	rmt_symbol_word_t symbols[LED_COUNT * 24 + 1];
	rmt_transmit_config_t tx_cfg = {.loop_count = 0};
	int idx = 0;

	for (int led = 0; led < LED_COUNT; led++) {
		uint8_t grb[3] = {led_pixels[led].g, led_pixels[led].r, led_pixels[led].b};
		for (int byte = 0; byte < 3; byte++) {
			for (int bit = 7; bit >= 0; bit--) {
				symbols[idx++] = ((grb[byte] >> bit) & 0x1) ? bit1 : bit0;
			}
		}
	}

	symbols[idx] = (rmt_symbol_word_t){
		.level0 = 0, .duration0 = 600,
		.level1 = 0, .duration1 = 600,
	};

	if (rmt_transmit(led_rmt_chan, led_rmt_encoder, symbols, sizeof(symbols), &tx_cfg) == ESP_OK) {
		rmt_tx_wait_all_done(led_rmt_chan, portMAX_DELAY);
	}
}

static int led_index_from_slot(int slot)
{
	if (slot < 0 || slot >= LED_COUNT) {
		return -1;
	}

	return slot;
}

static void led_set_all(uint8_t r, uint8_t g, uint8_t b)
{
	for (int i = 0; i < LED_COUNT; i++) {
		led_pixels[i].r = r;
		led_pixels[i].g = g;
		led_pixels[i].b = b;
	}
	led_ws2812_show();
}

static void led_apply_base_state(bool edit_active, int edit_slot)
{
	if (edit_active) {
		int led_idx = led_index_from_slot(edit_slot);
		led_set_all(0, 0, 0);
		if (led_idx >= 0) {
			led_pixels[led_idx].r = LED_EDIT_R;
			led_pixels[led_idx].g = LED_EDIT_G;
			led_pixels[led_idx].b = LED_EDIT_B;
			led_ws2812_show();
		}
	} else {
		led_set_all(0, 0, 0);
	}
}

static void led_flash_sequence(uint8_t r, uint8_t g, uint8_t b, int flashes)
{
	for (int i = 0; i < flashes; i++) {
		led_set_all(r, g, b);
		vTaskDelay(pdMS_TO_TICKS(LED_FLASH_ON_MS));
		led_set_all(0, 0, 0);
		vTaskDelay(pdMS_TO_TICKS(LED_FLASH_OFF_MS));
	}
}

static void led_enqueue_event(led_event_t event, int slot)
{
	led_event_msg_t msg;

	if (led_event_queue == NULL) {
		return;
	}

	msg.event = event;
	msg.slot = slot;

	if (xQueueSend(led_event_queue, &msg, 0) != pdTRUE) {
		ESP_LOGW(TAG, "LED event queue full, dropping event=%d", (int)event);
	}
}

static void led_task(void *arg)
{
	led_event_msg_t msg;
	(void)arg;
	bool edit_active = false;
	int edit_slot = -1;

	led_apply_base_state(false, edit_slot);

	while (1) {
		if (xQueueReceive(led_event_queue, &msg, portMAX_DELAY) != pdTRUE) {
			continue;
		}

		if (msg.event == LED_EVENT_SUCCESS) {
			led_flash_sequence(0, 255, 0, 3);
		} else if (msg.event == LED_EVENT_FAILURE) {
			led_flash_sequence(255, 0, 0, 3);
		} else if (msg.event == LED_EVENT_EDIT_BEGIN) {
			edit_active = true;
			edit_slot = msg.slot;
		} else if (msg.event == LED_EVENT_EDIT_END) {
			edit_active = false;
			edit_slot = -1;
		}

		led_apply_base_state(edit_active, edit_slot);
	}
}

static void start_led_feedback(void)
{
	rmt_tx_channel_config_t rmt_chan_cfg;
	rmt_copy_encoder_config_t copy_cfg = {};

	led_event_queue = xQueueCreate(8, sizeof(led_event_msg_t));
	if (led_event_queue == NULL) {
		ESP_LOGE(TAG, "Failed to create LED event queue");
		return;
	}

	rmt_chan_cfg = (rmt_tx_channel_config_t){
		.gpio_num = LED_WS2812_GPIO,
		.clk_src = RMT_CLK_SRC_DEFAULT,
		.resolution_hz = 10 * 1000 * 1000,
		.mem_block_symbols = 64,
		.trans_queue_depth = 4,
	};
	if (rmt_new_tx_channel(&rmt_chan_cfg, &led_rmt_chan) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create RMT channel for LEDs");
		return;
	}

	if (rmt_new_copy_encoder(&copy_cfg, &led_rmt_encoder) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to create RMT encoder for LEDs");
		return;
	}

	if (rmt_enable(led_rmt_chan) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to enable RMT LED channel");
		return;
	}

	xTaskCreatePinnedToCore(led_task, "led_task", 3072, NULL, 3, NULL, 1);
	ESP_LOGI(TAG, "LED feedback ready on WS2812 GPIO=%d with %d LEDs",
		 (int)LED_WS2812_GPIO, LED_COUNT);
}

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

/* Forward declaration needed by schedule helpers in this section. */
static void bridge_copy_string(char *dest, size_t dest_size, const char *src);

static bool screen_parse_hhmm(const char *token, int *minutes_out)
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

static void screen_get_next_dispense_string(const pico_bridge_state_t *snapshot, char *out, size_t out_len)
{
	time_t now;
	struct tm ti;
	int current_minutes;
	int best_delta = (24 * 60) + 1;
	int best_minutes = -1;
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

		snprintf(out,
			 out_len,
			 "%02d:%02d FOR %s",
			 best_minutes / 60,
			 best_minutes % 60,
			 slots_buf);
	}
	return;
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

/* Forward declarations for bridge helpers used before their definitions */
static void bridge_copy_string(char *dest, size_t dest_size, const char *src);
static bool bridge_state_save_to_nvs(const pico_bridge_state_t *state);
static void bridge_send_dispense_for_slot(int slot_number);
static void bridge_send_load_profile_for_slot(const pill_slot_state_t *slot_state);

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

static void start_lcd_display(void)
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

static void bridge_reset_slot_defaults(pill_slot_state_t *slot_state, int slot_number)
{
	slot_state->pills_left = -1;
	slot_state->pills_per_dose = -1;
	slot_state->doses_remaining = -1;
	slot_state->total_pills = -1;
	slot_state->slot_number = slot_number;
	slot_state->time_ms = 0;
	strcpy(slot_state->medication_name, "Waiting for data");
	strcpy(slot_state->schedule, "none");
	strcpy(slot_state->last_dispensed, "No confirmed dispense yet");
	strcpy(slot_state->last_event, "Waiting for live data");
	strcpy(slot_state->notes, "Waiting for live pill slot data.");
	strcpy(slot_state->last_dispense_result, "unknown");
	slot_state->has_data = false;
	slot_state->is_active = false;
}

static void bridge_state_reset_defaults(pico_bridge_state_t *state)
{
	int slot_index;

	state->connected = false;
	state->active_profile_slot = 0;
	strcpy(state->controller_transport, "UART bridge pending");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		bridge_reset_slot_defaults(&state->slots[slot_index], slot_index);
	}
	strcpy(state->slots[0].last_event, "ESP dashboard ready. Waiting for Pico 2 data link.");
	strcpy(state->slots[0].notes, "Expect TIME_REQ, BOOT_SYNC, STATUS, and ACK from Pico over UART.");
	state->history_count = 0;
	state->dispense_queue_count = 0;
	strcpy(state->last_ack_action, "none");
	strcpy(state->last_ack_result, "none");
	state->awaiting_dispense_ack = false;
	state->dispense_ack_deadline_us = 0;
	state->last_update_us = 0;
}

static void bridge_copy_string(char *dest, size_t dest_size, const char *src)
{
	if (dest_size == 0) {
		return;
	}

	if (src == NULL) {
		dest[0] = '\0';
		return;
	}

	strncpy(dest, src, dest_size - 1);
	dest[dest_size - 1] = '\0';
}

static int bridge_slot_index_from_number(int slot_number)
{
	if (slot_number < 0 || slot_number >= PILL_SLOT_COUNT) {
		return -1;
	}

	return slot_number;
}

static const pill_slot_state_t *bridge_get_active_slot_const(const pico_bridge_state_t *state)
{
	int slot_index = bridge_slot_index_from_number(state->active_profile_slot);

	if (slot_index < 0) {
		slot_index = 0;
	}

	return &state->slots[slot_index];
}

static bool bridge_state_save_to_nvs(const pico_bridge_state_t *state)
{
	nvs_handle_t handle;
	esp_err_t err = nvs_open(BRIDGE_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
		return false;
	}

	err = nvs_set_blob(handle, "state", state, sizeof(*state));
	if (err == ESP_OK) {
		err = nvs_commit(handle);
	}
	nvs_close(handle);

	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to persist bridge state: %s", esp_err_to_name(err));
		return false;
	}

	return true;
}

static bool bridge_state_load_from_nvs(pico_bridge_state_t *state)
{
	nvs_handle_t handle;
	size_t size = sizeof(*state);
	size_t min_legacy_size = offsetof(pico_bridge_state_t, dispense_queue);
	esp_err_t err = nvs_open(BRIDGE_NVS_NAMESPACE, NVS_READWRITE, &handle);

	if (state == NULL) {
		return false;
	}

	memset(state, 0, sizeof(*state));

	if (err != ESP_OK) {
		ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
		return false;
	}

	err = nvs_get_blob(handle, "state", state, &size);
	nvs_close(handle);
	if (err != ESP_OK) {
		return false;
	}

	/* Accept legacy blobs that predate dispense queue fields. */
	if (size < min_legacy_size || size > sizeof(*state)) {
		return false;
	}

	state->dispense_queue_count = 0;

	return true;
}

static void bridge_log_status_locked(pico_bridge_state_t *state, const pill_slot_state_t *slot_state)
{
	int write_index;

	if (state->history_count < DISPENSE_HISTORY_COUNT) {
		write_index = state->history_count;
		state->history_count++;
	} else {
		memmove(&state->history[0],
				&state->history[1],
				(sizeof(state->history[0]) * (DISPENSE_HISTORY_COUNT - 1)));
		write_index = DISPENSE_HISTORY_COUNT - 1;
	}

	state->history[write_index].slot_number = slot_state->slot_number;
	state->history[write_index].pills_left_after = slot_state->pills_left;
	state->history[write_index].esp_timestamp = (int64_t)time(NULL);
	bridge_copy_string(state->history[write_index].medication_name,
				 sizeof(state->history[write_index].medication_name),
				 slot_state->medication_name);
	bridge_copy_string(state->history[write_index].result,
				 sizeof(state->history[write_index].result),
				 slot_state->last_dispense_result);
	bridge_copy_string(state->history[write_index].event,
				 sizeof(state->history[write_index].event),
				 slot_state->last_event);
}

static void uart_bridge_send_line(const char *line)
{
	if (line == NULL) {
		return;
	}

	uart_write_bytes(UART_BRIDGE_PORT, line, strlen(line));
	ESP_LOGI(TAG, "UART TX: %s", line);
}

static bool bridge_is_time_valid(void)
{
	time_t now = time(NULL);

	return now > 1700000000;
}

static bool bridge_send_set_time(void)
{
	char line[96];
	time_t now = time(NULL);

	if (!bridge_is_time_valid()) {
		ESP_LOGW(TAG, "Skipping SET_TIME because current epoch is invalid");
		return false;
	}

	snprintf(line, sizeof(line), "CMD|action=SET_TIME|epoch=%lld\n", (long long)now);
	uart_bridge_send_line(line);
	return true;
}

static bool bridge_set_local_time(time_t epoch)
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

static void bridge_send_dispense_for_slot(int slot_number)
{
	char line[64];

	if (slot_number < 0) {
		snprintf(line, sizeof(line), "CMD|action=DISPENSE\n");
	} else {
		snprintf(line, sizeof(line), "CMD|action=DISPENSE|slot=%d\n", slot_number);
	}

	uart_bridge_send_line(line);
}

static void bridge_start_dispense_locked(pico_bridge_state_t *state, int slot_number)
{
	if (state == NULL || bridge_slot_index_from_number(slot_number) < 0) {
		return;
	}

	bridge_send_dispense_for_slot(slot_number);
	state->active_profile_slot = slot_number;
	state->awaiting_dispense_ack = true;
	state->dispense_ack_deadline_us = esp_timer_get_time() + DISPENSE_ACK_TIMEOUT_US;
	bridge_copy_string(state->last_ack_action, sizeof(state->last_ack_action), "DISPENSE");
	bridge_copy_string(state->last_ack_result, sizeof(state->last_ack_result), "pending");
}

static bool bridge_enqueue_dispense_slot_locked(pico_bridge_state_t *state, int slot_number)
{
	if (state == NULL || bridge_slot_index_from_number(slot_number) < 0) {
		return false;
	}

	if (state->awaiting_dispense_ack && state->active_profile_slot == slot_number) {
		return true;
	}

	for (int i = 0; i < state->dispense_queue_count; i++) {
		if (state->dispense_queue[i] == slot_number) {
			return true;
		}
	}

	if (state->dispense_queue_count >= PILL_SLOT_COUNT) {
		return false;
	}

	state->dispense_queue[state->dispense_queue_count++] = slot_number;
	return true;
}

static bool bridge_start_next_dispense_locked(pico_bridge_state_t *state)
{
	int next_slot;

	if (state == NULL || state->awaiting_dispense_ack || state->dispense_queue_count <= 0) {
		return false;
	}

	next_slot = state->dispense_queue[0];
	if (state->dispense_queue_count > 1) {
		memmove(&state->dispense_queue[0],
			&state->dispense_queue[1],
			(size_t)(state->dispense_queue_count - 1) * sizeof(state->dispense_queue[0]));
	}
	state->dispense_queue_count--;
	bridge_start_dispense_locked(state, next_slot);
	return true;
}

static void bridge_send_load_profile_for_slot(const pill_slot_state_t *slot_state)
{
	char line[256];
	int total_pills;

	if (slot_state == NULL || !slot_state->is_active) {
		return;
	}

	total_pills = slot_state->pills_left >= 0 ? slot_state->pills_left : slot_state->total_pills;
	if (total_pills <= 0 || slot_state->pills_per_dose <= 0) {
		ESP_LOGW(TAG, "Skipping LOAD_PROFILE for slot %d due to incomplete data", slot_state->slot_number);
		return;
	}

	if (strcmp(slot_state->schedule, "none") == 0 || slot_state->schedule[0] == '\0') {
		snprintf(line,
			 sizeof(line),
			 "CMD|action=LOAD_PROFILE|slot=%d|med=%s|total=%d|dose=%d|time=%lu\n",
			 slot_state->slot_number,
			 slot_state->medication_name,
			 total_pills,
			 slot_state->pills_per_dose,
			 (unsigned long)slot_state->time_ms);
	} else {
		snprintf(line,
			 sizeof(line),
			 "CMD|action=LOAD_PROFILE|slot=%d|med=%s|total=%d|dose=%d|time=%lu|schedule=%s\n",
			 slot_state->slot_number,
			 slot_state->medication_name,
			 total_pills,
			 slot_state->pills_per_dose,
			 (unsigned long)slot_state->time_ms,
			 slot_state->schedule);
	}

	uart_bridge_send_line(line);
}

static int bridge_extract_slot_number(const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');

		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		if (strcmp(token, "slot") == 0) {
			return atoi(separator + 1);
		}
	}

	return -1;
}

static void bridge_handle_ack_line(pico_bridge_state_t *state, const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;
	int ack_slot = -1;
	char action[32] = "unknown";
	char result[32] = "unknown";

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');

		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		if (strcmp(token, "action") == 0) {
			bridge_copy_string(action, sizeof(action), separator + 1);
		} else if (strcmp(token, "result") == 0) {
			bridge_copy_string(result, sizeof(result), separator + 1);
		} else if (strcmp(token, "slot") == 0) {
			ack_slot = atoi(separator + 1);
		}
	}

	bridge_copy_string(state->last_ack_action, sizeof(state->last_ack_action), action);
	bridge_copy_string(state->last_ack_result, sizeof(state->last_ack_result), result);
	if (strcmp(action, "DISPENSE") == 0) {
		state->awaiting_dispense_ack = false;
		state->dispense_ack_deadline_us = 0;
		if (strcmp(result, "ok") == 0) {
			audio_enqueue_event(AUDIO_EVENT_SUCCESS);
			led_enqueue_event(LED_EVENT_SUCCESS, ack_slot);
		} else if (strcmp(result, "fail") == 0 || strcmp(result, "timeout") == 0) {
			audio_enqueue_event(AUDIO_EVENT_FAILURE);
			led_enqueue_event(LED_EVENT_FAILURE, ack_slot);
		}
		bridge_start_next_dispense_locked(state);
	}
	if (ack_slot >= 0 && ack_slot < PILL_SLOT_COUNT) {
		state->active_profile_slot = ack_slot;
	}

	ESP_LOGI(TAG, "Pico ACK action=%s slot=%d result=%s", action, ack_slot, result);
	bridge_state_save_to_nvs(state);
}

static void bridge_apply_boot_sync_field(pill_slot_state_t *slot_state, const char *key, const char *value)
{
	if (strcmp(key, "med") == 0) {
		bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), value);
	} else if (strcmp(key, "left") == 0) {
		slot_state->pills_left = atoi(value);
	} else if (strcmp(key, "dose") == 0) {
		slot_state->pills_per_dose = atoi(value);
	} else if (strcmp(key, "doses") == 0) {
		slot_state->doses_remaining = atoi(value);
	} else if (strcmp(key, "schedule") == 0) {
		bridge_copy_string(slot_state->schedule, sizeof(slot_state->schedule), value);
	}
}

static void bridge_handle_boot_sync_line(pico_bridge_state_t *state, const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;
	int slot_number;
	int slot_index;
	pill_slot_state_t *slot_state;

	slot_number = bridge_extract_slot_number(line);
	slot_index = bridge_slot_index_from_number(slot_number);
	if (slot_index < 0) {
		ESP_LOGW(TAG, "Ignoring BOOT_SYNC with invalid slot number: %d", slot_number);
		return;
	}

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	slot_state = &state->slots[slot_index];
	slot_state->slot_number = slot_number;
	slot_state->is_active = true;
	slot_state->has_data = true;
	state->active_profile_slot = slot_number;
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');

		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		bridge_apply_boot_sync_field(slot_state, token, separator + 1);
	}

	if (slot_state->pills_left >= 0) {
		slot_state->total_pills = slot_state->pills_left;
	}
	bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), "State mirrored from Pico BOOT_SYNC.");
	bridge_state_save_to_nvs(state);
	ESP_LOGI(TAG, "Pico BOOT_SYNC applied for slot %d", slot_number);
}

static bool bridge_append_text(char *buffer, size_t buffer_size, size_t *used, const char *format, ...)
{
	va_list args;
	int written;

	if (*used >= buffer_size) {
		return false;
	}

	va_start(args, format);
	written = vsnprintf(buffer + *used, buffer_size - *used, format, args);
	va_end(args);

	if (written < 0) {
		return false;
	}

	if ((size_t)written >= (buffer_size - *used)) {
		*used = buffer_size - 1;
		return false;
	}

	*used += (size_t)written;
	return true;
}

static void bridge_update_connected_flag_locked(void)
{
	int64_t age_us = esp_timer_get_time() - bridge_state.last_update_us;

	if (bridge_state.awaiting_dispense_ack &&
		bridge_state.dispense_ack_deadline_us > 0 &&
		esp_timer_get_time() > bridge_state.dispense_ack_deadline_us) {
		pill_slot_state_t *timed_out_slot = &bridge_state.slots[bridge_state.active_profile_slot];
		bridge_state.awaiting_dispense_ack = false;
		bridge_state.dispense_ack_deadline_us = 0;
		bridge_copy_string(bridge_state.last_ack_result,
				   sizeof(bridge_state.last_ack_result),
				   "timeout");
		/* Log the timeout as a failed dispense so history shows it */
		bridge_copy_string(timed_out_slot->last_dispense_result,
				   sizeof(timed_out_slot->last_dispense_result),
				   "timeout");
		bridge_copy_string(timed_out_slot->last_event,
				   sizeof(timed_out_slot->last_event),
				   "Dispense timed out - no ACK from Pico.");
		bridge_log_status_locked(&bridge_state, timed_out_slot);
		audio_enqueue_event(AUDIO_EVENT_FAILURE);
		led_enqueue_event(LED_EVENT_FAILURE, bridge_state.active_profile_slot);
		bridge_start_next_dispense_locked(&bridge_state);
	}

	bridge_state.connected = bridge_state.last_update_us > 0 && age_us < UART_BRIDGE_TIMEOUT_US;
	if (!bridge_state.connected) {
		bridge_copy_string(bridge_state.controller_transport,
					   sizeof(bridge_state.controller_transport),
					   "UART bridge pending");
	}
}

static void bridge_apply_field(pico_bridge_state_t *state, pill_slot_state_t *slot_state, const char *key, const char *value)
{
	if (strcmp(key, "med") == 0) {
		bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), value);
	} else if (strcmp(key, "slot") == 0) {
		state->active_profile_slot = atoi(value);
	} else if (strcmp(key, "left") == 0) {
		slot_state->pills_left = atoi(value);
	} else if (strcmp(key, "dose") == 0) {
		slot_state->pills_per_dose = atoi(value);
	} else if (strcmp(key, "doses") == 0) {
		slot_state->doses_remaining = atoi(value);
	} else if (strcmp(key, "total") == 0) {
		slot_state->total_pills = atoi(value);
	} else if (strcmp(key, "time") == 0) {
		slot_state->time_ms = (uint32_t)strtoul(value, NULL, 10);
	} else if (strcmp(key, "last") == 0) {
		bridge_copy_string(slot_state->last_dispensed, sizeof(slot_state->last_dispensed), value);
	} else if (strcmp(key, "event") == 0) {
		bridge_copy_string(slot_state->last_event, sizeof(slot_state->last_event), value);
	} else if (strcmp(key, "notes") == 0) {
		bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), value);
	} else if (strcmp(key, "result") == 0) {
		bridge_copy_string(slot_state->last_dispense_result, sizeof(slot_state->last_dispense_result), value);
	} else if (strcmp(key, "schedule") == 0) {
		bridge_copy_string(slot_state->schedule, sizeof(slot_state->schedule), value);
	}
}

static void bridge_handle_status_line(pico_bridge_state_t *state, const char *line)
{
	char line_copy[UART_BRIDGE_LINE_SIZE];
	char *saveptr = NULL;
	char *token;
	pill_slot_state_t *slot_state;
	int slot_number;
	int slot_index;

	slot_number = bridge_extract_slot_number(line);
	if (slot_number < 0) {
		slot_number = state->active_profile_slot;
	}

	slot_index = bridge_slot_index_from_number(slot_number);
	if (slot_index < 0) {
		ESP_LOGW(TAG, "Ignoring STATUS update with invalid slot number: %d", slot_number);
		return;
	}

	bridge_copy_string(line_copy, sizeof(line_copy), line);
	token = strtok_r(line_copy, "|", &saveptr);
	slot_state = &state->slots[slot_index];
	state->active_profile_slot = slot_number;
	slot_state->slot_number = slot_number;
	slot_state->is_active = true;
	while ((token = strtok_r(NULL, "|", &saveptr)) != NULL) {
		char *separator = strchr(token, '=');
		if (separator == NULL) {
			continue;
		}

		*separator = '\0';
		bridge_apply_field(state, slot_state, token, separator + 1);
	}

	slot_state->has_data = true;
	state->last_update_us = esp_timer_get_time();
	state->connected = true;
	bridge_copy_string(state->controller_transport,
				   sizeof(state->controller_transport),
				   "UART linked");
	bridge_log_status_locked(state, slot_state);
	bridge_state_save_to_nvs(state);

	ESP_LOGI(TAG, "Pico status updated over UART");
}

static void bridge_process_uart_line(char *line)
{
	char raw_line[UART_BRIDGE_LINE_SIZE];
	char *type_end;

	if (line == NULL || line[0] == '\0') {
		return;
	}

	bridge_copy_string(raw_line, sizeof(raw_line), line);
	type_end = strchr(raw_line, '|');
	if (type_end != NULL) {
		*type_end = '\0';
	}

	if (strcmp(raw_line, "TIME_REQ") == 0) {
		ESP_LOGI(TAG, "Pico requested current time");
		if (!bridge_send_set_time()) {
			ESP_LOGW(TAG, "Unable to answer TIME_REQ because ESP time is not valid yet");
		}
		return;
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
		return;
	}

	if (strcmp(raw_line, "BOOT_SYNC") == 0) {
		bridge_handle_boot_sync_line(&bridge_state, line);
	} else if (strcmp(raw_line, "STATUS") == 0) {
		bridge_handle_status_line(&bridge_state, line);
	} else if (strcmp(raw_line, "ACK") == 0) {
		bridge_handle_ack_line(&bridge_state, line);
	} else {
		ESP_LOGI(TAG, "UART RX: %s", line);
	}

	xSemaphoreGive(bridge_state_mutex);
}

/* Checks every active slot's schedule against the current time and enqueues a
 * dispense if a scheduled time matches.  Must be called with the bridge state
 * mutex already held. */
static void bridge_check_schedule_locked(pico_bridge_state_t *state)
{
	/* Stores the minute-boundary timestamp of the last auto-fire per slot so
	 * we never queue the same slot twice in the same clock minute. */
	static time_t last_fired_minute[PILL_SLOT_COUNT];
	time_t now;
	struct tm ti;
	int current_minutes;
	time_t now_minute;
	int si;
	bool any_queued = false;

	now = time(NULL);
	if (now <= 1700000000 || localtime_r(&now, &ti) == NULL) {
		return; /* clock not set yet */
	}

	current_minutes = ti.tm_hour * 60 + ti.tm_min;
	now_minute = now - ti.tm_sec; /* truncate to minute boundary */

	for (si = 0; si < PILL_SLOT_COUNT; si++) {
		const pill_slot_state_t *slot = &state->slots[si];
		char schedule_copy[40];
		char *saveptr = NULL;
		char *token;

		if (!slot->is_active || slot->schedule[0] == '\0' ||
		    strcmp(slot->schedule, "none") == 0) {
			continue;
		}

		if (slot->pills_left == 0) {
			continue; /* empty — skip */
		}

		if (last_fired_minute[si] == now_minute) {
			continue; /* already queued this minute */
		}

		bridge_copy_string(schedule_copy, sizeof(schedule_copy), slot->schedule);
		token = strtok_r(schedule_copy, ",", &saveptr);
		while (token != NULL) {
			int event_minutes;

			if (screen_parse_hhmm(token, &event_minutes) &&
			    event_minutes == current_minutes) {
				if (bridge_enqueue_dispense_slot_locked(state, si)) {
					last_fired_minute[si] = now_minute;
					ESP_LOGI(TAG,
						 "Schedule: queued auto-dispense slot %d at %02d:%02d",
						 si, ti.tm_hour, ti.tm_min);
					any_queued = true;
				}
				break;
			}
			token = strtok_r(NULL, ",", &saveptr);
		}
	}

	if (any_queued) {
		bridge_start_next_dispense_locked(state);
		bridge_state_save_to_nvs(state);
	}
}

static void uart_bridge_task(void *arg)
{
	char line_buffer[UART_BRIDGE_LINE_SIZE];
	size_t line_length = 0;
	uint8_t rx_buffer[64];
	bool discarding_line = false;

	(void)arg;

	while (1) {
		int bytes_read = uart_read_bytes(UART_BRIDGE_PORT,
						   rx_buffer,
						   sizeof(rx_buffer),
						   pdMS_TO_TICKS(100));
		if (bytes_read > 0) {
			for (int index = 0; index < bytes_read; ++index) {
				char current = (char)rx_buffer[index];

				if (current == '\r') {
					continue;
				}

				if (current == '\n') {
					if (!discarding_line) {
						line_buffer[line_length] = '\0';
					}
					if (!discarding_line && line_length > 0) {
						bridge_process_uart_line(line_buffer);
					}
					line_length = 0;
					discarding_line = false;
					continue;
				}

				if (discarding_line) {
					continue;
				}

				if (line_length < (sizeof(line_buffer) - 1)) {
					line_buffer[line_length++] = current;
				} else {
					discarding_line = true;
					line_length = 0;
				}
			}
		}

		if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
			bridge_update_connected_flag_locked();
			bridge_check_schedule_locked(&bridge_state);
			xSemaphoreGive(bridge_state_mutex);
		}
	}
}

static void start_uart_bridge(void)
{
	const uart_config_t uart_config = {
		.baud_rate = UART_BRIDGE_BAUD,
		.data_bits = UART_DATA_8_BITS,
		.parity = UART_PARITY_DISABLE,
		.stop_bits = UART_STOP_BITS_1,
		.flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
		.source_clk = UART_SCLK_DEFAULT,
	};

	bridge_state_mutex = xSemaphoreCreateMutex();
	if (bridge_state_mutex == NULL) {
		ESP_LOGE(TAG, "Failed to create UART bridge mutex");
		return;
	}

	if (!bridge_state_load_from_nvs(&bridge_state)) {
		bridge_state_reset_defaults(&bridge_state);
		bridge_state_save_to_nvs(&bridge_state);
	} else {
		int slot_index;

		bridge_state.connected = false;
		bridge_state.last_update_us = 0;
		/* Dispense-ack state is transient — never restore it across reboots.
		 * The old deadline_us would be stale and the timeout would never fire. */
		bridge_state.awaiting_dispense_ack = false;
		bridge_state.dispense_ack_deadline_us = 0;
		bridge_copy_string(bridge_state.controller_transport,
					   sizeof(bridge_state.controller_transport),
					   "UART bridge pending");
		for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
			bridge_state.slots[slot_index].slot_number = slot_index;
		}
		if (bridge_slot_index_from_number(bridge_state.active_profile_slot) < 0) {
			bridge_state.active_profile_slot = 0;
		}
	}

	ESP_ERROR_CHECK(uart_driver_install(UART_BRIDGE_PORT,
					     UART_BRIDGE_BUFFER_SIZE,
					     0,
					     0,
					     NULL,
					     0));
	ESP_ERROR_CHECK(uart_param_config(UART_BRIDGE_PORT, &uart_config));
	ESP_ERROR_CHECK(uart_set_pin(UART_BRIDGE_PORT,
					  UART_BRIDGE_TX_PIN,
					  UART_BRIDGE_RX_PIN,
					  UART_PIN_NO_CHANGE,
					  UART_PIN_NO_CHANGE));

	ESP_LOGI(TAG,
		 "UART bridge ready on ESP GPIO%d(TX) and GPIO%d(RX). Waiting for Pico to push STATUS events.",
		 UART_BRIDGE_TX_PIN,
		 UART_BRIDGE_RX_PIN);
	xTaskCreate(uart_bridge_task, "uart_bridge", 4096, NULL, 5, NULL);
}

static void dns_server_task(void *arg)
{
	int sock;
	struct sockaddr_in listen_addr;

	(void)arg;

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (sock < 0) {
		ESP_LOGE(TAG, "DNS socket create failed");
		vTaskDelete(NULL);
		return;
	}

	memset(&listen_addr, 0, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_port = htons(DNS_PORT);
	listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sock, (struct sockaddr *)&listen_addr, sizeof(listen_addr)) < 0) {
		ESP_LOGE(TAG, "DNS socket bind failed");
		close(sock);
		vTaskDelete(NULL);
		return;
	}

	ESP_LOGI(TAG, "Captive DNS started on UDP 53");

	while (1) {
		uint8_t request[512];
		uint8_t response[512];
		struct sockaddr_in source_addr;
		socklen_t source_addr_len = sizeof(source_addr);
		ssize_t req_len = recvfrom(sock,
							   request,
							   sizeof(request),
							   0,
							   (struct sockaddr *)&source_addr,
							   &source_addr_len);

		if (req_len < 12) {
			continue;
		}

		if (request[2] & 0x80) {
			continue;
		}

		int index = 12;
		while (index < req_len && request[index] != 0) {
			index += request[index] + 1;
		}

		if ((index + 5) >= req_len) {
			continue;
		}

		int question_len = (index + 1) - 12 + 4;
		if ((12 + question_len) > req_len) {
			continue;
		}

		memcpy(response, request, 12 + question_len);
		response[2] = 0x81;
		response[3] = 0x80;
		response[6] = 0x00;
		response[7] = 0x01;
		response[8] = 0x00;
		response[9] = 0x00;
		response[10] = 0x00;
		response[11] = 0x00;

		int resp_len = 12 + question_len;
		response[resp_len++] = 0xC0;
		response[resp_len++] = 0x0C;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x01;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x01;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x3C;
		response[resp_len++] = 0x00;
		response[resp_len++] = 0x04;
		response[resp_len++] = AP_IP_OCTET_1;
		response[resp_len++] = AP_IP_OCTET_2;
		response[resp_len++] = AP_IP_OCTET_3;
		response[resp_len++] = AP_IP_OCTET_4;

		sendto(sock,
			   response,
			   resp_len,
			   0,
			   (struct sockaddr *)&source_addr,
			   source_addr_len);
	}
}

static void start_captive_dns(void)
{
	xTaskCreate(dns_server_task, "dns_server", 4096, NULL, 4, NULL);
}

static esp_err_t redirect_to_root(httpd_req_t *req)
{
	httpd_resp_set_status(req, "302 Found");
	httpd_resp_set_hdr(req, "Location", "/");
	return httpd_resp_send(req, NULL, 0);
}

static esp_err_t apple_captive_handler(httpd_req_t *req)
{
	const char *response = "<html><body>Login</body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static void get_device_time_string(char *out, size_t out_len)
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

static const char *json_bool(bool value)
{
	return value ? "true" : "false";
}

static bool ap_uses_password(void)
{
	return strlen(AP_PASS) >= 8;
}

static esp_err_t read_http_body(httpd_req_t *req, char *buffer, size_t buffer_size)
{
	int total_received = 0;

	if (buffer_size == 0) {
		return ESP_ERR_INVALID_SIZE;
	}

	while (total_received < req->content_len && total_received < (int)(buffer_size - 1)) {
		int received = httpd_req_recv(req,
					     buffer + total_received,
					     buffer_size - 1 - total_received);

		if (received <= 0) {
			return ESP_FAIL;
		}

		total_received += received;
	}

	buffer[total_received] = '\0';
	return total_received == req->content_len ? ESP_OK : ESP_ERR_HTTPD_RESULT_TRUNC;
}

static bool http_body_get_string(const char *body, const char *key, char *value, size_t value_size)
{
	char *src;
	char *dst;

	if (httpd_query_key_value(body, key, value, value_size) == ESP_OK) {
		/* Decode application/x-www-form-urlencoded values in-place. */
		src = value;
		dst = value;
		while (*src != '\0') {
			if (*src == '+') {
				*dst++ = ' ';
				src++;
			} else if (src[0] == '%' && src[1] != '\0' && src[2] != '\0') {
				char hi = src[1];
				char lo = src[2];
				int hi_val = (hi >= '0' && hi <= '9') ? (hi - '0') :
					     (hi >= 'A' && hi <= 'F') ? (hi - 'A' + 10) :
					     (hi >= 'a' && hi <= 'f') ? (hi - 'a' + 10) : -1;
				int lo_val = (lo >= '0' && lo <= '9') ? (lo - '0') :
					     (lo >= 'A' && lo <= 'F') ? (lo - 'A' + 10) :
					     (lo >= 'a' && lo <= 'f') ? (lo - 'a' + 10) : -1;
				if (hi_val >= 0 && lo_val >= 0) {
					*dst++ = (char)((hi_val << 4) | lo_val);
					src += 3;
				} else {
					*dst++ = *src++;
				}
			} else {
				*dst++ = *src++;
			}
		}
		*dst = '\0';
		return true;
	}

	value[0] = '\0';
	return false;
}

static bool http_body_get_int(const char *body, const char *key, int *value)
{
	char temp[16];

	if (!http_body_get_string(body, key, temp, sizeof(temp))) {
		return false;
	}

	*value = atoi(temp);
	return true;
}

static bool bridge_validate_medication_name(const char *name)
{
	if (name == NULL || name[0] == '\0') {
		return false;
	}

	return strchr(name, '|') == NULL && strchr(name, '\n') == NULL && strchr(name, '\r') == NULL;
}

static esp_err_t send_json_response(httpd_req_t *req, const char *status, const char *body)
{
	httpd_resp_set_status(req, status);
	httpd_resp_set_type(req, "application/json");
	return httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t profile_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char medication_name[32];
	char schedule[40];
	int slot_number;
	int total_pills;
	int dose;
	int time_ms;
	pill_slot_state_t *slot_state;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_int(body, "slot", &slot_number) ||
		!http_body_get_int(body, "total", &total_pills) ||
		!http_body_get_int(body, "dose", &dose) ||
		!http_body_get_int(body, "time", &time_ms) ||
		!http_body_get_string(body, "med", medication_name, sizeof(medication_name))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing required fields");
	}

	if (bridge_slot_index_from_number(slot_number) < 0 || total_pills <= 0 || dose <= 0 || !bridge_validate_medication_name(medication_name)) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid profile data");
	}

	http_body_get_string(body, "schedule", schedule, sizeof(schedule));
	if (schedule[0] == '\0') {
		strcpy(schedule, "none");
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return send_json_response(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"bridge_busy\"}");
	}

	slot_state = &bridge_state.slots[slot_number];
	slot_state->slot_number = slot_number;
	slot_state->total_pills = total_pills;
	slot_state->pills_per_dose = dose;
	slot_state->time_ms = (uint32_t)time_ms;
	slot_state->is_active = true;
	slot_state->has_data = true;
	bridge_copy_string(slot_state->medication_name, sizeof(slot_state->medication_name), medication_name);
	bridge_copy_string(slot_state->schedule, sizeof(slot_state->schedule), schedule);
	/* Always reset to the new total — LOAD_PROFILE resets the Pico count to
	 * total= anyway, so the ESP cache must always match after a profile save. */
	slot_state->pills_left = total_pills;
	slot_state->doses_remaining = dose > 0 ? total_pills / dose : -1;
	bridge_copy_string(slot_state->notes, sizeof(slot_state->notes), "Profile saved on ESP and sent to Pico.");
	bridge_state.active_profile_slot = slot_number;
	bridge_state_save_to_nvs(&bridge_state);
	bridge_send_load_profile_for_slot(slot_state);
	xSemaphoreGive(bridge_state_mutex);

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t dispense_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char slots_csv[64];
	bool has_slots_csv;
	char slots_copy[64];
	char *saveptr = NULL;
	char *token;
	int slot_number;
	int enqueued_count = 0;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	has_slots_csv = http_body_get_string(body, "slots", slots_csv, sizeof(slots_csv)) && slots_csv[0] != '\0';

	if (!http_body_get_int(body, "slot", &slot_number)) {
		slot_number = -1;
	}

	if (xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
		return send_json_response(req, "503 Service Unavailable", "{\"ok\":false,\"error\":\"bridge_busy\"}");
	}

	bridge_update_connected_flag_locked();
	if (has_slots_csv) {
		bridge_copy_string(slots_copy, sizeof(slots_copy), slots_csv);
		token = strtok_r(slots_copy, ",", &saveptr);
		while (token != NULL) {
			int requested_slot = atoi(token);
			if (bridge_slot_index_from_number(requested_slot) < 0 ||
			    !bridge_enqueue_dispense_slot_locked(&bridge_state, requested_slot)) {
				xSemaphoreGive(bridge_state_mutex);
				return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid or full dispense queue");
			}
			enqueued_count++;
			token = strtok_r(NULL, ",", &saveptr);
		}
		if (enqueued_count == 0) {
			xSemaphoreGive(bridge_state_mutex);
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No valid slots in slots list");
		}
	} else {
		if (slot_number < 0) {
			slot_number = bridge_state.active_profile_slot;
		}

		if (bridge_slot_index_from_number(slot_number) < 0 ||
		    !bridge_enqueue_dispense_slot_locked(&bridge_state, slot_number)) {
			xSemaphoreGive(bridge_state_mutex);
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot number or full queue");
		}
	}

	bridge_start_next_dispense_locked(&bridge_state);
	bridge_state_save_to_nvs(&bridge_state);
	xSemaphoreGive(bridge_state_mutex);

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t time_sync_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	time_t epoch = 0;
	char epoch_str[24];

	if (req->content_len > 0) {
		if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
			return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
		}
		if (http_body_get_string(body, "epoch", epoch_str, sizeof(epoch_str))) {
			epoch = (time_t)strtoll(epoch_str, NULL, 10);
		}
	}

	if (epoch > 0 && !bridge_set_local_time(epoch)) {
		return send_json_response(req, "500 Internal Server Error", "{\"ok\":false,\"error\":\"settimeofday_failed\"}");
	}

	if (!bridge_send_set_time()) {
		return send_json_response(req, "409 Conflict", "{\"ok\":false,\"error\":\"time_invalid\"}");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t feedback_test_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char result[16];

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_string(body, "result", result, sizeof(result))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing result field");
	}

	if (strcmp(result, "success") == 0) {
		audio_enqueue_event(AUDIO_EVENT_SUCCESS);
		led_enqueue_event(LED_EVENT_SUCCESS, -1);
	} else if (strcmp(result, "fail") == 0 || strcmp(result, "failure") == 0) {
		audio_enqueue_event(AUDIO_EVENT_FAILURE);
		led_enqueue_event(LED_EVENT_FAILURE, -1);
	} else {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid result value");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t edit_mode_post_handler(httpd_req_t *req)
{
	char body[HTTP_BODY_BUFFER_SIZE];
	char state[16];
	int slot_number;

	if (read_http_body(req, body, sizeof(body)) != ESP_OK) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request body");
	}

	if (!http_body_get_int(body, "slot", &slot_number) ||
	    bridge_slot_index_from_number(slot_number) < 0) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid slot");
	}

	if (!http_body_get_string(body, "state", state, sizeof(state))) {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing state field");
	}

	if (strcmp(state, "on") == 0 || strcmp(state, "start") == 0) {
		led_enqueue_event(LED_EVENT_EDIT_BEGIN, slot_number);
	} else if (strcmp(state, "off") == 0 || strcmp(state, "end") == 0) {
		led_enqueue_event(LED_EVENT_EDIT_END, slot_number);
	} else {
		return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid state value");
	}

	return send_json_response(req, "200 OK", "{\"ok\":true}");
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
	char time_buf[64];
	char *response;
	char *slots_json;
	char *history_json;
	pico_bridge_state_t *snapshot;
	size_t slots_used = 0;
	size_t hist_used  = 0;
	bool has_fail = false;
	bool has_low  = false;
	const pill_slot_state_t *active_slot;
	int slot_index;
	esp_err_t result;

	response     = malloc(STATUS_RESPONSE_BUFFER_SIZE);
	slots_json   = malloc(STATUS_SLOTS_BUFFER_SIZE);
	history_json = malloc(HISTORY_JSON_BUFFER_SIZE);
	snapshot     = malloc(sizeof(*snapshot));
	if (response == NULL || slots_json == NULL || history_json == NULL || snapshot == NULL) {
		free(response);
		free(slots_json);
		free(history_json);
		free(snapshot);
		ESP_LOGE(TAG, "Failed to allocate status response buffers");
		return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
	}

	get_device_time_string(time_buf, sizeof(time_buf));
	memset(snapshot, 0, sizeof(*snapshot));

	if (bridge_state_mutex != NULL && xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
		bridge_update_connected_flag_locked();
		*snapshot = bridge_state;
		xSemaphoreGive(bridge_state_mutex);
	} else {
		bridge_state_reset_defaults(snapshot);
	}

	active_slot = bridge_get_active_slot_const(snapshot);
	bridge_append_text(slots_json, STATUS_SLOTS_BUFFER_SIZE, &slots_used, "[");
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; ++slot_index) {
		const pill_slot_state_t *slot_state = &snapshot->slots[slot_index];

		bridge_append_text(slots_json,
				   STATUS_SLOTS_BUFFER_SIZE,
				   &slots_used,
				   "%s{\"slot\":%d,\"has_data\":%s,\"is_active\":%s,\"medication_name\":\"%s\",\"pills_left\":%d,\"pills_per_dose\":%d,\"doses_remaining\":%d,\"total_pills\":%d,\"time_ms\":%lu,\"schedule\":\"%s\",\"last_dispensed\":\"%s\",\"last_event\":\"%s\",\"last_dispense_result\":\"%s\",\"notes\":\"%s\"}",
				   slot_index == 0 ? "" : ",",
				   slot_state->slot_number,
				   json_bool(slot_state->has_data),
				   json_bool(slot_state->is_active),
				   slot_state->medication_name,
				   slot_state->pills_left,
				   slot_state->pills_per_dose,
				   slot_state->doses_remaining,
				   slot_state->total_pills,
				   (unsigned long)slot_state->time_ms,
				   slot_state->schedule,
				   slot_state->last_dispensed,
				   slot_state->last_event,
				   slot_state->last_dispense_result,
				   slot_state->notes);
	}
	bridge_append_text(slots_json, STATUS_SLOTS_BUFFER_SIZE, &slots_used, "]");

	/* Compute alert flags and build history JSON array (newest first) */
	for (slot_index = 0; slot_index < PILL_SLOT_COUNT; slot_index++) {
		const pill_slot_state_t *s = &snapshot->slots[slot_index];
		if (!s->is_active) continue;
		if (strcmp(s->last_dispense_result, "fail") == 0 ||
		    strcmp(s->last_dispense_result, "timeout") == 0) has_fail = true;
		if (s->pills_left >= 0 && s->pills_left < LOW_PILL_THRESHOLD) has_low = true;
	}
	bridge_append_text(history_json, HISTORY_JSON_BUFFER_SIZE, &hist_used, "[");
	for (slot_index = snapshot->history_count - 1; slot_index >= 0; slot_index--) {
		const dispense_history_entry_t *h = &snapshot->history[slot_index];
		bridge_append_text(history_json, HISTORY_JSON_BUFFER_SIZE, &hist_used,
				   "%s{\"slot\":%d,\"medication\":\"%s\",\"result\":\"%s\","
				   "\"pills_left_after\":%d,\"event\":\"%s\",\"time\":%lld}",
				   slot_index == snapshot->history_count - 1 ? "" : ",",
				   h->slot_number, h->medication_name, h->result,
				   h->pills_left_after, h->event, (long long)h->esp_timestamp);
	}
	bridge_append_text(history_json, HISTORY_JSON_BUFFER_SIZE, &hist_used, "]");

	snprintf(response,
			 STATUS_RESPONSE_BUFFER_SIZE,
			 "{"
			 "\"device_time\":\"%s\","
			 "\"bridge_connected\":%s,"
			 "\"bridge_status\":\"waiting_for_pico\","
			 "\"controller_name\":\"Raspberry Pi Pico 2\","
			 "\"controller_transport\":\"%s\","
			 "\"active_profile_slot\":%d,"
			 "\"slot_count\":%d,"
			 "\"history_count\":%d,"
			 "\"last_ack_action\":\"%s\","
			 "\"last_ack_result\":\"%s\","
			 "\"awaiting_dispense_ack\":%s,"
			 "\"medication_name\":\"%s\","
			 "\"pills_left\":%d,"
			 "\"pills_per_dose\":%d,"
			 "\"doses_remaining\":%d,"
			 "\"dispense_mode\":\"sensor_based\","
			 "\"piezo_enabled\":true,"
			 "\"hall_enabled\":true,"
			 "\"ir_enabled\":true,"
			 "\"last_dispensed\":\"%s\","
			 "\"last_event\":\"%s\","
			 "\"last_dispense_result\":\"%s\","
			 "\"notes\":\"%s\","
			 "\"dispense_fail\":%s,"
			 "\"low_pill_warn\":%s,"
			 "\"history\":%s,"
			 "\"slots\":%s"
			 "}",
			 time_buf,
			 json_bool(snapshot->connected),
			 snapshot->controller_transport,
			 snapshot->active_profile_slot,
			 PILL_SLOT_COUNT,
			 snapshot->history_count,
			 snapshot->last_ack_action,
			 snapshot->last_ack_result,
			 json_bool(snapshot->awaiting_dispense_ack),
			 active_slot->medication_name,
			 active_slot->pills_left,
			 active_slot->pills_per_dose,
			 active_slot->doses_remaining,
			 active_slot->last_dispensed,
			 active_slot->last_event,
			 active_slot->last_dispense_result,
			 active_slot->notes,
			 json_bool(has_fail),
			 json_bool(has_low),
			 history_json,
			 slots_json);

	httpd_resp_set_type(req, "application/json");
	result = httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
	free(response);
	free(slots_json);
	free(history_json);
	free(snapshot);
	return result;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
	const char *response =
		"<!doctype html><html><head><meta charset=\"utf-8\">"
		"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
		"<title>Pill Dispenser Home</title>"
		"<style>"
		":root{color-scheme:light;--bg:#f4efe7;--ink:#112027;--muted:#5d6b70;--panel:#fffaf3;--line:rgba(17,32,39,.1);--shadow:0 22px 60px rgba(17,32,39,.14);--teal:#0f766e;--teal-soft:#d8f1ee;--amber:#c77b18;--amber-soft:#fff0d8;}"
		"*{box-sizing:border-box;}"
		"body{margin:0;font-family:\"Trebuchet MS\",\"Segoe UI Variable\",sans-serif;color:var(--ink);background:radial-gradient(circle at top left,#fff8ef 0,#f4efe7 45%,#e9f3f1 100%);min-height:100vh;}"
		"body:before,body:after{content:\"\";position:fixed;border-radius:999px;filter:blur(12px);opacity:.45;pointer-events:none;}"
		"body:before{width:280px;height:280px;background:#f5d6a5;top:-90px;right:-70px;}"
		"body:after{width:220px;height:220px;background:#b7e4db;left:-60px;bottom:-40px;}"
		".shell{max-width:980px;margin:0 auto;padding:24px 18px 40px;}"
		".hero{position:relative;overflow:hidden;background:linear-gradient(135deg,#12333b 0,#184f5b 52%,#1b6d67 100%);color:#f7fbfb;border-radius:28px;padding:24px;box-shadow:var(--shadow);margin-bottom:18px;}"
		".hero:after{content:\"\";position:absolute;inset:auto -40px -70px auto;width:240px;height:240px;border-radius:50%;background:rgba(255,255,255,.08);box-shadow:-120px -70px 0 rgba(255,255,255,.06);pointer-events:none;}"
		".eyebrow{letter-spacing:.16em;text-transform:uppercase;font-size:.72rem;opacity:.78;margin-bottom:10px;}"
		"h1{font-family:Georgia,\"Times New Roman\",serif;font-size:clamp(2rem,7vw,3.8rem);line-height:.96;margin:0;max-width:8ch;}"
		".lede{max-width:40rem;margin:14px 0 0;font-size:1rem;line-height:1.5;color:rgba(247,251,251,.82);}"
		".stack{display:grid;gap:18px;}"
		".panel{background:rgba(255,250,243,.9);border:1px solid rgba(255,255,255,.6);border-radius:24px;padding:18px;box-shadow:var(--shadow);backdrop-filter:blur(10px);}"
		".panel-head{display:flex;justify-content:space-between;gap:12px;align-items:flex-start;margin-bottom:16px;}"
		".panel-title{margin:0;font-size:1.05rem;letter-spacing:.04em;text-transform:uppercase;color:var(--muted);font-weight:700;}"
		".status-pill{display:inline-flex;align-items:center;gap:8px;padding:10px 14px;border-radius:999px;font-weight:700;background:var(--amber-soft);color:#8a5410;border:1px solid rgba(199,123,24,.16);}"
		".status-pill.online{background:var(--teal-soft);color:#0e5d58;border-color:rgba(15,118,110,.18);}"
		".dot{width:10px;height:10px;border-radius:50%;background:currentColor;box-shadow:0 0 0 6px rgba(255,255,255,.18);}"
		".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:14px;}"
		".metric{padding:16px;border-radius:20px;background:#fffdf9;border:1px solid var(--line);}"
		".slot-card{transition:border-color .2s ease,transform .2s ease,box-shadow .2s ease;}"
		".slot-card.active{border-color:rgba(15,118,110,.5);box-shadow:0 16px 36px rgba(15,118,110,.12);transform:translateY(-2px);}"
		".metric .label{font-size:.8rem;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin-bottom:10px;}"
		".metric .value{font-family:Georgia,\"Times New Roman\",serif;font-size:2rem;line-height:1;margin-bottom:8px;}"
		".metric .hint{font-size:.95rem;color:var(--muted);line-height:1.35;}"
		".accent-teal{background:linear-gradient(180deg,#f7fffe 0,#ecfaf8 100%);}"
		".accent-amber{background:linear-gradient(180deg,#fffaf3 0,#fff1dc 100%);}"
		".connection-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:14px;}"
		".summary{padding:16px;border-radius:20px;background:#fffdf9;border:1px solid var(--line);min-height:124px;}"
		".summary .label{font-size:.8rem;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin-bottom:10px;}"
		".summary .value{font-size:1.15rem;font-weight:700;line-height:1.25;}"
		".summary .hint{margin-top:8px;color:var(--muted);font-size:.94rem;line-height:1.35;}"
		".list{display:grid;gap:12px;}"
		".row{display:flex;justify-content:space-between;gap:14px;padding:12px 0;border-bottom:1px solid var(--line);}"
		".row:last-child{border-bottom:none;padding-bottom:0;}"
		".row:first-child{padding-top:0;}"
		".k{color:var(--muted);}"
		".v{font-weight:700;text-align:right;}"
		".result-ok{color:#0e5d58;}"
		".result-fail{color:#b91c1c;}"
		".footer{display:flex;flex-wrap:wrap;gap:10px;margin-top:18px;color:var(--muted);font-size:.92rem;}"
		".controls{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;}"
		".edit-panel{display:none;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px;margin-top:10px;}"
		"label{display:grid;gap:6px;font-size:.9rem;color:var(--muted);}"
		"input,select,button{font:inherit;border-radius:14px;border:1px solid var(--line);padding:10px 12px;background:#fffdf9;color:var(--ink);}"
		"button{cursor:pointer;background:#12333b;color:#f7fbfb;border:none;}"
		"button.alt{background:#e7efe7;color:#12333b;border:1px solid var(--line);}"
		".control-actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;}"
		".status-copy{font-size:.92rem;color:var(--muted);margin-top:10px;}"
		".footer-card{padding:12px 14px;border-radius:16px;background:rgba(255,255,255,.58);border:1px solid rgba(255,255,255,.7);}"
		".alert-banner{display:none;background:#fef2f2;border:1.5px solid #fca5a5;border-radius:16px;padding:14px 18px;color:#b91c1c;font-weight:700;margin-bottom:18px;}"
		".alert-banner.visible{display:block;}"
		".warn-banner{display:none;background:#fffbeb;border:1.5px solid #fcd34d;border-radius:16px;padding:14px 18px;color:#92400e;font-weight:700;margin-bottom:18px;}"
		".warn-banner.visible{display:block;}"
		".hist-table{width:100%;border-collapse:collapse;font-size:.9rem;}"
		".hist-table th{text-align:left;color:var(--muted);font-size:.78rem;letter-spacing:.1em;text-transform:uppercase;padding:6px 4px;border-bottom:1px solid var(--line);}"
		".hist-table td{padding:8px 4px;border-bottom:1px solid var(--line);}"
		".hist-table tr:last-child td{border-bottom:none;}"
		"@media (max-width:760px){.shell{padding:14px 14px 28px;}.hero{padding:20px;}}"
		"</style></head><body>"
		"<main class=\"shell\">"
		"<section class=\"hero\">"
		"<div class=\"eyebrow\">ESP32 access point dashboard</div>"
		"<h1>Pill Dispenser Home</h1>"
		"<p class=\"lede\">Use this page to check connection status, review each slot, save medication settings, and run a dispense when needed.</p>"
		"</section>"
		"<div class=\"stack\">"
		"<div id=\"fail-banner\" class=\"alert-banner\">&#9888; Dispense failure detected &mdash; check the dispenser.</div>"
		"<div id=\"low-pill-banner\" class=\"warn-banner\">&#9888; Low pill count &mdash; one or more slots need refilling soon.</div>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\">"
		"<div><p class=\"panel-title\">System Status</p><div id=\"bridge-copy\">Dashboard is running. Waiting for dispenser connection.</div></div>"
		"<div class=\"status-pill\" id=\"bridge-pill\"><span class=\"dot\"></span><span id=\"bridge-label\">Connecting...</span></div>"
		"</div>"
		"<div class=\"connection-grid\">"
		"<article class=\"summary accent-teal\"><div class=\"label\">Controller</div><div class=\"value\" id=\"controller-name\">Raspberry Pi Pico 2</div><div class=\"hint\">This board controls the motors and confirms dispense events.</div></article>"
		"<article class=\"summary accent-amber\"><div class=\"label\">Connection</div><div class=\"value\" id=\"controller-transport\">Waiting for dispenser link</div><div class=\"hint\">Shows whether live updates are arriving from the dispenser controller.</div></article>"
		"<article class=\"summary accent-teal\"><div class=\"label\">Device Time</div><div class=\"value\" id=\"device-time\">Loading...</div><div class=\"hint\">Current time reported by the ESP dashboard host.</div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Medication Slots</p><div>All five slots are shown below. The highlighted card is the currently active slot.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-0\"><div class=\"label\">Slot 0</div><div class=\"value\" id=\"slot-medication-0\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-0\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-0\">--</span> | Doses remaining: <span id=\"slot-doses-0\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-0\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-0\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-1\"><div class=\"label\">Slot 1</div><div class=\"value\" id=\"slot-medication-1\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-1\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-1\">--</span> | Doses remaining: <span id=\"slot-doses-1\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-1\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-1\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-2\"><div class=\"label\">Slot 2</div><div class=\"value\" id=\"slot-medication-2\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-2\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-2\">--</span> | Doses remaining: <span id=\"slot-doses-2\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-2\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-2\">unknown</span></div></article>"
		"<article class=\"metric accent-amber slot-card\" id=\"slot-card-3\"><div class=\"label\">Slot 3</div><div class=\"value\" id=\"slot-medication-3\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-3\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-3\">--</span> | Doses remaining: <span id=\"slot-doses-3\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-3\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-3\">unknown</span></div></article>"
		"<article class=\"metric accent-teal slot-card\" id=\"slot-card-4\"><div class=\"label\">Slot 4</div><div class=\"value\" id=\"slot-medication-4\">Waiting for data</div><div class=\"hint\">Pills left: <span id=\"slot-left-4\">--</span></div><div class=\"hint\">Dose: <span id=\"slot-dose-4\">--</span> | Doses remaining: <span id=\"slot-doses-4\">--</span></div><div class=\"hint\">Schedule: <span id=\"slot-schedule-4\">none</span></div><div class=\"hint\">Result: <span id=\"slot-result-4\">unknown</span></div></article>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Selected Slot Details</p><div>Latest medication and dispense details for the active slot.</div></div></div>"
		"<div class=\"grid\">"
		"<article class=\"metric accent-teal\"><div class=\"label\">Medication</div><div class=\"value\" id=\"medication-name\">Waiting for data</div><div class=\"hint\">Active profile slot <span id=\"profile-slot\">0</span>.</div></article>"
		"<article class=\"metric accent-amber\"><div class=\"label\">Pills Left</div><div class=\"value\" id=\"pills-left\">--</div><div class=\"hint\"><span id=\"doses-remaining\">--</span> full doses remaining at <span id=\"pills-per-dose\">--</span> pills per dose.</div></article>"
		"</div>"
		"<div class=\"list\">"
		"<div class=\"row\"><span class=\"k\">Last Dispense</span><span class=\"v\" id=\"last-dispensed\">No confirmed dispense yet</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Event</span><span class=\"v\" id=\"last-event\">Waiting for live data</span></div>"
		"<div class=\"row\"><span class=\"k\">Last Result</span><span class=\"v\" id=\"last-result\">Waiting</span></div>"
		"<div class=\"row\"><span class=\"k\">Notes</span><span class=\"v\" id=\"dashboard-notes\">Waiting for live pill slot data.</span></div>"
		"</div>"
		"</section>"
		"<section class=\"panel\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Actions</p><div>Pick a slot, update its settings, run a dispense, or test lights and sounds.</div></div></div>"
		"<div class=\"controls\">"
		"<label>Slot<select id=\"control-slot\"><option value=\"0\">Slot 0</option><option value=\"1\">Slot 1</option><option value=\"2\">Slot 2</option><option value=\"3\">Slot 3</option><option value=\"4\">Slot 4</option></select></label>"
		"<label>Manual Time<input id=\"control-manual-time\" type=\"datetime-local\"></label>"
		"</div>"
		"<div class=\"edit-panel\" id=\"edit-panel\">"
		"<label>Medication Name<input id=\"control-med\" maxlength=\"31\" placeholder=\"Aspirin\"></label>"
		"<label>Pills in Slot<input id=\"control-total\" type=\"number\" min=\"1\" step=\"1\" value=\"20\"></label>"
		"<label>Pills per Dose<input id=\"control-dose\" type=\"number\" min=\"1\" step=\"1\" value=\"1\"></label>"
		"<label>Dispense Time (ms)<input id=\"control-time\" type=\"number\" min=\"100\" step=\"50\" value=\"800\"></label>"
		"<label>Daily Schedule<input id=\"control-schedule\" placeholder=\"Example: 08:00,20:00 (or none)\"></label>"
		"</div>"
		"<div class=\"control-actions\">"
		"<button id=\"edit-start\" type=\"button\">Edit Slot Settings</button>"
		"<button id=\"save-profile\" type=\"button\">Save Slot Settings</button>"
		"<button id=\"dispense-slot\" type=\"button\">Dispense Now</button>"
		"<button id=\"sync-time\" type=\"button\" class=\"alt\">Sync Clock</button>"
		"<button id=\"test-success\" type=\"button\" class=\"alt\">Test Success Alert</button>"
		"<button id=\"test-fail\" type=\"button\" class=\"alt\">Test Failure Alert</button>"
		"</div>"
		"<div class=\"status-copy\" id=\"control-status\">Choose an action to begin.</div>"
		"</section>"
		"<section class=\"panel\" id=\"history-panel\" style=\"display:none\">"
		"<div class=\"panel-head\"><div><p class=\"panel-title\">Dispense History</p><div>Most recent dispense events, newest first.</div></div></div>"
		"<table class=\"hist-table\"><thead><tr><th>#</th><th>Slot</th><th>Medication</th><th>Result</th><th>Pills After</th><th>Time</th></tr></thead>"
		"<tbody id=\"history-body\"><tr><td colspan=\"6\">No history yet.</td></tr></tbody></table>"
		"</section>"
		"<div class=\"footer\">"
		"<div class=\"footer-card\">Wi-Fi SSID: ESP-Time-Server</div>"
		"<div class=\"footer-card\">Password: Open network</div>"
		"<div class=\"footer-card\">Portal URL: http://192.168.4.1</div>"
		"</div>"
		"</section>"
		"</div>"
		"</main>"
		"<script>"
		"const displayNumber=v=>typeof v==='number'&&v>=0?String(v):'--';"
		"const control=(id)=>document.getElementById(id);"
		"const controlIds=['control-slot','control-med','control-total','control-dose','control-time','control-schedule','control-manual-time'];"
		"let latestSlots=[];"
		"let webEditActive=false;"
		"let webEditSlot='0';"
		"const text=(id,value)=>{const el=document.getElementById(id);if(el)el.textContent=value;};"
		"const toLocalDateTimeValue=date=>{const pad=v=>String(v).padStart(2,'0');return date.getFullYear()+'-'+pad(date.getMonth()+1)+'-'+pad(date.getDate())+'T'+pad(date.getHours())+':'+pad(date.getMinutes());};"
		"const setResult=(id,value)=>{const el=document.getElementById(id);if(!el)return;const normalized=value||'unknown';el.textContent=normalized==='ok'?'Success':normalized==='fail'?'Missed':normalized==='timeout'?'No Response':normalized;el.className=(id==='last-result'?'v ':'')+(normalized==='ok'?'result-ok':normalized==='fail'||normalized==='timeout'?'result-fail':'');};"
		"const setActiveSlotCard=slot=>{for(let n=0;n<5;n+=1){const card=document.getElementById('slot-card-'+n);if(card)card.className='metric '+(n%2===0?'accent-teal ':'accent-amber ')+'slot-card'+(n===slot?' active':'');}};"
		"const setStatusCopy=msg=>text('control-status',msg);"
		"const setSlotCard=slot=>{if(!slot||slot.slot==null)return;text('slot-medication-'+slot.slot,slot.medication_name||'Waiting for data');text('slot-left-'+slot.slot,displayNumber(slot.pills_left));text('slot-dose-'+slot.slot,displayNumber(slot.pills_per_dose));text('slot-doses-'+slot.slot,displayNumber(slot.doses_remaining));text('slot-schedule-'+slot.slot,slot.schedule||'none');setResult('slot-result-'+slot.slot,slot.last_dispense_result||'unknown');};"
		"const isEditingControls=()=>{const active=document.activeElement;return Boolean(active&&controlIds.includes(active.id));};"
		"const getSlotByNumber=slotNumber=>latestSlots.find(slot=>slot&&slot.slot===slotNumber);"
		"const fillControlsFromSlot=slot=>{if(!slot)return;control('control-slot').value=String(slot.slot);control('control-med').value=slot.medication_name&&slot.medication_name!=='Waiting for data'?slot.medication_name:'';control('control-total').value=slot.total_pills>0?slot.total_pills:20;control('control-dose').value=slot.pills_per_dose>0?slot.pills_per_dose:1;control('control-time').value=slot.time_ms>0?slot.time_ms:800;control('control-schedule').value=slot.schedule&&slot.schedule!=='none'?slot.schedule:'';};"
		"const postForm=async(url,data)=>{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});if(!r.ok)throw new Error(await r.text());return r.json();};"
		"const setEditPanel=(open)=>{const panel=document.getElementById('edit-panel');const startBtn=control('edit-start');const saveBtn=control('save-profile');if(panel)panel.style.display=open?'grid':'none';if(startBtn)startBtn.style.display=open?'none':'';if(saveBtn)saveBtn.style.display=open?'':'none';};"
		"const setWebEditMode=async(on)=>{const slot=control('control-slot').value;await postForm('/api/edit-mode',{slot,state:on?'on':'off'});webEditActive=on;webEditSlot=slot;};"
		"const saveProfile=async()=>{const data={slot:control('control-slot').value,med:control('control-med').value,total:control('control-total').value,dose:control('control-dose').value,time:control('control-time').value,schedule:control('control-schedule').value||'none'};await postForm('/api/profile',data);setStatusCopy('Slot settings saved.');await refreshStatus();if(webEditActive){await setWebEditMode(false);setEditPanel(false);}};"
		"const dispenseSelected=async()=>{await postForm('/api/dispense',{slot:control('control-slot').value});setStatusCopy('Dispense started. Waiting for confirmation...');await refreshStatus();};"
		"const syncTime=async()=>{const raw=control('control-manual-time').value;if(!raw)throw new Error('missing-time');const epoch=Math.floor(new Date(raw+'Z').getTime()/1000);if(!Number.isFinite(epoch)||epoch<=0)throw new Error('invalid-time');await postForm('/api/time-sync',{epoch:String(epoch)});setStatusCopy('Clock sync sent.');};"
		"const testFeedback=async(result)=>{await postForm('/api/test-feedback',{result});setStatusCopy(result==='success'?'Success alert test sent.':'Failure alert test sent.');};"
		"const setBridgeState=(connected,status)=>{const pill=document.getElementById('bridge-pill');text('bridge-label',connected?'Connected':'Connecting...');text('bridge-copy',connected?'Live updates are coming in from the dispenser controller.':'Dashboard is running. Waiting for dispenser connection.');if(pill)pill.className=connected?'status-pill online':'status-pill';if(status){text('controller-transport',status);} };"
		"async function refreshStatus(){"
		"try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('bad-response');const d=await r.json();"
		"text('device-time',d.device_time||'Unavailable');"
		"text('controller-name',d.controller_name||'Raspberry Pi Pico 2');"
		"text('controller-transport',d.controller_transport||'Waiting for dispenser link');"
		"text('medication-name',d.medication_name||'No active profile');"
		"text('profile-slot',d.active_profile_slot!=null?d.active_profile_slot:'-');"
		"text('pills-left',displayNumber(d.pills_left));"
		"text('pills-per-dose',displayNumber(d.pills_per_dose));"
		"text('doses-remaining',displayNumber(d.doses_remaining));"
		"text('last-dispensed',d.last_dispensed||'No confirmed dispense yet');"
		"text('last-event',d.last_event||'Waiting for live data');"
		"setResult('last-result',d.last_dispense_result||'unknown');"
		"text('dashboard-notes',d.notes||'Waiting for live pill slot data.');"
		"const slots=Array.isArray(d.slots)?d.slots:[];latestSlots=slots;slots.forEach(setSlotCard);"
		"if(!isEditingControls()){const selected=Number(control('control-slot').value);fillControlsFromSlot(getSlotByNumber(selected) || slots.find(slot=>slot&&slot.slot===Number(d.active_profile_slot)) || slots[0]);}"
		"setActiveSlotCard(Number(d.active_profile_slot)||0);"
		"if(d.awaiting_dispense_ack){setStatusCopy('Waiting for dispenser confirmation...');}"
		"setBridgeState(Boolean(d.bridge_connected),d.controller_transport);"
		"const failBanner=document.getElementById('fail-banner');if(failBanner)failBanner.className='alert-banner'+(d.dispense_fail?' visible':'');"
		"const lowBanner=document.getElementById('low-pill-banner');if(lowBanner)lowBanner.className='warn-banner'+(d.low_pill_warn?' visible':'');"
		"const history=Array.isArray(d.history)?d.history:[];"
		"const histPanel=document.getElementById('history-panel');if(histPanel)histPanel.style.display=history.length>0?'':'none';"
		"const histBody=document.getElementById('history-body');if(histBody&&history.length>0){histBody.innerHTML=history.map((h,i)=>{const t=h.time>0?new Date(h.time*1000).toLocaleTimeString():'--';const res=h.result==='ok'?'Success':h.result==='fail'?'Missed':h.result==='timeout'?'No Response':h.result||'--';return '<tr><td>'+(i+1)+'</td><td>'+h.slot+'</td><td>'+(h.medication||'--')+'</td><td>'+res+'</td><td>'+displayNumber(h.pills_left_after)+'</td><td>'+t+'</td></tr>';}).join('');}"
		"}catch(e){text('device-time','Disconnected');text('last-event','ESP status endpoint is unavailable.');setBridgeState(false,'ESP status unavailable');}"
		"}"
		"control('edit-start').addEventListener('click',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));setWebEditMode(true).then(()=>{setEditPanel(true);setStatusCopy('Edit mode on for selected slot.');}).catch(()=>setStatusCopy('Could not start edit mode.'));});"
		"control('save-profile').addEventListener('click',()=>{saveProfile().catch(()=>setStatusCopy('Could not save slot settings.'));});"
		"control('dispense-slot').addEventListener('click',()=>{dispenseSelected().catch(()=>setStatusCopy('Could not start dispense.'));});"
		"control('sync-time').addEventListener('click',()=>{syncTime().catch(()=>setStatusCopy('Pick a valid date/time first.'));});"
		"control('test-success').addEventListener('click',()=>{testFeedback('success').catch(()=>setStatusCopy('Could not run success alert test.'));});"
		"control('test-fail').addEventListener('click',()=>{testFeedback('fail').catch(()=>setStatusCopy('Could not run failure alert test.'));});"
		"if(!control('control-manual-time').value){control('control-manual-time').value=toLocalDateTimeValue(new Date());}"
		"control('control-slot').addEventListener('change',()=>{fillControlsFromSlot(getSlotByNumber(Number(control('control-slot').value)));if(webEditActive){setWebEditMode(true).catch(()=>{});}});"
		"setEditPanel(false);"
		"refreshStatus();setInterval(refreshStatus,1500);"
		"</script></body></html>";

	httpd_resp_set_type(req, "text/html");
	return httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

static void start_webserver(void)
{
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	httpd_handle_t server = NULL;
	config.max_uri_handlers = 16;
	config.stack_size = 10240;
	config.uri_match_fn = httpd_uri_match_wildcard;
	config.max_open_sockets = 4; // Reserve sockets for DNS + lwIP internals (total lwIP sockets = 10)

	if (httpd_start(&server, &config) == ESP_OK) {
		httpd_uri_t root = {
			.uri = "/",
			.method = HTTP_GET,
			.handler = root_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t status = {
			.uri = "/api/status",
			.method = HTTP_GET,
			.handler = status_get_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t profile = {
			.uri = "/api/profile",
			.method = HTTP_POST,
			.handler = profile_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t dispense = {
			.uri = "/api/dispense",
			.method = HTTP_POST,
			.handler = dispense_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t time_sync = {
			.uri = "/api/time-sync",
			.method = HTTP_POST,
			.handler = time_sync_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t feedback_test = {
			.uri = "/api/test-feedback",
			.method = HTTP_POST,
			.handler = feedback_test_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t edit_mode = {
			.uri = "/api/edit-mode",
			.method = HTTP_POST,
			.handler = edit_mode_post_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t android_204 = {
			.uri = "/generate_204",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t android_gen_204 = {
			.uri = "/gen_204",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_hotspot = {
			.uri = "/hotspot-detect.html",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_library_test = {
			.uri = "/library/test/success.html",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_success_txt = {
			.uri = "/success.txt",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t apple_mobile_status = {
			.uri = "/mobile/status.php",
			.method = HTTP_GET,
			.handler = apple_captive_handler,
			.user_ctx = NULL,
		};
		httpd_uri_t msft_ncsi = {
			.uri = "/ncsi.txt",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t msft_connect = {
			.uri = "/connecttest.txt",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};
		httpd_uri_t catch_all = {
			.uri = "/*",
			.method = HTTP_GET,
			.handler = redirect_to_root,
			.user_ctx = NULL,
		};

		httpd_register_uri_handler(server, &root);
		httpd_register_uri_handler(server, &status);
		httpd_register_uri_handler(server, &profile);
		httpd_register_uri_handler(server, &dispense);
		httpd_register_uri_handler(server, &time_sync);
		httpd_register_uri_handler(server, &feedback_test);
		httpd_register_uri_handler(server, &edit_mode);
		httpd_register_uri_handler(server, &android_204);
		httpd_register_uri_handler(server, &android_gen_204);
		httpd_register_uri_handler(server, &apple_hotspot);
		httpd_register_uri_handler(server, &apple_library_test);
		httpd_register_uri_handler(server, &apple_success_txt);
		httpd_register_uri_handler(server, &apple_mobile_status);
		httpd_register_uri_handler(server, &msft_ncsi);
		httpd_register_uri_handler(server, &msft_connect);
		httpd_register_uri_handler(server, &catch_all);
		ESP_LOGI(TAG, "HTTP server started");
	} else {
		ESP_LOGE(TAG, "Failed to start HTTP server");
	}
}

static void start_wifi_ap(void)
{
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	bool use_password = ap_uses_password();
	wifi_config_t ap_config = {
		.ap = {
			.ssid = AP_SSID,
			.ssid_len = strlen(AP_SSID),
			.channel = 1,
			.password = AP_PASS,
			.max_connection = AP_MAX_CONN,
			.authmode = use_password ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN,
			.pmf_cfg = {
				.required = false,
			},
		},
	};

	if (!use_password) {
		ESP_LOGW(TAG,
				 "AP password must be at least 8 characters for WPA/WPA2. Starting open network for SSID %s.",
				 AP_SSID);
	}

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());
	esp_netif_create_default_wifi_ap();

	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_start());
	esp_wifi_set_max_tx_power(84); /* 84 = 21 dBm, maximum */

	ESP_LOGI(TAG, "Wi-Fi AP started. SSID: %s, Password: %s", AP_SSID, use_password ? AP_PASS : "<open>");
	ESP_LOGI(TAG, "Open portal: http://192.168.4.1/");
}

void app_main(void)
{
	esp_err_t ret = nvs_flash_init();
	if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
		ESP_ERROR_CHECK(nvs_flash_erase());
		ret = nvs_flash_init();
	}
	ESP_ERROR_CHECK(ret);

	start_audio_feedback();
	start_led_feedback();
	start_lcd_display();
	start_uart_bridge();
	start_wifi_ap();
	start_captive_dns();
	start_webserver();
}
