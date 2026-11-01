#pragma once

#include "pico/types.h"

typedef struct pio_hw {
    uint32_t txf[4];
} pio_hw_t;

typedef pio_hw_t *PIO;

typedef struct pio_program {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
} pio_program_t;

extern pio_hw_t emu_pio0_hw;
extern pio_hw_t emu_pio1_hw;

#define pio0 (&emu_pio0_hw)
#define pio1 (&emu_pio1_hw)

#ifdef __cplusplus
extern "C" {
#endif

uint pio_add_program(PIO pio, const pio_program_t *program);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data);

#ifdef __cplusplus
}
#endif
