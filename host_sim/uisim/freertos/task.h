/* Host stand-in for freertos/task.h (UI simulator only): vTaskDelay is
 * a no-op -- the sim is single-threaded and the yield only exists so
 * the device IDLE task survives the classification scan. */
#ifndef UISIM_FREERTOS_TASK_H
#define UISIM_FREERTOS_TASK_H

#include "freertos/FreeRTOS.h"

static inline void vTaskDelay(TickType_t ticks) { (void)ticks; }

#endif /* UISIM_FREERTOS_TASK_H */
