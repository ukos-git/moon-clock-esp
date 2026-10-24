/* Q15.15 sin/cos (2048-entry quarter-symmetric table, built once).
 *
 * For the "ball in hand" rotation the angle is a monotonic accumulator
 * updated per frame; these two lookups are the only per-frame trig.
 *
 * Values are Q15.15 in an INT32: cos(0) = 32768 does not fit int16_t
 * (would wrap to -32768 and mirror the disk).
 */
#include "moon.h"

#include <math.h>

#define NQ 1024 /* quarter-table entries: 0..pi/2 inclusive */

static int32_t sin_tab[NQ + 1];
static int fixp_init = 0;

static void fixp_build(void) {
    for (int i = 0; i <= NQ; i++) {
        double a = (double)i / NQ * (M_PI / 2.0);
        int q = (int)(sin(a) * 32768.0 + 0.5);
        sin_tab[i] = q;
    }
    fixp_init = 1;
}

/* Reduce to [0, 4*NQ) (one quadrant = NQ steps). */
static int red16(int i) {
    i %= 4 * NQ;
    if (i < 0)
        i += 4 * NQ;
    return i;
}

int32_t moon_sin15(float rad) {
    if (!fixp_init)
        fixp_build();
    /* 2*pi -> 4*NQ steps; quadrant q covers steps [q*NQ, (q+1)*NQ). */
    int i = red16((int)(rad / (2.0f * M_PI) * 4.0f * NQ + 0.5f));
    int q = i / NQ;
    int idx = i % NQ;
    int v = sin_tab[idx];
    if (q == 1)
        v = sin_tab[NQ - idx];
    else if (q == 2)
        v = -v;
    else if (q == 3)
        v = -sin_tab[NQ - idx];
    return v;
}

int32_t moon_cos15(float rad) { return moon_sin15(rad + (float)(M_PI / 2.0)); }
