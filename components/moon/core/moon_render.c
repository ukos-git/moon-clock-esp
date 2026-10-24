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

/* Gray -> RGB565, round-to-nearest, built once (256 entries). */
static uint16_t g565[256];
static int g565_init = 0;

static void g565_build(void) {
    for (int g = 0; g < 256; g++) {
        /* Gray: all three 5-bit channels equal. q = round(g * 31 / 255). */
        int q = (g * 31 + 127) / 255;
        g565[g] = (uint16_t)((q << 11) | (q << 5) | q);
    }
    g565_init = 1;
}

uint16_t moon_gray565(uint8_t g) {
    if (!g565_init)
        g565_build();
    return g565[g];
}

void moon_rotate(const uint8_t *frame, int frame_size, uint16_t *out, int size, int cx, int cy, int f_cx, int f_cy,
                 int32_t cos15, int32_t sin15) {
    if (!g565_init)
        g565_build();
    int32_t c15 = cos15, s15 = sin15;
    for (int y = 0; y < size; y++) {
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
