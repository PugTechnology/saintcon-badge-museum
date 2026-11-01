#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_app_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_OTA_BASE            0x1500
#define ESP_ERR_OTA_VALIDATE_FAILED (ESP_ERR_OTA_BASE + 0x03)

typedef struct {
    uint32_t address;
    uint32_t size;
    char label[17];
} esp_partition_t;

typedef enum {
    ESP_OTA_IMG_NEW = 0,
    ESP_OTA_IMG_PENDING_VERIFY,
    ESP_OTA_IMG_VALID,
    ESP_OTA_IMG_INVALID,
    ESP_OTA_IMG_ABORTED,
    ESP_OTA_IMG_UNDEFINED,
} esp_ota_img_states_t;

const esp_partition_t *esp_ota_get_running_partition(void);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *app_desc);
esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *ota_state);
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void);
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void);

#ifdef __cplusplus
}
#endif
