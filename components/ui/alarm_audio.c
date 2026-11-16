/**
 * @file alarm_audio.c
 * @brief The alarm ring + sound selection (both boards: the ES8311 codec +
 * speaker amp)
 *
 * Both boards carry an ES8311 codec on the shared I2C bus plus a speaker
 * amp. Both BSPs (the managed P4 BSP and the vendored S3 BSP) expose the
 * same bring-up — bsp_audio_codec_speaker_init(): bsp_i2c_init
 * (idempotent — the touch/IMU/RTC already run on that bus; the ES8311
 * just joins it) + bsp_audio_init (I2S master TX/RX, mono 16-bit @22050)
 * + the ES8311 DAC with the amp PA pin. An init failure soft-fails like
 * the IMU: the alarm stays display-only (the sound selection still
 * remembers its choice).
 *
 * Four baked loops (1 s each, mono s16 @22050 — ~44 KB each, ~176 KB
 * total, in PSRAM): 0 two-tone A5/E5 (the original default), 1 classic
 * digital beep (2 kHz square, 3x200 ms bursts), 2 soft bell chime (the
 * 880 Hz fundamental + partials, exponential decay), 3 melodic arpeggio
 * (C5-E5-G5-C6, 4x250 ms). The RING itself is a short-lived task per
 * alarm: it opens the codec (I2S TX + DAC), streams the SELECTED loop
 * while ramping the volume up gradually (~2.5 s to full — no startup
 * click at a full level), then on the touch dismiss fades out (~150 ms)
 * and closes (the next alarm re-opens it). The PREVIEW (settings screen)
 * plays its loop once with a short ramp + fade as its own short-lived
 * task; the codec streams one loop at a time, so the two never overlap.
 * The I2S write is BLOCKING (portMAX_DELAY), so one chunk of N samples
 * takes N/FS real seconds: the stream self-paces at the audio rate and
 * the ramp advances with wall time.
 */

#include <math.h>
#include <stdint.h>

#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "alarm_audio.h"
#include "settings_store.h"

static const char *TAG = "alarm_audio";

#define RING_SAMPLE_RATE 22050                           // the BSP's bsp_audio_init default (mono 16-bit)
#define LOOP_MS 1000                                     // one loop of any sound
#define LOOP_SAMPLES (RING_SAMPLE_RATE * LOOP_MS / 1000) // 22050
#define CHUNK_SAMPLES 1024                               // ~47 ms of audio per blocking write
#define RING_VOL_FULL 75                                 // of 100 (the default curve: 0 -> -50 dB, 100 -> 0 dB)
#define RING_RAMP_MS 2500                                // the ring's gradual volume ramp (0 -> full)
#define PREV_VOL_FULL 75                                 // the preview's full level
#define PREV_RAMP_MS 300                                 // the preview's short ramp
#define RING_FADE_STEP_MS 10                             // the stop fade-out granularity (~150 ms total)
/* The hard ring cap (user decision 2026-11-16: "the alarm should not
 * fire longer than 30min"): the play_task breaks on its OWN elapsed
 * deadline, so the audio stops unconditionally -- even while the UI
 * tick is parked in a sleep state (the UI-tick auto-dismiss in
 * actions.c rides the same Kconfig value). The #ifndef seed only serves
 * hosts without the generated sdkconfig (the Kconfig-`n`-leaves-the-
 * macro-undefined gotcha; on device the Kconfig value always wins). */
#ifndef CONFIG_UI_ALARM_RING_MAX_MIN
#define CONFIG_UI_ALARM_RING_MAX_MIN 30
#endif
#define RING_CAP_MS ((uint32_t)CONFIG_UI_ALARM_RING_MAX_MIN * 60000u)

static esp_codec_dev_handle_t s_codec_dev = NULL;
static int16_t *s_loops = NULL; // ALARM_SOUND_COUNT x LOOP_SAMPLES (PSRAM)
static volatile int s_selected = 0;
static volatile bool s_ringing = false;
static volatile bool s_stop = false;
static volatile bool s_busy = false; // one player task at a time (codec is shared)

/* The player task's args (the s_busy guard keeps them single-writer). */
struct play_arg {
    int loop;
    bool looping;
    bool is_ring;
    int ramp_ms;
    int vol_full;
};
static struct play_arg s_play;

static const char *ALARM_NAMES[ALARM_SOUND_COUNT] = {"Two-Tone", "Beep", "Bell", "Arpeggio"};

int alarm_audio_count(void) { return ALARM_SOUND_COUNT; }

const char *alarm_audio_name(int id) {
    if (id < 0 || id >= ALARM_SOUND_COUNT) {
        return NULL;
    }
    return ALARM_NAMES[id];
}

void alarm_audio_select(int id) {
    if (id >= 0 && id < ALARM_SOUND_COUNT) {
        s_selected = id;
    }
}

int alarm_audio_selected(void) { return s_selected; }

static int16_t clip16(double v) {
    if (v > 32767.0)
        return 32767;
    if (v < -32767.0)
        return -32767;
    return (int16_t)v;
}

// Bake all four loops (once, at init): 1 s each at 22050 mono s16.
static void loop_gen_all(void) {
    const int fade = (int)(0.010 * RING_SAMPLE_RATE); // 10 ms attack/release
    const double amp = 0.5 * 32767.0;
    const int seg = LOOP_SAMPLES / 4; // 5512 (250 ms, the 1-sample remainder lands in the last segment)

    // 0: two-tone A5/E5 — four 250 ms segments, alternating.
    {
        const double freqs[2] = {880.0, 659.255};
        int16_t *dst = s_loops; // loop 0
        for (int h = 0; h < 4; h++) {
            const int n = (h == 3) ? LOOP_SAMPLES - 3 * seg : seg;
            double phase = 0.0;
            const double w = 2.0 * M_PI * freqs[h & 1] / (double)RING_SAMPLE_RATE;
            for (int i = 0; i < n; i++) {
                phase += w;
                double env = 1.0;
                if (i < fade) {
                    env = (double)i / (double)fade;
                } else if (i >= n - fade) {
                    env = (double)(n - i) / (double)fade;
                }
                dst[i] = clip16(sin(phase) * amp * env);
            }
            dst += n;
        }
    }

    // 1: classic digital beep — 2 kHz square, three 200 ms bursts with
    // 200 ms gaps (the square keeps 10 ms attack/release against clicks).
    {
        const int burst = RING_SAMPLE_RATE * 2 / 10; // 200 ms
        const double w = 2.0 * M_PI * 2000.0 / (double)RING_SAMPLE_RATE;
        int16_t *dst = s_loops + LOOP_SAMPLES; // loop 1
        for (int i = 0; i < LOOP_SAMPLES; i++) {
            const int t = i % (2 * burst); // 0..burst-1 on, burst..2*burst-1 off
            double s = 0.0;
            if (t < burst) {
                double env = 1.0;
                if (t < fade) {
                    env = (double)t / (double)fade;
                } else if (t >= burst - fade) {
                    env = (double)(burst - t) / (double)fade;
                }
                s = sin(w * (double)t) > 0.0 ? 1.0 : -1.0; // the square
                dst[i] = clip16(s * amp * env);
            } else {
                dst[i] = 0;
            }
        }
    }

    // 2: soft bell chime — one strike, the 880 Hz fundamental + partials
    // with an exponential decay (tau 350 ms) over the whole second.
    {
        const double f[4] = {880.0, 1760.0, 2640.0, 3520.0};
        const double a[4] = {1.0, 0.5, 0.25, 0.12};
        const double tau = 0.35;
        const double bell_amp = 0.30 * 32767.0; // the partials sum > 1; the 0.30 keeps the peak in range
        int16_t *dst = s_loops + 2 * LOOP_SAMPLES;
        for (int i = 0; i < LOOP_SAMPLES; i++) {
            double t = (double)i / (double)RING_SAMPLE_RATE;
            double env = 1.0;
            if (i < 5) {
                env = (double)i / 5.0; // 5 ms attack (no tick)
            }
            double v = 0.0;
            for (int p = 0; p < 4; p++) {
                v += a[p] * sin(2.0 * M_PI * f[p] * t) * exp(-t / tau);
            }
            dst[i] = clip16(v * bell_amp * env);
        }
    }

    // 3: melodic arpeggio — C5-E5-G5-C6, four 250 ms segments.
    {
        const double freqs[4] = {523.25, 659.255, 783.99, 1046.5};
        int16_t *dst = s_loops + 3 * LOOP_SAMPLES;
        for (int h = 0; h < 4; h++) {
            const int n = (h == 3) ? LOOP_SAMPLES - 3 * seg : seg;
            double phase = 0.0;
            const double w = 2.0 * M_PI * freqs[h] / (double)RING_SAMPLE_RATE;
            for (int i = 0; i < n; i++) {
                phase += w;
                double env = 1.0;
                if (i < fade) {
                    env = (double)i / (double)fade;
                } else if (i >= n - fade) {
                    env = (double)(n - i) / (double)fade;
                }
                dst[i] = clip16(sin(phase) * amp * env);
            }
            dst += n;
        }
    }
}

/* The shared player: the ring (looping until s_stop, the 2.5 s ramp) and
 * the preview (one pass, the 300 ms ramp) both stream from s_loops. The
 * blocking I2S writes pace the stream at the audio rate. */
static void play_task(void *arg) {
    (void)arg;
    const int loop = s_play.loop;
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 1,
        .sample_rate = RING_SAMPLE_RATE,
        .mclk_multiple = 0, // MCLK = 256 x FS
    };
    int ret = esp_codec_dev_open(s_codec_dev, &fs);
    if (ret != 0) {
        ESP_LOGE(TAG, "codec open failed (%d): the alarm stays display-only", ret);
        goto done;
    }
    esp_codec_dev_set_out_vol(s_codec_dev, 0);
    if (s_play.is_ring) {
        ESP_LOGI(TAG, "Alarm ring started (sound %d: %s, ramp %d ms -> vol %d)", s_selected,
                 alarm_audio_name(s_selected), s_play.ramp_ms, s_play.vol_full);
    } else {
        ESP_LOGI(TAG, "Alarm preview (sound %d: %s)", loop, alarm_audio_name(loop));
    }

    const TickType_t start = xTaskGetTickCount();
    int last_vol = 0;
    int off = 0;
    for (;;) {
        // The gradual ramp (the blocking writes keep the stream real-time,
        // so task ticks advance with the audio that is actually sounding).
        int vol =
            (int)(((uint32_t)(xTaskGetTickCount() - start) * (uint32_t)portTICK_PERIOD_MS * (uint32_t)s_play.vol_full) /
                  (uint32_t)s_play.ramp_ms);
        if (vol > s_play.vol_full) {
            vol = s_play.vol_full;
        }
        if (vol != last_vol) {
            esp_codec_dev_set_out_vol(s_codec_dev, vol);
            last_vol = vol;
        }
        // Stream one chunk of the loop (the blocking write paces it).
        int remain = LOOP_SAMPLES - off;
        int n = CHUNK_SAMPLES < remain ? CHUNK_SAMPLES : remain;
        ret = esp_codec_dev_write(s_codec_dev, s_loops + (size_t)loop * LOOP_SAMPLES + off, n * (int)sizeof(int16_t));
        off += n;
        if (off >= LOOP_SAMPLES) {
            off = 0;
            if (!s_play.looping) {
                break; // the preview: one pass, then fade
            }
        }
        if (s_play.looping && s_stop) {
            break; // the touch snooze
        }
        if (s_play.looping && (uint32_t)((xTaskGetTickCount() - start) * (uint32_t)portTICK_PERIOD_MS) >= RING_CAP_MS) {
            // The hard cap: one alarm never rings longer than
            // UI_ALARM_RING_MAX_MIN minutes (see the define; the fade
            // below + the codec close run the same way as a dismiss).
            ESP_LOGW(TAG, "Alarm ring reached the %d min cap", CONFIG_UI_ALARM_RING_MAX_MIN);
            break;
        }
        if (ret != 0) {
            ESP_LOGW(TAG, "play write failed (%d); stopping", ret);
            break;
        }
    }

    // Fade the volume out (~150 ms — the I2S keeps streaming the drained
    // DMA, so the fade rides on silence), then close (DAC + I2S TX
    // disabled; the next ring/preview re-opens the codec).
    for (int vol = last_vol; vol > 0; vol -= 5) {
        esp_codec_dev_set_out_vol(s_codec_dev, vol);
        vTaskDelay(pdMS_TO_TICKS(RING_FADE_STEP_MS));
    }
    esp_codec_dev_set_out_vol(s_codec_dev, 0);
    esp_codec_dev_close(s_codec_dev);
    if (s_play.is_ring) {
        ESP_LOGI(TAG, "Alarm ring stopped");
    }
done:
    if (s_play.is_ring) {
        s_ringing = false;
    }
    s_busy = false;
    vTaskDelete(NULL);
}

void alarm_audio_init(void) {
    // bsp_audio_codec_speaker_init: bsp_i2c_init (idempotent — the touch
    // already runs on that bus; the ES8311 just joins it) +
    // bsp_audio_init (I2S master TX/RX, mono 16-bit @22050) + the ES8311
    // DAC with the amp PA pin. Soft-fails like the IMU.
    s_codec_dev = bsp_audio_codec_speaker_init();
    if (s_codec_dev == NULL) {
        ESP_LOGW(TAG, "ES8311 speaker init failed: the alarm stays display-only");
        return;
    }
    s_loops = heap_caps_malloc((size_t)ALARM_SOUND_COUNT * LOOP_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (s_loops == NULL) {
        ESP_LOGE(TAG, "loop buffer (%u KB PSRAM) not available: the alarm stays display-only",
                 (unsigned)((size_t)ALARM_SOUND_COUNT * LOOP_SAMPLES * sizeof(int16_t) / 1024));
        s_codec_dev = NULL;
        return;
    }
    loop_gen_all();
    /* The selection the settings screen persists: the app loads the store
     * (settings_store_load) before calling init, so the stored id is the
     * ring's sound. */
    s_selected = settings_alarm_sound_id();
    ESP_LOGI(TAG, "Alarm audio ready (%d sounds, %u KB PSRAM, selected %d: %s)", ALARM_SOUND_COUNT,
             (unsigned)((size_t)ALARM_SOUND_COUNT * LOOP_SAMPLES * sizeof(int16_t) / 1024), s_selected,
             alarm_audio_name(s_selected));
}

void alarm_audio_ring_start(void) {
    if (s_busy) {
        return;
    }
    if (s_codec_dev == NULL || s_loops == NULL) {
        ESP_LOGW(TAG, "audio unavailable; the alarm stays display-only");
        return;
    }
    s_stop = false;
    s_busy = true;
    s_ringing = true;
    s_play = (struct play_arg){
        .loop = s_selected, .looping = true, .is_ring = true, .ramp_ms = RING_RAMP_MS, .vol_full = RING_VOL_FULL};
    if (xTaskCreate(play_task, "alarm_audio", 6144, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        s_ringing = false;
        ESP_LOGE(TAG, "play task create failed");
    }
}

void alarm_audio_ring_stop(void) {
    if (!s_ringing) {
        return;
    }
    s_stop = true; // the ring task does the fade + teardown (~150 ms)
}

void alarm_audio_preview(int id) {
    if (id < 0 || id >= ALARM_SOUND_COUNT) {
        return;
    }
    if (s_busy) {
        return; // the codec streams one loop at a time (ring or preview)
    }
    if (s_codec_dev == NULL || s_loops == NULL) {
        ESP_LOGW(TAG, "audio unavailable; the preview is skipped");
        return;
    }
    s_stop = false;
    s_busy = true;
    s_play = (struct play_arg){
        .loop = id, .looping = false, .is_ring = false, .ramp_ms = PREV_RAMP_MS, .vol_full = PREV_VOL_FULL};
    if (xTaskCreate(play_task, "alarm_audio", 6144, NULL, 4, NULL) != pdPASS) {
        s_busy = false;
        ESP_LOGE(TAG, "preview task create failed");
    }
}
