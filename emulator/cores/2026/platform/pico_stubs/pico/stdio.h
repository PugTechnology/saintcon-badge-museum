#pragma once

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool stdio_init_all(void);
int stdio_puts(const char *s);
int stdio_putchar(int c);
int stdio_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int getchar_timeout_us(uint32_t timeout_us);
void stdio_set_chars_available_callback(void (*fn)(void *), void *param);

#ifdef __cplusplus
}
#endif
