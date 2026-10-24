/* QMI8658 gravity reader (see include/imu.h).
 *
 * Register map (from the Waveshare SensorLib that ships with this board --
 * the exact lib the board vendor's 03_QMI8658 example uses):
 *   WHO_AM_I  0x00 (0x05)
 *   CTRL1     0x02  (bit 6 = EN.ADDR_AI)
 *   CTRL2     0x03  (bits 4-5 accel range, bits 0-3 accel ODR)
 *   CTRL7     0x08  (bit 0 = accel enable)
 *   ACC X/Y/Z 16-bit little-endian, 6-byte burst from 0x35..0x3A
 *
 * Config (mirrors the vendor app for this board): no soft reset on a
 * healthy chip (the reset path has been seen to leave the chip's I2C
 * reads stuck) -> EN.ADDR_AI -> accel +/-8 g @ 1000 Hz (self-test off)
 * -> enable. If the WHO_AM_I read already fails (chip left in a stuck
 * state by an earlier reset), imu_init() runs the vendor's soft-reset
 * sequence (0x60=0xB0, poll RST_RESULT, EN.ADDR_AI) once and retries.
 * The driver is range-agnostic:
 * it averages 16 samples at rest and calibrates "1 g" to that magnitude, so
 * the screen-plane angle atan2(gy, gx) and the runtime |g| sanity band are
 * in calibrated units regardless of the chosen range.
 *
 * Bus: the shared BSP I2C master. The bus handle is probed with
 * i2c_master_probe (both candidate addresses), then a device handle is
 * added (same pattern as the PCF85063A driver on this bus); transactions
 * use the device-level API.
 *
 * Cost: one 6-byte burst read per tick (a few hundred microseconds on the
 * shared 100 kHz bus); init does ~30 short transactions, once at boot.
 */
#include "imu.h"

#include <math.h>
#include <stddef.h>

#include "bsp/esp32_s3_touch_amoled_1_75.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu";

/* Register map (see file header). */
#define IMU_REG_WHOAMI 0x00
#define IMU_WHOAMI_VAL 0x05
#define IMU_REG_SOFT_RST 0x60
#define IMU_SOFT_RST_VAL 0xB0
#define IMU_REG_RST_RESULT 0x4D
#define IMU_RST_RESULT_VAL 0x80
#define IMU_REG_CTRL1 0x02
#define IMU_CTRL1_ADDR_AI_BIT 6
#define IMU_REG_CTRL2 0x03
#define IMU_REG_CTRL7 0x08
#define IMU_CTRL7_ACC_EN_BIT 0
/* CTRL2: bits 4-5 = accel range (8 g = 0b10), bits 0-3 = ODR (1000 Hz = 3);
 * self-test (bit 7) stays clear. => 0x20 | 0x03 = 0x23. */
#define IMU_CTRL2_ACC_8G_1KHZ 0x23
#define IMU_REG_AX_L 0x35
#define IMU_ACC_LEN 6
#define IMU_XFER_TIMEOUT_MS 100
#define IMU_CAL_SAMPLES 16
#define IMU_CAL_SETTLE_MS 20 /* between samples (1000 Hz ODR settles fast) */
#define IMU_ANG_EMA 0.3f     /* per read, shortest-path */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static uint8_t s_addr; /* probed IMU address (for logging) */
static float s_inv_g;  /* 1 / magnitude of 1 g, in LSB^-1 (calibrated) */
static float s_angle;  /* EMA-smoothed atan2(gy, gx) */
static bool s_have_angle;
static bool s_ready;

static esp_err_t imu_write(uint8_t reg, uint8_t val) {
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, IMU_XFER_TIMEOUT_MS);
}

/* Register reads use a STOP + fresh START (select the register, then a
 * separate receive) rather than i2c_master_transmit_receive's repeated
 * START, matching how the vendor driver reads through Arduino Wire (STOP
 * after the register-select, a new START for the data). */
static uint8_t imu_read_reg(uint8_t reg) {
    uint8_t v = 0;
    if (i2c_master_transmit(s_dev, &reg, 1, IMU_XFER_TIMEOUT_MS) != ESP_OK)
        return 0xFF;
    return i2c_master_receive(s_dev, &v, 1, IMU_XFER_TIMEOUT_MS) == ESP_OK ? v : 0xFF;
}

static void imu_set_bit(uint8_t reg, uint8_t bit) {
    uint8_t v = imu_read_reg(reg);
    if (v != 0xFF)
        imu_write(reg, (uint8_t)(v | (1u << bit)));
}

/* Software reset (the vendor lib's reset(), for a chip whose I2C reads
 * are stuck): write 0x60=0xB0, poll RST_RESULT 0x4D == 0x80 for up to
 * ~500 ms, then set EN.ADDR_AI (CTRL1 bit 6). */
#define IMU_REG_SOFT_RST 0x60
#define IMU_SOFT_RST_VAL 0xB0
#define IMU_REG_RST_RESULT 0x4D
#define IMU_RST_RESULT_VAL 0x80
static bool imu_soft_reset(void) {
    esp_err_t err = imu_write(IMU_REG_SOFT_RST, IMU_SOFT_RST_VAL);
    if (err != ESP_OK) {
        /* Distinguishes "chip NACKs even the reset write" (I2C core dead or
         * wrong address) from "write ACKed but the chip never came back"
         * (reset ignored -- rail held / chip faulted). */
        ESP_LOGW(TAG, "soft reset write 0x60=0xB0 not ACKed (%s)", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "soft reset write 0x60=0xB0 ACKed; polling RST_RESULT");
    for (int i = 0; i < 50; i++) { /* ~500 ms */
        if (imu_read_reg(IMU_REG_RST_RESULT) == IMU_RST_RESULT_VAL)
            break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    imu_set_bit(IMU_REG_CTRL1, IMU_CTRL1_ADDR_AI_BIT);
    return true;
}

/* AXP2101 PMIC status probe (read-only): the board's power state decides
 * whether a "power cycle" ever actually cut the IMU rail. If a battery is
 * connected (STATUS1 bit 3) the PMIC rails survive a USB unplug, so the
 * IMU never sees a true cold power-on and can stay stuck in whatever state
 * it was left in. Registers (vendor XPowersLib AXP2101Constants.h):
 *   STATUS1 0x00 -- bit5 VBUS good, bit4 BATFET, bit3 battery connected
 *   STATUS2 0x01 -- bits6-5 charging=01/discharging=10/standby=00, bit2 chg
 *   PWRON_STATUS 0x20 -- what woke the board (bit4 battery, bit2 VBUS). */
static int imu_axp_read(i2c_master_dev_handle_t d, uint8_t reg) {
    uint8_t v = 0;
    if (i2c_master_transmit(d, &reg, 1, IMU_XFER_TIMEOUT_MS) != ESP_OK)
        return -1;
    return i2c_master_receive(d, &v, 1, IMU_XFER_TIMEOUT_MS) == ESP_OK ? v : -1;
}

/* AXP2101 PMIC status probe (read-only): tells whether a battery holds
 * the IMU rail alive across a "power cycle". Registers (vendor XPowersLib
 * AXP2101Constants.h): STATUS1 0x00 (bit5 VBUS good, bit3 battery
 * connected), STATUS2 0x01 (bits6-5 charge state), PWRON_STATUS 0x20
 * (what started the board: bit4 battery, bit2 VBUS). */
static void imu_axp_probe(void) {
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x34, .scl_speed_hz = 100000};
    i2c_master_dev_handle_t axp;
    if (i2c_master_bus_add_device(s_bus, &cfg, &axp) != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 @0x34 not probing; cannot read power state");
        return;
    }
    int s1 = imu_axp_read(axp, 0x00), s2 = imu_axp_read(axp, 0x01), s3 = imu_axp_read(axp, 0x20);
    i2c_master_bus_rm_device(axp);
    if (s1 < 0) {
        ESP_LOGW(TAG, "AXP2101 answers 0x34 but status reads fail");
        return;
    }
    ESP_LOGI(TAG, "AXP2101: STATUS1=0x%02X (VBUS %s, battery %s) STATUS2=0x%02X PWRON=0x%02X", s1,
             (s1 & 0x20) ? "good" : "absent", (s1 & 0x08) ? "connected" : "absent", s2, s3);
}

/* Diagnostic: probe the whole 7-bit address range and log every responder.
 * A healthy board shows the PCF85063 (0x51) and the TCA9554 (0x20) here;
 * if the IMU does not appear at 0x6A/0x6B the chip has not power-cycled
 * (its rail was held by the battery/caps) or is faulted. */
static void imu_bus_scan(void) {
    char buf[160];
    int n = 0;
    for (uint8_t a = 0x08; a <= 0x77; a++) {
        if (i2c_master_probe(s_bus, a, 20) == ESP_OK)
            n += snprintf(buf + n, sizeof(buf) - (size_t)n, " %02X", a);
    }
    ESP_LOGI(TAG, "bus scan:%s", n ? buf + 1 : " (no responders)");
}

static esp_err_t imu_read_raw(int16_t xyz[3]) {
    uint8_t reg = IMU_REG_AX_L;
    uint8_t buf[IMU_ACC_LEN];
    /* STOP + fresh START (see imu_read_reg for the why). */
    if (i2c_master_transmit(s_dev, &reg, 1, IMU_XFER_TIMEOUT_MS) != ESP_OK)
        return ESP_FAIL;
    if (i2c_master_receive(s_dev, buf, IMU_ACC_LEN, IMU_XFER_TIMEOUT_MS) != ESP_OK)
        return ESP_FAIL;
    xyz[0] = (int16_t)(buf[0] | (buf[1] << 8));
    xyz[1] = (int16_t)(buf[2] | (buf[3] << 8));
    xyz[2] = (int16_t)(buf[4] | (buf[5] << 8));
    return ESP_OK;
}

esp_err_t imu_init(void) {
    if (s_ready)
        return ESP_OK;
    s_bus = bsp_i2c_get_handle();
    if (!s_bus) {
        ESP_LOGE(TAG, "no I2C bus handle from BSP");
        return ESP_ERR_INVALID_STATE;
    }

    /* Probe 0x6A (SAD high), then 0x6B, for a matching WHO_AM_I. A brief
     * retry: right after a cold power-on the chip's I2C peripheral can be
     * a few ms behind its own ready state, and the vendor example polls
     * WHO_AM_I in a loop for exactly this reason. */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(250));
            imu_bus_scan();
        }
        for (uint8_t addr = 0x6A; addr <= 0x6B && !s_dev; addr++) {
            if (i2c_master_probe(s_bus, addr, IMU_XFER_TIMEOUT_MS) != ESP_OK)
                continue;
            i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
                                       .device_address = addr,
                                       .scl_speed_hz = 100000}; /* the vendor QMI8658
                                                                 * example runs this bus at
                                                                 * 100 kHz; the chip's reads
                                                                 * have been seen to fail at
                                                                 * 400 kHz */
            i2c_master_dev_handle_t tmp;
            if (i2c_master_bus_add_device(s_bus, &cfg, &tmp) != ESP_OK)
                continue;
            /* s_dev must be live before any transaction: imu_read_reg and
             * imu_soft_reset use it, and a NULL handle fails every
             * transfer with ESP_ERR_INVALID_ARG before touching the wire,
             * which reads back as 0xFF and looks like a dead chip. */
            s_dev = tmp;
            s_addr = addr;
            uint8_t who = 0xFF;
            uint8_t reg = IMU_REG_WHOAMI;
            esp_err_t werr = i2c_master_transmit(s_dev, &reg, 1, IMU_XFER_TIMEOUT_MS);
            if (werr == ESP_OK)
                werr = i2c_master_receive(s_dev, &who, 1, IMU_XFER_TIMEOUT_MS);
            if (werr != ESP_OK)
                ESP_LOGW(TAG, "@0x%02X WHO_AM_I read failed: %s", addr, esp_err_to_name(werr));
            if (who != IMU_WHOAMI_VAL) {
                /* Chip present but reads stuck (e.g. after an earlier
                 * software reset): run the vendor's reset sequence once
                 * and re-check. */
                ESP_LOGW(TAG, "@0x%02X answers but WHO_AM_I 0x%02X != 0x%02X; trying soft reset", addr, who,
                         IMU_WHOAMI_VAL);
                imu_soft_reset();
                who = imu_read_reg(IMU_REG_WHOAMI);
                if (who != IMU_WHOAMI_VAL) {
                    i2c_master_bus_rm_device(s_dev);
                    s_dev = NULL;
                    s_addr = 0;
                    continue;
                }
                ESP_LOGI(TAG, "@0x%02X recovered after soft reset", addr);
            }
        }
        if (s_dev)
            break;
    }
    if (!s_dev) {
        imu_bus_scan();
        imu_axp_probe(); /* did the PMIC rails survive a "power cycle"? */
        ESP_LOGW(TAG, "QMI8658 not found on the I2C bus (0x6A/0x6B); moon tilt disabled");
        return ESP_ERR_NOT_FOUND;
    }

    /* No soft reset: the Waveshare reference app for this board configures
     * + enables the accel directly on the power-on sensor (the reset path
     * leaves the accel stuck off). Set EN.ADDR_AI (CTRL1 bit 6), then
     * accel +/-8 g @ 1000 Hz (self-test off), then enable (CTRL7 bit 0).
     * The gyro stays off (unused). */
    imu_set_bit(IMU_REG_CTRL1, IMU_CTRL1_ADDR_AI_BIT);
    {
        uint8_t v = imu_read_reg(IMU_REG_CTRL2);
        if (v != 0xFF)
            imu_write(IMU_REG_CTRL2, (uint8_t)((v & 0x80u) | IMU_CTRL2_ACC_8G_1KHZ));
    }
    imu_set_bit(IMU_REG_CTRL7, IMU_CTRL7_ACC_EN_BIT);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Calibrate: average 16 samples; the device is assumed at rest, so the
     * mean vector magnitude is 1 g. */
    float sx = 0, sy = 0, sz = 0;
    for (int i = 0; i < IMU_CAL_SAMPLES; i++) {
        vTaskDelay(pdMS_TO_TICKS(IMU_CAL_SETTLE_MS));
        int16_t v[3];
        if (imu_read_raw(v) != ESP_OK) {
            ESP_LOGW(TAG, "accel read failed during init; moon tilt disabled");
            return ESP_FAIL;
        }
        sx += v[0];
        sy += v[1];
        sz += v[2];
    }
    float ax = sx / IMU_CAL_SAMPLES, ay = sy / IMU_CAL_SAMPLES, az = sz / IMU_CAL_SAMPLES;
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    if (mag < 0.2f) {
        ESP_LOGE(TAG, "init |g| %.3f is implausible (chip moving or stuck); moon tilt disabled", mag);
        return ESP_ERR_INVALID_STATE;
    }
    s_inv_g = 1.0f / mag;
    ESP_LOGI(TAG, "QMI8658 @0x%02X ready: avg (%.3f, %.3f, %.3f) g", s_addr, ax * s_inv_g, ay * s_inv_g, az * s_inv_g);
    s_ready = true;
    return ESP_OK;
}

bool imu_is_ready(void) { return s_ready; }

bool imu_read_roll(float *angle_rad) {
    if (!s_ready)
        return false;
    int16_t v[3];
    if (imu_read_raw(v) != ESP_OK)
        return false;
    float gx = v[0] * s_inv_g, gy = v[1] * s_inv_g, gz = v[2] * s_inv_g;
    float mag = sqrtf(gx * gx + gy * gy + gz * gz);
    if (mag < 0.5f || mag > 1.5f) /* shaking / dropped: skip the sample */
        return false;
    float ang = atan2f(gy, gx);
    if (s_have_angle) {
        float diff = ang - s_angle;
        if (diff > M_PI)
            diff -= 2.0f * M_PI;
        else if (diff < -M_PI)
            diff += 2.0f * M_PI;
        s_angle += IMU_ANG_EMA * diff;
        if (s_angle > M_PI)
            s_angle -= 2.0f * M_PI;
        else if (s_angle < -M_PI)
            s_angle += 2.0f * M_PI;
    } else {
        s_angle = ang;
        s_have_angle = true;
    }
    *angle_rad = s_angle;
    return true;
}
