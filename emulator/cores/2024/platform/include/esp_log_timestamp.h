#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t esp_log_timestamp(void);
char *esp_log_system_timestamp(void);

#ifdef __cplusplus
}
#endif
