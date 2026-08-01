/* ============================================================================
 * AUDIO_FEEDBACK.H - I2S Tone Playback
 * ----------------------------------------------------------------------------
 * Plays short melodic tones over I2S (success chime, failure buzz, edit-
 * mode cue) using a synthesized sine wave rather than any pre-recorded
 * audio file. Events are queued, same pattern as led_feedback, so any task
 * can request a sound without owning the I2S peripheral itself.
 * ============================================================================ */

#ifndef AUDIO_FEEDBACK_H
#define AUDIO_FEEDBACK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* One note in a melody: a frequency in Hz (0 = silent rest) held for a
 * duration in milliseconds. */
typedef struct {
    float freq;
    int dur_ms;
} audio_note_t;

typedef enum {
    AUDIO_EVENT_SUCCESS = 1,
    AUDIO_EVENT_FAILURE = 2,
    AUDIO_EVENT_EDIT_BEGIN = 3,
} audio_event_t;

/* Queues a tone event for playback. Safe to call from any task; never
 * blocks. */
void audio_enqueue_event(audio_event_t event);

/* Initializes the I2S peripheral and starts the task that consumes queued
 * events. Call once at boot. */
void start_audio_feedback(void);

#ifdef __cplusplus
}
#endif

#endif
