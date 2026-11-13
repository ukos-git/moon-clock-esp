/*
 * TF-card bake cache for the synodic boot sweep -- implementation.
 * See moon_tfcache.h for the contract (versioned folder + meta
 * self-description, hourly keys, write-through, bounded window, silent
 * fallback to on-device baking).
 *
 * Codec: esp_new_jpeg (vendored, pulled transitively by esp_lv_decoder).
 * The bake output is gray8, and the encoder documents
 * JPEG_PIXEL_FORMAT_GRAY ("1-byte luminance ... Encoder supported")
 * while the decoder only outputs RGB888/565/CbYCrY -- so we ENCODE in
 * the gray domain (JPEG_SUBSAMPLE_GRAY, one luminance component: the
 * smallest, fastest file) and DECODE to RGB888, taking the R channel
 * (for a gray source JPEG, R == G == B == Y). The gray encode path is
 * verified at init with a 16x16 probe (encode -> decode -> max-diff);
 * if the prebuilt library ever rejects it, store transparently falls
 * back to a 565/RGB888 duplicate of the gray (chroma of a gray image is
 * neutral, so the round trip still lands inside the probe tolerance).
 * The fetched frame goes through the SAME moon_gray565() tint LUT as a
 * baked frame (the sweep's convert block), so cache and compute paths
 * are pixel-identical by construction.
 *
 * Buffer alignment: the S3 codec wants 16-byte aligned in/out buffers;
 * PSRAM allocations are not, so every codec buffer comes from alloc16()
 * (SPIRAM + a stashed base pointer, 16-aligned).
 */
#include "sdkconfig.h"

#if CONFIG_UI_MOON_TFCACHE /* whole module compiled out when off (task #4's                                            \
                            * sweep branch is guarded by the same symbol) */

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "esp_jpeg_dec.h"
#include "esp_jpeg_enc.h"

#include "bsp/esp-bsp.h" /* bsp_sdcard_mount() + BSP_SD_MOUNT_POINT, per target */
#if !defined(CONFIG_BOARD_P4)
#include "driver/sdspi_host.h" /* the S3 socket is SPI-wired -- mount it on the SPI host */
#include "driver/spi_master.h" /* the caller must bring up the SPI bus itself */
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#endif

#include "moon_tfcache.h"

static const char *TAG = "tfcache";

#ifndef MOON_CALC_VER
#define MOON_CALC_VER "unknown" /* no git at configure time (tarball build) */
#endif
#ifndef CONFIG_UI_MOON_TFCACHE_JPEG_Q
#define CONFIG_UI_MOON_TFCACHE_JPEG_Q 90
#endif

/* The cache lives ON the FATFS volume: the BSP mounts the card at
 * BSP_SD_MOUNT_POINT, so the root must be under it (an unprefixed
 * "/moon_cache" is outside the VFS mount -- every path ENOENTs). */
#define CACHE_ROOT BSP_SD_MOUNT_POINT "/moon_cache"
#define SYNODIC_S ((int64_t)(29.53 * 86400)) /* same constant as the sweep */
#define TF_MAX_HOURS 709                     /* ceil(29.53 d / 1 h) + slack */
#define PROBE_TOL 24                         /* gray round-trip max |diff|  */

static volatile bool s_ready;
static int s_px;       /* bake size the scratch was sized for   */
static char s_dir[96]; /* /moon_cache/<calcver>_<tgt>_<px>      */

/* Codec scratch (one direction per task: fetch runs on the prefetch task,
 * store on the sweep task -- no locking needed on any of these). */
static uint8_t *s_dec_rgb; /* 3*S*S RGB888 decode out (16-aligned)  */
static uint8_t *s_enc_in;  /* encode in, fmt-sized (16-aligned)     */
static uint8_t *s_enc_out; /* encode out (16-aligned)               */
static size_t s_enc_out_cap;
static uint8_t *s_fetch_in; /* persistent fetch input (PREFETCH TASK
                             * only, sized s_enc_out_cap = the max a
                             * valid cached file can be); allocated on
                             * demand by the prefetch owner BEFORE the
                             * slot queue, so fetch never races the
                             * queue for the last of the PSRAM pool
                             * (a per-call file buffer OOM'd there at
                             * q90 file sizes -- "cache off for this
                             * boot: no psram (fetch)", 2026-11-08) */
static int s_enc_fmt;       /* 1 GRAY, 2 RGB565_LE dup, 3 RGB888 dup */

static tfcache_stats_t s_st;
static int64_t s_max_hour;        /* newest key stored this boot           */
static int64_t s_last_evict_hour; /* hour basis of the last eviction scan  */
static int s_files;               /* cached hours found at init (warm-folder verdict;    \
                                   * a cold folder never decodes, so the decode buffer   \
                                   * stays unallocated -- see tfcache_folder_warm())      */
static bool s_dec_warned;         /* one shot when the deferred decode alloc fails */

/* ---- 16-byte-aligned SPIRAM helpers ---------------------------------- */

static void *alloc16(size_t size) {
    /* +32 slack: 16 for the alignment shift (forced >=8 so the base
     * pointer stash at aligned-8 never eats the original malloc base). */
    void *p = heap_caps_malloc(size + 32, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p)
        return NULL;
    uintptr_t a = ((uintptr_t)p + 8 + 15) & ~(uintptr_t)15;
    ((uintptr_t *)a)[-1] = (uintptr_t)p;
    return (void *)a;
}

static void free16(void *p) {
    if (p)
        free((void *)((uintptr_t *)p)[-1]);
}

/* ---- small FATFS helpers ---------------------------------------------- */

static int mkdir_p(const char *path) {
    if (mkdir(path, 0755) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "mkdir %s failed: %d", path, errno);
        return -1;
    }
    return 0;
}

static void hour_path(int64_t hour, char *out, size_t n) { snprintf(out, n, "%s/%lld.jpg", s_dir, (long long)hour); }

/* Recursive delete, depth-bounded (the cache tree is 2 levels). */
static void rmrf(const char *path, int depth) {
    if (depth <= 0)
        return;
    DIR *d = opendir(path);
    if (!d) {
        unlink(path); /* a file, not a dir */
        return;
    }
    struct dirent *e;
    char child[512]; /* bounds proven to GCC below (precision specifiers) */
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(child, sizeof(child), "%.240s/%.100s", path,
                 e->d_name); /* names <= 255 on FAT; ours are far shorter */
        if (e->d_type == DT_DIR)
            rmrf(child, depth - 1);
        else
            unlink(child);
    }
    closedir(d);
    rmdir(path);
}

/* Parse "<hour>.jpg" -> hour (false on any other name). */
static bool parse_hour_name(const char *name, int64_t *hour) {
    char *end = NULL;
    long long v = strtoll(name, &end, 10);
    if (end == name || strcmp(end, ".jpg") != 0)
        return false;
    *hour = (int64_t)v;
    return true;
}

/* Number of "<hour>.jpg" hours cached in path (the warm-folder verdict).
 * Cheap: one FAT readdir of a folder bounded by TF_MAX_HOURS entries, and
 * only ever on the exists path of init. */
static int count_hours(const char *path) {
    int n = 0;
    DIR *d = opendir(path);
    if (!d)
        return 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        int64_t hour;
        if (parse_hour_name(e->d_name, &hour))
            n++;
    }
    closedir(d);
    return n;
}

/* ---- meta.txt: the self-describing cache (resolution can never lie) --- */

#define META_KEYS 6
static const char *const META_KEY[META_KEYS] = {"calcver", "target", "bake_px", "moon_albedo_w", "relief", "jpeg_q"};

static void meta_write(const char *dir, int bake_px, int albedo_w, bool relief) {
    char path[128];
    snprintf(path, sizeof(path), "%s/meta.txt", dir);
    FILE *f = fopen(path, "w");
    if (!f) {
        ESP_LOGW(TAG, "meta write open failed (%d)", errno);
        return;
    }
    fprintf(f, "calcver=%s\ntarget=%s\nbake_px=%d\nmoon_albedo_w=%d\nrelief=%d\njpeg_q=%d\n", MOON_CALC_VER,
            CONFIG_IDF_TARGET, bake_px, albedo_w, relief ? 1 : 0, CONFIG_UI_MOON_TFCACHE_JPEG_Q);
    fclose(f);
}

/* true when an existing meta.txt matches the live values exactly (all
 * six keys present); false also on a missing/unreadable/partial file --
 * anything but an exact match invalidates the folder. */
static bool meta_matches(const char *dir, int bake_px, int albedo_w, bool relief) {
    char path[128];
    snprintf(path, sizeof(path), "%s/meta.txt", dir);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    char want[META_KEYS][32];
    snprintf(want[0], 32, "%s", MOON_CALC_VER);
    snprintf(want[1], 32, "%s", CONFIG_IDF_TARGET);
    snprintf(want[2], 32, "%d", bake_px);
    snprintf(want[3], 32, "%d", albedo_w);
    snprintf(want[4], 32, "%d", relief ? 1 : 0);
    snprintf(want[5], 32, "%d", CONFIG_UI_MOON_TFCACHE_JPEG_Q);
    bool seen[META_KEYS] = {false};
    char line[80];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq++ = '\0';
        char *nl = strpbrk(eq, "\r\n");
        if (nl)
            *nl = '\0';
        for (int i = 0; i < META_KEYS; i++) {
            if (!strcmp(line, META_KEY[i]))
                seen[i] = (strcmp(eq, want[i]) == 0);
        }
    }
    fclose(f);
    for (int i = 0; i < META_KEYS; i++)
        if (!seen[i])
            return false;
    return true;
}

/* Delete every sibling version folder (stale calculations are stale by
 * definition -- the card only ever carries the current one). */
static void wipe_sibling_folders(void) {
    DIR *d = opendir(CACHE_ROOT);
    if (!d)
        return;
    const char *base = strrchr(s_dir, '/');
    base = base ? base + 1 : s_dir;
    struct dirent *e;
    char child[160];
    while ((e = readdir(d)) != NULL) {
        if (e->d_type != DT_DIR || !strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (!strcmp(e->d_name, base))
            continue;
        snprintf(child, sizeof(child), "%s/%.100s", CACHE_ROOT, e->d_name);
        ESP_LOGI(TAG, "dropping stale cache folder %s", child);
        rmrf(child, 3);
    }
    closedir(d);
}

/* ---- codec ------------------------------------------------------------- */

/* Encode one 16x16 gray probe with `fmt` and decode it back; true when
 * every pixel round-trips within PROBE_TOL. */
static bool probe_fmt(int fmt) {
    const int W = 16, H = 16;
    uint8_t src[16 * 16];
    for (int i = 0; i < W * H; i++)
        src[i] = (uint8_t)(i * 255 / (W * H - 1));

    int in_bytes = (fmt == 3) ? W * H * 3 : (fmt == 2 ? W * H * 2 : W * H);
    uint8_t *in = alloc16(in_bytes);
    uint8_t *out = alloc16(W * H * 2 + 1024);
    uint8_t *rgb = alloc16(W * H * 3);
    bool ok = in && out && rgb;
    if (ok) {
        if (fmt == 1) {
            memcpy(in, src, W * H);
        } else if (fmt == 2) {
            uint16_t *p = (uint16_t *)in;
            for (int i = 0; i < W * H; i++) {
                uint8_t v = src[i];
                p[i] = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3)); /* 565, r=g=b */
            }
        } else {
            for (int i = 0; i < W * H; i++) {
                in[3 * i] = in[3 * i + 1] = in[3 * i + 2] = src[i];
            }
        }
        jpeg_enc_config_t ec = DEFAULT_JPEG_ENC_CONFIG();
        ec.width = W;
        ec.height = H;
        ec.src_type =
            (fmt == 1) ? JPEG_PIXEL_FORMAT_GRAY : (fmt == 2 ? JPEG_PIXEL_FORMAT_RGB565_LE : JPEG_PIXEL_FORMAT_RGB888);
        ec.subsampling = (fmt == 1) ? JPEG_SUBSAMPLE_GRAY : JPEG_SUBSAMPLE_420;
        ec.quality = CONFIG_UI_MOON_TFCACHE_JPEG_Q;
        ec.task_enable = false;
        jpeg_enc_handle_t eh = NULL;
        int out_len = 0;
        ok = jpeg_enc_open(&ec, &eh) == JPEG_ERR_OK;
        if (ok) {
            ok = (jpeg_enc_process(eh, in, in_bytes, out, W * H * 2 + 1024, &out_len) == JPEG_ERR_OK) && out_len > 0;
            jpeg_enc_close(eh);
        }
        if (ok) {
            jpeg_dec_config_t dc = DEFAULT_JPEG_DEC_CONFIG();
            dc.output_type = JPEG_PIXEL_FORMAT_RGB888;
            jpeg_dec_handle_t dh = NULL;
            jpeg_dec_io_t io = {.inbuf = out, .inbuf_len = out_len, .outbuf = rgb};
            jpeg_dec_header_info_t hi;
            if (jpeg_dec_open(&dc, &dh) == JPEG_ERR_OK) {
                ok = (jpeg_dec_parse_header(dh, &io, &hi) == JPEG_ERR_OK) && hi.width == W && hi.height == H &&
                     (jpeg_dec_process(dh, &io) == JPEG_ERR_OK);
                jpeg_dec_close(dh);
            } else {
                ok = false;
            }
            if (ok) {
                for (int i = 0; i < W * H; i++) {
                    int d = (int)rgb[3 * i] - (int)src[i];
                    if (d < -PROBE_TOL || d > PROBE_TOL) {
                        ok = false;
                        break;
                    }
                }
            }
        }
    }
    free16(in);
    free16(out);
    free16(rgb);
    return ok;
}

static int probe_encoder(void) {
    for (int fmt = 1; fmt <= 3; fmt++) {
        if (probe_fmt(fmt)) {
            ESP_LOGI(TAG, "gray encode probe: fmt %d OK (%s)", fmt,
                     fmt == 1 ? "GRAY" : (fmt == 2 ? "RGB565 dup" : "RGB888 dup"));
            return fmt;
        }
        ESP_LOGW(TAG, "gray encode probe: fmt %d rejected", fmt);
    }
    return 0;
}

/* Mount the card.
 *
 * P4: the BSP's own 4-bit SDMMC slot (CLK 43 / CMD 44 / D0-3 39-42) --
 * proper SD wiring, bsp_sdcard_mount() verbatim.
 *
 * S3: the socket is SPI-wired (MOSI 1 / SCK 2 / MISO 3 / CS 41, the
 * Waveshare datasheet). The vendored BSP instead maps those pins onto
 * an SDMMC 1-bit slot -- it mounts, but the bench caught a ~19 s
 * first-touch init plus writes dying mid-run after ~33 files (the
 * classic CRC-retry collapse of SDMMC-on-SPI-wiring). Mount on a real
 * SPI host, which is what the wiring was built for. sdspi only ATTACHES
 * a device -- the bus must be initialised by the caller on the socket's
 * lines, and it cannot be SPI2: the panel holds that host
 * (BSP_LCD_SPI_NUM), and the bench caught a silent card_init timeout
 * (0x107) when the SD device rode the panel's bus. GPSPI3 is free. */
static esp_err_t card_mount(void) {
#if defined(CONFIG_BOARD_P4)
    return bsp_sdcard_mount();
#else
    static sdmmc_card_t *s_card; /* mounted earlier this boot */
    if (s_card)
        return ESP_OK;
    static bool s_bus_done;
    if (!s_bus_done) {
        const spi_bus_config_t buscfg = {
            .mosi_io_num = BSP_SD_CMD, /* GPIO1: the socket's CMD line drives the card's DI */
            .miso_io_num = BSP_SD_D0,  /* GPIO3: D0 carries the card's DO */
            .sclk_io_num = BSP_SD_CLK, /* GPIO2 */
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,
        };
        esp_err_t be = spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);
        if (be != ESP_OK && be != ESP_ERR_INVALID_STATE)
            return be;
        s_bus_done = true;
    }
    const esp_vfs_fat_sdmmc_mount_config_t mcfg = {
        .format_if_mount_failed = false, /* never format a foreign card */
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = SPI3_HOST; /* SPI2 is the QSPI panel's (BSP_LCD_SPI_NUM) */
    slot.gpio_cs = GPIO_NUM_41;
    slot.gpio_cd = GPIO_NUM_NC;
    slot.gpio_wp = GPIO_NUM_NC;
    slot.gpio_int = GPIO_NUM_NC;
    esp_err_t e = esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, &host, &slot, &mcfg, &s_card);
    if (e != ESP_OK)
        s_card = NULL;
    return e;
#endif
}

/* ---- public API -------------------------------------------------------- */

esp_err_t tfcache_init(int bake_px, int moon_px, int albedo_w, bool relief) {
    if (s_ready)
        return ESP_OK;
    int S = bake_px > moon_px ? moon_px : bake_px;
    if (S <= 0) {
        tfcache_disable("bad bake px");
        return ESP_FAIL;
    }
    esp_err_t e = card_mount();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) {
        tfcache_disable("no card");
        return e;
    }
    mkdir_p(CACHE_ROOT);
    snprintf(s_dir, sizeof(s_dir), "%s/%s_%s_%d", CACHE_ROOT, MOON_CALC_VER, CONFIG_IDF_TARGET, S);

    struct stat st;
    bool exists = (stat(s_dir, &st) == 0 && S_ISDIR(st.st_mode));
    if (exists && !meta_matches(s_dir, S, albedo_w, relief)) {
        ESP_LOGW(TAG, "meta mismatch (px/albedo/relief/q) -- wiping %s", s_dir);
        rmrf(s_dir, 3);
        exists = false;
    }
    if (!exists) {
        if (mkdir_p(s_dir) != 0 || mkdir_p(CACHE_ROOT) != 0) {
            tfcache_disable("mkdir fail");
            return ESP_FAIL;
        }
        meta_write(s_dir, S, albedo_w, relief);
        struct stat st2;
        if (stat(s_dir, &st2) != 0) { /* never claim ready on a folder that is not there */
            tfcache_disable("no cache dir");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "created cache folder %s (calcver %s)", s_dir, MOON_CALC_VER);
    }
    wipe_sibling_folders();
    s_files = exists ? count_hours(s_dir) : 0; /* warm-folder verdict (below) */

    s_enc_fmt = probe_encoder();
    if (!s_enc_fmt) {
        tfcache_disable("codec probe");
        return ESP_FAIL;
    }

    size_t in_bytes = (s_enc_fmt == 3) ? (size_t)S * S * 3 : (s_enc_fmt == 2 ? (size_t)S * S * 2 : (size_t)S * S);
    s_enc_out_cap = (size_t)S * S + (size_t)S * S / 2 + 1024;
    /* The decode out-buffer (3*S*S) is NOT allocated here: a cold folder
     * never decodes anything this boot (every fetch is a miss until the
     * first write-through, and the prefetch runs ahead of those), and on
     * the 8 MB S3 those 470 KB were exactly what starved the slot queue
     * -- "tf slots: only 1/2 x 156KB -- card-less path" baked all 1344
     * frames every boot and seeded nothing (task #38). tfcache_fetch
     * allocates it on the first real hit; the prefetch budget reserves it
     * when this boot can actually hit (s_files > 0). */
    s_enc_in = alloc16(in_bytes);
    s_enc_out = alloc16(s_enc_out_cap);
    if (!s_enc_in || !s_enc_out) {
        free16(s_enc_in);
        free16(s_enc_out);
        s_enc_in = s_enc_out = NULL;
        tfcache_disable("no psram");
        return ESP_ERR_NO_MEM;
    }
    s_px = S;
    s_ready = true;
    ESP_LOGI(TAG, "ready: %s px=%d q=%d scratch=%u KB (+%d hours)", s_dir, S, CONFIG_UI_MOON_TFCACHE_JPEG_Q,
             (unsigned)(in_bytes + s_enc_out_cap) / 1024, s_files);
    return ESP_OK;
}

/* true when the version folder already held hours at init: the prefetch
 * budget must then reserve the deferred decode buffer (every hit needs
 * it). A cold folder (0 hours) never decodes -- prefetch may spend that
 * PSRAM on slots instead. */
bool tfcache_folder_warm(void) { return s_ready && s_files > 0; }

bool tfcache_ready(void) { return s_ready; }

void tfcache_scratch_free(void) {
    if (!s_ready)
        return; /* never up, or already handed back */
    free16(s_dec_rgb);
    free16(s_enc_in);
    free16(s_enc_out);
    free16(s_fetch_in); /* task #39: the persistent fetch input joins the
                         * hand-back -- post-sweep it is 241 KB of dead
                         * weight the 8 MB S3 needs for the mask snapshot */
    s_dec_rgb = s_enc_in = s_enc_out = s_fetch_in = NULL;
    s_enc_out_cap = 0;
    ESP_LOGI(TAG, "codec scratch returned: PSRAM back for the mask snapshot");
}

bool tfcache_fetch_buf_alloc(void) {
    if (!s_ready || !s_enc_out)
        return false; /* scratch handed back after the card-less verdict */
    if (!s_fetch_in)
        s_fetch_in = alloc16(s_enc_out_cap);
    return s_fetch_in != NULL;
}

void tfcache_disable(const char *reason) {
    if (!s_ready)
        return; /* already off; the first reason is the truth */
    s_ready = false;
    s_st.disable_reason = reason;
    ESP_LOGW(TAG, "cache off for this boot: %s (sweep recomputes on-device)", reason ? reason : "?");
}

bool tfcache_fetch(int64_t hour, uint8_t *gray_out, int S) {
    if (!s_ready || S != s_px || !s_enc_out)
        return false;
    char path[128];
    hour_path(hour, path, sizeof(path));
    struct stat st;
    if (stat(path, &st) != 0) {
        s_st.misses++;
        return false; /* the normal miss: nothing cached for this hour */
    }
    if (st.st_size <= 0 || (size_t)st.st_size > s_enc_out_cap) {
        unlink(path); /* truncated/garbage -- heal, recompute later */
        s_st.misses++;
        return false;
    }
    if (!s_fetch_in) { /* the prefetch owner never got its buffer */
        tfcache_disable("no psram (fetch buf)");
        return false;
    }
    uint8_t *in = s_fetch_in; /* persistent (prefetch-task only), sized
                               * s_enc_out_cap -- the corrupt-file size
                               * check above bounds st.st_size to that */
    bool ok = false;
    FILE *f = fopen(path, "rb");
    int64_t t0 = esp_timer_get_time();
    if (f) {
        ok = fread(in, 1, (size_t)st.st_size, f) == (size_t)st.st_size;
        fclose(f);
    }
    s_st.read_us += esp_timer_get_time() - t0;
    if (!ok) {
        s_st.misses++;
        return false;
    }
    s_st.bytes_read += st.st_size;
    if (!s_dec_rgb) { /* first real hit this boot: pay for the decode out-
                       * buffer here, never on a cold miss (prefetch task
                       * only -- no locking, same rule as the other codec
                       * buffers). Out of PSRAM: degrade to a miss so the
                       * sweep bakes this frame (and stores it, like any
                       * miss) -- correctness is untouched. */
        s_dec_rgb = alloc16((size_t)S * S * 3);
        if (!s_dec_rgb) {
            if (!s_dec_warned) {
                s_dec_warned = true;
                ESP_LOGW(TAG, "deferred decode scratch out of PSRAM -- hits decode as misses this boot");
            }
            s_st.misses++;
            return false;
        }
    }
    jpeg_dec_config_t dc = DEFAULT_JPEG_DEC_CONFIG();
    dc.output_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_dec_handle_t dh = NULL;
    jpeg_dec_io_t io = {.inbuf = in, .inbuf_len = st.st_size, .outbuf = s_dec_rgb};
    jpeg_dec_header_info_t hi;
    t0 = esp_timer_get_time();
    if (jpeg_dec_open(&dc, &dh) == JPEG_ERR_OK) {
        ok = (jpeg_dec_parse_header(dh, &io, &hi) == JPEG_ERR_OK) && hi.width == S && hi.height == S &&
             (jpeg_dec_process(dh, &io) == JPEG_ERR_OK);
        jpeg_dec_close(dh);
    }
    s_st.dec_us += esp_timer_get_time() - t0;
    if (ok) { /* gray domain end-to-end: a gray JPEG decodes with R == G == B == Y */
        const uint8_t *rgb = s_dec_rgb;
        for (int i = 0, n = S * S; i < n; i++)
            gray_out[i] = rgb[3 * i];
        s_st.hits++;
    } else {
        unlink(path); /* undecodable (foreign/damaged) -- heal + recompute */
        s_st.misses++;
    }
    return ok;
}

bool tfcache_store(int64_t hour, const uint8_t *gray, int S) {
    if (!s_ready || S != s_px || !s_enc_in || !s_enc_out)
        return false;
    int in_bytes;
    if (s_enc_fmt == 1) {
        memcpy(s_enc_in, gray, (size_t)S * S);
        in_bytes = S * S;
    } else if (s_enc_fmt == 2) {
        uint16_t *p = (uint16_t *)s_enc_in;
        for (int i = 0, n = S * S; i < n; i++) {
            uint8_t v = gray[i];
            p[i] = (uint16_t)(((v >> 3) << 11) | ((v >> 2) << 5) | (v >> 3));
        }
        in_bytes = S * S * 2;
    } else {
        for (int i = 0, n = S * S; i < n; i++) {
            s_enc_in[3 * i] = s_enc_in[3 * i + 1] = s_enc_in[3 * i + 2] = gray[i];
        }
        in_bytes = S * S * 3;
    }
    jpeg_enc_config_t ec = DEFAULT_JPEG_ENC_CONFIG();
    ec.width = S;
    ec.height = S;
    ec.src_type = (s_enc_fmt == 1) ? JPEG_PIXEL_FORMAT_GRAY
                                   : (s_enc_fmt == 2 ? JPEG_PIXEL_FORMAT_RGB565_LE : JPEG_PIXEL_FORMAT_RGB888);
    ec.subsampling = (s_enc_fmt == 1) ? JPEG_SUBSAMPLE_GRAY : JPEG_SUBSAMPLE_420;
    ec.quality = CONFIG_UI_MOON_TFCACHE_JPEG_Q;
    ec.task_enable = false;
    jpeg_enc_handle_t eh = NULL;
    int out_len = 0;
    bool ok = false;
    int64_t t0 = esp_timer_get_time();
    if (jpeg_enc_open(&ec, &eh) == JPEG_ERR_OK) {
        ok = (jpeg_enc_process(eh, s_enc_in, in_bytes, s_enc_out, (int)s_enc_out_cap, &out_len) == JPEG_ERR_OK) &&
             out_len > 0;
        jpeg_enc_close(eh);
    }
    s_st.enc_us += esp_timer_get_time() - t0;
    if (!ok) {
        s_st.put_fails++;
        ESP_LOGD(TAG, "encode failed (hour %lld)", (long long)hour);
        return false;
    }
    char path[128];
    hour_path(hour, path, sizeof(path));
    t0 = esp_timer_get_time();
    FILE *f = fopen(path, "wb");
    if (!f) {
        ok = false;
    } else {
        ok = fwrite(s_enc_out, 1, (size_t)out_len, f) == (size_t)out_len;
        if (fclose(f) != 0)
            ok = false;
    }
    s_st.wr_us += esp_timer_get_time() - t0;
    if (!ok) {
        s_st.put_fails++;
        unlink(path); /* never leave a truncated frame: the next boot recomputes */
        if (errno == ENODEV || errno == EIO || errno == ENOENT)
            tfcache_disable("write io"); /* card gone / foreign FS -- stop trying */
        return false;
    }
    s_st.puts++;
    s_st.bytes_written += out_len;
    if (hour > s_max_hour) {
        s_max_hour = hour;
        if (s_max_hour - s_last_evict_hour > 24) /* one scan per day of new hours */
            tfcache_evict(s_max_hour);
    }
    return true;
}

void tfcache_evict(int64_t newest_hour) {
    if (!s_ready)
        return;
    int64_t floor_hour = (newest_hour * 3600 - SYNODIC_S) / 3600;
    int64_t t0 = esp_timer_get_time();
    DIR *d = opendir(s_dir);
    if (!d) {
        tfcache_disable("dir gone");
        return;
    }
    struct dirent *e;
    char path[192];
    int64_t hour;
    while ((e = readdir(d)) != NULL) {
        if (!parse_hour_name(e->d_name, &hour) || hour >= floor_hour)
            continue;
        snprintf(path, sizeof(path), "%.90s/%.60s", s_dir, e->d_name); /* hour keys are <= 14 chars */
        if (unlink(path) == 0)
            s_st.evict_del++;
    }
    closedir(d);
    s_st.evict_us += esp_timer_get_time() - t0;
    s_last_evict_hour = newest_hour;
}

const tfcache_stats_t *tfcache_stats(void) { return &s_st; }

void tfcache_stats_log(void) {
    const tfcache_stats_t *s = &s_st;
    ESP_LOGI(TAG,
             "stats: hits=%d miss=%d put=%d/%d evict=%d read=%lldKB/%lldms dec=%lldms enc=%lldms wr=%lldms "
             "evict=%lldms",
             s->hits, s->misses, s->puts, s->put_fails, s->evict_del, s->bytes_read / 1024, s->read_us / 1000,
             s->dec_us / 1000, s->enc_us / 1000, s->wr_us / 1000, s->evict_us / 1000);
    if (s->disable_reason)
        ESP_LOGW(TAG, "disabled: %s", s->disable_reason);
}

#endif /* CONFIG_UI_MOON_TFCACHE */
