#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef unsigned int uint;
typedef uint64_t absolute_time_t;

#ifndef __unused
#define __unused __attribute__((unused))
#endif

#ifndef PICO_ERROR_TIMEOUT
#define PICO_ERROR_TIMEOUT (-1)
#endif
