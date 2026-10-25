#include <stdio.h>
#include <string.h>

#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "clock.h"
#include "clock_alarm_scheduler.h"
#include "clock_cb.h"
#include "clock_interrupt.h"
#include "clock_minute.h"
#include "pcf85063a.h"

static const char *TAG = "clock_interrupt";

// EXIO 3 pin definition (IO expander pin for RTC interrupt)
#define EXIO_RTC_INT_PIN IO_EXPANDER_PIN_NUM_3

// Polling interval for interrupt detection (in milliseconds)
// Expiration detection is time-based (see clock_handle_interrupts); the
// interval only affects how late the first poll past expiry can be.
static uint32_t s_rtc_poll_interval_ms = 1300; // Default value

// External references from clock.c
extern pcf85063a_dev_t s_rtc_dev;
extern esp_io_expander_handle_t s_io_expander;
extern bool s_initialized;

/**
 * @brief Handle all RTC interrupts in one function
 *
 * This function checks for pending RTC interrupts and processes them appropriately.
 * It handles both timer and alarm interrupts, managing synchronization and clearing flags.
 * Callbacks are invoked directly for both timer and alarm interrupts.
 *
 * @return esp_err_t ESP_OK on success
 */
esp_err_t clock_handle_interrupts(void) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    // Check if there's a pending interrupt on EXIO 3
    bool interrupt_pending = clock_is_interrupt_pending();
    if (!interrupt_pending) {
        return ESP_OK; // No interrupt to process
    }

    // Get interrupt flags
    clock_interrupt_t ints;
    esp_err_t ret = clock_get_interrupts(&ints);
    if (ret != ESP_OK || ints == CLOCK_INT_NONE) {
        return ret; // No valid interrupts
    }

    ESP_LOGD(TAG, "RTC interrupt detected, ints=0x%x", ints);

    // IMPORTANT: Check countdown timer BEFORE processing minute interrupt
    // Both share the TF flag, but minute interrupt processing clears it
    // We need to read timer value while TF is still set
    if (ints & CLOCK_INT_COUNTDOWN) {
        // Read timer value to determine if countdown expired (value will be 0)
        uint8_t timer_val = 0;
        esp_err_t timer_read_ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_VAL, &timer_val, 1);

        if (timer_read_ret == ESP_OK && clock_timer_is_active()) {
            uint8_t initial_value = clock_timer_get_value();

            // Expiration is judged against the host-side start timestamp
            // (esp_timer_get_time at enable), not the RTC register value: the
            // timer auto-reloads at 0 (re-setting TF every period) and the 1 Hz
            // source ticks once per second, so the register value alone cannot
            // distinguish "just expired" from "just started a while ago". The
            // old value-based heuristic (value >= initial - missed_ticks)
            // matched the *starting* value within the first poll and fired the
            // light->deep transition ~2 s after the countdown began.
            //
            // At 1 Hz the counter runs initial_value -> 1 -> 0(reload) per
            // period, so it has expired once at least (initial_value - 1)
            // seconds have elapsed; the -1 tolerates the 1 Hz tick quantization
            // and up to ~1.3 s of poll latency after expiry.
            int64_t elapsed_s = (esp_timer_get_time() - clock_timer_get_enabled_at_us()) / 1000000;
            int64_t expiry_s = (initial_value > 1) ? (initial_value - 1) : 0;

            ESP_LOGD(TAG, "Countdown timer check: value=%d, elapsed=%llds, expiry>=%llds", timer_val,
                     (long long)elapsed_s, (long long)expiry_s);

            bool expired = (elapsed_s >= expiry_s);
            if (expired || timer_val == 0 || timer_val == 1) {
                // Countdown timer has expired (time-based, or caught at 0/1)
                ESP_LOGI(TAG, "Countdown timer expired (value=%d, elapsed=%llds) - triggering deep sleep transition",
                         timer_val, (long long)elapsed_s);

                // CRITICAL: Disable the timer first to prevent further restarts
                // The timer auto-restarts when TF is cleared if TE is still set
                esp_err_t disable_ret = clock_timer_disable();
                if (disable_ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to disable countdown timer: %s", esp_err_to_name(disable_ret));
                }

                // Trigger countdown timer callback (e.g., transition to deep sleep)
                clock_trigger_countdown_timer();

                // Clear the TF flag
                clock_clear_interrupts(CLOCK_INT_COUNTDOWN);

                // Skip minute interrupt processing since we already cleared TF
                return ESP_OK;
            } else {
                // Countdown still running (elapsed < expiry), this TF is from the
                // minute interrupt
                ESP_LOGD(TAG, "Countdown timer still running (value=%d), will process MI", timer_val);
            }
        } else if (timer_read_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read timer value: %s", esp_err_to_name(timer_read_ret));
        }
    }

    // Handle minute interrupts (MI/HMI)
    if (ints & CLOCK_INT_MINUTE) {
        bool is_minute_tick = false;
        esp_err_t process_ret = clock_minute_process_interrupt(&is_minute_tick);

        if (process_ret == ESP_OK && is_minute_tick) {
            ESP_LOGI(TAG, "Minute interrupt processed - updating display");
            clock_trigger_time_update();
        } else if (process_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to process minute interrupt: %s", esp_err_to_name(process_ret));
        }
        // Note: clock_minute_process_interrupt() clears TF flag internally
    }

    // Handle alarm interrupts separately
    if (ints & CLOCK_INT_ALARM) {
        ESP_LOGI(TAG, "Alarm interrupt triggered!");

        // Trigger alarm trigger callback (e.g. show alarm screen)
        clock_trigger_alarm_triggered();

        // Clear alarm flag
        esp_err_t clear_ret = clock_clear_interrupts(CLOCK_INT_ALARM);
        if (clear_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear alarm flag: %s", esp_err_to_name(clear_ret));
        }

        // Schedule next alarm based on current time
        clock_time_t current_time;
        esp_err_t time_ret = clock_get_time(&current_time);
        if (time_ret == ESP_OK) {
            esp_err_t sched_ret = clock_alarm_schedule_next(&current_time);
            if (sched_ret == ESP_OK) {
                ESP_LOGI(TAG, "Next alarm scheduled successfully");
            } else {
                ESP_LOGW(TAG, "Failed to schedule next alarm: %s", esp_err_to_name(sched_ret));
            }
        } else {
            ESP_LOGE(TAG, "Failed to get current time for alarm rescheduling: %s", esp_err_to_name(time_ret));
        }
    }

    return ESP_OK;
}

esp_err_t clock_get_interrupts(clock_interrupt_t *out) {
    if (!out || !s_initialized) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t ctrl2;
    esp_err_t ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    *out = CLOCK_INT_NONE;

    // Check for TF (Timer Flag, bit 3) - set by MI, HMI, or countdown timer
    if (ctrl2 & PCF85063A_RTC_CTRL_2_TF) {
        // Read Timer Mode register to check if countdown timer is enabled
        uint8_t timer_mode = 0;
        esp_err_t timer_ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_TIMER_MODE, &timer_mode, 1);

        // Check which interrupt sources are enabled
        bool mi_enabled = (ctrl2 & PCF85063A_RTC_CTRL_2_MI) != 0;
        bool hmi_enabled = (ctrl2 & PCF85063A_RTC_CTRL_2_HMI) != 0;
        // Countdown timer must have both TE (Timer Enable) AND TIE (Timer Interrupt Enable) set
        bool countdown_enabled = (timer_ret == ESP_OK) && (timer_mode & PCF85063A_RTC_TIMER_MODE_TE) != 0 &&
                                 (timer_mode & PCF85063A_RTC_TIMER_MODE_TIE) != 0;

        // Set appropriate interrupt flags based on what's enabled
        // Note: TF can be set by MI, HMI, or countdown timer
        if (mi_enabled || hmi_enabled) {
            *out |= CLOCK_INT_MINUTE;
        }
        if (countdown_enabled) {
            *out |= CLOCK_INT_COUNTDOWN;
        }

        // If TF is set but nothing is enabled, log a warning with details
        if (!mi_enabled && !hmi_enabled && !countdown_enabled) {
            ESP_LOGW(TAG,
                     "TF flag set but no interrupt source enabled (CTRL2=0x%02x, TIMER_MODE=0x%02x, MI=%d, HMI=%d, "
                     "TE=%d, TIE=%d)",
                     ctrl2, timer_mode, mi_enabled, hmi_enabled, (timer_mode & PCF85063A_RTC_TIMER_MODE_TE) != 0,
                     (timer_mode & PCF85063A_RTC_TIMER_MODE_TIE) != 0);
        }
    }

    // Check for alarm interrupt flag (AF)
    if (ctrl2 & PCF85063A_RTC_CTRL_2_AF) {
        *out |= CLOCK_INT_ALARM;
    }

    return ESP_OK;
}

esp_err_t clock_clear_interrupts(clock_interrupt_t flags) {
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Clearing interrupt flags: 0x%x", flags);
    esp_err_t ret = ESP_OK;

    // Clear minute interrupt or countdown timer (both use TF flag)
    if ((flags & CLOCK_INT_MINUTE) || (flags & CLOCK_INT_COUNTDOWN)) {
        const char *int_type = (flags & CLOCK_INT_MINUTE) ? "MINUTE" : "COUNTDOWN";
        ESP_LOGD(TAG, "Clearing %s flag (TF)", int_type);
        esp_err_t timer_ret = pcf85063a_clear_timer_flag(&s_rtc_dev);
        if (timer_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear timer flag: %s", esp_err_to_name(timer_ret));
            ret = timer_ret;
        }
    }

    // Clear alarm interrupt
    if (flags & CLOCK_INT_ALARM) {
        ESP_LOGD(TAG, "Clearing ALARM flag (AF)");
        uint8_t ctrl2;
        esp_err_t alarm_ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
        if (alarm_ret == ESP_OK) {
            uint8_t ctrl2_before = ctrl2;
            ctrl2 &= ~PCF85063A_RTC_CTRL_2_AF;
            ESP_LOGD(TAG, "Clearing AF: CTRL2 before=0x%02x, after=0x%02x (MI=%d->%d, HMI=%d->%d)", ctrl2_before, ctrl2,
                     (ctrl2_before & PCF85063A_RTC_CTRL_2_MI) != 0, (ctrl2 & PCF85063A_RTC_CTRL_2_MI) != 0,
                     (ctrl2_before & PCF85063A_RTC_CTRL_2_HMI) != 0, (ctrl2 & PCF85063A_RTC_CTRL_2_HMI) != 0);
            uint8_t write_buf[2] = {PCF85063A_RTC_CTRL_2_ADDR, ctrl2};
            alarm_ret = pcf85063a_write_register(&s_rtc_dev, write_buf, 2);
        }
        if (alarm_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to clear alarm flag: %s", esp_err_to_name(alarm_ret));
            ret = alarm_ret;
        }
    }

    if (ret == ESP_OK) {
        // Check EXIO 3 pin status after clearing flags
        bool exio_int_active = false;
        if (s_io_expander) {
            uint32_t level_mask = 0;
            esp_err_t exio_ret = esp_io_expander_get_level(s_io_expander, EXIO_RTC_INT_PIN, &level_mask);
            if (exio_ret == ESP_OK) {
                // RTC interrupt is active low
                exio_int_active = (level_mask & EXIO_RTC_INT_PIN) == 0;
            }
        }
        ESP_LOGD(TAG, "Cleared interrupt flags - EXIO_INT=%d", exio_int_active);
    }

    return ret;
}

/**
 * @brief Check if RTC has pending interrupt flags
 *
 * This function checks the RTC's internal interrupt flags (TF and AF) to determine
 * if there are pending interrupts to process.
 *
 * Note on interrupt detection:
 * - MI/HMI pulses are only ~15ms wide (1/64 second)
 * - Countdown timer pulses depend on TI_TP setting
 * - With polling at 1.3s intervals, we'll miss the pulse but TF/AF flags stay set
 * - We check flags directly since they persist until software clears them
 * - Optional: Can check EXIO pin state, but not reliable for MI/HMI due to short pulse
 *
 * @return true if RTC has pending timer/alarm interrupt flags, false otherwise
 */
bool clock_is_interrupt_pending(void) {
    if (!s_initialized) {
        return false;
    }

    // Check EXIO 3 pin level (useful for wake-up from deep sleep detection)
    bool exio_int_active = false;
    if (s_io_expander) {
        uint32_t level_mask = 0;
        esp_err_t exio_ret = esp_io_expander_get_level(s_io_expander, EXIO_RTC_INT_PIN, &level_mask);
        if (exio_ret == ESP_OK) {
            // RTC interrupt is active low
            exio_int_active = (level_mask & EXIO_RTC_INT_PIN) == 0;
        }
    }

    // Read RTC interrupt flags (TF and AF)
    // These flags are sticky - stay set until explicitly cleared by software
    uint8_t ctrl2;
    esp_err_t ret = pcf85063a_read_register(&s_rtc_dev, PCF85063A_RTC_CTRL_2_ADDR, &ctrl2, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read RTC CTRL2 for interrupt check: %s", esp_err_to_name(ret));
        return false;
    }

    // Check if RTC has any interrupt flags set
    // TF (bit 3): Timer Flag - set by MI, HMI, or countdown timer
    // AF (bit 6): Alarm Flag
    bool tf_flag = (ctrl2 & PCF85063A_RTC_CTRL_2_TF) != 0;
    bool af_flag = (ctrl2 & PCF85063A_RTC_CTRL_2_AF) != 0;

    if (tf_flag || af_flag) {
        ESP_LOGI(TAG, "RTC interrupt flags detected - CTRL2=0x%02x, TF=%d, AF=%d, EXIO_INT=%d", ctrl2, tf_flag, af_flag,
                 exio_int_active);
        return true;
    }

    return false;
}

void clock_set_poll_interval_ms(uint32_t interval_ms) {
    s_rtc_poll_interval_ms = interval_ms;
    ESP_LOGI(TAG, "RTC polling interval set to %lu ms", interval_ms);
}
