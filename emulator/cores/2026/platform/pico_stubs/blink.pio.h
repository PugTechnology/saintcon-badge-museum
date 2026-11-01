#pragma once

#include "hardware/pio.h"

static const pio_program_t blink_program = { NULL, 0, -1 };

static inline void blink_program_init(PIO pio, uint sm, uint offset, uint pin) {
    (void)pio;
    (void)sm;
    (void)offset;
    (void)pin;
}
