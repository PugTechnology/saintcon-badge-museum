#ifndef SC21_HAL_EXT_IRQ_H
#define SC21_HAL_EXT_IRQ_H

#include <stdint.h>

typedef void (*ext_irq_cb_t)(void);

int32_t ext_irq_register(const uint32_t pin, ext_irq_cb_t cb);
int32_t ext_irq_enable(const uint32_t pin);
int32_t ext_irq_disable(const uint32_t pin);

#endif /* SC21_HAL_EXT_IRQ_H */
