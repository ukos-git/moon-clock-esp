/* In-plane rotation ("ball in hand") + gray -> RGB565.
 *
 * Nearest-neighbor gather: 1 read per pixel of the baked 8-bit frame. The
 * disk is centered on the frame center and the rotation is about that same
 * point, so inside-disk pixels always gather inside the frame (disk pixels
 * beyond it read 0 -- no separate disk test needed). Out of frame = black.
 */
#include "moon.h"

#include <math.h>
#include <string.h>

/* Gray -> RGB565, round-to-nearest, built once (256 entries).
 *
 * The LUT is WARM by default (ARTISTIC display choice): NASA's reference
 * frames render the lunar lit side measurably warm (lit-region RGB
 * (164,160,153) across the 2025 set, R/B = 1.07, R-G +3.9, G-B +6.7 -- the
 * SVS render's white balance, not a lens artifact), and a warm moon reads
 * as a real moon photo against the AMOLED's true black. The multipliers
 * (R x1.038, G x1.000, B x0.933) reproduce that balance at every gray
 * level. `moon_gray565_set_warm(0)` rebuilds the neutral LUT (the gate's
 * --neutral path: the C-vs-NASA / C-vs-Python metrics validate the neutral
 * gray physics, so the display tint never enters the gate numbers). */
static uint16_t g565[256];
static int g565_init = 0;
static int g565_warm = 1;
/* The pre-calculated sky-event tint (moon_event.h, moon_gray565_set_event):
 * per-channel multipliers composed OVER the warm base (display-only --
 * the neutral gate LUT is never tinted: set_warm(0) wins over set_event,
 * so the gate's C-vs-NASA / C-vs-Python numbers validate the physics
 * without any event color entering them). All 1.0 = no event. */
static float g565_ev_r = 1.0f, g565_ev_g = 1.0f, g565_ev_b = 1.0f;

static void g565_build(void) {
    for (int g = 0; g < 256; g++) {
        /* R5/B5 (5-bit) and G6 (6-bit). Using the full 6-bit green is what
         * keeps the gray balanced -- writing the 5-bit value into the 6-bit
         * green field caps green at 31/63 (~49%) and renders every gray as
         * magenta/pink on the panel. Warm: R +3.8%, B -4.9% (calibrated to
         * the NASA lit-region balance; R is capped at full 31). */
        int r5 = (g * 31 + 127) / 255;
        int g6 = (g * 63 + 127) / 255;
        int b5 = r5;
        if (g565_warm) {
            r5 = (g * 32 + 127) / 255; /* x1.038 ~= 31 * 1.038 = 32.2/255 per gray */
            if (r5 > 31)
                r5 = 31;
            b5 = (g * 29 + 127) / 255; /* x0.933 ~= 31 * 0.933 = 28.9/255 per gray */
            if (b5 > 31)
                b5 = 31;
            /* Sky-event tint: multiply the channels by the (pre-calculated)
             * event multipliers and cap (see moon_gray565_set_event). */
            int nr = (int)(r5 * g565_ev_r + 0.5f);
            if (nr > 31)
                nr = 31;
            int ng = (int)(g6 * g565_ev_g + 0.5f);
            if (ng > 63)
                ng = 63;
            int nb = (int)(b5 * g565_ev_b + 0.5f);
            if (nb > 31)
                nb = 31;
            r5 = nr;
            g6 = ng;
            b5 = nb;
        }
        g565[g] = (uint16_t)((r5 << 11) | (g6 << 5) | b5);
    }
    g565_init = 1;
}

void moon_gray565_set_warm(int on) {
    if (on != g565_warm) {
        g565_warm = on;
        g565_build();
    }
}

/* Sky-event tint multipliers (composed over the warm base, see the
 * g565_ev_* note). The values are the plan's artistic tints -- the
 * event should read CLEARLY, "more visible than in reality" (the
 * 2026-11-04 bugreport): blood = copper-red (a total-eclipse disk reads
 * deep orange-red at peak warm gray, ~(255, 138, 98) 8-bit), harvest =
 * golden, blue = cool blue-grey, super/micro = +8% / -8% brightness on
 * all channels. They compose multiplicatively (harvest + super, ...),
 * so one call per day covers all of the instant's events. The LUT
 * rebuilds only when the multipliers actually change (the device calls
 * this once per day; an event-less day is a no-op). */
void moon_gray565_set_event(const moon_event_t *ev) {
    float r = 1.0f, g = 1.0f, b = 1.0f;
    if (ev != NULL) {
        if (ev->blood) {
            r = 1.15f;
            g = 0.55f;
            b = 0.42f;
        }
        if (ev->harvest) {
            r *= 1.10f;
            g *= 0.92f;
            b *= 0.62f;
        }
        if (ev->blue) {
            r *= 0.92f;
            g *= 1.00f;
            b *= 1.12f;
        }
        if (ev->super_moon) {
            r *= 1.08f;
            g *= 1.08f;
            b *= 1.08f;
        } else if (ev->micro_moon) {
            r *= 0.92f;
            g *= 0.92f;
            b *= 0.92f;
        }
    }
    if (r != g565_ev_r || g != g565_ev_g || b != g565_ev_b) {
        g565_ev_r = r;
        g565_ev_g = g;
        g565_ev_b = b;
        g565_build();
    }
}

uint16_t moon_gray565(uint8_t g) {
    if (!g565_init)
        g565_build();
    return g565[g];
}

static void moon_rotate_range(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx,
                              int f_cy, int32_t cos15, int32_t sin15, int y0, int y1) {
    int32_t c15 = cos15, s15 = sin15;
    for (int y = y0; y < y1; y++) {
        int dy = y - cy;
        uint16_t *row = &out[y * size];
        /* 4 pixels per iteration with incremental Q15.15 updates (fx += c15,
         * fy -= s15 per pixel, exact integer math). The unroll keeps up to 4
         * PSRAM frame loads in flight so the gather is not latency-bound. */
        for (int x0 = 0; x0 < size; x0 += 4) {
            int n = size - x0;
            if (n > 4)
                n = 4;
            int dx = x0 - cx;
            int32_t fx = (int32_t)dx * c15 + (int32_t)dy * s15;
            int32_t fy = (int32_t)dy * c15 - (int32_t)dx * s15;
            for (int k = 0; k < n; k++) {
                /* Inverse rotation (gather): p = R(-theta) s, integer
                 * Q15.15 (|dx|, |dy| < 234, max |fx| ~ 1.6e9 fits int32). */
                int32_t fx15 = fx >> 15; /* arithmetic shift = floor */
                int32_t fy15 = fy >> 15;
                int ix = f_cx + (int)fx15 + ((fx & 32767) > 16383 ? 1 : 0); /* round half up */
                int iy = f_cy + (int)fy15 + ((fy & 32767) > 16383 ? 1 : 0);
                uint8_t gval = 0;
                if ((unsigned)ix < (unsigned)frame_size && (unsigned)iy < (unsigned)frame_size) {
                    gval = frame[iy * frame_size + ix];
                }
                row[x0 + k] = g565[gval];
                fx += c15;
                fy -= s15;
            }
        }
    }
}

void moon_rotate(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx, int f_cy,
                 int32_t cos15, int32_t sin15) {
    if (!g565_init)
        g565_build();
    moon_rotate_range(frame, frame_size, out, size, cx, cy, f_cx, f_cy, cos15, sin15, 0, size);
}

void moon_rotate_rows(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx, int f_cy,
                      int32_t cos15, int32_t sin15, int y0, int y1) {
    if (!g565_init)
        g565_build();
    moon_rotate_range(frame, frame_size, out, size, cx, cy, f_cx, f_cy, cos15, sin15, y0, y1);
}
