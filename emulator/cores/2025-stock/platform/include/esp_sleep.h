#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_SLEEP_WAKEUP_UNDEFINED = 0,
    ESP_SLEEP_WAKEUP_ALL,
    ESP_SLEEP_WAKEUP_EXT0,
    ESP_SLEEP_WAKEUP_EXT1,
    ESP_SLEEP_WAKEUP_TIMER,
    ESP_SLEEP_WAKEUP_TOUCHPAD,
    ESP_SLEEP_WAKEUP_ULP,
    ESP_SLEEP_WAKEUP_GPIO,
    ESP_SLEEP_WAKEUP_UART,
} esp_sleep_wakeup_cause_t;

typedef enum {
    ESP_EXT1_WAKEUP_ALL_LOW = 0,
    ESP_EXT1_WAKEUP_ANY_HIGH = 1,
    ESP_EXT1_WAKEUP_ANY_LOW = 2,
} esp_sleep_ext1_wakeup_mode_t;

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us);
esp_err_t esp_sleep_enable_gpio_wakeup(void);
esp_err_t esp_sleep_enable_ext0_wakeup(int gpio_num, int level);
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode);
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t io_mask, esp_sleep_ext1_wakeup_mode_t level_mode);
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void);
void esp_deep_sleep_start(void);
esp_err_t esp_light_sleep_start(void);

#ifdef __cplusplus
}
#endif
