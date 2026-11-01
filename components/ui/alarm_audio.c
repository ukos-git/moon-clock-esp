/**
 * @file alarm_audio.c
 * @brief The alarm ring (P4: the board's ES8311 codec + speaker amp)
 *
 * The P4 (XC 4C) carries an ES8311 codec on the shared I2C bus (with the
 * GT911 touch) plus a speaker amp (the amp PA pin is driven by the codec
 * init). The S3 has no audio hardware: there the functions below are no-op
 * stubs and the alarm stays display-only (its path is untouched).
 *
 * Design: the codec device handle is created once at boot (init is cheap —
 * it only configures the ES8311 registers and enables the amp GPIO). The
 * RING itself is a short-lived task per alarm: it opens the codec (I2S TX +
 * DAC), streams a pre-baked two-tone loop while ramping the volume up
 * gradually (~2.5 s to full — no startup click at a full level), then on
 * the touch dismiss fades out (~150 ms) and closes (the next alarm re-opens
 * it). The I2S write is BLOCKING (portMAX_DELAY), so one chunk of N samples
 * takes N/FS real seconds: the stream self-paces at the audio rate and the
 * ramp advances with wall time.
 */

#include <math.h>
#include <stdint.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "alarm_audio.h"

static const char *TAG = "alarm_audio";

#if defined(CONFIG_BOARD_P4)

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"

#define RING_SAMPLE_RATE 22050 // the BSP's bsp_audio_init default (mono 16-bit)
#define RING_TONE_MS 500       // one loop = A5 250 ms + E5 250 ms
#define TONE_SAMPLES (RING_SAMPLE_RATE * RING_TONE_MS / 1000)
#define CHUNK_SAMPLES 1024   // ~47 ms of audio per blocking write
#define RING_VOL_FULL 75     // of 100 (the default curve: 0 -> -50 dB, 100 -> 0 dB)
#define RING_RAMP_MS 2500    // the gradual volume ramp (0 -> full)
#define RING_FADE_STEP_MS 10 // the stop fade-out granularity (~150 ms total)

static esp_codec_dev_handle_t s_codec_dev = NULL;
static volatile bool s_ringing = false;
static volatile bool s_stop = false;

// One loop of the two-tone alarm (A5 / E5, 250 ms each, 10 ms attack/release
// envelope per tone to kill the on/off clicks). Baked once at init; the ring
// task just streams it.
static int16_t s_tone[TONE_SAMPLES];

static void ring_tone_gen(void) {
    const int half = TONE_SAMPLES / 2;                // 250 ms per tone
    const int fade = (int)(0.010 * RING_SAMPLE_RATE); // 10 ms
    const double amp = 0.5 * 32767.0;
    const double freqs[2] = {880.0, 659.255};
    for (int h = 0; h < 2; h++) {
        double phase = 0.0;
        const double w = 2.0 * M_PI * freqs[h] / (double)RING_SAMPLE_RATE;
        for (int i = 0; i < half; i++) {
            phase += w;
            double env = 1.0;
            if (i < fade) {
                env = (double)i / (double)fade;
            } else if (i >= half - fade) {
                env = (double)(half - i) / (double)fade;
            }
            s_tone[h * half + i] = (int16_t)(sin(phase) * amp * env);
        }
    }
}

static void ring_task(void *arg) {
    (void)arg;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 1,
        .sample_rate = RING_SAMPLE_RATE,
        .mclk_multiple = 0, // MCLK = 256 x FS
    };
    int ret = esp_codec_dev_open(s_codec_dev, &fs);
    if (ret != 0) {
        ESP_LOGE(TAG, "codec open failed (%d): alarm stays display-only", ret);
        s_ringing = false;
        vTaskDelete(NULL);
        return;
    }
    esp_codec_dev_set_out_vol(s_codec_dev, 0);
    ESP_LOGI(TAG, "Alarm ring started (ramp %d ms -> vol %d)", RING_RAMP_MS, RING_VOL_FULL);

    const TickType_t start = xTaskGetTickCount();
    int last_vol = 0;
    int off = 0;
    while (!s_stop) {
        // The gradual ramp (the blocking writes keep the stream real-time,
        // so task ticks advance with the audio that is actually sounding).
        int vol =
            (int)(((uint32_t)(xTaskGetTickCount() - start) * (uint32_t)portTICK_PERIOD_MS * (uint32_t)RING_VOL_FULL) /
                  (uint32_t)RING_RAMP_MS);
        if (vol > RING_VOL_FULL) {
            vol = RING_VOL_FULL;
        }
        if (vol != last_vol) {
            esp_codec_dev_set_out_vol(s_codec_dev, vol);
            last_vol = vol;
        }
        // Stream one chunk of the loop (the blocking write paces it).
        int remain = TONE_SAMPLES - off;
        int n = CHUNK_SAMPLES < remain ? CHUNK_SAMPLES : remain;
        ret = esp_codec_dev_write(s_codec_dev, (int16_t *)s_tone + off, n * (int)sizeof(int16_t));
        off += n;
        if (off >= TONE_SAMPLES) {
            off = 0;
        }
        if (ret != 0) {
            ESP_LOGW(TAG, "ring write failed (%d); stopping the ring", ret);
            break;
        }
    }

    // The touch snooze: fade the volume out (~150 ms — the I2S keeps
    // streaming the drained DMA, so the fade rides on silence), then close
    // (DAC + I2S TX disabled; the next alarm re-opens the codec).
    for (int vol = last_vol; vol > 0; vol -= 5) {
        esp_codec_dev_set_out_vol(s_codec_dev, vol);
        vTaskDelay(pdMS_TO_TICKS(RING_FADE_STEP_MS));
    }
    esp_codec_dev_set_out_vol(s_codec_dev, 0);
    esp_codec_dev_close(s_codec_dev);
    ESP_LOGI(TAG, "Alarm ring stopped");
    s_ringing = false;
    vTaskDelete(NULL);
}

void alarm_audio_init(void) {
    // bsp_audio_codec_speaker_init: bsp_i2c_init (idempotent — the GT911
    // touch already runs on that bus; the ES8311 just joins it) +
    // bsp_audio_init (I2S master TX/RX, mono 16-bit @22050) + the ES8311
    // DAC with the amp PA pin (GPIO53). Soft-fails like the IMU.
    s_codec_dev = bsp_audio_codec_speaker_init();
    if (s_codec_dev == NULL) {
        ESP_LOGW(TAG, "ES8311 speaker init failed: the alarm stays display-only");
        return;
    }
    ring_tone_gen();
    ESP_LOGI(TAG, "Alarm audio ready (ES8311 + amp)");
}

void alarm_audio_ring_start(void) {
    if (s_ringing) {
        return;
    }
    if (s_codec_dev == NULL) {
        ESP_LOGW(TAG, "audio unavailable; the alarm stays display-only");
        return;
    }
    s_ringing = true;
    s_stop = false;
    if (xTaskCreate(ring_task, "alarm_ring", 4096, NULL, 4, NULL) != pdPASS) {
        s_ringing = false;
        ESP_LOGE(TAG, "ring task create failed");
    }
}

void alarm_audio_ring_stop(void) {
    if (!s_ringing) {
        return;
    }
    s_stop = true; // the ring task does the fade + teardown (~150 ms)
}

#else

// The S3 has no audio hardware: the alarm is display-only (its path is
// untouched).

void alarm_audio_init(void) {}
void alarm_audio_ring_start(void) {}
void alarm_audio_ring_stop(void) {}

#endif
