/**
 * @file companion_proto.h
 * @brief Wire contract between the companion knob (knob/) and the clock —
 *        fixed-size binary UDP frames, no dependencies (the host-side test
 *        host_sim/companion_proto_test.c compiles this file verbatim).
 *
 * One frame is exactly COMPANION_FRAME_LEN bytes, sent unicast to the
 * clock's UDP port (Kconfig COMPANION_PORT, advertised via mDNS service
 * _moonclock._udp.local). Security posture: LAN-trusted; the optional
 * shared token byte (COMPANION_TOKEN, 0 = accept any) keeps a curious
 * neighbour off the dial, it is not a secret. See
 * docs/implementation/companion-knob.rst.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMPANION_MAGIC 0x4D43u /* 'M','C' — moon clock, on the wire LE */
#define COMPANION_VERSION 1u
#define COMPANION_FRAME_LEN 10

/* Frame kinds. KEY_* are keyboard-wheel effects (valid only on the
 * WiFi-password screen); STEP/FOCUS/COMMIT/ABORT are roller effects (valid
 * on the time/alarm/pomodoro/TZ screens). The clock validates kind against
 * the CURRENT screen and drops anything else — it is authoritative, the
 * knob is a dumb remote with a mirror UI. */
typedef enum {
    COMPANION_FRAME_HELLO = 0x01,      /* probe; field/delta unused, logged */
    COMPANION_FRAME_KEY_ADD = 0x02,    /* commit the highlighted slot (delta) */
    COMPANION_FRAME_KEY_BACK = 0x03,   /* backspace */
    COMPANION_FRAME_KEY_COMMIT = 0x04, /* save the password + start sync */
    COMPANION_FRAME_KEY_MODE = 0x05,   /* register switch, delta = 0..3 */
    COMPANION_FRAME_KEY_HOT = 0x06,    /* highlight slot (preview), delta */
    COMPANION_FRAME_STEP = 0x07,       /* roll a field: field + delta */
    COMPANION_FRAME_FOCUS_NEXT = 0x08, /* next field / next register */
    COMPANION_FRAME_COMMIT = 0x09,     /* save the current screen's value */
    COMPANION_FRAME_ABORT = 0x0A,      /* discard (where possible) + exit */
} companion_frame_type_t;

/* STEP field ids — which value the knob's dial drives right now. */
typedef enum {
    COMPANION_FIELD_NONE = 0,
    COMPANION_FIELD_HOUR,         /* SETTINGS_TIME */
    COMPANION_FIELD_MINUTE,       /* SETTINGS_TIME */
    COMPANION_FIELD_DAY,          /* SETTINGS_TIME */
    COMPANION_FIELD_MONTH,        /* SETTINGS_TIME */
    COMPANION_FIELD_YEAR,         /* SETTINGS_TIME (2000+YY) */
    COMPANION_FIELD_ALARM_HOUR,   /* SETTINGS_ALARM_TIME */
    COMPANION_FIELD_ALARM_MINUTE, /* SETTINGS_ALARM_TIME */
    COMPANION_FIELD_ALARM_DAYS,   /* SETTINGS_ALARM_TIME (shifts the range) */
    COMPANION_FIELD_POM_HOUR,     /* SETTINGS_POMODORO */
    COMPANION_FIELD_POM_MINUTE,   /* SETTINGS_POMODORO */
    COMPANION_FIELD_TZ,           /* SETTINGS_TZ */
} companion_field_t;

typedef struct __attribute__((packed)) {
    uint16_t magic;  /* COMPANION_MAGIC */
    uint8_t version; /* COMPANION_VERSION */
    uint8_t type;    /* companion_frame_type_t */
    uint16_t seq;    /* send counter; an exact repeat of the last seq is
                      * dropped (UDP retransmit / duplicate defence) */
    uint8_t token;   /* must equal the Kconfig token unless that is 0 */
    uint8_t field;   /* companion_field_t (STEP) */
    int8_t delta;    /* STEP steps / HOT|ADD slot / MODE register */
    uint8_t rsvd;    /* 0 */
} companion_frame_t;

/* Validate the transport-level fields (no screen knowledge here — the
 * screen gate lives in screens.c's injection API). expected_token 0 means
 * "accept any" (the Kconfig default). have_last/last_seq implement the
 * duplicate rejection. */
static inline bool companion_frame_ok(const void *buf, size_t len, uint8_t expected_token, bool have_last,
                                      uint16_t last_seq) {
    if (len != COMPANION_FRAME_LEN) {
        return false;
    }
    const companion_frame_t *f = (const companion_frame_t *)buf;
    if (f->magic != COMPANION_MAGIC || f->version != COMPANION_VERSION) {
        return false;
    }
    if (expected_token != 0 && f->token != expected_token) {
        return false;
    }
    if (have_last && f->seq == last_seq) {
        return false;
    }
    if (f->type < COMPANION_FRAME_HELLO || f->type > COMPANION_FRAME_ABORT) {
        return false;
    }
    return true;
}

#ifdef __cplusplus
}
#endif
