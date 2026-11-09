/**
 * @file knob_alarm_audio_stub.c
 * @brief The knob's answer to alarm_audio.h: a codec-less silent board.
 *
 * The two AMOLED clocks carry an ES8311 + speaker behind this header; the
 * knob has no codec, no I2S and no amp (bench-verified hardware list).
 * The shared actions.c calls the ring control on every alarm fire and
 * dismiss, and the settings sound screen (P3) calls the selection set --
 * all of it must LINK and run honestly on the knob: ring/preview are
 * silent no-ops, the four selectable sounds are all "Silent" (not fake
 * chime names), and the selection persists in-process so the settings
 * round-trip behaves.
 */
#include <stddef.h>

#include "alarm_audio.h"

static int s_selected;

void alarm_audio_init(void) {}

void alarm_audio_ring_start(void) {}

void alarm_audio_ring_stop(void) {}

int alarm_audio_count(void) { return ALARM_SOUND_COUNT; }

const char *alarm_audio_name(int id) {
    if (id < 0 || id >= ALARM_SOUND_COUNT) {
        return NULL;
    }
    return "Silent"; /* honest: this board has no speaker at all */
}

void alarm_audio_select(int id) {
    if (id < 0) {
        id = 0;
    }
    if (id >= ALARM_SOUND_COUNT) {
        id = ALARM_SOUND_COUNT - 1;
    }
    s_selected = id;
}

int alarm_audio_selected(void) { return s_selected; }

void alarm_audio_preview(int id) { (void)id; }
