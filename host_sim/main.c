/* Moon host simulator: runs the DEVICE code path (cheap ephemeris +
 * fixed-point rotation + gray->RGB565) and writes PNGs, which are
 * image-diffed against NASA with the same gate as the Python prototype
 * (see check_gate.py).
 *
 * Usage:
 *   moon_sim --utc "2025-06-15T12:00" [--rot deg] [--pole deg] [--out DIR]
 *   moon_sim --utc list.txt          (one ISO [rot] [pole] per line -> sim.NNNN.png)
 *   moon_sim --pole deg              (camera basis rotation about the body
 *                                     pole = the device's dusk spin)
 *   moon_sim --frames N --start ISO --end ISO [--out DIR]  (even spacing)
 *
 * Panel modes: --size 466 (device, r=MOON_DISK_RADIUS_PX) or
 * --size 400 (reference geometry, r=199.3 -- the gate).
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

/* --- one frame ---------------------------------------------------------- */
static int g_darkzero = 0;           /* --darkzero: the device's night side (the computed
                                      * backlit surface, dark_zero = 1) */
static int g_event = 0;              /* --event: classify the instant's sky events
                                      * (moon_event.c) and bake the blood occlusion +
                                      * the tinted LUT (the device's daily path,
                                      * host-verified this way) */
static int g_neutral = 0;            /* --neutral: the neutral gray->565 LUT (no warm
                                      * display tint) -- the image gate runs with this so
                                      * its metrics validate the physics, not the tint */
static int g_pole_deg = 0;           /* --pole: camera basis rotation about the body
                                      * pole (+z), deg -- the device's dusk spin
                                      * (moon_ui.c moon_pole_geometry, copied
                                      * 1:1). The pole stays at the top of the
                                      * disk; the light source S stays put, so
                                      * the terminator sweeps the disk. */
static int g_dusk = 0;               /* --dusk: the device's light-sleep dusk, mirrored
                                      * 1:1 from moon_dusk_task (shade map at dk_kill,
                                      * pole-spin re-projects, final dk_kill bake) */
static int g_dusk_kill_margin = 120; /* --dusk-kill x: the kill margin in
                                      * thousandths of d (device's
                                      * MOON_DUSK_KILL_MARGIN 0.12f = 120) */
static int g_dusk_fade_hold = 150;   /* --dusk-fade-hold x: the lit-fade
                                      * hold (thousandths of the spin;
                                      * device's MOON_DUSK_FADE_HOLD 0.15f
                                      * = 150) */
static int g_dusk_fade_end = 700;    /* --dusk-fade-end x: the lit-fade
                                      * completion (thousandths of the spin;
                                      * device's MOON_DUSK_FADE_END 0.70f =
                                      * 700) -- the lit side is fully dark by
                                      * this point, so the spin's last third
                                      * rests on the clean backlit dark side */

/* The pole spin, host-side copy of moon_ui.c moon_pole_geometry:
 * each basis vector's x/y components are rotated about +z (right ->
 * up for +phi), z invariant. */
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

/* One gray frame -> the exact panel pixel (RGB565 via the identity
 * moon_rotate) -> RGB24 PNG. The display LUT is the currently selected
 * one (the warm device tint unless --neutral; the event tint if --event
 * or the dusk selected it). */
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

    float fr = size == 400 ? 199.3f : (float)radius;
    float fc = size == 400 ? 199.3f : (float)(size / 2);
    float fcy = size == 400 ? 199.5f : (float)(size / 2);
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
    if (idx >= 0)
        snprintf(p, sizeof p, "%s/sim.%04d.png", out_path, idx);
    else
        snprintf(p, sizeof p, "%s/sim.png", out_path);
    if (!bake_to_png(frame, size, center, rot_deg, p))
        return 0;
    fprintf(stderr, "%s  utc=%s  rot=%d  phase=%.2f%%  ss=(%.2f,%.2f)\n", p, iso, rot_deg, g.illum * 100.0,
            g.subsolar_lon, g.subsolar_lat);
    return 1;
}

/* The device's light-sleep dusk, mirrored 1:1 from moon_ui.c
 * moon_dusk_task: the one-time shade map built at the endpoint's rim-sliver
 * kill bound (dk_sup + the kill margin, the device's MOON_DUSK_KILL_MARGIN),
 * one re-projected frame per degree of pole spin along the shortest arc to
 * the fully unilluminated view, and the final full bake at the same bound.
 * The in-plane fold (the parked free-spin angle) is a pure display rotation
 * and is left out (it never shifts the endpoint: the in-plane fold leaves E
 * unchanged). --dusk-kill x (thousandths of d) swaps the margin for an A/B
 * comparison. Writes dusk.%03d.png per frame + dusk.map.png (the body-frame
 * equirect map itself, for the lit-band inspection). */
static int render_dusk(const char *iso, int size, const char *out_path) {
    int64_t epoch;
    if (!parse_iso(iso, &epoch)) {
        fprintf(stderr, "bad time '%s'\n", iso);
        return 0;
    }
    /* The device's bake geometry (moon_ui.c): radius = MOON_PX/2, center =
     * MOON_PX/2 (400/200 on the S3, 618/309 on the P4) -- NOT the 400/199
     * gate geometry. */
    int radius = size / 2;
    int center = size / 2;

    const uint8_t *alb = moon_albedo();
    const moon_grel_t *grel = moon_relief();
    if (!alb || !grel) {
        fprintf(stderr, "no albedo/relief\n");
        return 0;
    }
    static uint8_t *frame;
    if (!frame)
        frame = malloc((size_t)size * size);
    static uint8_t *map;
    if (!map)
        map = malloc((size_t)MOON_ALB_W * MOON_ALB_H);
    static uint8_t *map_dark;
    if (!map_dark)
        map_dark = malloc((size_t)MOON_ALB_W * MOON_ALB_H);

    /* The device's endpoint math (moon_dusk_task): orbit-fold the view
     * first (host-side orbit = 0 -- the mirror is pose-independent for the
     * endpoint), then phi* = atan2(-B, -A). */
    moon_geom_t g0;
    moon_geometry(epoch, &g0);
    moon_geom_t g1 = g0; /* orbit 0: identity */
    float A = g1.E[0] * g1.S[0] + g1.E[1] * g1.S[1];
    float B = g1.E[0] * g1.S[1] - g1.E[1] * g1.S[0];
    float phi_star = atan2f(-B, -A);
    float e_dot_s_end = A * cosf(phi_star) + B * sinf(phi_star) + g1.E[2] * g1.S[2];
    float dk_sup = sqrtf(1.0f - e_dot_s_end * e_dot_s_end);
    float dk_kill = dk_sup + (float)g_dusk_kill_margin / 1000.0f;
    if (dk_kill >= 1.0f)
        dk_kill = 0.999f;
    int frames = (int)lrintf(fabsf(phi_star) * 57.29577951308232f);
    if (frames < 1)
        frames = 1;

    /* Sky event for this epoch (the device sets the LUT + the blood
     * occlusion for the dusk the same way). */
    moon_event_t ev;
    moon_event_classify(epoch, &ev);
    moon_gray565_set_event(&ev);
    const moon_bake_ev_t *bev = moon_event_any(&ev) ? &ev : NULL;

    fprintf(stderr, "dusk: utc=%s phi*=%+.2f deg  E.S(phi*)=%.4f  dk_sup=%.4f  dk_kill=%.4f  %d frames\n", iso,
            phi_star * 57.29577951308232f, e_dot_s_end, dk_sup, dk_kill, frames);

    /* One-time body-frame equirect shade map at the kill bound + the
     * lit-fade companion (the device builds both in the same pass). */
    moon_shade_map_build(&g0, alb, grel, map, map_dark, MOON_ALB_W, MOON_ALB_H, 1, dk_kill, bev);
    {
        char p[512];
        snprintf(p, sizeof p, "%s/dusk.map.png", out_path);
        if (!bake_to_png(map, MOON_ALB_H, MOON_ALB_H / 2, 0, p)) {
            fprintf(stderr, "map write failed\n");
            return 0;
        }
    }
    /* The map's lit band (the ray's source): over the lit cells (gray > the
     * backlit peak + a margin), report the WIDEST base direction -- its
     * d = r.S tells how far past dk_kill the lit crescent's edge reaches
     * (relief-boosted lns keeps a cell lit past the base-sphere d = 0). */
    {
        int lit_cells = 0, max_lit = 0, max_any = 0;
        float widest_d = -2.0f;
        const float deg2rad = (float)M_PI / 180.0f;
        for (int y = 0; y < MOON_ALB_H; y++) {
            const float lat = 90.0f - 180.0f * ((float)y + 0.5f) / (float)MOON_ALB_H;
            const float sinlat = sinf(lat * deg2rad), coslat = cosf(lat * deg2rad);
            for (int x = 0; x < MOON_ALB_W; x++) {
                const int v = map[(size_t)y * MOON_ALB_W + x];
                if (v > max_any)
                    max_any = v;
                if (v > 64) { /* well past the backlit peak (~51) */
                    lit_cells++;
                    if (v > max_lit)
                        max_lit = v;
                    /* Cell body direction (the map build's theta convention). */
                    const float th = (float)x / (float)MOON_ALB_W * 2.0f * (float)M_PI - (float)M_PI;
                    const float cl = cosf(th), sl = (cosf(th) >= 0.0f) ? sinf(th) : -sinf(th);
                    const float d = coslat * (cl * g0.S[0] + sl * g0.S[1]) + sinlat * g0.S[2];
                    if (d > widest_d)
                        widest_d = d;
                }
            }
        }
        fprintf(stderr,
                "dusk map: %d lit cells, max lit gray %d, max any gray %d, widest lit cell d=%+.4f (dk_kill=%+.4f)\n",
                lit_cells, max_lit, max_any, widest_d, dk_kill);
    }

    for (int f = 0; f < frames; f++) {
        float phi = phi_star * ((float)f + 1.0f) / (float)frames;
        moon_geom_t g = g1;
        if (f == frames - 1) {
            /* Final frame: the one pixel-exact dark-side bake at phi*, at
             * the same bound as the map (the device's rule). */
            pole_geometry(&g, phi);
            moon_bake(&g, alb, grel, frame, size, (float)radius, (float)center, (float)center, MOON_ALB_W, MOON_ALB_H,
                      1, dk_kill, bev);
        } else {
            /* Intermediate frames: re-project the constant map with the
             * lit-fade (the device's rule: hold 1.0 for the first
             * HOLD of the spin, smoothstep to 0.0 by END). */
            pole_geometry(&g, phi);
            float hold = (float)g_dusk_fade_hold / 1000.0f;
            float end = (float)g_dusk_fade_end / 1000.0f;
            float t = (frames > 2) ? (float)f / (float)(frames - 2) : 0.0f;
            float lit_w = 1.0f;
            if (t > hold) {
                float p = (t - hold) / (end - hold);
                if (p > 1.0f)
                    p = 1.0f;
                float e = p * p * (3.0f - 2.0f * p);
                lit_w = 1.0f - e;
            }
            moon_reproject_rows(map, map_dark, MOON_ALB_W, MOON_ALB_H, &g, frame, size, (float)radius, (float)center,
                                (float)center, 0, size, lit_w);
        }
        char p[512];
        snprintf(p, sizeof p, "%s/dusk.%03d.png", out_path, f);
        if (!bake_to_png(frame, size, center, 0, p)) {
            fprintf(stderr, "frame %d write failed\n", f);
            return 0;
        }
    }
    fprintf(stderr, "%d dusk frames -> %s\n", frames, out_path);
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
        else if (!strcmp(argv[i], "--neutral"))
            g_neutral = 1;
        else if (!strcmp(argv[i], "--event"))
            g_event = 1;
        else if (!strcmp(argv[i], "--dusk"))
            g_dusk = 1;
        else if (!strcmp(argv[i], "--dusk-kill"))
            g_dusk_kill_margin = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dusk-fade-hold"))
            g_dusk_fade_hold = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dusk-fade-end"))
            g_dusk_fade_end = atoi(argv[++i]);
    }

    if (g_neutral)
        moon_gray565_set_warm(0);

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
            int64_t e = (int64_t)(e0 + t * ((double)e1 - e0));
            char iso2[40];
            {
                long long days = e / 86400LL;
                int64_t rem = e % 86400LL;
                if (rem < 0)
                    rem += 86400;
                int h = (int)(rem / 3600), mi = (int)((rem % 3600) / 60);
                /* civil from days */
                long long z = days + 719468LL;
                long long era = (z >= 0 ? z : z - 146096) / 146097;
                unsigned yoe = (unsigned)(z - era * 146097);
                int y = (int)(era * 400 + (yoe - yoe / 1460 + yoe / 36520 - yoe / 146000));
                int doy = (int)(yoe - (y / 4 - y / 100 + y / 400));
                int mp = (int)((5 * doy + 2) / 153);
                int d = (int)(doy - (153 * mp + 2) / 5 + 1);
                int m = mp + (mp < 10 ? 3 : -9);
                if (m <= 2)
                    y++;
                snprintf(iso2, sizeof iso2, "%04d-%02d-%02dT%02d:%02d", y, m, d, h, mi);
            }
            ok += render_one(iso2, rot, g_pole_deg, size, out, k);
        }
        printf("%d frames -> %s\n", ok, out);
        return ok == frames ? 0 : 1;
    }

    if (g_dusk) {
        if (!iso)
            fprintf(stderr, "--dusk needs --utc ISO\n");
        return render_dusk(iso, size, out) ? 0 : 1;
    }

    if (iso)
        return render_one(iso, rot, g_pole_deg, size, out, -1) ? 0 : 1;

    fprintf(stderr, "usage: moon_sim --utc ISO|--list FILE|--frames N --start S --end E\n"
                    "                [--rot deg] [--pole deg] [--size 400|466] [--out DIR] [--darkzero]\n"
                    "                [--dusk] [--dusk-kill thousandths]\n");
    return 2;
}
