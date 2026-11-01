/**
 * @file alarm_audio.h
 * @brief The alarm ring (P4: the board's ES8311 codec + speaker amp)
 *
 * The P4 (XC 4C) carries an ES8311 codec on the shared I2C bus (with the
 * GT911 touch) and a speaker amp; the S3 has no audio hardware. The public
 * functions are the alarm's ring control: start on the alarm fire, stop on
 * the touch dismiss. On the S3 they are no-op stubs and the alarm stays
 * display-only (unchanged).
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the alarm audio path (P4: I2S + ES8311 DAC + amp)
 *
 * Soft-fails like the IMU: an init failure logs a warning and the alarm
 * stays display-only. Call once from app_main (after the UI is up).
 */
void alarm_audio_init(void);

/**
 * @brief Start the ring (P4)
 *
 * No-op when already ringing or when the audio path is unavailable. The
 * volume ramps up gradually over the first ~2.5 s (no startup click at a
 * full level).
 */
void alarm_audio_ring_start(void);

/**
 * @brief Stop the ring (P4) — the touch snooze/dismiss
 *
 * No-op when not ringing. The fade-out + teardown runs in the ring task
 * (~150 ms); this call only sets the stop flag.
 */
void alarm_audio_ring_stop(void);

#ifdef __cplusplus
}
#endif
