/**
 * @file knob_battery.c
 * @brief P4 battery gauge: adc_oneshot on GPIO 1 (ADC1_CH0) with the
 *        eFuse curve-fitting calibration, scaled across the 10K/10K
 *        divider, linear between the Kconfig placeholder points.
 *
 * The calibration pair (MV_EMPTY/MV_FULL) is the plan's sanctioned
 * LiPo PLACEHOLDER: the multimeter ritual (user present) decides
 * whether the divider's top rail is the cell or VBUS and lands the
 * honest numbers + the divider ratio. Until then the gauge reads
 * plausible, not verified — the device page carries that verdict.
 * With CONFIG_KNOB_BATTERY_GAUGE=n every entry point is inert (the
 * component still links: the early requirements pass sees only the
 * literal REQUIRES).
 */
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include "knob_battery.h"

static const char *TAG = "knob_battery";

/* #ifndef-seeded (repo invariant: seed the Kconfig defaults, never
 * redefine the macros — with n the macros are UNDEFINED entirely). */
#ifndef CONFIG_KNOB_BATTERY_ADC_CHANNEL
#define CONFIG_KNOB_BATTERY_ADC_CHANNEL 0
#endif
#ifndef CONFIG_KNOB_BATTERY_DIV_NUM
#define CONFIG_KNOB_BATTERY_DIV_NUM 2
#endif
#ifndef CONFIG_KNOB_BATTERY_DIV_DEN
#define CONFIG_KNOB_BATTERY_DIV_DEN 1
#endif
#ifndef CONFIG_KNOB_BATTERY_MV_EMPTY
#define CONFIG_KNOB_BATTERY_MV_EMPTY 3300
#endif
#ifndef CONFIG_KNOB_BATTERY_MV_FULL
#define CONFIG_KNOB_BATTERY_MV_FULL 4200
#endif
#ifndef CONFIG_KNOB_BATTERY_CHARGING_MV
#define CONFIG_KNOB_BATTERY_CHARGING_MV 4400
#endif

#if defined(CONFIG_KNOB_BATTERY_GAUGE) && CONFIG_KNOB_BATTERY_GAUGE

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali; /* NULL -> linear raw approximation */
static bool s_ready;
static bool s_first_logged;

void knob_battery_init(void) {
    adc_oneshot_unit_init_cfg_t unit_cfg = {.unit_id = ADC_UNIT_1};
    esp_err_t err = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC_UNIT_1 init failed (%s) -- gauge off", esp_err_to_name(err));
        s_adc = NULL;
        return;
    }
    adc_oneshot_chan_cfg_t chan_cfg = {.bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12};
    err = adc_oneshot_config_channel(s_adc, CONFIG_KNOB_BATTERY_ADC_CHANNEL, &chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "channel %d config failed (%s) -- gauge off", CONFIG_KNOB_BATTERY_ADC_CHANNEL,
                 esp_err_to_name(err));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return;
    }
    /* Fuse calibration (the documented S3 curve-fitting scheme); its
     * absence is not fatal — the approximation below keeps the gauge
     * alive (placeholder accuracy either way). */
    adc_cali_curve_fitting_config_t cali_cfg = {.unit_id = ADC_UNIT_1,
                                                .chan = CONFIG_KNOB_BATTERY_ADC_CHANNEL,
                                                .atten = ADC_ATTEN_DB_12,
                                                .bitwidth = ADC_BITWIDTH_DEFAULT};
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "no fuse calibration scheme -- linear raw approximation");
    }
    s_ready = true;
    ESP_LOGI(TAG, "gauge up (ADC1 ch%d, divider %d/%d, placeholder %d..%d mV%s)", CONFIG_KNOB_BATTERY_ADC_CHANNEL,
             (int)CONFIG_KNOB_BATTERY_DIV_NUM, (int)CONFIG_KNOB_BATTERY_DIV_DEN, (int)CONFIG_KNOB_BATTERY_MV_EMPTY,
             (int)CONFIG_KNOB_BATTERY_MV_FULL, s_cali ? "" : ", raw approx");
}

bool knob_battery_available(void) { return s_ready; }

int knob_battery_cell_mv(void) {
    if (!s_ready) {
        return -1;
    }
    int raw = 0;
    if (adc_oneshot_read(s_adc, CONFIG_KNOB_BATTERY_ADC_CHANNEL, &raw) != ESP_OK) {
        return -1;
    }
    int mv = 0;
    if (s_cali != NULL && adc_cali_raw_to_voltage(s_cali, raw, &mv) != ESP_OK) {
        mv = 0;
    }
    if (mv == 0) { /* no/zero calibration: ~3100 mV full scale at 12 dB */
        adc_bitwidth_t bw = ADC_BITWIDTH_DEFAULT;
        const int full = (bw == ADC_BITWIDTH_12) ? 3100 : 775;
        mv = raw * full / ((1 << (bw == ADC_BITWIDTH_12 ? 12 : 10)) - 1);
    }
    mv = mv * (int)CONFIG_KNOB_BATTERY_DIV_NUM /
         (int)(CONFIG_KNOB_BATTERY_DIV_DEN > 0 ? CONFIG_KNOB_BATTERY_DIV_DEN : 1);
    if (!s_first_logged) { /* the bench line: raw + calibrated + cell side */
        s_first_logged = true;
        ESP_LOGI(TAG, "first read: raw %d -> %d mV sense -> %d mV cell", raw,
                 mv * (int)CONFIG_KNOB_BATTERY_DIV_DEN /
                     (int)(CONFIG_KNOB_BATTERY_DIV_NUM > 0 ? CONFIG_KNOB_BATTERY_DIV_NUM : 1),
                 mv);
    }
    return mv;
}

int knob_battery_percent(void) {
    const int mv = knob_battery_cell_mv();
    if (mv < 0) {
        return -1;
    }
    const int span = (int)CONFIG_KNOB_BATTERY_MV_FULL - (int)CONFIG_KNOB_BATTERY_MV_EMPTY;
    if (span <= 0) {
        return -1;
    }
    int pct = (mv - (int)CONFIG_KNOB_BATTERY_MV_EMPTY) * 100 / span;
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    return pct;
}

bool knob_battery_charging(void) {
    if (!s_ready || (int)CONFIG_KNOB_BATTERY_CHARGING_MV <= 0) {
        return false;
    }
    const int mv = knob_battery_cell_mv();
    return mv >= (int)CONFIG_KNOB_BATTERY_CHARGING_MV;
}

#else /* !CONFIG_KNOB_BATTERY_GAUGE */

void knob_battery_init(void) {}
bool knob_battery_available(void) { return false; }
int knob_battery_cell_mv(void) { return -1; }
int knob_battery_percent(void) { return -1; }
bool knob_battery_charging(void) { return false; }

#endif /* CONFIG_KNOB_BATTERY_GAUGE */
