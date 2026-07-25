#include "led_feedback.h"

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "time_server";

#define LED_WS2812_GPIO GPIO_NUM_9
#define LED_COUNT 3
#define LED_FLASH_ON_MS 140
#define LED_FLASH_OFF_MS 120
#define LED_EDIT_R 140
#define LED_EDIT_G 0
#define LED_EDIT_B 180

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

void led_enqueue_event(led_event_t event, int slot)
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

void start_led_feedback(void)
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
