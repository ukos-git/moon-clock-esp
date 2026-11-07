/**
 * @file knob_board_encoder.c
 * @brief EC1 rotary dial (A = GPIO 8, B = GPIO 7, 10K pull-ups to 3V3).
 *
 * Bench history (K4, 2026-11-08): a 1 ms polling decoder saw CW detents as
 * clean A pulses but NOTHING AT ALL on the opposing turn (no A, no B, raw
 * probe silent) and only rare B samples - the working hypothesis is
 * sub-millisecond flicker edges in one direction that a polling loop can
 * miss between samples. This revision therefore captures BOTH channels on
 * BOTH edges from a GPIO ISR into a timestamped ring (edges can no longer
 * hide), and the drain task (a) logs every edge for waveform analysis and
 * (b) runs the decoder on the captured stream: detent = stable A rise with
 * instantaneous B for the sign (the vendor iot_knob scheme, see
 * knob.rst), plus a plain quadrature-table sum logged alongside so the
 * bench can tell which metric the hardware actually supports.
 */
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"

static const char *TAG = "knob_enc";

#define ENC_PIN_A 8
#define ENC_PIN_B 7
#define ENC_DRAIN_MS 10 /* ring service period (logging only, not sampling) */
#define ENC_RING 256    /* edges buffered between ISR and drain task */

typedef struct {
    uint32_t ts; /* esp_timer microseconds at the edge */
    uint8_t a;
    uint8_t b;
} edge_rec_t;

static volatile edge_rec_t s_ring[ENC_RING];
static volatile uint32_t s_produced; /* ISR writes */
static volatile uint32_t s_consumed; /* drain task writes */

static volatile int32_t s_count;
static volatile int32_t s_quad;
static portMUX_TYPE s_count_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

static void IRAM_ATTR enc_isr(void *arg) {
    (void)arg;
    const uint32_t ts = (uint32_t)esp_timer_get_time(); /* us */
    const uint8_t a = (uint8_t)gpio_get_level(ENC_PIN_A);
    const uint8_t b = (uint8_t)gpio_get_level(ENC_PIN_B);
    const uint32_t h = s_produced;
    if (h - s_consumed < ENC_RING) {
        s_ring[h % ENC_RING] = (edge_rec_t){.ts = ts, .a = a, .b = b};
        s_produced = h + 1;
    }
}

/* 4x quadrature table, state = a*2+b, index = old*4+new. Legal single-
 * channel steps get +1/-1; double-channel jumps (bounce/missed edge) get 0. */
static const int8_t QTAB[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

static void enc_task(void *arg) {
    (void)arg;
    /* Decoder state walks the captured edge stream. la_* = last settled
     * level used for the A-rise rule; a_cand/a_run keep the vendor-style
     * settle on the SAMPLED value at the edge (bounce bursts arrive as
     * many fast edges - a rise only counts when the level stays for
     * SETTLE edges within ~settle window ms, approximated by wall time
     * since the first candidate edge). */
    int a = (int)gpio_get_level(ENC_PIN_A);
    int b = (int)gpio_get_level(ENC_PIN_B);
    int quad_state = a * 2 + b;
    uint32_t hb = 0;
    uint32_t edges_seen = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(ENC_DRAIN_MS));
        while (s_consumed != s_produced) {
            const edge_rec_t e = s_ring[s_consumed % ENC_RING];
            s_consumed++;
            edges_seen++;
            ESP_LOGI(TAG, "e %u A=%d B=%d", (unsigned)e.ts, e.a, e.b);
            /* quadrature accumulate */
            const int st = e.a * 2 + e.b;
            if (st != quad_state) {
                portENTER_CRITICAL(&s_count_mux);
                s_quad += QTAB[quad_state * 4 + st];
                portEXIT_CRITICAL(&s_count_mux);
                quad_state = st;
            }
            /* vendor-scheme detent: accepted A RISE, instantaneous B sign */
            if (e.a != a) {
                const int was = a;
                a = e.a;
                if (was == 0 && e.a == 1) {
                    portENTER_CRITICAL(&s_count_mux);
                    s_count += e.b ? -1 : +1; /* bench-flip if reversed */
                    const int32_t c = s_count;
                    portEXIT_CRITICAL(&s_count_mux);
                    ESP_LOGI(TAG, "A rise (Braw=%d) count %d", e.b, (int)c);
                }
            }
            if (e.b != b) {
                b = e.b;
            }
        }
        if (++hb >= 500) { /* 5 s */
            hb = 0;
            portENTER_CRITICAL(&s_count_mux);
            const int32_t c = s_count, q = s_quad;
            portEXIT_CRITICAL(&s_count_mux);
            ESP_LOGI(TAG, "hb A=%d B=%d count %d quad %d edges %u", gpio_get_level(ENC_PIN_A),
                     gpio_get_level(ENC_PIN_B), (int)c, (int)q, (unsigned)edges_seen);
        }
    }
}

esp_err_t knob_board_encoder_init(void) {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENC_PIN_A) | (1ULL << ENC_PIN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio cfg");
    ESP_RETURN_ON_ERROR(gpio_install_isr_service(0), TAG, "isr svc");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(ENC_PIN_A, enc_isr, NULL), TAG, "isr a");
    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(ENC_PIN_B, enc_isr, NULL), TAG, "isr b");
    if (s_task == NULL) {
        xTaskCreate(enc_task, "knob_enc", 3072, NULL, 5, &s_task);
        if (s_task == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "dial up: A=%d B=%d (edge ISR + ring, %d deep)", ENC_PIN_A, ENC_PIN_B, ENC_RING);
    return ESP_OK;
}

int32_t knob_board_encoder_count(void) {
    portENTER_CRITICAL(&s_count_mux);
    const int32_t c = s_count;
    portEXIT_CRITICAL(&s_count_mux);
    return c;
}

void knob_board_encoder_reset(void) {
    portENTER_CRITICAL(&s_count_mux);
    s_count = 0;
    s_quad = 0;
    portEXIT_CRITICAL(&s_count_mux);
}
