/* ============================================================================
 * DRAWER_SENSOR.C - Pickup Confirmation Sensor Driver
 * ----------------------------------------------------------------------------
 * Polls a hall-effect sensor wired directly to the ESP (independent of the
 * Pico) that trips when the pill drawer is opened, and reports that event
 * into the shared bridge_state so a dispensed dose can be marked as
 * actually picked up.
 * ============================================================================ */

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

/* ----------------------------------------------------------------------------
 * drawer_sensor_task()
 * ----------------------------------------------------------------------------
 * Polls the sensor pin every HALL_POLL_MS and looks for an edge into the
 * trigger level (drawer opened). On a trigger, unconditionally calls
 * bridge_mark_dispense_taken_locked() rather than checking any "is a
 * dispense pending" flag first, that function already checks each slot's
 * own state internally and safely does nothing if no slot is actually
 * waiting on pickup, so there's no need to duplicate that check here.
 * Runs forever once started.
 * ---------------------------------------------------------------------------- */
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
					bridge_mark_dispense_taken_locked(&bridge_state);
					xSemaphoreGive(bridge_state_mutex);
				}
			}
		}

		vTaskDelay(pdMS_TO_TICKS(HALL_POLL_MS));
	}
}

/* Configures the sensor pin as a pulled-up input and starts
 * drawer_sensor_task(). Call once at boot, after start_uart_bridge() so
 * bridge_state_mutex already exists. */
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
