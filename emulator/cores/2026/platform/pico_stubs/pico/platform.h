#pragma once

#include "pico/types.h"

#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif
