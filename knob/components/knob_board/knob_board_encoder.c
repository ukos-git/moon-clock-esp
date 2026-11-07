/**
 * @file knob_board_encoder.c
 * @brief EC1 rotary dial (A = GPIO 8, B = GPIO 7, 10K pull-ups to 3V3).
 *
 * The vendor's driver is a software-polling debouncer (no PCNT, no ISR) —
 * we keep that shape: a 2 ms task walks the 4-state Gray-code wheel and
 * counts signed detents. CONFIG_FREERTOS_HZ=1000 so 2 ms really is 2 ticks
 * (the clock's 100 Hz spin lesson).
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
#define ENC_POLL_MS 2

static volatile int32_t s_count;
static portMUX_TYPE s_count_mux = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;

/* 4-state Gray wheel: index = (A<<1)|B of the PREVIOUS sample << 2 |
 * current sample; +1/-1 valid detents, anything else a bounce. */
static const int8_t s_steps[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};

static uint8_t enc_read_state(void) { return (uint8_t)((gpio_get_level(ENC_PIN_A) << 1) | gpio_get_level(ENC_PIN_B)); }

static void enc_task(void *arg) {
    uint8_t prev = enc_read_state();
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(ENC_POLL_MS));
        const uint8_t now = enc_read_state();
        if (now != prev) {
            const int8_t step = s_steps[(prev << 2) | now];
            if (step != 0) {
                portENTER_CRITICAL(&s_count_mux);
                s_count += step;
                portEXIT_CRITICAL(&s_count_mux);
            }
            prev = now;
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
    ESP_LOGI(TAG, "dial up: A=%d B=%d (2 ms poll)", ENC_PIN_A, ENC_PIN_B);
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
