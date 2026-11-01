#pragma once

#include "py/obj.h"
#include "shared/runtime/interrupt_char.h"

#define mp_hal_stdin_rx_chr() (0)
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);

void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
uint64_t mp_hal_time_ms(void);
uint64_t mp_hal_time_ns(void);

int mp_hal_get_interrupt_char(void);

#include <errno.h>

#define MP_HAL_RETRY_SYSCALL(ret, syscall, raise) \
    { \
        for (;;) { \
            ret = syscall; \
            if (ret == -1) { \
                int err = errno; \
                if (err == EINTR) { \
                    mp_handle_pending(true); \
                    continue; \
                } \
                raise; \
            } \
            break; \
        } \
    }
