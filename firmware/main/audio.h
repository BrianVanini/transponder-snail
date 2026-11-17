#pragma once

#include <stddef.h>
#include <stdint.h>
#include "driver/gpio.h"

// I2S audio: ICS-43434 mic on I2S0 (input) and MAX98357A amp on I2S1 (output).
// Pins and wiring: docs/breadboard-setup.md section 8.
// Both sides use 16-bit signed mono samples at AUDIO_SAMPLE_RATE. The voice
// streaming code will reuse these functions later.

#define AUDIO_SAMPLE_RATE    16000   // Hz. Must stay in the mic's 6.25-18.75 kHz low-power range.
#define AUDIO_DEFAULT_VOLUME 25      // %, software volume for the amp (protects the small speaker)

// Mic pins (I2S0)
#define MIC_BCLK_GPIO  GPIO_NUM_26
#define MIC_WS_GPIO    GPIO_NUM_25
#define MIC_DOUT_GPIO  GPIO_NUM_35   // mic DOUT -> ESP32 (input-only pin, fine here)
// Amp pins (I2S1)
#define AMP_BCLK_GPIO  GPIO_NUM_18
#define AMP_LRC_GPIO   GPIO_NUM_19
#define AMP_DIN_GPIO   GPIO_NUM_21   // ESP32 -> amp DIN

// What the last audio_mic_read() looked like on the wire (before any gain).
// Used by the mic meter to give wiring hints.
typedef enum {
    MIC_OK,          // changing data: the mic is talking
    MIC_NO_DATA,     // every sample was exactly 0: DOUT not connected, or mic has no power/clock
    MIC_STUCK,       // every sample was the same non-zero value (e.g. -1 = DOUT stuck high)
    MIC_WRONG_SLOT,  // our slot was all 0 but the other slot had data: change MIC_SLOT in audio.c
} mic_status_t;

// Start the mic clock, then throw away the first ~50 ms while the mic wakes up.
void audio_mic_init(void);

// Read n samples (waits for them, at most ~100 ms per chunk).
// Returns how many samples were actually read (less than n on timeout/error).
size_t audio_mic_read(int16_t *buf, size_t n);

mic_status_t audio_mic_status(void);

// Start the amp. It plays silence until audio_amp_write() is called.
void audio_amp_init(void);

// Play n samples (waits until they are queued). Samples are scaled by the volume.
void audio_amp_write(const int16_t *buf, size_t n);

// Software volume, 0-100 (%).
void audio_amp_set_volume(int percent);
