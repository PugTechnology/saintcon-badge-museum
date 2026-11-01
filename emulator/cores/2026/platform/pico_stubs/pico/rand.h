#pragma once

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint32_t get_rand_32(void);
uint64_t get_rand_64(void);

#ifdef __cplusplus
}
#endif
