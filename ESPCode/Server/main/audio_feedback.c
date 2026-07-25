#include "audio_feedback.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "time_server";

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

static QueueHandle_t audio_event_queue;
static i2s_chan_handle_t audio_i2s_tx;

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

void audio_enqueue_event(audio_event_t event)
{
    if (audio_event_queue == NULL) {
        return;
    }

    if (xQueueSend(audio_event_queue, &event, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Audio event queue full, dropping tone event=%d", (int)event);
    }
}

void start_audio_feedback(void)
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
