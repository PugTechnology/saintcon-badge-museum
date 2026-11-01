#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "platform_internal.h"
#include "../../../core/emu.h"

#include "sdkconfig.h"
#include "version.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_sleep.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_spiffs.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/rmt_types.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_gt911.h"

/* Generated at build time from upstream/Firmware/spiffs/ LVL{1,2,3}.bin */
#include "spiffs_assets.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ============================================================================
 * Core ABI Buffers & State (320x240 RGB565 LE landscape, 0 LEDs, 0 EEPROM)
 * ============================================================================ */

#define STOCK24_FB_W      320
#define STOCK24_FB_H      240
#define STOCK24_LED_COUNT 0
#define STOCK24_EEPROM_SZ 0

_Alignas(4) static uint16_t s_fb[STOCK24_FB_W * STOCK24_FB_H];
_Alignas(4) static uint8_t  s_leds_dummy[4];
_Alignas(4) static uint8_t  s_eeprom_dummy[4];

static uint8_t  s_backlight_u8 = 255;
static uint32_t s_speed_mult   = 1;
static uint64_t s_rng_state    = 0x202453439E3779B9ULL;
static bool     s_log_enabled  = false;
static esp_log_level_t s_default_log_level = ESP_LOG_INFO;

/* ============================================================================
 * Logging & Error Strings
 * ============================================================================ */

const char *esp_err_to_name(esp_err_t code) {
    switch (code) {
        case ESP_OK:                    return "ESP_OK";
        case ESP_FAIL:                  return "ESP_FAIL";
        case ESP_ERR_NO_MEM:            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:       return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:     return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_INVALID_SIZE:      return "ESP_ERR_INVALID_SIZE";
        case ESP_ERR_NOT_FOUND:         return "ESP_ERR_NOT_FOUND";
        case ESP_ERR_NOT_SUPPORTED:     return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:           return "ESP_ERR_TIMEOUT";
        default:                        return "ESP_ERR";
    }
}

const char *esp_err_to_name_r(esp_err_t code, char *buf, size_t buflen) {
    const char *name = esp_err_to_name(code);
    if (buf && buflen > 0) {
        snprintf(buf, buflen, "%s", name);
    }
    return buf;
}

uint32_t esp_log_timestamp(void) {
    return (uint32_t)(platform_now_us() / 1000ULL);
}

char *esp_log_system_timestamp(void) {
    static char ts[16];
    snprintf(ts, sizeof(ts), "%lu", (unsigned long)esp_log_timestamp());
    return ts;
}

void esp_log_level_set(const char *tag, esp_log_level_t level) {
    if (!tag || strcmp(tag, "*") == 0) {
        s_default_log_level = level;
    }
}

esp_log_level_t esp_log_level_get(const char *tag) {
    (void)tag;
    return s_default_log_level;
}

void esp_log_writev(esp_log_level_t level, const char *tag, const char *format, va_list args) {
    (void)tag;
    if (!s_log_enabled && level > ESP_LOG_ERROR) {
        return;
    }
#ifdef __EMSCRIPTEN__
    vprintf(format, args);
#else
    vfprintf(stderr, format, args);
#endif
}

void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...) {
    if (!s_log_enabled && level > ESP_LOG_ERROR) {
        return;
    }
    va_list args;
    va_start(args, format);
    esp_log_writev(level, tag, format, args);
    va_end(args);
}

void esp_log_buffer_hexdump_internal(const char *tag, const void *buffer, uint16_t buff_len, esp_log_level_t log_level) {
    (void)tag;
    (void)buffer;
    (void)buff_len;
    (void)log_level;
}

/* ============================================================================
 * PRNG, MAC Address (safe during pre-main C++ static init!), System, Sleep, OTA
 * ============================================================================ */

uint32_t platform_rand_u32(void) {
    s_rng_state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = s_rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return (uint32_t)((z ^ (z >> 31)) & 0xFFFFFFFFULL);
}

uint32_t esp_random(void) {
    return platform_rand_u32();
}

void esp_fill_random(void *buf, size_t len) {
    uint8_t *dst = (uint8_t *)buf;
    if (!dst) return;
    for (size_t i = 0; i < len; i++) {
        dst[i] = (uint8_t)(platform_rand_u32() & 0xFFU);
    }
}

esp_err_t esp_read_mac(uint8_t *mac, esp_mac_type_t type) {
    if (!mac) return ESP_ERR_INVALID_ARG;
    /* Deterministic museum badge MAC (locally administered 02:5C:24:00:00:01) */
    static const uint8_t k_base_mac[6] = {0x02, 0x5C, 0x24, 0x00, 0x00, 0x01};
    memcpy(mac, k_base_mac, 6);
    mac[5] = (uint8_t)(k_base_mac[5] + ((int)type & 0x03));
    return ESP_OK;
}

esp_err_t esp_efuse_mac_get_default(uint8_t *mac) {
    return esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

void esp_restart(void) {
    /* No-op in museum core */
}

esp_reset_reason_t esp_reset_reason(void) {
    return ESP_RST_POWERON;
}

uint32_t esp_get_free_heap_size(void) {
    return 8 * 1024 * 1024;
}

uint32_t esp_get_minimum_free_heap_size(void) {
    return 4 * 1024 * 1024;
}

esp_err_t esp_sleep_enable_timer_wakeup(uint64_t time_in_us) { (void)time_in_us; return ESP_OK; }
esp_err_t esp_sleep_enable_ext0_wakeup(int gpio_num, int level) { (void)gpio_num; (void)level; return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup(uint64_t mask, esp_sleep_ext1_wakeup_mode_t mode) { (void)mask; (void)mode; return ESP_OK; }
esp_err_t esp_sleep_enable_ext1_wakeup_io(uint64_t io_mask, esp_sleep_ext1_wakeup_mode_t level_mode) { (void)io_mask; (void)level_mode; return ESP_OK; }
esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause(void) { return ESP_SLEEP_WAKEUP_UNDEFINED; }
void esp_deep_sleep_start(void) {}
esp_err_t esp_light_sleep_start(void) { return ESP_OK; }

static const esp_app_desc_t k_app_desc = {
    .magic_word = 0xABCD5432U,
    .secure_version = 1,
    .version = FIRMWARE_VERSION_STRING,
    .project_name = "saintcon-2024-badge",
    .time = "00:00:00",
    .date = "Oct 22 2024",
    .idf_ver = "v5.3.1-museum",
};

static const esp_partition_t k_running_part = {
    .address = 0x20000,
    .size = 0x200000,
    .label = "ota_0",
};

const esp_app_desc_t *esp_app_get_description(void) {
    return &k_app_desc;
}

const char *esp_app_get_elf_sha256_str(void) {
    return "5e9412c837334379d7f6cadf68327fc0771f636d000000000000000000000000";
}

int esp_app_get_elf_sha256(char *dst, size_t size) {
    if (!dst || size == 0) return 0;
    return snprintf(dst, size, "%s", esp_app_get_elf_sha256_str());
}

const esp_partition_t *esp_ota_get_running_partition(void) {
    return &k_running_part;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition, esp_app_desc_t *app_desc) {
    (void)partition;
    if (!app_desc) return ESP_ERR_INVALID_ARG;
    *app_desc = k_app_desc;
    return ESP_OK;
}

esp_err_t esp_ota_get_state_partition(const esp_partition_t *partition, esp_ota_img_states_t *ota_state) {
    (void)partition;
    if (ota_state) *ota_state = ESP_OTA_IMG_VALID;
    return ESP_OK;
}

esp_err_t esp_ota_mark_app_valid_cancel_rollback(void) { return ESP_OK; }
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void) { return ESP_OK; }

/* ============================================================================
 * Task-Backed esp_event Loop Implementation
 *
 * UI pages (tower_battle, shop, levelup, secret) create task-backed event loops
 * and post events from LVGL callbacks while holding lv_general_mutex; handlers
 * acquire lvgl_lock(), so dispatching asynchronously on each loop's FreeRTOS
 * task avoids mutex deadlocks and matches real ESP-IDF semantics.
 * ============================================================================ */

#define MAX_EVENT_LOOPS    12
#define MAX_EVENT_HANDLERS 64
#define INLINE_EVT_BYTES   64

typedef struct {
    esp_event_base_t base;
    int32_t id;
    size_t data_size;
    uint8_t inline_data[INLINE_EVT_BYTES];
    void *heap_data;
} sim_posted_event_t;

typedef struct {
    bool used;
    QueueHandle_t queue;
    TaskHandle_t task;
} sim_event_loop_t;

typedef struct {
    bool used;
    esp_event_loop_handle_t loop;
    esp_event_base_t base;
    int32_t id;
    esp_event_handler_t handler;
    void *arg;
} sim_event_sub_t;

static sim_event_loop_t s_event_loops[MAX_EVENT_LOOPS];
static sim_event_sub_t s_event_subs[MAX_EVENT_HANDLERS];

static esp_event_loop_handle_t default_loop_handle(void) {
    return (esp_event_loop_handle_t)&s_event_loops[0];
}

static void dispatch_event_now(esp_event_loop_handle_t loop_handle,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data) {
    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (!s_event_subs[i].used) continue;
        if (s_event_subs[i].loop != loop_handle) continue;
        if (s_event_subs[i].base != ESP_EVENT_ANY_BASE &&
            s_event_subs[i].base != event_base &&
            (!s_event_subs[i].base || !event_base || strcmp(s_event_subs[i].base, event_base) != 0)) {
            continue;
        }
        if (s_event_subs[i].id != ESP_EVENT_ANY_ID && s_event_subs[i].id != event_id) {
            continue;
        }
        /* Copy fields locally in case the handler unregisters itself (e.g. wifi_scan_event_handler) */
        esp_event_handler_t fn = s_event_subs[i].handler;
        void *fn_arg = s_event_subs[i].arg;
        if (fn) {
            fn(fn_arg, event_base, event_id, event_data);
        }
    }
}

static void event_loop_task_entry(void *arg) {
    sim_event_loop_t *loop = (sim_event_loop_t *)arg;
    esp_event_loop_handle_t handle = (esp_event_loop_handle_t)loop;
    sim_posted_event_t ev;
    while (1) {
        if (xQueueReceive(loop->queue, &ev, portMAX_DELAY) == pdTRUE) {
            void *data_ptr = NULL;
            if (ev.data_size > 0) {
                data_ptr = ev.heap_data ? ev.heap_data : (void *)ev.inline_data;
            }
            dispatch_event_now(handle, ev.base, ev.id, data_ptr);
            if (ev.heap_data) {
                free(ev.heap_data);
            }
        }
    }
}

esp_err_t esp_event_loop_create_default(void) {
    sim_event_loop_t *def = &s_event_loops[0];
    if (def->used) {
        return ESP_OK;
    }
    def->queue = xQueueCreate(24, sizeof(sim_posted_event_t));
    if (!def->queue) return ESP_ERR_NO_MEM;
    def->used = true;
    if (xTaskCreate(event_loop_task_entry, "sys_evt", 8192, def, 6, &def->task) != pdPASS) {
        vQueueDelete(def->queue);
        memset(def, 0, sizeof(*def));
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t esp_event_loop_create(const esp_event_loop_args_t *args, esp_event_loop_handle_t *out_loop) {
    if (!args || !out_loop) return ESP_ERR_INVALID_ARG;
    for (int i = 1; i < MAX_EVENT_LOOPS; i++) {
        if (!s_event_loops[i].used) {
            sim_event_loop_t *loop = &s_event_loops[i];
            memset(loop, 0, sizeof(*loop));
            int32_t qsz = args->queue_size > 0 ? args->queue_size : 10;
            loop->queue = xQueueCreate((UBaseType_t)qsz, sizeof(sim_posted_event_t));
            if (!loop->queue) return ESP_ERR_NO_MEM;
            loop->used = true;
            if (args->task_name) {
                UBaseType_t prio = args->task_priority > 0 ? args->task_priority : 5;
                if (xTaskCreate(event_loop_task_entry, args->task_name, 8192, loop, prio, &loop->task) != pdPASS) {
                    vQueueDelete(loop->queue);
                    memset(loop, 0, sizeof(*loop));
                    return ESP_ERR_NO_MEM;
                }
            }
            *out_loop = (esp_event_loop_handle_t)loop;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_event_loop_delete(esp_event_loop_handle_t event_loop) {
    if (!event_loop || event_loop == default_loop_handle()) return ESP_ERR_INVALID_ARG;
    sim_event_loop_t *loop = (sim_event_loop_t *)event_loop;
    int idx = (int)(loop - s_event_loops);
    if (idx <= 0 || idx >= MAX_EVENT_LOOPS || !loop->used) return ESP_ERR_INVALID_ARG;

    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (s_event_subs[i].used && s_event_subs[i].loop == event_loop) {
            memset(&s_event_subs[i], 0, sizeof(s_event_subs[i]));
        }
    }
    if (loop->task) {
        vTaskDelete(loop->task);
        loop->task = NULL;
    }
    if (loop->queue) {
        sim_posted_event_t ev;
        while (xQueueReceive(loop->queue, &ev, 0) == pdTRUE) {
            if (ev.heap_data) free(ev.heap_data);
        }
        vQueueDelete(loop->queue);
        loop->queue = NULL;
    }
    memset(loop, 0, sizeof(*loop));
    return ESP_OK;
}

static esp_err_t register_handler_internal(esp_event_loop_handle_t loop_handle,
                                           esp_event_base_t event_base,
                                           int32_t event_id,
                                           esp_event_handler_t event_handler,
                                           void *event_handler_arg,
                                           esp_event_handler_instance_t *out_instance) {
    if (!event_handler) return ESP_ERR_INVALID_ARG;
    if (!loop_handle) loop_handle = default_loop_handle();
    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (!s_event_subs[i].used) {
            s_event_subs[i].used = true;
            s_event_subs[i].loop = loop_handle;
            s_event_subs[i].base = event_base;
            s_event_subs[i].id = event_id;
            s_event_subs[i].handler = event_handler;
            s_event_subs[i].arg = event_handler_arg;
            if (out_instance) {
                *out_instance = (esp_event_handler_instance_t)&s_event_subs[i];
            }
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t event_loop,
                                          esp_event_base_t event_base,
                                          int32_t event_id,
                                          esp_event_handler_t event_handler,
                                          void *event_handler_arg) {
    return register_handler_internal(event_loop, event_base, event_id, event_handler, event_handler_arg, NULL);
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t event_handler,
                                     void *event_handler_arg) {
    return register_handler_internal(default_loop_handle(), event_base, event_id, event_handler, event_handler_arg, NULL);
}

esp_err_t esp_event_handler_instance_register(esp_event_base_t event_base,
                                              int32_t event_id,
                                              esp_event_handler_t event_handler,
                                              void *event_handler_arg,
                                              esp_event_handler_instance_t *instance) {
    return register_handler_internal(default_loop_handle(), event_base, event_id, event_handler, event_handler_arg, instance);
}

esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t event_loop,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            esp_event_handler_t event_handler) {
    if (!event_loop) event_loop = default_loop_handle();
    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (s_event_subs[i].used &&
            s_event_subs[i].loop == event_loop &&
            s_event_subs[i].base == event_base &&
            s_event_subs[i].id == event_id &&
            s_event_subs[i].handler == event_handler) {
            memset(&s_event_subs[i], 0, sizeof(s_event_subs[i]));
        }
    }
    return ESP_OK;
}

esp_err_t esp_event_handler_unregister(esp_event_base_t event_base,
                                       int32_t event_id,
                                       esp_event_handler_t event_handler) {
    return esp_event_handler_unregister_with(default_loop_handle(), event_base, event_id, event_handler);
}

esp_err_t esp_event_handler_instance_unregister(esp_event_base_t event_base,
                                                int32_t event_id,
                                                esp_event_handler_instance_t instance) {
    (void)event_base;
    (void)event_id;
    sim_event_sub_t *sub = (sim_event_sub_t *)instance;
    int idx = (int)(sub - s_event_subs);
    if (idx >= 0 && idx < MAX_EVENT_HANDLERS && s_event_subs[idx].used) {
        memset(&s_event_subs[idx], 0, sizeof(s_event_subs[idx]));
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_event_post_to(esp_event_loop_handle_t event_loop,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            const void *event_data,
                            size_t event_data_size,
                            TickType_t ticks_to_wait) {
    if (!event_loop) event_loop = default_loop_handle();
    sim_event_loop_t *loop = (sim_event_loop_t *)event_loop;
    int idx = (int)(loop - s_event_loops);
    if (idx < 0 || idx >= MAX_EVENT_LOOPS || !loop->used) return ESP_ERR_INVALID_STATE;

    if (!loop->task) {
        dispatch_event_now(event_loop, event_base, event_id, (void *)event_data);
        return ESP_OK;
    }

    sim_posted_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.base = event_base;
    ev.id = event_id;
    if (event_data && event_data_size > 0) {
        ev.data_size = event_data_size;
        if (event_data_size <= INLINE_EVT_BYTES) {
            memcpy(ev.inline_data, event_data, event_data_size);
        } else {
            ev.heap_data = malloc(event_data_size);
            if (!ev.heap_data) return ESP_ERR_NO_MEM;
            memcpy(ev.heap_data, event_data, event_data_size);
        }
    }
    if (xQueueSend(loop->queue, &ev, ticks_to_wait) != pdPASS) {
        if (ev.heap_data) free(ev.heap_data);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t esp_event_post(esp_event_base_t event_base,
                         int32_t event_id,
                         const void *event_data,
                         size_t event_data_size,
                         TickType_t ticks_to_wait) {
    return esp_event_post_to(default_loop_handle(), event_base, event_id, event_data, event_data_size, ticks_to_wait);
}

/* ============================================================================
 * ESP Wi-Fi & Netif Simulation (used directly by upstream wifi_manager.c)
 * ============================================================================ */

ESP_EVENT_DEFINE_BASE(WIFI_EVENT);
ESP_EVENT_DEFINE_BASE(IP_EVENT);

struct esp_netif_obj {
    int dummy;
};

static struct esp_netif_obj s_sta_netif;
static wifi_sta_config_t s_wifi_sta_cfg;
static bool s_wifi_connected = false;
static wifi_ps_type_t s_wifi_ps = WIFI_PS_NONE;
static esp_timer_handle_t s_badge_sync_timer = NULL;
static esp_timer_handle_t s_post_splash_sync_timer = NULL;

extern void wifi_status_callback(wifi_status_t wifi_status);

static void badge_sync_timer_cb(void *arg) {
    (void)arg;
    if (s_wifi_connected) {
        wifi_status_callback(WIFI_STATUS_CONNECTED);
    }
}

void platform_schedule_badge_sync(uint64_t delay_us) {
    if (!s_badge_sync_timer) {
        esp_timer_create_args_t args = {
            .callback = badge_sync_timer_cb,
            .arg = NULL,
            .name = "sim_badge_sync",
        };
        esp_timer_create(&args, &s_badge_sync_timer);
    }
    if (s_badge_sync_timer) {
        if (esp_timer_is_active(s_badge_sync_timer)) {
            esp_timer_stop(s_badge_sync_timer);
        }
        esp_timer_start_once(s_badge_sync_timer, delay_us);
    }
}

esp_err_t esp_netif_init(void) { return ESP_OK; }
esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &s_sta_netif; }
esp_err_t esp_wifi_init(const wifi_init_config_t *config) { (void)config; return ESP_OK; }
esp_err_t esp_wifi_set_mode(wifi_mode_t mode) { (void)mode; return ESP_OK; }

esp_err_t esp_wifi_start(void) {
    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_START, NULL, 0, 0);
    return ESP_OK;
}

esp_err_t esp_wifi_stop(void) {
    s_wifi_connected = false;
    return ESP_OK;
}

esp_err_t esp_wifi_disconnect(void) {
    s_wifi_connected = false;
    return ESP_OK;
}

esp_err_t esp_wifi_set_config(wifi_interface_t interface, wifi_config_t *conf) {
    (void)interface;
    if (!conf) return ESP_ERR_INVALID_ARG;
    s_wifi_sta_cfg = conf->sta;
    return ESP_OK;
}

esp_err_t esp_wifi_connect(void) {
    s_wifi_connected = true;
    wifi_event_sta_connected_t ev;
    memset(&ev, 0, sizeof(ev));
    const char *ssid = s_wifi_sta_cfg.ssid[0] ? (const char *)s_wifi_sta_cfg.ssid : "MUSEUM";
    size_t slen = strlen(ssid);
    if (slen > sizeof(ev.ssid)) slen = sizeof(ev.ssid);
    memcpy(ev.ssid, ssid, slen);
    ev.ssid_len = (uint8_t)slen;
    ev.channel = 6;
    ev.authmode = WIFI_AUTH_OPEN;

    esp_event_post(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &ev, sizeof(ev), 0);
    esp_event_post(IP_EVENT, IP_EVENT_STA_GOT_IP, NULL, 0, 0);
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info) {
    if (!ap_info) return ESP_ERR_INVALID_ARG;
    if (!s_wifi_connected) return ESP_ERR_WIFI_NOT_CONNECT;
    memset(ap_info, 0, sizeof(*ap_info));
    const char *ssid = s_wifi_sta_cfg.ssid[0] ? (const char *)s_wifi_sta_cfg.ssid : "MUSEUM";
    snprintf((char *)ap_info->ssid, sizeof(ap_info->ssid), "%s", ssid);
    ap_info->rssi = -42;
    ap_info->primary = 6;
    ap_info->authmode = WIFI_AUTH_OPEN;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block) {
    (void)config;
    if (!block) {
        esp_event_post(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, NULL, 0, 0);
    }
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_num(uint16_t *number) {
    if (!number) return ESP_ERR_INVALID_ARG;
    *number = 2;
    return ESP_OK;
}

esp_err_t esp_wifi_scan_get_ap_records(uint16_t *number, wifi_ap_record_t *ap_records) {
    if (!number || !ap_records) return ESP_ERR_INVALID_ARG;
    uint16_t max_n = *number;
    uint16_t n = 0;
    if (n < max_n) {
        memset(&ap_records[n], 0, sizeof(wifi_ap_record_t));
        snprintf((char *)ap_records[n].ssid, sizeof(ap_records[n].ssid), "%s", "MUSEUM");
        ap_records[n].rssi = -42;
        ap_records[n].primary = 6;
        ap_records[n].authmode = WIFI_AUTH_OPEN;
        n++;
    }
    if (n < max_n) {
        memset(&ap_records[n], 0, sizeof(wifi_ap_record_t));
        snprintf((char *)ap_records[n].ssid, sizeof(ap_records[n].ssid), "%s", "SAINTCON-ARCHIVE");
        ap_records[n].rssi = -64;
        ap_records[n].primary = 11;
        ap_records[n].authmode = WIFI_AUTH_WPA2_PSK;
        n++;
    }
    *number = n;
    return ESP_OK;
}

esp_err_t esp_wifi_clear_ap_list(void) { return ESP_OK; }

esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type) {
    if (type) *type = s_wifi_ps;
    return ESP_OK;
}

esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) {
    s_wifi_ps = type;
    return ESP_OK;
}

/* ============================================================================
 * GPIO
 * ============================================================================ */

typedef struct {
    uint8_t out_level;
    gpio_int_type_t intr_type;
    gpio_isr_t isr;
    void *isr_arg;
} sim_gpio_pin_t;

static sim_gpio_pin_t s_gpio[SOC_GPIO_PIN_COUNT];

esp_err_t gpio_config(const gpio_config_t *cfg) {
    if (!cfg) return ESP_ERR_INVALID_ARG;
    for (int pin = 0; pin < SOC_GPIO_PIN_COUNT; pin++) {
        if (cfg->pin_bit_mask & (1ULL << pin)) {
            s_gpio[pin].intr_type = cfg->intr_type;
        }
    }
    return ESP_OK;
}

esp_err_t gpio_reset_pin(gpio_num_t gpio_num) {
    if ((int)gpio_num >= 0 && (int)gpio_num < SOC_GPIO_PIN_COUNT) {
        s_gpio[gpio_num].out_level = 0;
    }
    return ESP_OK;
}

esp_err_t gpio_set_intr_type(gpio_num_t gpio_num, gpio_int_type_t intr_type) {
    if ((int)gpio_num < 0 || (int)gpio_num >= SOC_GPIO_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    s_gpio[gpio_num].intr_type = intr_type;
    return ESP_OK;
}

esp_err_t gpio_intr_enable(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t gpio_intr_disable(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t gpio_wakeup_enable(gpio_num_t gpio_num, gpio_int_type_t intr_type) { (void)gpio_num; (void)intr_type; return ESP_OK; }
esp_err_t gpio_wakeup_disable(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t esp_sleep_enable_gpio_wakeup(void) { return ESP_OK; }

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
    if ((int)gpio_num < 0 || (int)gpio_num >= SOC_GPIO_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    s_gpio[gpio_num].out_level = level ? 1 : 0;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num) {
    if ((int)gpio_num >= 0 && (int)gpio_num < SOC_GPIO_PIN_COUNT) {
        return s_gpio[gpio_num].out_level;
    }
    return 1;
}

esp_err_t gpio_set_direction(gpio_num_t gpio_num, gpio_mode_t mode) { (void)gpio_num; (void)mode; return ESP_OK; }
esp_err_t gpio_set_pull_mode(gpio_num_t gpio_num, gpio_pull_mode_t pull) { (void)gpio_num; (void)pull; return ESP_OK; }
esp_err_t gpio_pullup_en(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t gpio_pullup_dis(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t gpio_pulldown_en(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t gpio_pulldown_dis(gpio_num_t gpio_num) { (void)gpio_num; return ESP_OK; }
esp_err_t gpio_install_isr_service(int intr_alloc_flags) { (void)intr_alloc_flags; return ESP_OK; }
void gpio_uninstall_isr_service(void) {}

esp_err_t gpio_isr_handler_add(gpio_num_t gpio_num, gpio_isr_t isr_handler, void *args) {
    if ((int)gpio_num < 0 || (int)gpio_num >= SOC_GPIO_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    s_gpio[gpio_num].isr = isr_handler;
    s_gpio[gpio_num].isr_arg = args;
    return ESP_OK;
}

esp_err_t gpio_isr_handler_remove(gpio_num_t gpio_num) {
    if ((int)gpio_num < 0 || (int)gpio_num >= SOC_GPIO_PIN_COUNT) return ESP_ERR_INVALID_ARG;
    s_gpio[gpio_num].isr = NULL;
    s_gpio[gpio_num].isr_arg = NULL;
    return ESP_OK;
}

/* ============================================================================
 * ADC (Battery on ADC_UNIT_1 / ADC_CHANNEL_9 -> 2075 mV -> ~4212 mV = 100%)
 * ============================================================================ */

struct adc_oneshot_unit_ctx_t {
    adc_unit_t unit_id;
};

struct adc_cali_scheme_t {
    adc_unit_t unit_id;
    adc_channel_t chan;
};

static struct adc_oneshot_unit_ctx_t s_adc_units[SOC_ADC_PERIPH_NUM];
static struct adc_cali_scheme_t s_adc_calis[8];
static int s_adc_cali_count = 0;

esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *init_config, adc_oneshot_unit_handle_t *ret_unit) {
    if (!init_config || !ret_unit) return ESP_ERR_INVALID_ARG;
    int idx = ((int)init_config->unit_id >= 0 && (int)init_config->unit_id < SOC_ADC_PERIPH_NUM)
                  ? (int)init_config->unit_id
                  : 0;
    s_adc_units[idx].unit_id = init_config->unit_id;
    *ret_unit = &s_adc_units[idx];
    return ESP_OK;
}

esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle,
                                     adc_channel_t chan,
                                     const adc_oneshot_chan_cfg_t *config) {
    (void)handle;
    (void)chan;
    (void)config;
    return ESP_OK;
}

esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int *out_raw) {
    (void)handle;
    if (!out_raw) return ESP_ERR_INVALID_ARG;
    if (chan == ADC_CHANNEL_9) {
        *out_raw = 2580;
    } else {
        *out_raw = 2048;
    }
    return ESP_OK;
}

esp_err_t adc_oneshot_del_unit(adc_oneshot_unit_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t *config,
                                               adc_cali_handle_t *ret_handle) {
    if (!config || !ret_handle) return ESP_ERR_INVALID_ARG;
    int idx = s_adc_cali_count % 8;
    s_adc_cali_count++;
    s_adc_calis[idx].unit_id = config->unit_id;
    s_adc_calis[idx].chan = config->chan;
    *ret_handle = &s_adc_calis[idx];
    return ESP_OK;
}

esp_err_t adc_cali_delete_scheme_curve_fitting(adc_cali_handle_t handle) {
    (void)handle;
    return ESP_OK;
}

esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t handle, int raw, int *voltage) {
    if (!voltage) return ESP_ERR_INVALID_ARG;
    if (handle && handle->chan == ADC_CHANNEL_9) {
        /* 2075 mV * 2.0 divider * 1.01505 cal factor = 4212 mV (~100% LiPo) */
        *voltage = 2075;
    } else {
        *voltage = (raw * 3200) / 4095;
    }
    return ESP_OK;
}

/* ============================================================================
 * I2C Master:
 *   Port 0 (Touch Bus): GT911 (0x5D / 0x14)
 *   Port 1 (Peripheral Bus): TCA9544A 4-ch switch (0x70):
 *     - ch 0, 1: Klip-on / minibadge slots (empty -> ESP_ERR_NOT_FOUND)
 *     - ch 2:    MC3419 accelerometer (0x4C)
 *     - ch 3:    BQ24296M charger (0x6B) + PI5USB30216C Type-C (0x1D)
 * ============================================================================ */

#define TCA9544A_I2C_ADDR     0x70
#define MC3419_I2C_ADDR       0x4C
#define BQ24296M_I2C_ADDR     0x6B
#define PI5USB30216C_I2C_ADDR 0x1D
#define GT911_I2C_ADDR_A      0x5D
#define GT911_I2C_ADDR_B      0x14

struct i2c_master_bus_t {
    int port;
};

struct i2c_master_dev_t {
    bool used;
    int bus_port;
    uint16_t addr;
};

static struct i2c_master_bus_t s_i2c_buses[SOC_I2C_NUM];
static struct i2c_master_dev_t s_i2c_devs[32];

static uint8_t s_tca_ctrl    = 0x04; /* Enable bit + channel 0, zero active interrupts */
static uint8_t s_mc3419_regs[64];
static uint8_t s_mc3419_cursor = 0;
static int16_t s_accel_x = 0;
static int16_t s_accel_y = 0;
static int16_t s_accel_z = 16384; /* +1g flat on Z axis at +-2g */

static uint8_t s_bq_regs[11];
static uint8_t s_bq_cursor = 0;

static uint8_t s_typec_regs[4];

static void peripheral_regs_reset(void) {
    s_tca_ctrl = 0x04;

    memset(s_mc3419_regs, 0, sizeof(s_mc3419_regs));
    s_mc3419_regs[0x18] = 0xA4; /* MC3419 CHIP_ID */
    s_mc3419_cursor = 0;
    s_accel_x = 0;
    s_accel_y = 0;
    s_accel_z = 16384;

    memset(s_bq_regs, 0, sizeof(s_bq_regs));
    s_bq_regs[0x00] = 0x37; /* Input source control */
    s_bq_regs[0x01] = 0x1B; /* Power-on configuration */
    s_bq_regs[0x02] = 0x60; /* Charge current */
    s_bq_regs[0x03] = 0x11; /* Pre-charge / termination */
    s_bq_regs[0x04] = 0xB2; /* Charge voltage */
    s_bq_regs[0x05] = 0x9C; /* Charge termination / timer */
    s_bq_regs[0x06] = 0x73; /* Boost voltage / thermal */
    s_bq_regs[0x07] = 0x4B; /* Misc operation */
    /* SYSTEM_STATUS: VBUS=USB host (01<<6), CHRG=Charge Done (11<<4), PG=1 (1<<2) -> 0x74 */
    s_bq_regs[0x08] = 0x74;
    s_bq_regs[0x09] = 0x00; /* NEW_FAULT: no faults */
    s_bq_regs[0x0A] = 0x20; /* VENDOR_STATUS: BQ24296M */
    s_bq_cursor = 0;

    s_typec_regs[0] = 0x20; /* Device ID = 0x01 << 5 */
    s_typec_regs[1] = 0x04; /* Dual-role control */
    s_typec_regs[2] = 0x00; /* Interrupt status */
    /* CC status: VBUS detected (1<<7) | medium current (2<<5) | device attached (1<<2) | CC1 (1) */
    s_typec_regs[3] = 0xA5;
}

static void sync_mc3419_Dynamic_regs(void) {
    s_mc3419_regs[0x05] = (uint8_t)(s_mc3419_regs[0x07] & 0x03U);
    s_mc3419_regs[0x0D] = (uint8_t)(s_accel_x & 0xFF);
    s_mc3419_regs[0x0E] = (uint8_t)((s_accel_x >> 8) & 0xFF);
    s_mc3419_regs[0x0F] = (uint8_t)(s_accel_y & 0xFF);
    s_mc3419_regs[0x10] = (uint8_t)((s_accel_y >> 8) & 0xFF);
    s_mc3419_regs[0x11] = (uint8_t)(s_accel_z & 0xFF);
    s_mc3419_regs[0x12] = (uint8_t)((s_accel_z >> 8) & 0xFF);
    s_mc3419_regs[0x18] = 0xA4;
}

void emu_set_accel(int16_t x, int16_t y, int16_t z) {
    s_accel_x = x;
    s_accel_y = y;
    s_accel_z = z;
}

esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *bus_config, i2c_master_bus_handle_t *ret_bus_handle) {
    if (!bus_config || !ret_bus_handle) return ESP_ERR_INVALID_ARG;
    int port = (bus_config->i2c_port >= 0 && bus_config->i2c_port < SOC_I2C_NUM) ? bus_config->i2c_port : 0;
    s_i2c_buses[port].port = port;
    *ret_bus_handle = &s_i2c_buses[port];
    return ESP_OK;
}

esp_err_t i2c_del_master_bus(i2c_master_bus_handle_t bus_handle) { (void)bus_handle; return ESP_OK; }
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus_handle) { (void)bus_handle; return ESP_OK; }

esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus_handle,
                                    const i2c_device_config_t *dev_config,
                                    i2c_master_dev_handle_t *ret_handle) {
    if (!bus_handle || !dev_config || !ret_handle) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < 32; i++) {
        if (!s_i2c_devs[i].used) {
            s_i2c_devs[i].used = true;
            s_i2c_devs[i].bus_port = bus_handle->port;
            s_i2c_devs[i].addr = dev_config->device_address;
            *ret_handle = &s_i2c_devs[i];
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t i2c_master_bus_rm_device(i2c_master_dev_handle_t handle) {
    if (handle) memset(handle, 0, sizeof(*handle));
    return ESP_OK;
}

static bool i2c_device_present_now(int bus_port, uint16_t addr) {
    if (bus_port == I2C_NUM_0) {
        return (addr == GT911_I2C_ADDR_A || addr == GT911_I2C_ADDR_B);
    }
    if (bus_port == I2C_NUM_1) {
        if (addr == TCA9544A_I2C_ADDR) return true;
        if (!(s_tca_ctrl & 0x04U)) return false;
        uint8_t ch = s_tca_ctrl & 0x03U;
        if (ch == 2 && addr == MC3419_I2C_ADDR) return true;
        if (ch == 3 && (addr == BQ24296M_I2C_ADDR || addr == PI5USB30216C_I2C_ADDR)) return true;
        /* Channels 0 and 1 are empty klip-on / minibadge slots */
    }
    return false;
}

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle, uint16_t address, int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (!bus_handle) return ESP_ERR_INVALID_ARG;
    return i2c_device_present_now(bus_handle->port, address) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev,
                              const uint8_t *write_buffer,
                              size_t write_size,
                              int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (!i2c_dev || (!write_buffer && write_size > 0)) return ESP_ERR_INVALID_ARG;
    if (!i2c_device_present_now(i2c_dev->bus_port, i2c_dev->addr)) return ESP_ERR_NOT_FOUND;

    if (i2c_dev->bus_port == I2C_NUM_0) {
        return ESP_OK;
    }

    if (i2c_dev->addr == TCA9544A_I2C_ADDR) {
        if (write_size >= 1) {
            s_tca_ctrl = (uint8_t)(write_buffer[0] & 0x07U);
        }
        return ESP_OK;
    }

    if (i2c_dev->addr == MC3419_I2C_ADDR) {
        if (write_size >= 1) {
            uint8_t reg = write_buffer[0];
            s_mc3419_cursor = reg;
            for (size_t i = 1; i < write_size; i++) {
                uint8_t r = (uint8_t)((reg + (i - 1)) & 0x3FU);
                if (r == 0x24 && (write_buffer[i] & 0x40U)) {
                    /* Soft reset */
                    memset(s_mc3419_regs, 0, sizeof(s_mc3419_regs));
                    s_mc3419_regs[0x18] = 0xA4;
                } else if (r != 0x18) {
                    s_mc3419_regs[r] = write_buffer[i];
                }
            }
            sync_mc3419_Dynamic_regs();
        }
        return ESP_OK;
    }

    if (i2c_dev->addr == BQ24296M_I2C_ADDR) {
        if (write_size >= 1) {
            uint8_t reg = write_buffer[0];
            s_bq_cursor = reg;
            for (size_t i = 1; i < write_size; i++) {
                uint8_t r = (uint8_t)(reg + (i - 1));
                if (r <= 0x07) {
                    s_bq_regs[r] = write_buffer[i];
                }
            }
        }
        return ESP_OK;
    }

    if (i2c_dev->addr == PI5USB30216C_I2C_ADDR) {
        if (write_size >= 2) {
            s_typec_regs[1] = (uint8_t)(write_buffer[1] & ~0x01U);
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_receive(i2c_master_dev_handle_t i2c_dev,
                             uint8_t *read_buffer,
                             size_t read_size,
                             int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (!i2c_dev || (!read_buffer && read_size > 0)) return ESP_ERR_INVALID_ARG;
    if (!i2c_device_present_now(i2c_dev->bus_port, i2c_dev->addr)) return ESP_ERR_NOT_FOUND;

    if (i2c_dev->bus_port == I2C_NUM_0) {
        memset(read_buffer, 0, read_size);
        return ESP_OK;
    }

    if (i2c_dev->addr == TCA9544A_I2C_ADDR) {
        for (size_t i = 0; i < read_size; i++) {
            read_buffer[i] = s_tca_ctrl;
        }
        return ESP_OK;
    }

    if (i2c_dev->addr == MC3419_I2C_ADDR) {
        sync_mc3419_Dynamic_regs();
        for (size_t i = 0; i < read_size; i++) {
            read_buffer[i] = s_mc3419_regs[s_mc3419_cursor & 0x3FU];
            s_mc3419_cursor = (uint8_t)((s_mc3419_cursor + 1) & 0x3FU);
        }
        return ESP_OK;
    }

    if (i2c_dev->addr == BQ24296M_I2C_ADDR) {
        for (size_t i = 0; i < read_size; i++) {
            read_buffer[i] = (s_bq_cursor < sizeof(s_bq_regs)) ? s_bq_regs[s_bq_cursor] : 0x00;
            s_bq_cursor++;
        }
        return ESP_OK;
    }

    if (i2c_dev->addr == PI5USB30216C_I2C_ADDR) {
        for (size_t i = 0; i < read_size; i++) {
            read_buffer[i] = (i < sizeof(s_typec_regs)) ? s_typec_regs[i] : 0x00;
        }
        return ESP_OK;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t i2c_dev,
                                      const uint8_t *write_buffer,
                                      size_t write_size,
                                      uint8_t *read_buffer,
                                      size_t read_size,
                                      int xfer_timeout_ms) {
    esp_err_t err = i2c_master_transmit(i2c_dev, write_buffer, write_size, xfer_timeout_ms);
    if (err != ESP_OK) return err;
    return i2c_master_receive(i2c_dev, read_buffer, read_size, xfer_timeout_ms);
}

/* ============================================================================
 * esp_lcd i80 + ST7789 (320x240 RGB565 LE) & LEDC Backlight
 *
 * display.c configures LANDSCAPE_FLIP (swap_xy=true, mirror_x=true, mirror_y=false)
 * for BADGE_WRIST_LEFT (the default orientation matching board.png, where bottom
 * bezel "SAINTCON" is upright), and LANDSCAPE (swap_xy=true, mirror_x=false,
 * mirror_y=true) for BADGE_WRIST_RIGHT (180 degrees rotated).
 * ============================================================================ */

struct esp_lcd_i80_bus_t {
    int dummy;
};

struct esp_lcd_panel_io_t {
    esp_lcd_panel_io_color_trans_done_cb_t on_color_trans_done;
    void *user_ctx;
};

struct esp_lcd_panel_t {
    esp_lcd_panel_io_handle_t io;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool disp_on;
};

static struct esp_lcd_i80_bus_t s_lcd_bus;
static struct esp_lcd_panel_io_t s_lcd_io;
static struct esp_lcd_panel_t s_lcd_panel;
static struct esp_lcd_panel_io_t s_touch_io;

esp_err_t esp_lcd_new_i80_bus(const esp_lcd_i80_bus_config_t *bus_config, esp_lcd_i80_bus_handle_t *ret_bus) {
    (void)bus_config;
    if (!ret_bus) return ESP_ERR_INVALID_ARG;
    *ret_bus = &s_lcd_bus;
    return ESP_OK;
}

esp_err_t esp_lcd_del_i80_bus(esp_lcd_i80_bus_handle_t bus) { (void)bus; return ESP_OK; }

esp_err_t esp_lcd_new_panel_io_i80(esp_lcd_i80_bus_handle_t bus,
                                   const esp_lcd_panel_io_i80_config_t *io_config,
                                   esp_lcd_panel_io_handle_t *ret_io) {
    (void)bus;
    if (!io_config || !ret_io) return ESP_ERR_INVALID_ARG;
    s_lcd_io.on_color_trans_done = io_config->on_color_trans_done;
    s_lcd_io.user_ctx = io_config->user_ctx;
    *ret_io = &s_lcd_io;
    return ESP_OK;
}

esp_err_t esp_lcd_new_panel_io_i2c(i2c_master_bus_handle_t bus,
                                   const esp_lcd_panel_io_i2c_config_t *io_config,
                                   esp_lcd_panel_io_handle_t *ret_io) {
    (void)bus;
    (void)io_config;
    if (!ret_io) return ESP_ERR_INVALID_ARG;
    memset(&s_touch_io, 0, sizeof(s_touch_io));
    *ret_io = &s_touch_io;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) { (void)io; return ESP_OK; }

esp_err_t esp_lcd_new_panel_st7789(const esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel) {
    (void)panel_dev_config;
    if (!ret_panel) return ESP_ERR_INVALID_ARG;
    memset(&s_lcd_panel, 0, sizeof(s_lcd_panel));
    s_lcd_panel.io = io;
    s_lcd_panel.swap_xy = true;
    s_lcd_panel.mirror_x = true;
    s_lcd_panel.mirror_y = false;
    *ret_panel = &s_lcd_panel;
    return ESP_OK;
}

esp_err_t esp_lcd_panel_reset(esp_lcd_panel_handle_t panel) { (void)panel; return ESP_OK; }
esp_err_t esp_lcd_panel_init(esp_lcd_panel_handle_t panel) { (void)panel; return ESP_OK; }
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel) { (void)panel; return ESP_OK; }
esp_err_t esp_lcd_panel_set_gap(esp_lcd_panel_handle_t panel, int x_gap, int y_gap) { (void)panel; (void)x_gap; (void)y_gap; return ESP_OK; }
esp_err_t esp_lcd_panel_invert_color(esp_lcd_panel_handle_t panel, bool invert_color_data) { (void)panel; (void)invert_color_data; return ESP_OK; }

esp_err_t esp_lcd_panel_mirror(esp_lcd_panel_handle_t panel, bool mirror_x, bool mirror_y) {
    if (panel) {
        panel->mirror_x = mirror_x;
        panel->mirror_y = mirror_y;
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_swap_xy(esp_lcd_panel_handle_t panel, bool swap_axes) {
    if (panel) {
        panel->swap_xy = swap_axes;
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on_off) {
    if (panel) {
        panel->disp_on = on_off;
    }
    return ESP_OK;
}

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel,
                                    int x_start,
                                    int y_start,
                                    int x_end,
                                    int y_end,
                                    const void *color_data) {
    if (!panel || !color_data) return ESP_ERR_INVALID_ARG;

    const uint16_t *src = (const uint16_t *)color_data;
    int w = x_end - x_start;
    int h = y_end - y_start;

    /* Default Left-Wrist orientation: mirror_x=true, mirror_y=false -> upright on board.png.
     * Right-Wrist orientation: mirror_x=false, mirror_y=true -> rotated 180 degrees. */
    const bool flip_x = !panel->mirror_x;
    const bool flip_y = panel->mirror_y;

    if (!flip_x && !flip_y) {
        for (int row = 0; row < h; row++) {
            int dy = y_start + row;
            if (dy < 0 || dy >= STOCK24_FB_H) continue;
            for (int col = 0; col < w; col++) {
                int dx = x_start + col;
                if (dx >= 0 && dx < STOCK24_FB_W) {
                    s_fb[dy * STOCK24_FB_W + dx] = src[row * w + col];
                }
            }
        }
    } else {
        for (int row = 0; row < h; row++) {
            int sy = y_start + row;
            int dy = flip_y ? (STOCK24_FB_H - 1 - sy) : sy;
            if (dy < 0 || dy >= STOCK24_FB_H) continue;
            for (int col = 0; col < w; col++) {
                int sx = x_start + col;
                int dx = flip_x ? (STOCK24_FB_W - 1 - sx) : sx;
                if (dx >= 0 && dx < STOCK24_FB_W) {
                    s_fb[dy * STOCK24_FB_W + dx] = src[row * w + col];
                }
            }
        }
    }

    if (panel->io && panel->io->on_color_trans_done) {
        panel->io->on_color_trans_done(panel->io, NULL, panel->io->user_ctx);
    }
    return ESP_OK;
}

static uint32_t s_ledc_freq[LEDC_TIMER_MAX];
static uint32_t s_ledc_duty[LEDC_CHANNEL_MAX];

esp_err_t ledc_timer_config(const ledc_timer_config_t *timer_conf) {
    if (!timer_conf || timer_conf->timer_num >= LEDC_TIMER_MAX) return ESP_ERR_INVALID_ARG;
    s_ledc_freq[timer_conf->timer_num] = timer_conf->freq_hz;
    return ESP_OK;
}

esp_err_t ledc_channel_config(const ledc_channel_config_t *ledc_conf) {
    if (!ledc_conf || ledc_conf->channel >= LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_ARG;
    s_ledc_duty[ledc_conf->channel] = ledc_conf->duty;
    return ESP_OK;
}

uint32_t ledc_get_freq(ledc_mode_t speed_mode, ledc_timer_t timer_num) {
    (void)speed_mode;
    if (timer_num >= LEDC_TIMER_MAX) return 0;
    return s_ledc_freq[timer_num] ? s_ledc_freq[timer_num] : 5000U;
}

uint32_t ledc_get_duty(ledc_mode_t speed_mode, ledc_channel_t channel) {
    (void)speed_mode;
    if (channel >= LEDC_CHANNEL_MAX) return LEDC_ERR_DUTY;
    return s_ledc_duty[channel];
}

esp_err_t ledc_set_duty(ledc_mode_t speed_mode, ledc_channel_t channel, uint32_t duty) {
    (void)speed_mode;
    if (channel >= LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_ARG;
    s_ledc_duty[channel] = duty;
    return ESP_OK;
}

esp_err_t ledc_update_duty(ledc_mode_t speed_mode, ledc_channel_t channel) {
    (void)speed_mode;
    if (channel >= LEDC_CHANNEL_MAX) return ESP_ERR_INVALID_ARG;
    uint32_t d = s_ledc_duty[channel];
    /* LCD_BACKLIGHT_ON in display.h is 128; scale 0..128+ cleanly to 0..255 */
    if (d == 0) {
        s_backlight_u8 = 0;
    } else if (d >= 128) {
        s_backlight_u8 = 255;
    } else {
        s_backlight_u8 = (uint8_t)(d * 2);
    }
    return ESP_OK;
}

/* ============================================================================
 * GT911 Capacitive Touch Controller (320x240)
 *
 * touch.c:36 debounces interrupts arriving < 30 ms apart. We latch presses for
 * at least 45 ms of virtual time so sub-frame taps are guaranteed to be sampled
 * by LVGL's 33 ms input timer, and re-pulse touch_intr_cb in emu_step whenever
 * the delivered touch state trails the host state once >= 32 ms have elapsed.
 * ============================================================================ */

struct esp_lcd_touch_s {
    bool inited;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    esp_lcd_touch_interrupt_callback_t interrupt_callback;
};

static struct esp_lcd_touch_s s_touch_dev;
static bool     s_touch_desired_pressed   = false;
static bool     s_touch_latched_pressed   = false;
static uint64_t s_touch_min_hold_until_us = 0;
static uint16_t s_touch_x                 = 0;
static uint16_t s_touch_y                 = 0;

static bool     s_touch_delivered_pressed = false;
static uint16_t s_touch_delivered_x       = 0;
static uint16_t s_touch_delivered_y       = 0;
static uint64_t s_touch_last_deliver_us   = 0;

static bool touch_effective_pressed(uint64_t now_us) {
    if (s_touch_desired_pressed) {
        return true;
    }
    if (s_touch_latched_pressed && now_us < s_touch_min_hold_until_us) {
        return true;
    }
    s_touch_latched_pressed = false;
    return false;
}

static void touch_pump_interrupt(void) {
    if (!s_touch_dev.inited || !s_touch_dev.interrupt_callback) return;
    uint64_t now_us = platform_now_us();
    bool want_pressed = touch_effective_pressed(now_us);
    bool dirty = (want_pressed != s_touch_delivered_pressed) ||
                 (want_pressed && (s_touch_x != s_touch_delivered_x || s_touch_y != s_touch_delivered_y));
    if (!dirty) return;
    if (s_touch_last_deliver_us == 0 || (now_us - s_touch_last_deliver_us) >= 32000ULL) {
        s_touch_dev.interrupt_callback(&s_touch_dev);
    }
}

void emu_set_touch(int pressed, int x, int y) {
    if (x < 0) x = 0;
    if (x >= STOCK24_FB_W) x = STOCK24_FB_W - 1;
    if (y < 0) y = 0;
    if (y >= STOCK24_FB_H) y = STOCK24_FB_H - 1;

    uint64_t now_us = platform_now_us();
    if (pressed) {
        s_touch_x = (uint16_t)x;
        s_touch_y = (uint16_t)y;
        if (!s_touch_desired_pressed) {
            s_touch_latched_pressed = true;
            s_touch_min_hold_until_us = now_us + 45000ULL;
        }
        s_touch_desired_pressed = true;
    } else {
        s_touch_desired_pressed = false;
    }
    touch_pump_interrupt();
}

esp_err_t esp_lcd_touch_new_i2c_gt911(const esp_lcd_panel_io_handle_t io,
                                      const esp_lcd_touch_config_t *config,
                                      esp_lcd_touch_handle_t *out_touch) {
    (void)io;
    if (!config || !out_touch) return ESP_ERR_INVALID_ARG;
    memset(&s_touch_dev, 0, sizeof(s_touch_dev));
    s_touch_dev.inited = true;
    s_touch_dev.swap_xy = config->flags.swap_xy;
    s_touch_dev.mirror_x = config->flags.mirror_x;
    s_touch_dev.mirror_y = config->flags.mirror_y;
    s_touch_dev.interrupt_callback = config->interrupt_callback;
    *out_touch = &s_touch_dev;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_read_data(esp_lcd_touch_handle_t tp) {
    return tp ? ESP_OK : ESP_ERR_INVALID_ARG;
}

bool esp_lcd_touch_get_coordinates(esp_lcd_touch_handle_t tp,
                                   uint16_t *x,
                                   uint16_t *y,
                                   uint16_t *strength,
                                   uint8_t *point_num,
                                   uint8_t max_point_num) {
    if (!tp || max_point_num == 0) {
        if (point_num) *point_num = 0;
        return false;
    }
    uint64_t now_us = platform_now_us();
    bool active = touch_effective_pressed(now_us);

    s_touch_delivered_pressed = active;
    s_touch_delivered_x = s_touch_x;
    s_touch_delivered_y = s_touch_y;
    s_touch_last_deliver_us = now_us;

    if (!active) {
        if (point_num) *point_num = 0;
        return false;
    }

    uint16_t tx = tp->mirror_x ? s_touch_x : (uint16_t)(STOCK24_FB_W - 1 - s_touch_x);
    uint16_t ty = tp->mirror_y ? (uint16_t)(STOCK24_FB_H - 1 - s_touch_y) : s_touch_y;

    if (x) *x = tx;
    if (y) *y = ty;
    if (strength) *strength = 64;
    if (point_num) *point_num = 1;
    return true;
}

esp_err_t esp_lcd_touch_set_swap_xy(esp_lcd_touch_handle_t tp, bool swap) {
    if (!tp) return ESP_ERR_INVALID_ARG;
    tp->swap_xy = swap;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_get_swap_xy(esp_lcd_touch_handle_t tp, bool *swap) {
    if (!tp || !swap) return ESP_ERR_INVALID_ARG;
    *swap = tp->swap_xy;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_set_mirror_x(esp_lcd_touch_handle_t tp, bool mirror) {
    if (!tp) return ESP_ERR_INVALID_ARG;
    tp->mirror_x = mirror;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_get_mirror_x(esp_lcd_touch_handle_t tp, bool *mirror) {
    if (!tp || !mirror) return ESP_ERR_INVALID_ARG;
    *mirror = tp->mirror_x;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_set_mirror_y(esp_lcd_touch_handle_t tp, bool mirror) {
    if (!tp) return ESP_ERR_INVALID_ARG;
    tp->mirror_y = mirror;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_get_mirror_y(esp_lcd_touch_handle_t tp, bool *mirror) {
    if (!tp || !mirror) return ESP_ERR_INVALID_ARG;
    *mirror = tp->mirror_y;
    return ESP_OK;
}

esp_err_t esp_lcd_touch_del(esp_lcd_touch_handle_t tp) {
    if (tp) tp->inited = false;
    return ESP_OK;
}

/* ============================================================================
 * RMT Infrared TX/RX & NEC Frame Injection (emu_send_ir)
 *
 * Synthesizes 34-symbol NEC frames directly into ir_comm.c's active rmt_receive
 * buffer so parse_nec_frame -> ir_rx_callback executes unmodified.
 * ============================================================================ */

struct rmt_channel_t {
    bool is_rx;
    bool enabled;
    bool armed;
    rmt_symbol_word_t *rx_buf;
    size_t rx_max_symbols;
    rmt_rx_done_callback_t on_recv_done;
    void *user_data;
};

typedef struct {
    rmt_encoder_t base;
} sim_copy_encoder_t;

typedef struct {
    rmt_encoder_t base;
    rmt_bytes_encoder_config_t cfg;
} sim_bytes_encoder_t;

static struct rmt_channel_t s_rmt_rx_chan;
static struct rmt_channel_t s_rmt_tx_chan;

static rmt_symbol_word_t *s_enc_out_buf = NULL;
static size_t s_enc_out_cap = 0;
static size_t s_enc_out_len = 0;

#define MAX_PENDING_IR 8
static uint32_t s_pending_ir[MAX_PENDING_IR];
static int s_pending_ir_head = 0;
static int s_pending_ir_tail = 0;
static int s_pending_ir_count = 0;

static void build_nec_symbols(uint16_t address, uint16_t command, rmt_symbol_word_t out_syms[34]) {
    out_syms[0] = (rmt_symbol_word_t){
        .level0 = 1, .duration0 = 9000,
        .level1 = 0, .duration1 = 4500,
    };
    for (int i = 0; i < 16; i++) {
        bool bit = ((address >> i) & 1U) != 0;
        out_syms[1 + i] = (rmt_symbol_word_t){
            .level0 = 1, .duration0 = 560,
            .level1 = 0, .duration1 = (uint16_t)(bit ? 1690 : 560),
        };
    }
    for (int i = 0; i < 16; i++) {
        bool bit = ((command >> i) & 1U) != 0;
        out_syms[17 + i] = (rmt_symbol_word_t){
            .level0 = 1, .duration0 = 560,
            .level1 = 0, .duration1 = (uint16_t)(bit ? 1690 : 560),
        };
    }
    out_syms[33] = (rmt_symbol_word_t){
        .level0 = 1, .duration0 = 560,
        .level1 = 0, .duration1 = 0x7FFF,
    };
}

static void rmt_deliver_symbols_to_rx(const rmt_symbol_word_t *syms, size_t count) {
    if (!s_rmt_rx_chan.enabled || !s_rmt_rx_chan.armed || !s_rmt_rx_chan.rx_buf) return;
    if (count > s_rmt_rx_chan.rx_max_symbols) count = s_rmt_rx_chan.rx_max_symbols;
    memcpy(s_rmt_rx_chan.rx_buf, syms, count * sizeof(rmt_symbol_word_t));
    s_rmt_rx_chan.armed = false;
    if (s_rmt_rx_chan.on_recv_done) {
        rmt_rx_done_event_data_t edata = {
            .received_symbols = s_rmt_rx_chan.rx_buf,
            .num_symbols = count,
            .flags = { .is_last = 1 },
        };
        s_rmt_rx_chan.on_recv_done(&s_rmt_rx_chan, &edata, s_rmt_rx_chan.user_data);
    }
}

static void ir_pump_pending(void) {
    if (s_pending_ir_count <= 0) return;
    if (!s_rmt_rx_chan.enabled || !s_rmt_rx_chan.armed || !s_rmt_rx_chan.rx_buf) return;

    uint32_t code = s_pending_ir[s_pending_ir_tail];
    s_pending_ir_tail = (s_pending_ir_tail + 1) % MAX_PENDING_IR;
    s_pending_ir_count--;

    uint16_t address = (uint16_t)((code >> 16) & 0xFFFFU);
    uint16_t command = (uint16_t)(code & 0xFFFFU);
    rmt_symbol_word_t syms[34];
    build_nec_symbols(address, command, syms);
    rmt_deliver_symbols_to_rx(syms, 34);
}

void emu_send_ir(uint32_t code) {
    if (s_pending_ir_count < MAX_PENDING_IR) {
        s_pending_ir[s_pending_ir_head] = code;
        s_pending_ir_head = (s_pending_ir_head + 1) % MAX_PENDING_IR;
        s_pending_ir_count++;
    }
    ir_pump_pending();
}

void *rmt_alloc_encoder_mem(size_t size) {
    return calloc(1, size);
}

static size_t sim_copy_encode(rmt_encoder_t *encoder,
                              rmt_channel_handle_t tx_channel,
                              const void *primary_data,
                              size_t data_size,
                              rmt_encode_state_t *ret_state) {
    (void)encoder;
    (void)tx_channel;
    size_t words = data_size / sizeof(rmt_symbol_word_t);
    const rmt_symbol_word_t *src = (const rmt_symbol_word_t *)primary_data;
    size_t written = 0;
    while (written < words && s_enc_out_len < s_enc_out_cap) {
        s_enc_out_buf[s_enc_out_len++] = src[written++];
    }
    if (ret_state) *ret_state = RMT_ENCODING_COMPLETE;
    return written;
}

static esp_err_t sim_generic_encoder_reset(rmt_encoder_t *encoder) {
    (void)encoder;
    return ESP_OK;
}

static esp_err_t sim_generic_encoder_del(rmt_encoder_t *encoder) {
    free(encoder);
    return ESP_OK;
}

esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder) {
    (void)config;
    if (!ret_encoder) return ESP_ERR_INVALID_ARG;
    sim_copy_encoder_t *enc = (sim_copy_encoder_t *)calloc(1, sizeof(*enc));
    if (!enc) return ESP_ERR_NO_MEM;
    enc->base.encode = sim_copy_encode;
    enc->base.reset = sim_generic_encoder_reset;
    enc->base.del = sim_generic_encoder_del;
    *ret_encoder = &enc->base;
    return ESP_OK;
}

static size_t sim_bytes_encode(rmt_encoder_t *encoder,
                               rmt_channel_handle_t tx_channel,
                               const void *primary_data,
                               size_t data_size,
                               rmt_encode_state_t *ret_state) {
    (void)tx_channel;
    sim_bytes_encoder_t *benc = (sim_bytes_encoder_t *)encoder;
    const uint8_t *bytes = (const uint8_t *)primary_data;
    size_t written = 0;
    for (size_t b = 0; b < data_size; b++) {
        for (int bit = 0; bit < 8; bit++) {
            int shift = benc->cfg.flags.msb_first ? (7 - bit) : bit;
            bool one = ((bytes[b] >> shift) & 1U) != 0;
            if (s_enc_out_len < s_enc_out_cap) {
                s_enc_out_buf[s_enc_out_len++] = one ? benc->cfg.bit1 : benc->cfg.bit0;
                written++;
            }
        }
    }
    if (ret_state) *ret_state = RMT_ENCODING_COMPLETE;
    return written;
}

esp_err_t rmt_new_bytes_encoder(const rmt_bytes_encoder_config_t *config, rmt_encoder_handle_t *ret_encoder) {
    if (!config || !ret_encoder) return ESP_ERR_INVALID_ARG;
    sim_bytes_encoder_t *enc = (sim_bytes_encoder_t *)calloc(1, sizeof(*enc));
    if (!enc) return ESP_ERR_NO_MEM;
    enc->cfg = *config;
    enc->base.encode = sim_bytes_encode;
    enc->base.reset = sim_generic_encoder_reset;
    enc->base.del = sim_generic_encoder_del;
    *ret_encoder = &enc->base;
    return ESP_OK;
}

esp_err_t rmt_del_encoder(rmt_encoder_handle_t encoder) {
    if (!encoder) return ESP_ERR_INVALID_ARG;
    return encoder->del ? encoder->del(encoder) : ESP_OK;
}

esp_err_t rmt_encoder_reset(rmt_encoder_handle_t encoder) {
    if (!encoder) return ESP_ERR_INVALID_ARG;
    return encoder->reset ? encoder->reset(encoder) : ESP_OK;
}

esp_err_t rmt_new_rx_channel(const rmt_rx_channel_config_t *config, rmt_channel_handle_t *ret_chan) {
    (void)config;
    if (!ret_chan) return ESP_ERR_INVALID_ARG;
    memset(&s_rmt_rx_chan, 0, sizeof(s_rmt_rx_chan));
    s_rmt_rx_chan.is_rx = true;
    *ret_chan = &s_rmt_rx_chan;
    return ESP_OK;
}

esp_err_t rmt_rx_register_event_callbacks(rmt_channel_handle_t rx_channel,
                                          const rmt_rx_event_callbacks_t *cbs,
                                          void *user_data) {
    if (!rx_channel || !cbs) return ESP_ERR_INVALID_ARG;
    rx_channel->on_recv_done = cbs->on_recv_done;
    rx_channel->user_data = user_data;
    return ESP_OK;
}

esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *config, rmt_channel_handle_t *ret_chan) {
    (void)config;
    if (!ret_chan) return ESP_ERR_INVALID_ARG;
    memset(&s_rmt_tx_chan, 0, sizeof(s_rmt_tx_chan));
    s_rmt_tx_chan.is_rx = false;
    *ret_chan = &s_rmt_tx_chan;
    return ESP_OK;
}

esp_err_t rmt_apply_carrier(rmt_channel_handle_t channel, const rmt_carrier_config_t *carrier) {
    (void)channel;
    (void)carrier;
    return ESP_OK;
}

esp_err_t rmt_enable(rmt_channel_handle_t channel) {
    if (!channel) return ESP_ERR_INVALID_ARG;
    channel->enabled = true;
    return ESP_OK;
}

esp_err_t rmt_disable(rmt_channel_handle_t channel) {
    if (!channel) return ESP_ERR_INVALID_ARG;
    channel->enabled = false;
    channel->armed = false;
    return ESP_OK;
}

esp_err_t rmt_del_channel(rmt_channel_handle_t channel) {
    if (channel) memset(channel, 0, sizeof(*channel));
    return ESP_OK;
}

esp_err_t rmt_receive(rmt_channel_handle_t rx_channel,
                      void *buffer,
                      size_t buffer_size,
                      const rmt_receive_config_t *config) {
    (void)config;
    if (!rx_channel || !buffer || buffer_size < sizeof(rmt_symbol_word_t)) return ESP_ERR_INVALID_ARG;
    rx_channel->rx_buf = (rmt_symbol_word_t *)buffer;
    rx_channel->rx_max_symbols = buffer_size / sizeof(rmt_symbol_word_t);
    rx_channel->armed = true;
    ir_pump_pending();
    return ESP_OK;
}

esp_err_t rmt_transmit(rmt_channel_handle_t tx_channel,
                       rmt_encoder_handle_t encoder,
                       const void *payload,
                       size_t payload_bytes,
                       const rmt_transmit_config_t *config) {
    (void)config;
    if (!tx_channel || !encoder || !payload) return ESP_ERR_INVALID_ARG;
    rmt_symbol_word_t tx_syms[64];
    s_enc_out_buf = tx_syms;
    s_enc_out_cap = 64;
    s_enc_out_len = 0;

    rmt_encoder_reset(encoder);
    rmt_encode_state_t st = RMT_ENCODING_RESET;
    encoder->encode(encoder, tx_channel, payload, payload_bytes, &st);

    s_enc_out_buf = NULL;
    s_enc_out_cap = 0;
    /* Note: do not loop badge TX back into badge RX during normal play (only in hwtest if needed) */
    return ESP_OK;
}

/* ============================================================================
 * Embedded SPIFFS (/spiffs) Virtual Filesystem (LVL1.bin, LVL2.bin, LVL3.bin)
 * ============================================================================ */

static bool s_spiffs_mounted = false;

#ifndef __EMSCRIPTEN__
#define NATIVE_SPIFFS_DIR "emulator/build/2024-spiffs"

static const char *translate_spiffs_path(const char *path, char *buf, size_t buflen) {
    if (!path) return NULL;
    if (strcmp(path, "/spiffs") == 0) {
        snprintf(buf, buflen, "%s", NATIVE_SPIFFS_DIR);
        return buf;
    }
    if (strncmp(path, "/spiffs/", 8) == 0) {
        snprintf(buf, buflen, "%s/%s", NATIVE_SPIFFS_DIR, path + 8);
        return buf;
    }
    return path;
}

FILE *__real_fopen(const char *pathname, const char *mode);
FILE *__wrap_fopen(const char *pathname, const char *mode) {
    char buf[512];
    return __real_fopen(translate_spiffs_path(pathname, buf, sizeof(buf)), mode);
}

DIR *__real_opendir(const char *name);
DIR *__wrap_opendir(const char *name) {
    char buf[512];
    return __real_opendir(translate_spiffs_path(name, buf, sizeof(buf)));
}

int __real_stat(const char *pathname, struct stat *statbuf);
int __wrap_stat(const char *pathname, struct stat *statbuf) {
    char buf[512];
    return __real_stat(translate_spiffs_path(pathname, buf, sizeof(buf)), statbuf);
}

int __real_remove(const char *pathname);
int __wrap_remove(const char *pathname) {
    char buf[512];
    return __real_remove(translate_spiffs_path(pathname, buf, sizeof(buf)));
}

int __real_unlink(const char *pathname);
int __wrap_unlink(const char *pathname) {
    char buf[512];
    return __real_unlink(translate_spiffs_path(pathname, buf, sizeof(buf)));
}

int __real_rename(const char *oldpath, const char *newpath);
int __wrap_rename(const char *oldpath, const char *newpath) {
    char b1[512], b2[512];
    return __real_rename(translate_spiffs_path(oldpath, b1, sizeof(b1)),
                         translate_spiffs_path(newpath, b2, sizeof(b2)));
}
#endif

esp_err_t esp_vfs_spiffs_register(const esp_vfs_spiffs_conf_t *conf) {
    (void)conf;
#ifdef __EMSCRIPTEN__
    mkdir("/spiffs", 0755);
#else
    mkdir("emulator", 0755);
    mkdir("emulator/build", 0755);
    mkdir(NATIVE_SPIFFS_DIR, 0755);
#endif
    for (size_t i = 0; i < k_spiffs_asset_count; i++) {
        char path[256];
        snprintf(path, sizeof(path), "/spiffs/%s", k_spiffs_assets[i].name);
        FILE *f = fopen(path, "wb");
        if (f) {
            fwrite(k_spiffs_assets[i].data, 1, k_spiffs_assets[i].size, f);
            fclose(f);
        }
    }
    s_spiffs_mounted = true;
    return ESP_OK;
}

esp_err_t esp_vfs_spiffs_unregister(const char *partition_label) {
    (void)partition_label;
    s_spiffs_mounted = false;
    return ESP_OK;
}

bool esp_spiffs_mounted(const char *partition_label) {
    (void)partition_label;
    return s_spiffs_mounted;
}

esp_err_t esp_spiffs_info(const char *partition_label, size_t *total_bytes, size_t *used_bytes) {
    (void)partition_label;
    size_t used = 0;
    for (size_t i = 0; i < k_spiffs_asset_count; i++) {
        used += k_spiffs_assets[i].size;
    }
    if (total_bytes) *total_bytes = 2 * 1024 * 1024;
    if (used_bytes)  *used_bytes  = used;
    return ESP_OK;
}

esp_err_t esp_spiffs_format(const char *partition_label) {
    (void)partition_label;
    return ESP_OK;
}

/* ============================================================================
 * 12-Function Museum Emulator Core ABI (emulator/core/emu.h) + 2024 Extensions
 * ============================================================================ */

void emu_init(uint32_t seed) {
    const char *log_env = getenv("EMU24_LOG");
    s_log_enabled = (log_env && log_env[0] == '1');

    s_rng_state = ((uint64_t)(seed ? seed : 0x20245343U) << 32) ^ 0x9E3779B97F4A7C15ULL;
    memset(s_fb, 0, sizeof(s_fb));
    memset(s_leds_dummy, 0, sizeof(s_leds_dummy));
    memset(s_eeprom_dummy, 0, sizeof(s_eeprom_dummy));
    memset(s_gpio, 0, sizeof(s_gpio));
    memset(s_i2c_devs, 0, sizeof(s_i2c_devs));
    memset(s_event_loops, 0, sizeof(s_event_loops));
    memset(s_event_subs, 0, sizeof(s_event_subs));
    memset(&s_wifi_sta_cfg, 0, sizeof(s_wifi_sta_cfg));
    memset(&s_touch_dev, 0, sizeof(s_touch_dev));
    memset(&s_rmt_rx_chan, 0, sizeof(s_rmt_rx_chan));
    memset(&s_rmt_tx_chan, 0, sizeof(s_rmt_tx_chan));

    s_wifi_connected = false;
    s_badge_sync_timer = NULL;
    s_post_splash_sync_timer = NULL;
    s_backlight_u8 = 255;

    s_touch_desired_pressed = false;
    s_touch_latched_pressed = false;
    s_touch_min_hold_until_us = 0;
    s_touch_x = 0;
    s_touch_y = 0;
    s_touch_delivered_pressed = false;
    s_touch_delivered_x = 0;
    s_touch_delivered_y = 0;
    s_touch_last_deliver_us = 0;

    s_pending_ir_head = 0;
    s_pending_ir_tail = 0;
    s_pending_ir_count = 0;

    peripheral_regs_reset();

#ifndef __EMSCRIPTEN__
    cli_fake_server_reset(seed);
#endif

    platform_storage_init(seed);
    platform_scheduler_init();

    /* Schedule post-splash sync at 5350 ms so returning registered visitors
     * refresh tower_info[0..10], minibadges, and OTA check once SCREEN_MAIN is active. */
    esp_timer_create_args_t sync_args = {
        .callback = badge_sync_timer_cb,
        .arg = NULL,
        .name = "post_splash_sync",
    };
    if (esp_timer_create(&sync_args, &s_post_splash_sync_timer) == ESP_OK) {
        esp_timer_start_once(s_post_splash_sync_timer, 5350000ULL);
    }

    platform_scheduler_start_app_main();
    platform_scheduler_step(20000ULL);
}

void emu_step(uint32_t ms, uint8_t buttons) {
    (void)buttons;

    uint64_t total_us = (uint64_t)ms * 1000ULL;
    if (total_us == 0) {
        touch_pump_interrupt();
        ir_pump_pending();
        platform_scheduler_step(0);
        return;
    }

    /* Slice larger dt values into <= 5 ms ticks so LVGL, touch debounce, & FreeRTOS tasks interleave smoothly */
    const uint64_t slice_us = 5000ULL;
    while (total_us > 0) {
        uint64_t step = (total_us > slice_us) ? slice_us : total_us;
        touch_pump_interrupt();
        ir_pump_pending();
        platform_scheduler_step(step);
        total_us -= step;
    }
    touch_pump_interrupt();
    ir_pump_pending();
}

uint16_t *emu_framebuffer(void) { return s_fb; }
uint8_t *emu_leds(void)         { return s_leds_dummy; }
uint8_t emu_backlight(void)     { return s_backlight_u8; }
uint8_t *emu_eeprom(void)       { return s_eeprom_dummy; }
int emu_eeprom_dirty(void)      { return 0; }

void emu_set_speed(uint32_t mult) {
    s_speed_mult = mult ? mult : 1;
}

int emu_fb_width(void)    { return STOCK24_FB_W; }
int emu_fb_height(void)   { return STOCK24_FB_H; }
int emu_led_count(void)   { return STOCK24_LED_COUNT; }
int emu_eeprom_size(void) { return STOCK24_EEPROM_SZ; }
