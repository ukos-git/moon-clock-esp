/**
 * @file companion_proto_test.c
 * @brief Host-side unit test for the companion wire contract
 *        (components/companion/include/companion_proto.h) — the plan's
 *        "accept / good / duplicate-seq / bad-token / wrong-screen" cases.
 *
 * Build + run (same flow as the moon_sim targets):
 *   cmake -S host_sim -B host_sim/build && cmake --build host_sim/build
 *   host_sim/build/companion_proto_test
 *
 * The screen-gate half of the plan's list lives on the device
 * (screens_companion_handle drops by ui_current_screen()); this test
 * covers the transport validation the listener task shares with it.
 */
#include <stdio.h>
#include <string.h>

#include "companion_proto.h"

static int s_failures;

static void check(const char *name, bool got, bool want) {
    if (got != want) {
        printf("FAIL %s: got %d want %d\n", name, got, want);
        s_failures++;
    } else {
        printf("ok   %s\n", name);
    }
}

static companion_frame_t make_frame(uint8_t type, uint8_t token, uint16_t seq) {
    companion_frame_t f;
    memset(&f, 0, sizeof(f));
    f.magic = COMPANION_MAGIC;
    f.version = COMPANION_VERSION;
    f.type = type;
    f.seq = seq;
    f.token = token;
    return f;
}

int main(void) {
    const uint8_t *b = NULL;

    /* good frame, token off (expected 0 = accept any) */
    companion_frame_t good = make_frame(COMPANION_FRAME_STEP, 0, 41);
    b = (const uint8_t *)&good;
    check("good/token-off", companion_frame_ok(b, COMPANION_FRAME_LEN, 0, false, 0), true);

    /* good frame, token required and matching */
    companion_frame_t tok = make_frame(COMPANION_FRAME_STEP, 77, 42);
    b = (const uint8_t *)&tok;
    check("good/token-match", companion_frame_ok(b, COMPANION_FRAME_LEN, 77, false, 0), true);

    /* bad token rejected */
    companion_frame_t bad_tok = make_frame(COMPANION_FRAME_STEP, 78, 43);
    b = (const uint8_t *)&bad_tok;
    check("bad/token-mismatch", companion_frame_ok(b, COMPANION_FRAME_LEN, 77, false, 0), false);

    /* token required but frame carries 0 */
    companion_frame_t zero_tok = make_frame(COMPANION_FRAME_STEP, 0, 44);
    b = (const uint8_t *)&zero_tok;
    check("bad/token-zero-when-required", companion_frame_ok(b, COMPANION_FRAME_LEN, 77, false, 0), false);

    /* duplicate seq (exact repeat of the last accepted) dropped */
    companion_frame_t dup = make_frame(COMPANION_FRAME_STEP, 0, 100);
    b = (const uint8_t *)&dup;
    check("bad/dup-seq", companion_frame_ok(b, COMPANION_FRAME_LEN, 0, true, 100), false);
    check("good/new-seq", companion_frame_ok(b, COMPANION_FRAME_LEN, 0, true, 99), true);
    /* seq WRAPS (u16 counter on the knob): any other value is accepted */
    check("good/seq-wrap", companion_frame_ok(b, COMPANION_FRAME_LEN, 0, true, 101), true);

    /* length, magic, version, type-range guards */
    companion_frame_t trunc = make_frame(COMPANION_FRAME_STEP, 0, 1);
    check("bad/short", companion_frame_ok(&trunc, COMPANION_FRAME_LEN - 1, 0, false, 0), false);
    check("bad/long", companion_frame_ok(&trunc, COMPANION_FRAME_LEN + 1, 0, false, 0), false);

    companion_frame_t bad_magic = make_frame(COMPANION_FRAME_STEP, 0, 2);
    bad_magic.magic = 0x1234;
    check("bad/magic", companion_frame_ok(&bad_magic, COMPANION_FRAME_LEN, 0, false, 0), false);

    companion_frame_t bad_ver = make_frame(COMPANION_FRAME_STEP, 0, 3);
    bad_ver.version = 2;
    check("bad/version", companion_frame_ok(&bad_ver, COMPANION_FRAME_LEN, 0, false, 0), false);

    companion_frame_t bad_type = make_frame(0x00, 0, 4);
    check("bad/type-zero", companion_frame_ok(&bad_type, COMPANION_FRAME_LEN, 0, false, 0), false);
    bad_type.type = COMPANION_FRAME_ABORT + 1;
    check("bad/type-over", companion_frame_ok(&bad_type, COMPANION_FRAME_LEN, 0, false, 0), false);

    /* every defined type passes the transport gate (screen gate is the
     * device's job, screens_companion_handle) */
    bool all_types = true;
    for (uint8_t t = COMPANION_FRAME_HELLO; t <= COMPANION_FRAME_ABORT; t++) {
        companion_frame_t f = make_frame(t, 0, 50);
        if (!companion_frame_ok(&f, COMPANION_FRAME_LEN, 0, false, 0)) {
            all_types = false;
        }
    }
    check("good/all-types", all_types, true);

    /* wire layout is frozen: 10 bytes, packed */
    check("size/fixed-10", sizeof(companion_frame_t) == COMPANION_FRAME_LEN, true);

    printf(s_failures ? "\nCOMPANION_PROTO_TEST: FAIL (%d)\n" : "\nCOMPANION_PROTO_TEST: PASS\n", s_failures);
    return s_failures ? 1 : 0;
}
