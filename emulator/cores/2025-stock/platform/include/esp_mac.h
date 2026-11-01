#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_MAC_WIFI_STA = 0,
    ESP_MAC_WIFI_SOFTAP,
    ESP_MAC_BT,
    ESP_MAC_ETH,
    ESP_MAC_IEEE802154,
    ESP_MAC_BASE,
    ESP_MAC_EFUSE_FACTORY,
    ESP_MAC_EFUSE_CUSTOM,
} esp_mac_type_t;

#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type);
esp_err_t esp_efuse_mac_get_default(uint8_t *mac);

#ifdef __cplusplus
}
#endif
