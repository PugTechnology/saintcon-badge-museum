#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct esp_lcd_i80_bus_t *esp_lcd_i80_bus_handle_t;
typedef struct esp_lcd_panel_io_t *esp_lcd_panel_io_handle_t;
typedef struct esp_lcd_panel_t *esp_lcd_panel_handle_t;

typedef enum {
    LCD_CLK_SRC_DEFAULT = 0,
    LCD_CLK_SRC_PLL160M,
    LCD_CLK_SRC_PLL240M,
} lcd_clock_source_t;

typedef struct {
    int dummy;
} esp_lcd_panel_io_event_data_t;

typedef bool (*esp_lcd_panel_io_color_trans_done_cb_t)(esp_lcd_panel_io_handle_t panel_io,
                                                       esp_lcd_panel_io_event_data_t *edata,
                                                       void *user_ctx);

typedef struct {
    int dc_gpio_num;
    int wr_gpio_num;
    lcd_clock_source_t clk_src;
    int data_gpio_nums[16];
    size_t bus_width;
    size_t max_transfer_bytes;
    size_t psram_trans_align;
    size_t sram_trans_align;
    size_t dma_burst_size;
} esp_lcd_i80_bus_config_t;

typedef struct {
    int cs_gpio_num;
    uint32_t pclk_hz;
    size_t trans_queue_depth;
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
    int lcd_cmd_bits;
    int lcd_param_bits;
    struct {
        unsigned int dc_idle_level  : 1;
        unsigned int dc_cmd_level   : 1;
        unsigned int dc_dummy_level : 1;
        unsigned int dc_data_level  : 1;
    } dc_levels;
    struct {
        unsigned int cs_active_high   : 1;
        unsigned int reverse_color_bits : 1;
        unsigned int swap_color_bytes : 1;
        unsigned int pclk_active_neg  : 1;
        unsigned int pclk_idle_low    : 1;
    } flags;
} esp_lcd_panel_io_i80_config_t;

esp_err_t esp_lcd_new_i80_bus(const esp_lcd_i80_bus_config_t *bus_config, esp_lcd_i80_bus_handle_t *ret_bus);
esp_err_t esp_lcd_del_i80_bus(esp_lcd_i80_bus_handle_t bus);
esp_err_t esp_lcd_new_panel_io_i80(esp_lcd_i80_bus_handle_t bus,
                                   const esp_lcd_panel_io_i80_config_t *io_config,
                                   esp_lcd_panel_io_handle_t *ret_io);
esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);

#ifdef __cplusplus
}
#endif
