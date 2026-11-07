/**
 * @file knob_board_touch.c
 * @brief CST816S capacitive touch, polled (the vendor demo polls too; the
 *        INT line on GPIO 9 is wired but unused there — we mirror that).
 *
 * Vendor read shape (Waveshare 08_LVGL_Test lcd_touch_bsp): normal-mode
 * write {0x00} to reg 0x00 once at init, then 7 bytes from reg 0x00 per
 * poll; contact count at [2], x = ([3]&0x0f)<<8 | [4], y = ([5]&0x0f)<<8 | [6].
 */
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "knob_board.h"

static const char *TAG = "knob_touch";

#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_I2C_ADDR 0x15
#define TOUCH_PIN_SDA 11
#define TOUCH_PIN_SCL 12
#define TOUCH_PIN_INT 9 /* wired, polled anyway (vendor style) */
#define TOUCH_PIN_RST 10

#define TOUCH_REG_MODE 0x00
#define TOUCH_POLL_LEN 7

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;

esp_err_t knob_board_touch_init(void) {
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "i2c bus");

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDR,
        .scl_speed_hz = 100 * 1000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev), TAG, "i2c dev");

    /* Hard reset pulse (the vendor holds RST low briefly, then releases). */
    gpio_config_t rst_io = {
        .pin_bit_mask = 1ULL << TOUCH_PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&rst_io), TAG, "rst cfg");
    gpio_set_level(TOUCH_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TOUCH_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Enter normal mode: vendor writes {0x00} to reg 0x00 once. */
    const uint8_t normal_mode[] = {TOUCH_REG_MODE, 0x00};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_dev, normal_mode, sizeof(normal_mode), -1), TAG, "mode");

    ESP_LOGI(TAG, "CST816S up @0x%02x (polled)", TOUCH_I2C_ADDR);
    return ESP_OK;
}

esp_err_t knob_board_touch_read(uint16_t *x, uint16_t *y, bool *pressed) {
    uint8_t buf[TOUCH_POLL_LEN];
    const uint8_t reg = TOUCH_REG_MODE;
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, buf, sizeof(buf), -1);
    if (err != ESP_OK) {
        return err;
    }
    const uint8_t count = buf[2];
    if (pressed) {
        *pressed = count > 0;
    }
    if (count > 0) {
        uint16_t tx = (uint16_t)((buf[3] & 0x0f) << 8) | buf[4];
        uint16_t ty = (uint16_t)((buf[5] & 0x0f) << 8) | buf[6];
        if (x) {
            *x = tx < KNOB_LCD_H_RES ? tx : KNOB_LCD_H_RES - 1;
        }
        if (y) {
            *y = ty < KNOB_LCD_V_RES ? ty : KNOB_LCD_V_RES - 1;
        }
    } else if (x) {
        *x = 0;
        if (y) {
            *y = 0;
        }
    }
    return ESP_OK;
}
