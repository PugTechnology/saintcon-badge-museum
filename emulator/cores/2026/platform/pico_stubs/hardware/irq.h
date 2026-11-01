#pragma once

#include "pico/types.h"

#define UART0_IRQ 20
#define UART1_IRQ 21

typedef void (*irq_handler_t)(void);

#ifdef __cplusplus
extern "C" {
#endif

void irq_set_exclusive_handler(uint num, irq_handler_t handler);
void irq_set_enabled(uint num, bool enabled);

#ifdef __cplusplus
}
#endif
