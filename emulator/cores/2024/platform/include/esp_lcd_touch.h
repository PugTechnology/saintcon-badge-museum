#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_lcd_touch_s *esp_lcd_touch_handle_t;
typedef void (*esp_lcd_touch_interrupt_callback_t)(esp_lcd_touch_handle_t tp);

typedef struct {
    uint16_t x_max;
    uint16_t y_max;
    gpio_num_t rst_gpio_num;
    gpio_num_t int_gpio_num;
    struct {
        unsigned int reset : 1;
        unsigned int interrupt : 1;
    } levels;
    struct {
        unsigned int swap_xy : 1;
        unsigned int mirror_x : 1;
        unsigned int mirror_y : 1;
    } flags;
    void (*process_coordinates)(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y,
                                uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
    esp_lcd_touch_interrupt_callback_t interrupt_callback;
    void *user_data;
    void *driver_data;
} esp_lcd_touch_config_t;

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp);
bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp,
                                   uint16_t *x,
                                   uint16_t *y,
                                   uint16_t *strength,
                                   uint8_t *point_num,
                                   uint8_t max_point_num);
esp_err_t esp_lcd_touch_set_swap_xy(esp_lcd_touch_handle_t tp, bool swap);
esp_err_t esp_lcd_touch_get_swap_xy(esp_lcd_touch_handle_t tp, bool *swap);
esp_err_t esp_lcd_touch_set_mirror_x(esp_lcd_touch_handle_t tp, bool mirror);
esp_err_t esp_lcd_touch_get_mirror_x(esp_lcd_touch_handle_t tp, bool *mirror);
esp_err_t esp_lcd_touch_set_mirror_y(esp_lcd_touch_handle_t tp, bool mirror);
esp_err_t esp_lcd_touch_get_mirror_y(esp_lcd_touch_handle_t tp, bool *mirror);
esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t tp);

#ifdef __cplusplus
}
#endif
