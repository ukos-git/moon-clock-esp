/* Moon host simulator: runs the DEVICE code path (cheap ephemeris +
 * fixed-point rotation + gray->RGB565) and writes PNGs, which are
 * image-diffed against NASA with the same gate as the Python prototype
 * (see check_gate.py).
 *
 * Usage:
 *   moon_sim --utc "2025-06-15T12:00" [--rot deg] [--pole deg] [--out DIR]
 *   moon_sim --utc list.txt          (one ISO [rot] [pole] per line -> sim.NNNN.png)
 *   moon_sim --pole deg              (camera basis rotation about the body
 *                                     pole -- --rot's out-of-plane sibling;
 *                                     it mirrored the dusk spin removed
 *                                     2026-11-13, the axis stays a probe)
 *   moon_sim --frames N --start ISO --end ISO [--out DIR]  (even spacing)
 *
 * Panel modes: --size 466 (device, r=MOON_DISK_RADIUS_PX) or
 * --size 400 (reference geometry, r=199.3 -- the gate). --radius F bakes
 * with an EXPLICIT disk radius centred at (size/2, size/2) -- the clocks'
 * device-identical bakes (moon_ui.c: r = MOON_PX/2; the knob's r < size/2,
 * knob_ui_set_disk_geom). --pgm writes the RAW bake output (the exact gray
 * the device stores, no LUT/rotation) instead of the RGB565-decoded PNG.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "moon.h"
#include "moon_assets.h"
#include "moon_consts.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* --- ISO "YYYY-MM-DDTHH:MM" -> epoch seconds (UTC) ---------------------- */
static long long days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (long long)era * 146097 + doe - 719468;
}

static int parse_iso(const char *s, int64_t *epoch) {
    int y, mo, d, h, mi;
    if (sscanf(s, "%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) < 3)
        return 0;
    *epoch = days_from_civil(y, mo, d) * 86400LL + h * 3600LL + mi * 60LL;
    return 1;
}

/* Epoch seconds -> "YYYY-MM-DDTHH:MM" (UTC). The civil-date inverse of
 * days_from_civil above (Hinnant; the --frames spacing path needs it). */
static void iso_from_epoch(int64_t e, char *out, size_t n) {
    int64_t days = e / 86400;
    int64_t rem = e % 86400;
    if (rem < 0)
        rem += 86400;
    int h = (int)(rem / 3600), mi = (int)((rem % 3600) / 60);
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = (int)(z - era * 146097);                               /* [0, 146096] */
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0,399] */
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);               /* [0, 365] */
    int mp = (5 * doy + 2) / 153;                                    /* [0, 11] */
    int d = doy - (153 * mp + 2) / 5 + 1;                            /* [1, 31] */
    int m = mp + (mp < 10 ? 3 : -9);                                 /* [1, 12] */
    int y = (int)(yoe + era * 400 + (m <= 2));
    snprintf(out, n, "%04d-%02d-%02dT%02d:%02d", y, m, d, h, mi);
}

/* --- one frame ---------------------------------------------------------- */
static int g_darkzero = 0; /* --darkzero: the device's night side (the computed
                            * backlit surface, dark_zero = 1) */
static int g_event = 0;    /* --event: classify the instant's sky events
                            * (moon_event.c) and bake the blood occlusion +
                            * the tinted LUT (the device's daily path,
                            * host-verified this way) */
static int g_neutral = 0;  /* --neutral: the neutral gray->565 LUT (no warm
                            * display tint) -- the image gate runs with this so
                            * its metrics validate the physics, not the tint */
static float g_radius = 0; /* --radius F: explicit bake disk radius (the
                            * device-identical geometry; 0 = the per-size
                            * defaults below) */
static int g_pgm = 0;      /* --pgm: write the raw bake output (P5 gray)
                            * instead of the RGB565-decoded PNG */
static int g_pole_deg = 0; /* --pole: camera basis rotation about the body
                            * pole (+z), deg. The pole stays at the
                            * top of the disk; the light source S
                            * stays put, so the terminator sweeps the
                            * disk. (It mirrored the device's dusk
                            * spin until that was removed
                            * 2026-11-13; kept as a general probe.) */

/* Camera basis rotation about the body pole (+z): each basis vector's
 * x/y components are rotated (right -> up for +phi), z invariant. */
static void pole_geometry(moon_geom_t *g, float phi) {
    float c = cosf(phi), s = sinf(phi);
    float x, y;
    x = g->right[0];
    y = g->right[1];
    g->right[0] = c * x - s * y;
    g->right[1] = s * x + c * y;
    x = g->up[0];
    y = g->up[1];
    g->up[0] = c * x - s * y;
    g->up[1] = s * x + c * y;
    x = g->E[0];
    y = g->E[1];
    g->E[0] = c * x - s * y;
    g->E[1] = s * x + c * y;
}

/* One raw bake frame -> P5 PGM (the exact gray bytes the device stores --
 * pre-LUT, pre-rotation -- e.g. the boot-movie generator's input). */
static int bake_to_pgm(const uint8_t *frame, int size, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "open failed: %s\n", path);
        return 0;
    }
    fprintf(f, "P5\n%d %d\n255\n", size, size);
    if (fwrite(frame, 1, (size_t)size * size, f) != (size_t)size * size) {
        fprintf(stderr, "write failed: %s\n", path);
        fclose(f);
        return 0;
    }
    fclose(f);
    return 1;
}

/* One gray frame -> the exact panel pixel (RGB565 via the identity
 * moon_rotate) -> RGB24 PNG. The display LUT is the currently selected
 * one (the warm device tint unless --neutral; the event tint if --event). */
static int bake_to_png(const uint8_t *frame, int size, int center, int rot_deg, const char *path) {
    static uint16_t *rgb;
    static uint8_t *px;
    if (!rgb)
        rgb = malloc((size_t)size * size * 2);
    if (!px)
        px = malloc((size_t)size * size * 3);
    float th = (float)rot_deg * 0.017453292519943295f;
    moon_rotate(frame, size, rgb, size, center, center, center, center, moon_cos15(th), moon_sin15(th));
    for (int i = 0; i < size * size; i++) {
        /* Decode the exact panel pixel: R5 (5-bit), G6 (6-bit), B5 (5-bit).
         * Green must be read as 6 bits and scaled by /63 -- decoding it as 5
         * bits (/31) would hide the encoder's green-width in the PNG. */
        uint16_t v = rgb[i];
        int r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
        px[i * 3 + 0] = (uint8_t)((r5 * 255 + 15) / 31);
        px[i * 3 + 1] = (uint8_t)((g6 * 255 + 31) / 63);
        px[i * 3 + 2] = (uint8_t)((b5 * 255 + 15) / 31);
    }
    if (stbi_write_png(path, size, size, 3, px, size * 3) == 0) {
        fprintf(stderr, "write failed: %s\n", path);
        return 0;
    }
    return 1;
}

static int render_one(const char *iso, int rot_deg, int pole_deg, int size, const char *out_path, int idx) {
    int64_t epoch;
    if (!parse_iso(iso, &epoch)) {
        fprintf(stderr, "bad time '%s'\n", iso);
        return 0;
    }

    int radius, center;
    if (size == 400) {
        radius = 199;
        center = 199; /* rot=0 must be identity: rot center == gather center */
    } else if (size == 466) {
        radius = (int)MOON_DISK_RADIUS_PX; /* device panel: r=200 disk, 33px margin */
        center = size / 2;
    } else {
        radius = size / 2; /* sweep intermediate: full disk in an S x S bake */
        center = size / 2;
    }

    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = moon_relief();
    if (!alb || !grel) {
        fprintf(stderr, "no albedo/relief\n");
        return 0;
    }

    static uint8_t *frame;
    if (!frame)
        frame = malloc((size_t)size * size);

    moon_geom_t g;
    moon_geometry(epoch, &g);
    if (pole_deg) {
        pole_geometry(&g, (float)pole_deg * 0.017453292519943295f);
    }

    float fr, fc, fcy;
    if (g_radius > 0.0f) {
        /* --radius: device-identical bake (clocks: r = MOON_PX/2 centred;
         * knob: r < size/2 via knob_ui_set_disk_geom). */
        fr = g_radius;
        fc = (float)(size / 2);
        fcy = (float)(size / 2);
    } else if (size == 400) {
        fr = 199.3f;
        fc = 199.3f;
        fcy = 199.5f;
    } else {
        fr = (float)radius;
        fc = (float)(size / 2);
        fcy = (float)(size / 2);
    }
    /* --event: classify the instant's sky events (moon_event.c) and pass
     * the blood occlusion into the bake + the tinted LUT (the device's
     * daily path, host-verified this way). The gate never sets it
     * (NULL bev + neutral LUT = byte-identical to the pre-event bake). */
    moon_event_t ev;
    const moon_bake_ev_t *bev = NULL;
    if (g_event) {
        moon_event_classify(epoch, &ev);
        moon_gray565_set_event(&ev);
        if (ev.blood || ev.harvest || ev.blue || ev.super_moon || ev.micro_moon)
            bev = &ev;
        fprintf(stderr, "event: blood=%d harvest=%d blue=%d super=%d micro=%d dist=%.0fkm\n", ev.blood, ev.harvest,
                ev.blue, ev.super_moon, ev.micro_moon, g.distance_km);
    }
    moon_bake(&g, alb, grel, frame, size, fr, fc, fcy, MOON_ALB_W, MOON_ALB_H, g_darkzero, 0.0f, bev);

    char p[512];
    if (g_pgm) {
        if (idx >= 0)
            snprintf(p, sizeof p, "%s/sim.%04d.pgm", out_path, idx);
        else
            snprintf(p, sizeof p, "%s/sim.pgm", out_path);
        if (!bake_to_pgm(frame, size, p))
            return 0;
    } else {
        if (idx >= 0)
            snprintf(p, sizeof p, "%s/sim.%04d.png", out_path, idx);
        else
            snprintf(p, sizeof p, "%s/sim.png", out_path);
        if (!bake_to_png(frame, size, center, rot_deg, p))
            return 0;
    }
    fprintf(stderr, "%s  utc=%s  rot=%d  phase=%.2f%%  ss=(%.2f,%.2f)\n", p, iso, rot_deg, g.illum * 100.0,
            g.subsolar_lon, g.subsolar_lat);
    return 1;
}

int main(int argc, char **argv) {
    const char *iso = NULL;
    const char *list = NULL;
    int frames = 0;
    const char *start = NULL, *end = NULL;
    const char *out = "/tmp/moon-sim";
    int rot = 0;
    int size = 400;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--utc"))
            iso = argv[++i];
        else if (!strcmp(argv[i], "--list"))
            list = argv[++i];
        else if (!strcmp(argv[i], "--frames"))
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--start"))
            start = argv[++i];
        else if (!strcmp(argv[i], "--end"))
            end = argv[++i];
        else if (!strcmp(argv[i], "--out"))
            out = argv[++i];
        else if (!strcmp(argv[i], "--darkzero"))
            g_darkzero = 1;
        else if (!strcmp(argv[i], "--rot"))
            rot = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pole"))
            g_pole_deg = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size"))
            size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--radius"))
            g_radius = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--pgm"))
            g_pgm = 1;
        else if (!strcmp(argv[i], "--neutral"))
            g_neutral = 1;
        else if (!strcmp(argv[i], "--event"))
            g_event = 1;
    }

    if (g_neutral)
        moon_gray565_set_warm(0);

    /* Asset dims at startup (the gate's stale-binary guard parses this:
     * a simulator linked against the wrong target's moon_consts.h +
     * moon_assets_gen.c would otherwise diff quietly at the wrong
     * texture size). */
    fprintf(stderr, "moon assets: albedo %dx%d, dem %dx%d\n", MOON_ALB_W, MOON_ALB_H, MOON_DEM_W, MOON_DEM_H);

    if (mkdir(out, 0755) != 0) {
        /* exists */
    }

    int ok = 0;
    if (list) {
        FILE *f = fopen(list, "r");
        if (!f) {
            fprintf(stderr, "no list %s\n", list);
            return 1;
        }
        char line[256];
        int idx = 0;
        while (fgets(line, sizeof line, f)) {
            int r = rot, p = g_pole_deg; /* one "ISO [rot] [pole]" per line */
            char *sp = strchr(line, ' ');
            if (sp) {
                *sp = 0;
                r = atoi(sp + 1);
                char *sp2 = strchr(sp + 1, ' ');
                if (sp2) {
                    *sp2 = 0;
                    p = atoi(sp2 + 1);
                }
            }
            line[strcspn(line, "\n")] = 0;
            if (*line)
                ok += render_one(line, r, p, size, out, idx);
            idx++;
        }
        fclose(f);
        printf("%d frames -> %s\n", ok, out);
        return ok == idx ? 0 : 1;
    }

    if (frames > 0 && start && end) {
        int64_t e0, e1;
        if (!parse_iso(start, &e0) || !parse_iso(end, &e1)) {
            fprintf(stderr, "bad --start/--end\n");
            return 1;
        }
        for (int k = 0; k < frames; k++) {
            double t = frames == 1 ? 0.0 : (double)k / (frames - 1);
            /* llround: the spacing is a double (k/(N-1)); truncating could
             * jitter a frame by 1 s off its exact hour for N not a power
             * of two. */
            int64_t e = e0 + llround(t * ((double)e1 - e0));
            char iso2[40];
            iso_from_epoch(e, iso2, sizeof iso2);
            ok += render_one(iso2, rot, g_pole_deg, size, out, k);
        }
        printf("%d frames -> %s\n", ok, out);
        return ok == frames ? 0 : 1;
    }

    if (!iso) {
        fprintf(stderr, "usage: moon_sim (--utc ISO | --list FILE | --frames N "
                        "--start S --end E) [--out DIR] [--size N] [--rot D] "
                        "[--pole P] [--darkzero] [--neutral] [--event]\n");
        return 1;
    }
    ok = render_one(iso, rot, g_pole_deg, size, out, -1);
    return ok ? 0 : 1;
}
