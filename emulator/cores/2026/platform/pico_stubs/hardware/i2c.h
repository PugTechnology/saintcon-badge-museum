#pragma once

#include "pico/types.h"

typedef struct i2c_inst {
    int id;
} i2c_inst_t;

extern i2c_inst_t emu_i2c0_inst;
extern i2c_inst_t emu_i2c1_inst;

#define i2c0 (&emu_i2c0_inst)
#define i2c1 (&emu_i2c1_inst)

#ifdef __cplusplus
extern "C" {
#endif

uint i2c_init(i2c_inst_t *i2c, uint baudrate);
int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop);
int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop);

#ifdef __cplusplus
}
#endif
