#pragma once

#include "pico/types.h"

typedef enum {
    clk_gpout0 = 0,
    clk_ref,
    clk_sys,
    clk_peri,
    clk_usb,
    clk_adc
} clock_index_t;

static inline uint32_t clock_get_hz(clock_index_t clk_index) {
    (void)clk_index;
    return 125000000u;
}
