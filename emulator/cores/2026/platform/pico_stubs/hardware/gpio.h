#pragma once

#include "pico/types.h"

#define GPIO_IN  0
#define GPIO_OUT 1

typedef enum {
    GPIO_FUNC_XIP = 0,
    GPIO_FUNC_SPI = 1,
    GPIO_FUNC_UART = 2,
    GPIO_FUNC_I2C = 3,
    GPIO_FUNC_PWM = 4,
    GPIO_FUNC_SIO = 5,
    GPIO_FUNC_PIO0 = 6,
    GPIO_FUNC_PIO1 = 7,
    GPIO_FUNC_NULL = 0x1f
} gpio_function_t;

#ifdef __cplusplus
extern "C" {
#endif

void gpio_init(uint gpio);
void gpio_set_dir(uint gpio, bool out);
void gpio_pull_up(uint gpio);
void gpio_pull_down(uint gpio);
void gpio_put(uint gpio, bool value);
bool gpio_get(uint gpio);
void gpio_set_function(uint gpio, gpio_function_t fn);

#ifdef __cplusplus
}
#endif
