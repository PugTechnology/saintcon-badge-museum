#ifndef SC21_HAL_SLEEP_H
#define SC21_HAL_SLEEP_H

#include <stdint.h>
#include <unistd.h>

static inline void sc21_hal_sleep(uint8_t mode) {
    (void)mode;
}

#define sleep(mode) sc21_hal_sleep((uint8_t)(mode))

#endif /* SC21_HAL_SLEEP_H */
