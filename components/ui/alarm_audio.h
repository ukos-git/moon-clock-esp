/**
 * @file alarm_audio.h
 * @brief The alarm ring + sound selection (both boards: ES8311 codec + speaker)
 *
 * Both boards carry an ES8311 codec on the shared I2C bus plus a speaker
 * amp (the P4 XC 4C with the GT911 touch, the S3 1.75 AMOLED with the
 * CST9217 touch); both BSPs expose the same bring-up
 * (bsp_audio_codec_speaker_init, I2S mono 16-bit @22050). The public
 * functions: the alarm's ring control (start on the alarm fire, stop on
 * the touch dismiss) and the sound selection (four baked ~1 s loops; the
 * ring streams the selected one, the preview plays any of them once from
 * the settings screen). An init failure soft-fails like the IMU: the
 * alarm stays display-only and the selection screen still works (it just
 * remembers the choice).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The number of selectable alarm sounds (the settings store validates
 * the stored id against 0 .. ALARM_SOUND_COUNT-1). */
#define ALARM_SOUND_COUNT 4

/**
 * @brief Bring up the alarm audio path (I2S + ES8311 DAC + speaker amp)
 *
 * Soft-fails like the IMU: an init failure logs a warning and the alarm
 * stays display-only. Call once from app_main (after the UI is up).
 */
void alarm_audio_init(void);

/**
 * @brief Start the ring — the selected sound loops until
 * alarm_audio_ring_stop
 *
 * No-op when already playing or when the audio path is unavailable. The
 * volume ramps up gradually over the first ~2.5 s (no startup click at a
 * full level).
 */
void alarm_audio_ring_start(void);

/**
 * @brief Stop the ring — the touch snooze/dismiss
 *
 * No-op when not ringing. The fade-out + teardown runs in the ring task
 * (~150 ms); this call only sets the stop flag.
 */
void alarm_audio_ring_stop(void);

/** @return the number of selectable sounds (ALARM_SOUND_COUNT). */
int alarm_audio_count(void);

/**
 * @brief The display name of a sound (the settings screen's roller)
 * @return the name, or NULL for an out-of-range id
 */
const char *alarm_audio_name(int id);

/**
 * @brief Select the ring's sound (clamped to the valid range)
 *
 * Takes effect at the next ring start; a running ring keeps its loop.
 */
void alarm_audio_select(int id);

/**
 * @brief The currently selected sound (the settings screen's seed)
 */
int alarm_audio_selected(void);

/**
 * @brief Preview a sound once (~1 s + short ramp + fade), from the
 * settings screen
 *
 * No-op for out-of-range ids and while anything is already playing
 * (ring or preview) — the codec streams one loop at a time.
 */
void alarm_audio_preview(int id);

#ifdef __cplusplus
}
#endif
