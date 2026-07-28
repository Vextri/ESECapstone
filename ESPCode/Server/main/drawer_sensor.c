#include "drawer_sensor.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bridge_state.h"

static const char *TAG = "time_server";

/* Wired directly to the ESP32, separate from anything on the Pico. */
#define HALL_SIGNAL_PIN 33
#define HALL_TRIGGER_LEVEL 0
#define HALL_POLL_MS 20

static void drawer_sensor_task(void *arg)
{
	int prev_level;

	(void)arg;
	prev_level = gpio_get_level(HALL_SIGNAL_PIN);

	while (1) {
		int level = gpio_get_level(HALL_SIGNAL_PIN);

		if (level != prev_level) {
			prev_level = level;
			if (level == HALL_TRIGGER_LEVEL) {
				ESP_LOGI(TAG, "Drawer sensor triggered on GPIO%d", HALL_SIGNAL_PIN);
				if (bridge_state_mutex != NULL &&
				    xSemaphoreTake(bridge_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
					if (bridge_state.awaiting_drawer_open) {
						bridge_mark_dispense_taken_locked(&bridge_state);
					} else {
						ESP_LOGI(TAG, "Drawer opened with no pending dispense pickup");
					}
					xSemaphoreGive(bridge_state_mutex);
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(HALL_POLL_MS));
	}
}

void start_drawer_sensor(void)
{
	gpio_config_t cfg = {
		.pin_bit_mask = (1ULL << HALL_SIGNAL_PIN),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_DISABLE,
	};

	ESP_ERROR_CHECK(gpio_config(&cfg));
	xTaskCreate(drawer_sensor_task, "drawer_sensor", 2048, NULL, 2, NULL);
	ESP_LOGI(TAG, "Drawer sensor ready on GPIO%d (trigger level=%d)", HALL_SIGNAL_PIN, HALL_TRIGGER_LEVEL);
}
