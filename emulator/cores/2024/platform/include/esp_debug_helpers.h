#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline esp_err_t esp_backtrace_print(int depth) {
    (void)depth;
    return ESP_OK;
}

#ifdef __cplusplus
}
#endif
