#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log_timestamp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ESP_LOG_NONE = 0,
    ESP_LOG_ERROR = 1,
    ESP_LOG_WARN = 2,
    ESP_LOG_INFO = 3,
    ESP_LOG_DEBUG = 4,
    ESP_LOG_VERBOSE = 5,
    ESP_LOG_MAX = 6
} esp_log_level_t;

#define LOG_COLOR_E ""
#define LOG_COLOR_W ""
#define LOG_COLOR_I ""
#define LOG_COLOR_D ""
#define LOG_COLOR_V ""
#define LOG_RESET_COLOR ""

void esp_log_level_set(const char *tag, esp_log_level_t level);
esp_log_level_t esp_log_level_get(const char *tag);
void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
void esp_log_writev(esp_log_level_t level, const char *tag, const char *format, va_list args);
void esp_log_buffer_hexdump_internal(const char *tag, const void *buffer, uint16_t buff_len, esp_log_level_t log_level);

#define ESP_LOG_LEVEL(level, tag, format, ...) do {                     \
    if (esp_log_level_get(tag) >= (level)) {                            \
        const char *__lvl_ch = (level == ESP_LOG_ERROR) ? "E" :         \
                               (level == ESP_LOG_WARN)  ? "W" :         \
                               (level == ESP_LOG_INFO)  ? "I" :         \
                               (level == ESP_LOG_DEBUG) ? "D" : "V";    \
        esp_log_write(level, tag, "%s (%lu) %s: " format "\n",          \
                      __lvl_ch, (unsigned long)esp_log_timestamp(),     \
                      (tag) ? (tag) : "", ##__VA_ARGS__);               \
    }                                                                   \
} while (0)

#define ESP_LOG_LEVEL_LOCAL(level, tag, format, ...) ESP_LOG_LEVEL(level, tag, format, ##__VA_ARGS__)

#define ESP_LOGE(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_ERROR,   tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_WARN,    tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_INFO,    tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_DEBUG,   tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) ESP_LOG_LEVEL(ESP_LOG_VERBOSE, tag, format, ##__VA_ARGS__)

#define ESP_EARLY_LOGE(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGW(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGI(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGD(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define ESP_EARLY_LOGV(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

#define ESP_LOG_BUFFER_HEXDUMP(tag, buffer, buff_len, level) \
    esp_log_buffer_hexdump_internal(tag, buffer, (uint16_t)(buff_len), level)
#define ESP_LOG_BUFFER_HEX_LEVEL(tag, buffer, buff_len, level) \
    esp_log_buffer_hexdump_internal(tag, buffer, (uint16_t)(buff_len), level)
#define ESP_LOG_BUFFER_HEX(tag, buffer, buff_len) \
    esp_log_buffer_hexdump_internal(tag, buffer, (uint16_t)(buff_len), ESP_LOG_INFO)

#ifdef __cplusplus
}
#endif
