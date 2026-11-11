/**
 * @file knob_board_encoder.c
 * @brief EC1 rotary dial (A = GPIO 8, B = GPIO 7, 10K pull-ups to 3V3).
 *
 * This is NOT a quadrature wheel - it is a UP/DOWN pulse pair: every
 * detent pulses exactly ONE channel, and which channel pulsed is the
 * direction (bench + vendor, 2026-11-08). The vendor's own driver
 * (ESP-IDF demo, user_encoder_bsp/src/bidi_switch_knob.c) treats the
 * channels as two independent pulse counters: debounced A rise = +1
 * (KNOB_RIGHT), debounced B rise = -1 (KNOB_LEFT), 3 ms poll, edge
 * accepted after DEBOUNCE_TICKS consecutive identical samples. Every
 * quadrature-shaped attempt failed exactly as the bench showed: an
 * A-edge-count scheme lost all B-detents (the "only moves one way"
 * symptom - those were the opposing-direction detents), and an A-rise
 * with B-sign scheme found no sign (B never shares the A pulse's
 * moment because the channels pulse on ALTERNATE detents).
 * The polling shape below mirrors bidi_switch_knob.c 1:1.
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
#define ENC_POLL_MS 3  /* vendor TICKS_INTERVAL */
#define ENC_DEBOUNCE 2 /* vendor DEBOUNCE_TICKS */

static volatile int32_t s_count;
static portMUX_TYPE s_count_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

/* Vendor process_knob_channel(): LOW samples only re-arm the counter;
 * the detent fires on the debounced RISE. step is +1 for A (right),
 * -1 for B (left). */
static void process_channel(int level, int *prev, int *deb, int32_t step) {
    if (level == 0) {
        *deb = (level != *prev) ? 0 : *deb + 1;
    } else {
        if (level != *prev && ++(*deb) >= ENC_DEBOUNCE) {
            *deb = 0;
            portENTER_CRITICAL(&s_count_mux);
            s_count += step;
            portEXIT_CRITICAL(&s_count_mux);
        } else {
            *deb = 0;
        }
    }
    *prev = level;
}

static void enc_task(void *arg) {
    (void)arg;
    int a = gpio_get_level(ENC_PIN_A);
    int b = gpio_get_level(ENC_PIN_B);
    int deb_a = 0, deb_b = 0;
    while (1) {
        /* Tick-safe floor: pdMS_TO_TICKS(3) == 0 at the knob's 100 Hz
         * systick, and vTaskDelay(0) does NOT yield — this priority-5
         * poller then busy-spins, starves IDLE0, and the task WDT
         * aborts the boot (bench 2026-11-11: hammer boot 1 died at
         * t≈6.4 s with "CPU 0: knob_enc", ~6 s boot loop). One tick =
         * 10 ms at 100 Hz (finger detents are >=30 ms apart and
         * dial_task consumes one detent per 20 ms anyway); a 1 kHz
         * build keeps the vendor's 3 ms. */
        TickType_t poll = pdMS_TO_TICKS(ENC_POLL_MS);
        vTaskDelay(poll < 1 ? 1 : poll);
        process_channel(gpio_get_level(ENC_PIN_A), &a, &deb_a, +1);
        process_channel(gpio_get_level(ENC_PIN_B), &b, &deb_b, -1);
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
        xTaskCreate(enc_task, "knob_enc", 3072, NULL, 5, &s_task);
        if (s_task == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    ESP_LOGI(TAG, "dial up: A=%d B=%d (pulse-pair, vendor scheme)", ENC_PIN_A, ENC_PIN_B);
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
