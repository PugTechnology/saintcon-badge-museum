#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info);
esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type);
esp_err_t esp_wifi_set_ps(wifi_ps_type_t type);

#ifdef __cplusplus
}
#endif
