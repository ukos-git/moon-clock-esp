#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Clock time structure
 */
typedef struct {
    int year;   // 2025
    int month;  // 1–12
    int day;    // 1–31
    int hour;   // 0–23
    int minute; // 0–59
    int second; // 0–59
} clock_time_t;

#ifdef __cplusplus
}
#endif
