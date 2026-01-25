#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "esp_log.h"

#include "clock.h"
#include "clock_alarm.h"
#include "clock_alarm_scheduler.h"
#include "clock_cb.h"

static const char *TAG = "clock_alarm_sched";
static const char *ALARM_FILE_PATH = "/spiffs/alarm.txt";

static alarm_rule_t s_alarm_rules[MAX_ALARM_RULES];
static int s_alarm_count = 0;

// Day of week calculation (Zeller's congruence for Gregorian calendar)
static int get_day_of_week(int year, int month, int day) {
    if (month < 3) {
        month += 12;
        year--;
    }
    int q = day;
    int m = month;
    int k = year % 100;
    int j = year / 100;
    int h = (q + ((13 * (m + 1)) / 5) + k + (k / 4) + (j / 4) - (2 * j)) % 7;
    // Convert to 0=Sunday, 1=Monday, ..., 6=Saturday
    return (h + 6) % 7;
}

// Parse day of week pattern (e.g., "1-5", "0,6", "*")
static bool parse_day_pattern(const char *pattern, uint8_t *day_mask, bool *all_days) {
    *day_mask = 0;
    *all_days = false;

    if (strcmp(pattern, "*") == 0) {
        *all_days = true;
        return true;
    }

    // Check for range (e.g., "1-5")
    if (strchr(pattern, '-')) {
        int start, end;
        if (sscanf(pattern, "%d-%d", &start, &end) == 2) {
            if (start < 0 || start > 6 || end < 0 || end > 6 || start > end) {
                return false;
            }
            for (int i = start; i <= end; i++) {
                *day_mask |= (1 << i);
            }
            return true;
        }
        return false;
    }

    // Check for comma-separated list (e.g., "0,6")
    if (strchr(pattern, ',')) {
        char *pattern_copy = strdup(pattern);
        if (!pattern_copy) {
            return false;
        }
        char *token = strtok(pattern_copy, ",");
        while (token) {
            int day = atoi(token);
            if (day < 0 || day > 6) {
                free(pattern_copy);
                return false;
            }
            *day_mask |= (1 << day);
            token = strtok(NULL, ",");
        }
        free(pattern_copy);
        return true;
    }

    // Single day
    int day = atoi(pattern);
    if (day < 0 || day > 6) {
        return false;
    }
    *day_mask = (1 << day);
    return true;
}

// Parse minute or hour field (supports *, */N, or number)
static bool parse_time_field(const char *field, int *value, int *interval, int max_value) {
    *value = -1;   // -1 means wildcard
    *interval = 0; // 0 means no interval

    if (strcmp(field, "*") == 0) {
        *value = -1; // wildcard
        return true;
    }

    // Check for */N pattern
    if (strncmp(field, "*/", 2) == 0) {
        int n = atoi(field + 2);
        if (n <= 0 || n > max_value) {
            return false;
        }
        *interval = n;
        *value = -1; // wildcard with interval
        return true;
    }

    // Regular number
    int n = atoi(field);
    if (n < 0 || n > max_value) {
        return false;
    }
    *value = n;
    return true;
}

// Parse a single alarm rule line (cron format: minute hour day_of_month month day_of_week)
static bool parse_alarm_line(const char *line, alarm_rule_t *rule) {
    char minute_str[16], hour_str[16], dom_str[16], month_str[16], dow_str[32];

    // Skip empty lines and comments
    if (line[0] == '#' || line[0] == '\0' || line[0] == '\n') {
        return false;
    }

    // Parse format: "minute hour day_of_month month day_of_week"
    int parsed = sscanf(line, "%15s %15s %15s %15s %31s", minute_str, hour_str, dom_str, month_str, dow_str);

    // Support both 5-field and 3-field formats for backward compatibility
    if (parsed == 3) {
        // Old 3-field format: minute hour day_of_week
        strcpy(dow_str, dom_str);
        strcpy(dom_str, "*");
        strcpy(month_str, "*");
    } else if (parsed != 5) {
        return false;
    }

    // Parse minute field
    if (!parse_time_field(minute_str, &rule->minute, &rule->minute_interval, 59)) {
        ESP_LOGE(TAG, "Invalid minute field: %s", minute_str);
        return false;
    }

    // Parse hour field
    if (!parse_time_field(hour_str, &rule->hour, &rule->hour_interval, 23)) {
        ESP_LOGE(TAG, "Invalid hour field: %s", hour_str);
        return false;
    }

    // Validate: can't have both minute and hour intervals
    if (rule->minute_interval > 0 && rule->hour_interval > 0) {
        ESP_LOGE(TAG, "Cannot specify both minute and hour intervals");
        return false;
    }

    // Parse day of week pattern
    if (!parse_day_pattern(dow_str, &rule->day_mask, &rule->all_days)) {
        ESP_LOGE(TAG, "Invalid day pattern: %s", dow_str);
        return false;
    }

    // Log the parsed rule
    if (rule->minute_interval > 0) {
        ESP_LOGI(TAG, "Parsed alarm rule: every %d minutes at hour %s, days=0x%02x", rule->minute_interval, hour_str,
                 rule->day_mask);
    } else if (rule->hour_interval > 0) {
        ESP_LOGI(TAG, "Parsed alarm rule: minute %d, every %d hours, days=0x%02x", rule->minute, rule->hour_interval,
                 rule->day_mask);
    } else {
        ESP_LOGI(TAG, "Parsed alarm rule: %02d:%02d, days=0x%02x, all_days=%d", rule->hour, rule->minute,
                 rule->day_mask, rule->all_days);
    }

    return true;
}

esp_err_t clock_alarm_scheduler_init(void) {
    ESP_LOGI(TAG, "Initializing alarm scheduler from SPIFFS");

    // SPIFFS is already initialized by the system
    // Just try to open the alarm file
    FILE *f = fopen(ALARM_FILE_PATH, "r");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open alarm file: %s (file may not exist yet)", ALARM_FILE_PATH);
        return ESP_ERR_NOT_FOUND;
    }

    // Read and parse alarm rules
    s_alarm_count = 0;
    char line[128];
    while (fgets(line, sizeof(line), f) && s_alarm_count < MAX_ALARM_RULES) {
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;

        alarm_rule_t rule;
        if (parse_alarm_line(line, &rule)) {
            s_alarm_rules[s_alarm_count++] = rule;
        }
    }

    fclose(f);

    ESP_LOGI(TAG, "Loaded %d alarm rules from SPIFFS", s_alarm_count);
    return ESP_OK;
}

// Calculate next alarm time based on a single rule
static bool calculate_next_alarm_time(const alarm_rule_t *rule, const clock_time_t *current, clock_time_t *next) {
    // Handle interval-based alarms (*/N syntax)
    if (rule->minute_interval > 0) {
        // Every N minutes
        next->year = current->year;
        next->month = current->month;
        next->day = current->day;
        next->hour = current->hour;
        next->second = 0;

        // Calculate next minute that's a multiple of the interval
        int current_minute = current->minute + 1; // Next minute
        int next_minute =
            ((current_minute + rule->minute_interval - 1) / rule->minute_interval) * rule->minute_interval;

        if (next_minute >= 60) {
            // Move to next hour
            next->hour++;
            next_minute = 0;

            if (next->hour >= 24) {
                // Move to next day
                next->hour = 0;
                struct tm time_info = {0};
                time_info.tm_year = current->year - 1900;
                time_info.tm_mon = current->month - 1;
                time_info.tm_mday = current->day + 1;
                mktime(&time_info);
                next->year = time_info.tm_year + 1900;
                next->month = time_info.tm_mon + 1;
                next->day = time_info.tm_mday;
            }
        }
        next->minute = next_minute;

        // Check if this day matches the rule's day mask
        int day_of_week = get_day_of_week(next->year, next->month, next->day);
        if (!rule->all_days && !(rule->day_mask & (1 << day_of_week))) {
            // This day doesn't match, find the next matching day
            for (int day_offset = 1; day_offset < 8; day_offset++) {
                struct tm time_info = {0};
                time_info.tm_year = current->year - 1900;
                time_info.tm_mon = current->month - 1;
                time_info.tm_mday = current->day + day_offset;
                time_info.tm_hour = 0;
                time_info.tm_min = 0;
                mktime(&time_info);

                int dow = get_day_of_week(time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday);
                if (rule->day_mask & (1 << dow)) {
                    next->year = time_info.tm_year + 1900;
                    next->month = time_info.tm_mon + 1;
                    next->day = time_info.tm_mday;
                    next->hour = 0;
                    next->minute = 0;
                    return true;
                }
            }
        }
        return true;
    }

    // Handle regular time-based alarms
    next->year = current->year;
    next->month = current->month;
    next->day = current->day;
    next->hour = rule->hour;
    next->minute = rule->minute;
    next->second = 0;

    // Check if alarm time today has already passed
    bool alarm_passed_today = false;
    if (current->hour > rule->hour || (current->hour == rule->hour && current->minute >= rule->minute)) {
        alarm_passed_today = true;
    }

    // Try up to 7 days to find the next matching day
    for (int day_offset = (alarm_passed_today ? 1 : 0); day_offset < 8; day_offset++) {
        // Calculate the date
        struct tm time_info = {0};
        time_info.tm_year = current->year - 1900;
        time_info.tm_mon = current->month - 1;
        time_info.tm_mday = current->day + day_offset;
        time_info.tm_hour = rule->hour;
        time_info.tm_min = rule->minute;
        time_info.tm_sec = 0;

        // Normalize the date (handles month/year rollover)
        mktime(&time_info);

        int day_of_week = get_day_of_week(time_info.tm_year + 1900, time_info.tm_mon + 1, time_info.tm_mday);

        // Check if this day matches the rule
        if (rule->all_days || (rule->day_mask & (1 << day_of_week))) {
            next->year = time_info.tm_year + 1900;
            next->month = time_info.tm_mon + 1;
            next->day = time_info.tm_mday;
            return true;
        }
    }

    return false;
}

// Compare two times and return true if time1 < time2
static bool time_is_before(const clock_time_t *time1, const clock_time_t *time2) {
    if (time1->year != time2->year)
        return time1->year < time2->year;
    if (time1->month != time2->month)
        return time1->month < time2->month;
    if (time1->day != time2->day)
        return time1->day < time2->day;
    if (time1->hour != time2->hour)
        return time1->hour < time2->hour;
    return time1->minute < time2->minute;
}

esp_err_t clock_alarm_schedule_next(const clock_time_t *current_time) {
    if (!current_time || s_alarm_count == 0) {
        ESP_LOGW(TAG, "No alarm rules loaded or invalid current time");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Calculating next alarm from current time: %04d-%02d-%02d %02d:%02d:%02d", current_time->year,
             current_time->month, current_time->day, current_time->hour, current_time->minute, current_time->second);

    clock_time_t next_alarm = {0};
    bool found_alarm = false;

    // Check all rules and find the nearest alarm
    for (int i = 0; i < s_alarm_count; i++) {
        clock_time_t candidate = {0};
        if (calculate_next_alarm_time(&s_alarm_rules[i], current_time, &candidate)) {
            ESP_LOGD(TAG, "Rule %d candidate: %04d-%02d-%02d %02d:%02d", i, candidate.year, candidate.month,
                     candidate.day, candidate.hour, candidate.minute);

            if (!found_alarm || time_is_before(&candidate, &next_alarm)) {
                next_alarm = candidate;
                found_alarm = true;
            }
        }
    }

    if (!found_alarm) {
        ESP_LOGE(TAG, "Failed to calculate next alarm time");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Next alarm scheduled for: %04d-%02d-%02d %02d:%02d", next_alarm.year, next_alarm.month,
             next_alarm.day, next_alarm.hour, next_alarm.minute);

    // Set the alarm in the RTC
    esp_err_t ret = clock_set_alarm(&next_alarm);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set alarm: %s", esp_err_to_name(ret));
        return ret;
    }

    // Enable the alarm
    ret = clock_enable_alarm(true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable alarm: %s", esp_err_to_name(ret));
        return ret;
    }

    // Trigger alarm display update via callback
    clock_trigger_alarm_update();

    return ESP_OK;
}

int clock_alarm_get_rule_count(void) { return s_alarm_count; }
