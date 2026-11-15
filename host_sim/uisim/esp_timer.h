/* Host stand-in for esp_timer.h (UI simulator only): the knob board
 * blocks in the shared sources (moon_event.c's scan-yield budget) time
 * on esp_timer_get_time(); the sim answers with the process monotonic
 * clock. */
#ifndef UISIM_ESP_TIMER_H
#define UISIM_ESP_TIMER_H

#include <stdint.h>
#include <time.h>

static inline int64_t esp_timer_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

#endif /* UISIM_ESP_TIMER_H */
