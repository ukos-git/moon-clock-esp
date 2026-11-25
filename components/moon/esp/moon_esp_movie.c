/* Boot-movie backend: the flash-resident synodic cycle (moon_movie
 * partition). Container layout in moon_movie.h; the reader is the same
 * pattern as moon_esp_assets.c (partition find + esp_partition_read into
 * PSRAM, no mmap: the S3 DROM 4-byte alignment rule vs per-pixel samples
 * applies to the decode path too). The JPEG decode mirrors
 * moon_tfcache.c's fetch (esp_new_jpeg RGB888 out, take R -- the movie's
 * gray JPEGs decode with R == G == B), so a movie frame and a cached card
 * hour are byte-identical grays at the same phase.
 */
#include "moon_movie.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"

#include "esp_jpeg_dec.h"

#ifndef MOON_CALC_VER
#define MOON_CALC_VER "unknown" /* no git at configure time (tarball build) */
#endif

#define MOVIE_HDR_LEN 62
#define MOVIE_MAGIC "MOOV"

/* Packed little-endian -- MUST match movie.py's HDR exactly
 * (struct "<4s16s8sHHBBQ5I": 62 bytes, no padding). */
#pragma pack(push, 1)
typedef struct {
    uint8_t magic[4]; /* "MOOV"                  */
    char calcver[16]; /* NUL-padded              */
    char target[8];   /* NUL-padded              */
    uint16_t s_px;
    uint16_t radius;
    uint8_t jpeg_q;
    uint8_t hours_per_frame;
    uint64_t frame_count;
    uint32_t ref_center; /* epoch s, frame 0's hour centre */
    uint32_t table_off;
    uint32_t table_len; /* == frame_count * 4              */
    uint32_t frame_off;
    uint32_t frame_len;
} movie_hdr_t;
#pragma pack(pop)

_Static_assert(sizeof(movie_hdr_t) == MOVIE_HDR_LEN, "movie header must stay 62 packed bytes (movie.py's HDR)");

static const char *TAG = "movie";

static const esp_partition_t *s_part;
static movie_hdr_t s_hdr;
static uint32_t *s_table;   /* PSRAM: frame_count u32 offsets */
static uint8_t *s_fetch_in; /* PSRAM: largest frame (16-aligned) */
static size_t s_fetch_cap;
static uint8_t *s_dec_rgb; /* PSRAM: s_px^2*3 RGB888 (16-aligned), lazy */

/* 16-byte-aligned SPIRAM helpers (the codec's io buffers want 16
 * alignment; the stash trick is the house pattern from moon_tfcache.c). */
static void *alloc16(size_t size) {
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

static size_t frame_len(uint32_t idx) {
    return (size_t)((idx + 1 < s_hdr.frame_count ? s_table[idx + 1] : s_hdr.frame_len) - s_table[idx]);
}

bool moon_movie_init(moon_movie_info_t *info) {
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "moon_movie");
    if (!s_part) {
        ESP_LOGI(TAG, "no moon_movie partition -- the boot movie is unavailable");
        return false;
    }
    if (s_part->size < MOVIE_HDR_LEN) {
        ESP_LOGE(TAG, "moon_movie partition too small (%u B < %u header)", (unsigned)s_part->size, MOVIE_HDR_LEN);
        return false;
    }
    movie_hdr_t h;
    if (esp_partition_read(s_part, 0, &h, sizeof h) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed (movie header)");
        return false;
    }
    if (memcmp(h.magic, MOVIE_MAGIC, 4) != 0) {
        ESP_LOGI(TAG, "moon_movie header magic invalid -- the partition is empty / unformatted");
        return false;
    }
    if (strcmp(h.calcver, MOON_CALC_VER) != 0) {
        ESP_LOGI(TAG, "movie calcver %s != %s -- unavailable (re-flash with the current build)", h.calcver,
                 MOON_CALC_VER);
        return false;
    }
    if (h.frame_count == 0) {
        ESP_LOGI(TAG, "zero-frame movie -- unavailable (the generator could not fit a cycle in the row)");
        return false;
    }
    uint32_t n = (uint32_t)h.frame_count;
    if (h.table_len != n * 4u || h.table_off + h.table_len > s_part->size || h.frame_off + h.frame_len > s_part->size) {
        ESP_LOGE(TAG, "movie container out of bounds (table %u+%u, frames %u+%u, partition %u B)",
                 (unsigned)h.table_off, (unsigned)h.table_len, (unsigned)h.frame_off, (unsigned)h.frame_len,
                 (unsigned)s_part->size);
        return false;
    }
    s_hdr = h;
    s_table = (uint32_t *)heap_caps_malloc((size_t)n * 4, MALLOC_CAP_SPIRAM);
    if (!s_table) {
        ESP_LOGE(TAG, "PSRAM allocation failed (movie table %u KB)", (n * 4) / 1024);
        return false;
    }
    if (esp_partition_read(s_part, h.table_off, s_table, (size_t)n * 4) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed (movie table)");
        free16(s_table);
        s_table = NULL;
        return false;
    }
    /* Size the fetch buffer to the largest frame (one flash read per frame). */
    size_t maxf = 0;
    for (uint32_t i = 0; i < n; i++) {
        size_t l = frame_len(i);
        if (l > maxf)
            maxf = l;
    }
    s_fetch_cap = maxf;
    s_fetch_in = (uint8_t *)alloc16(maxf + 1);
    if (!s_fetch_in) {
        ESP_LOGE(TAG, "PSRAM allocation failed (movie fetch %u KB)", (unsigned)(maxf / 1024));
        free16(s_table);
        s_table = NULL;
        return false;
    }
    if (info) {
        info->frame_count = n;
        info->hours_per_frame = h.hours_per_frame;
        info->s_px = h.s_px;
        info->radius = h.radius;
        info->ref_center = (int64_t)h.ref_center;
    }
    ESP_LOGI(TAG, "movie ready: %u frames @ %u h/frame, %dx%d r%u, %u KB of %u KB partition", (unsigned)n,
             (unsigned)h.hours_per_frame, h.s_px, h.s_px, h.radius, (unsigned)(h.frame_len / 1024),
             (unsigned)(s_part->size / 1024));
    return true;
}

bool moon_movie_fetch(uint32_t idx, uint8_t *gray_out, uint32_t s_px) {
    if (!s_table || !s_fetch_in || idx >= s_hdr.frame_count || s_px != s_hdr.s_px)
        return false;
    size_t off = s_table[idx];
    size_t len = frame_len(idx);
    if (len == 0 || len > s_fetch_cap)
        return false;
    if (esp_partition_read(s_part, (size_t)s_hdr.frame_off + off, s_fetch_in, len) != ESP_OK) {
        ESP_LOGE(TAG, "esp_partition_read failed (frame %u, %u B)", (unsigned)idx, (unsigned)len);
        return false;
    }
    if (!s_dec_rgb) { /* lazy: the scratch is only paid for when a frame is actually shown */
        s_dec_rgb = (uint8_t *)alloc16((size_t)s_px * s_px * 3 + 1);
        if (!s_dec_rgb) {
            ESP_LOGE(TAG, "PSRAM allocation failed (movie decode %u KB)", (unsigned)((size_t)s_px * s_px * 3 / 1024));
            return false;
        }
    }
    jpeg_dec_config_t dc = DEFAULT_JPEG_DEC_CONFIG();
    dc.output_type = JPEG_PIXEL_FORMAT_RGB888;
    jpeg_dec_handle_t dh = NULL;
    jpeg_dec_io_t io = {.inbuf = s_fetch_in, .inbuf_len = (int)len, .outbuf = s_dec_rgb};
    jpeg_dec_header_info_t hi;
    bool ok = false;
    if (jpeg_dec_open(&dc, &dh) == JPEG_ERR_OK) {
        ok = (jpeg_dec_parse_header(dh, &io, &hi) == JPEG_ERR_OK) && hi.width == (int)s_px && hi.height == (int)s_px &&
             (jpeg_dec_process(dh, &io) == JPEG_ERR_OK);
        jpeg_dec_close(dh);
    }
    if (ok) { /* gray domain end-to-end: a gray JPEG decodes with R == G == B */
        const uint8_t *rgb = s_dec_rgb;
        for (int i = 0, nn = (int)(s_px * s_px); i < nn; i++)
            gray_out[i] = rgb[3 * i];
    }
    return ok;
}
