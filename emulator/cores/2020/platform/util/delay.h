#ifndef SAINTCON_2020_UTIL_DELAY_H
#define SAINTCON_2020_UTIL_DELAY_H

#include "../emu_2020.h"

static inline void _delay_ms(double ms) {
  if (ms > 0.0) {
    emu_2020_delay_ms((uint32_t)ms);
  }
}

#endif /* SAINTCON_2020_UTIL_DELAY_H */
