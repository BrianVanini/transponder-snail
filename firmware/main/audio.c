#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "audio.h"

// --- Mic settings ---------------------------------------------------------
// The ICS-43434 sends 24-bit samples in a 32-bit slot, 2 slots (left/right) per frame.
// With SEL low it talks in the LEFT slot and lets go of the line in the right slot.
// We read both slots (stereo) and keep one. On the ESP32 the buffer order is
// [left, right, left, right, ...], so MIC_SLOT 0 = left. If the mic meter says
// "wrong slot", set this to 1.
#define MIC_SLOT        0

// Each raw 32-bit value holds the 24-bit sample in its top 24 bits (low 8 bits are 0).
// Shifting right by 16 keeps the top 16 bits = a normal 16-bit sample. But the mic is
// built for loud sounds (it only clips at 120 dB), so normal speech comes out very
// quiet. MIC_GAIN_SHIFT throws away fewer bits instead: each +1 doubles the level
// (+6 dB). Too high and loud sounds clip (they are clamped, not wrapped).
#define MIC_GAIN_SHIFT  2

#define MIC_WAKE_MS     50      // samples to throw away after the clock starts
#define MIC_CHUNK       256     // frames read from the driver at a time
#define READ_TIMEOUT_MS 100

static i2s_chan_handle_t mic_rx;
static i2s_chan_handle_t amp_tx;
static int32_t mic_raw[MIC_CHUNK * 2];      // stereo: 2 values per frame
static mic_status_t mic_status = MIC_NO_DATA;
static int amp_volume = AUDIO_DEFAULT_VOLUME;

void audio_mic_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan, NULL, &mic_rx));   // RX only

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        // 32-bit stereo = 64 BCLK ticks per frame, which the ICS-43434 requires.
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,     // the mic has no master clock pin
            .bclk = MIC_BCLK_GPIO,
            .ws   = MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din  = MIC_DOUT_GPIO,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(mic_rx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(mic_rx));

    // Mic wake-up: read and discard ~50 ms.
    int16_t junk[MIC_CHUNK];
    for (int got = 0; got < AUDIO_SAMPLE_RATE * MIC_WAKE_MS / 1000; ) {
        size_t n = audio_mic_read(junk, MIC_CHUNK);
        if (n == 0) break;   // no clock/data at all; the meter will report it
        got += n;
    }
}

// Raw 32-bit slot value -> 16-bit sample with gain, clamped so loud sounds don't wrap around.
static int16_t mic_convert(int32_t raw)
{
    int32_t s = raw >> (16 - MIC_GAIN_SHIFT);
    if (s > INT16_MAX) s = INT16_MAX;
    if (s < INT16_MIN) s = INT16_MIN;
    return (int16_t)s;
}

size_t audio_mic_read(int16_t *buf, size_t n)
{
    size_t done = 0;
    bool any_nonzero = false, all_same = true, other_nonzero = false;
    int32_t first = 0;

    while (done < n) {
        size_t frames = n - done;
        if (frames > MIC_CHUNK) frames = MIC_CHUNK;
        size_t bytes = 0;
        esp_err_t err = i2s_channel_read(mic_rx, mic_raw, frames * 2 * sizeof(int32_t),
                                         &bytes, READ_TIMEOUT_MS);
        size_t got = bytes / (2 * sizeof(int32_t));
        for (size_t i = 0; i < got; i++) {
            int32_t raw = mic_raw[i * 2 + MIC_SLOT];
            if (done == 0 && i == 0) first = raw;
            if (raw != 0) any_nonzero = true;
            if (raw != first) all_same = false;
            if (mic_raw[i * 2 + (1 - MIC_SLOT)] != 0) other_nonzero = true;
            buf[done + i] = mic_convert(raw);
        }
        done += got;
        if (err != ESP_OK || got == 0) break;
    }

    // Decide what the wire looked like, for the mic meter's hints.
    if (done == 0)            mic_status = MIC_NO_DATA;
    else if (!any_nonzero)    mic_status = other_nonzero ? MIC_WRONG_SLOT : MIC_NO_DATA;
    else if (all_same)        mic_status = MIC_STUCK;
    else                      mic_status = MIC_OK;
    return done;
}

mic_status_t audio_mic_status(void) { return mic_status; }

void audio_amp_init(void)
{
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    // Clear each DMA buffer after it is sent. Without this, when we stop writing,
    // the amp keeps repeating the last buffer (a loud buzz) instead of going silent.
    chan.auto_clear_after_cb = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan, &amp_tx, NULL));   // TX only

    i2s_std_config_t cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        // 16-bit stereo. We put the same sample in left and right; the amp plays (L+R)/2.
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,     // the MAX98357A doesn't need a master clock
            .bclk = AMP_BCLK_GPIO,
            .ws   = AMP_LRC_GPIO,
            .dout = AMP_DIN_GPIO,
            .din  = I2S_GPIO_UNUSED,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(amp_tx, &cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(amp_tx));
}

void audio_amp_set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    amp_volume = percent;
}

void audio_amp_write(const int16_t *buf, size_t n)
{
    int16_t out[128 * 2];   // stereo chunk: same sample in L and R
    while (n > 0) {
        size_t frames = n > 128 ? 128 : n;
        for (size_t i = 0; i < frames; i++) {
            int16_t s = (int16_t)((int32_t)buf[i] * amp_volume / 100);
            out[i * 2] = s;
            out[i * 2 + 1] = s;
        }
        size_t written = 0;
        i2s_channel_write(amp_tx, out, frames * 2 * sizeof(int16_t), &written, portMAX_DELAY);
        buf += frames;
        n -= frames;
    }
}
