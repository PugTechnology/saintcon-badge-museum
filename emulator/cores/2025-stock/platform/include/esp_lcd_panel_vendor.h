#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LCD_RGB_ELEMENT_ORDER_RGB = 0,
    LCD_RGB_ELEMENT_ORDER_BGR = 1,
} lcd_rgb_element_order_t;

typedef struct {
    int reset_gpio_num;
    lcd_rgb_element_order_t rgb_ele_order;
    uint32_t bits_per_pixel;
    struct {
        unsigned int reset_active_high : 1;
    } flags;
    void *vendor_config;
} esp_lcd_panel_dev_config_t;

esp_err_t esp_lcd_new_panel_st7789(const esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel);

#ifdef __cplusplus
}
#endif
