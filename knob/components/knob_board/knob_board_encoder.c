/**
 * @file knob_board_encoder.c
 * @brief EC1 rotary dial (A = GPIO 8, B = GPIO 7, 10K pull-ups to 3V3).
 *
 * The vendor's driver is a software-polling debouncer (no PCNT, no ISR) —
 * we keep that shape but NOT its sampling: a raw 2 ms poll of the 4-state
 * Gray wheel double-counted a detent and unwound it with the contact
 * bounce (bench, K4: one turn read +1 then -1, the ring only ever reached
 * z/a/b). Each channel now must read stable for ENC_SETTLE consecutive
 * 1 ms samples before it counts, and a detent is ONE step taken on the
 * stable A edge with stable B deciding the sign — B-only transitions (the
 * bounce half of a detent) move nothing. CONFIG_FREERTOS_HZ=1000 so 1 ms
 * really is 1 tick (the clock's 100 Hz spin lesson).
 */
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"

static const char *TAG = "knob_enc";

#define ENC_PIN_A 8
#define ENC_PIN_B 7
#define ENC_POLL_MS 1
/* Consecutive identical 1 ms samples before a channel edge is accepted.
 * EC1 detent contact bounce runs a few ms; 5 ms sits above it and well
 * below a fast hand's detent period (~80 ms at 12 detents/s). */
#define ENC_SETTLE 5

static volatile int32_t s_count;
static portMUX_TYPE s_count_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

/* Per-channel settle filter + A-edge counting. A detent is ONE step: the
 * stable A transition, signed by stable B (quadrature). A-only or A-then-
 * B order is what the mechanical detent guarantees; a B bounce while A
 * holds just re-filters B without touching the count. */
static void enc_task(void *arg) {
    (void)arg;
    int a = gpio_get_level(ENC_PIN_A);
    int b = gpio_get_level(ENC_PIN_B);
    int a_cand = -1, a_run = 0, b_cand = -1, b_run = 0;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(ENC_POLL_MS));
        const int la = gpio_get_level(ENC_PIN_A);
        if (la == a) {
            a_cand = -1;
            a_run = 0;
        } else if (la == a_cand && ++a_run >= ENC_SETTLE) {
            a = la;
            a_cand = -1;
            a_run = 0;
            portENTER_CRITICAL(&s_count_mux);
            s_count += b ? -1 : +1; /* stable B picks the sign (bench-flip if reversed) */
            portEXIT_CRITICAL(&s_count_mux);
        } else {
            a_cand = la;
            a_run = 1;
        }
        const int lb = gpio_get_level(ENC_PIN_B);
        if (lb == b) {
            b_cand = -1;
            b_run = 0;
        } else if (lb == b_cand && ++b_run >= ENC_SETTLE) {
            b = lb;
            b_cand = -1;
            b_run = 0;
        } else {
            b_cand = lb;
            b_run = 1;
        }
    }
}

esp_err_t knob_board_encoder_init(void) {
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << ENC_PIN_A) | (1ULL << ENC_PIN_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "gpio cfg");
    if (s_task == NULL) {
        /* Small stack: pure GPIO polling + a critical section. */
        xTaskCreate(enc_task, "knob_enc", 2048, NULL, 5, &s_task);
        if (s_task == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "dial up: A=%d B=%d (1 ms poll, %d-sample settle)", ENC_PIN_A, ENC_PIN_B, ENC_SETTLE);
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
    portEXIT_CRITICAL(&s_count_mux);
}
