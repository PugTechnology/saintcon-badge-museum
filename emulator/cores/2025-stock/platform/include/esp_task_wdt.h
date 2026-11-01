#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t timeout_ms;
    uint32_t idle_core_mask;
    bool trigger_panic;
} esp_task_wdt_config_t;

static inline esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t *config) { (void)config; return ESP_OK; }
static inline esp_err_t esp_task_wdt_reconfigure(const esp_task_wdt_config_t *config) { (void)config; return ESP_OK; }
static inline esp_err_t esp_task_wdt_deinit(void) { return ESP_OK; }
static inline esp_err_t esp_task_wdt_add(TaskHandle_t task_handle) { (void)task_handle; return ESP_OK; }
static inline esp_err_t esp_task_wdt_delete(TaskHandle_t task_handle) { (void)task_handle; return ESP_OK; }
static inline esp_err_t esp_task_wdt_reset(void) { return ESP_OK; }

#ifdef __cplusplus
}
#endif
