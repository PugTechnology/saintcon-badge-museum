#pragma once
#include <stdint.h>

typedef struct _mp_thread_mutex_t {
    int locked;
    int owner_fiber;
} mp_thread_mutex_t;

void mp_thread_gc_others(void);
