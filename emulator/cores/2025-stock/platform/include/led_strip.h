#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct led_strip_t *led_strip_handle_t;

typedef enum {
    LED_MODEL_WS2812 = 0,
    LED_MODEL_SK6812,
    LED_MODEL_WS2811,
    LED_MODEL_INVALID,
} led_model_t;

typedef enum {
    RMT_CLK_SRC_DEFAULT = 0,
} rmt_clock_source_t;

typedef uint32_t led_color_component_format_t;
#define LED_STRIP_COLOR_COMPONENT_FMT_GRB 0U
#define LED_STRIP_COLOR_COMPONENT_FMT_RGB 1U
#define LED_STRIP_COLOR_COMPONENT_FMT_BGR 2U

typedef struct {
    int strip_gpio_num;
    uint32_t max_leds;
    led_model_t led_model;
    led_color_component_format_t color_component_format;
    struct {
        uint32_t invert_out : 1;
    } flags;
} led_strip_config_t;

typedef struct {
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t mem_block_symbols;
    struct {
        uint32_t with_dma : 1;
    } flags;
} led_strip_rmt_config_t;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *led_config,
                                   const led_strip_rmt_config_t *rmt_config,
                                   led_strip_handle_t *ret_strip);
esp_err_t led_strip_set_pixel(led_strip_handle_t strip,
                              uint32_t index,
                              uint32_t red,
                              uint32_t green,
                              uint32_t blue);
esp_err_t led_strip_refresh(led_strip_handle_t strip);
esp_err_t led_strip_clear(led_strip_handle_t strip);
esp_err_t led_strip_del(led_strip_handle_t strip);

#ifdef __cplusplus
}
#endif
