/* Host stand-in for freertos/FreeRTOS.h (UI simulator only): the knob
 * board blocks in the shared sources include the RTOS headers for the
 * scan-yield; the sim needs only the tick type. */
#ifndef UISIM_FREERTOS_H
#define UISIM_FREERTOS_H

#include <stdint.h>

typedef uint32_t TickType_t;

#endif /* UISIM_FREERTOS_H */
