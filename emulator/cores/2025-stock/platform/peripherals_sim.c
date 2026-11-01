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
#include "secure_element.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "led_strip.h"

/* Generated at build time from upstream/Firmware/badge/spiffs_source/ PNGs */
#include "spiffs_assets.h"

#define EMU_BTN_UP     (1u << 0)
#define EMU_BTN_DOWN   (1u << 1)
#define EMU_BTN_LEFT   (1u << 2)
#define EMU_BTN_RIGHT  (1u << 3)
#define EMU_BTN_CENTER (1u << 4)

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
 * Core ABI Buffers & State (240x320 RGB565 LE, 20 LEDs, 512-byte ST25DV EEPROM)
 * ============================================================================ */

#define STOCK25_FB_W      240
#define STOCK25_FB_H      320
#define STOCK25_LED_COUNT 20
#define STOCK25_EEPROM_SZ 512

_Alignas(4) static uint16_t s_fb[STOCK25_FB_W * STOCK25_FB_H];
_Alignas(4) static uint8_t  s_leds[STOCK25_LED_COUNT * 3];
_Alignas(4) static uint8_t  s_leds_staging[STOCK25_LED_COUNT * 3];
_Alignas(4) static uint8_t  s_eeprom[STOCK25_EEPROM_SZ];
_Alignas(4) static uint8_t  s_eeprom_shadow[STOCK25_EEPROM_SZ];

static int      s_eeprom_dirty = 0;
static uint8_t  s_backlight_u8 = 255;
static uint8_t  s_buttons      = 0;
static uint32_t s_speed_mult   = 1;
static uint64_t s_rng_state    = 0x202553439E3779B9ULL;
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
    /* Deterministic museum badge MAC (locally administered 02:5C:25:00:00:01) */
    static const uint8_t k_base_mac[6] = {0x02, 0x5C, 0x25, 0x00, 0x00, 0x01};
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
    .project_name = "saintcon-2025-badge",
    .time = "00:00:00",
    .date = "Nov 11 2025",
    .idf_ver = "v5.5.1-museum",
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
    return "360c892981968822fbadfdaa6ff580a55f4396af000000000000000000000000";
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
 * esp_event Loop Simulation (used by minibadge D-pad events)
 * ============================================================================ */

#define MAX_EVENT_HANDLERS 16

typedef struct {
    bool used;
    esp_event_loop_handle_t loop;
    esp_event_base_t base;
    int32_t id;
    esp_event_handler_t handler;
    void *arg;
} sim_event_sub_t;

static sim_event_sub_t s_event_subs[MAX_EVENT_HANDLERS];
static int s_next_loop_token = 1;

esp_err_t esp_event_loop_create_default(void) {
    return ESP_OK;
}

esp_err_t esp_event_loop_create(const esp_event_loop_args_t *args, esp_event_loop_handle_t *out_loop) {
    (void)args;
    if (!out_loop) return ESP_ERR_INVALID_ARG;
    *out_loop = (esp_event_loop_handle_t)(intptr_t)(s_next_loop_token++);
    return ESP_OK;
}

esp_err_t esp_event_loop_delete(esp_event_loop_handle_t event_loop) {
    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (s_event_subs[i].used && s_event_subs[i].loop == event_loop) {
            memset(&s_event_subs[i], 0, sizeof(s_event_subs[i]));
        }
    }
    return ESP_OK;
}

esp_err_t esp_event_handler_register_with(esp_event_loop_handle_t event_loop,
                                          esp_event_base_t event_base,
                                          int32_t event_id,
                                          esp_event_handler_t event_handler,
                                          void *event_handler_arg) {
    if (!event_handler) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (!s_event_subs[i].used) {
            s_event_subs[i].used = true;
            s_event_subs[i].loop = event_loop;
            s_event_subs[i].base = event_base;
            s_event_subs[i].id = event_id;
            s_event_subs[i].handler = event_handler;
            s_event_subs[i].arg = event_handler_arg;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_event_handler_register(esp_event_base_t event_base,
                                     int32_t event_id,
                                     esp_event_handler_t event_handler,
                                     void *event_handler_arg) {
    return esp_event_handler_register_with(NULL, event_base, event_id, event_handler, event_handler_arg);
}

esp_err_t esp_event_handler_unregister_with(esp_event_loop_handle_t event_loop,
                                            esp_event_base_t event_base,
                                            int32_t event_id,
                                            esp_event_handler_t event_handler) {
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

esp_err_t esp_event_post_to(esp_event_loop_handle_t event_loop,
                            esp_event_base_t event_base,
                            int32_t event_id,
                            const void *event_data,
                            size_t event_data_size,
                            TickType_t ticks_to_wait) {
    (void)event_data_size;
    (void)ticks_to_wait;
    for (int i = 0; i < MAX_EVENT_HANDLERS; i++) {
        if (!s_event_subs[i].used) continue;
        if (s_event_subs[i].loop != event_loop) continue;
        if (s_event_subs[i].base != ESP_EVENT_ANY_BASE &&
            s_event_subs[i].base != event_base &&
            (!s_event_subs[i].base || !event_base || strcmp(s_event_subs[i].base, event_base) != 0)) {
            continue;
        }
        if (s_event_subs[i].id != ESP_EVENT_ANY_ID && s_event_subs[i].id != event_id) {
            continue;
        }
        s_event_subs[i].handler(s_event_subs[i].arg, event_base, event_id, (void *)event_data);
    }
    return ESP_OK;
}

/* ============================================================================
 * GPIO, 5-Way Joystick, Dawn Accord Intact Pins, and NFC GPO Interrupts
 * ============================================================================ */

typedef struct {
    uint8_t out_level;
    gpio_int_type_t intr_type;
    gpio_isr_t isr;
    void *isr_arg;
} sim_gpio_pin_t;

static sim_gpio_pin_t s_gpio[SOC_GPIO_PIN_COUNT];
static int s_nfc_gpo_level = 1;

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
    switch ((int)gpio_num) {
        /* 5-way joystick (active-low: 0 when pressed, 1 when idle) */
        case CONFIG_INPUT_JOY_UP_GPIO:     return (s_buttons & EMU_BTN_UP)     ? 0 : 1;
        case CONFIG_INPUT_JOY_DOWN_GPIO:   return (s_buttons & EMU_BTN_DOWN)   ? 0 : 1;
        case CONFIG_INPUT_JOY_LEFT_GPIO:   return (s_buttons & EMU_BTN_LEFT)   ? 0 : 1;
        case CONFIG_INPUT_JOY_RIGHT_GPIO:  return (s_buttons & EMU_BTN_RIGHT)  ? 0 : 1;
        case CONFIG_INPUT_JOY_CENTER_GPIO: return (s_buttons & EMU_BTN_CENTER) ? 0 : 1;

        /* Dawn Accord hardware integrity pins: both intact (pulled low) */
        case CONFIG_AI_SECURITY_STRIP_GPIO: return 0;
        case CONFIG_AI_INHIBITOR_GPIO:      return 0;

        /* ST25DV NFC GPO active-low open-drain interrupt pin */
        case CONFIG_NFC_GPO_PIN:            return s_nfc_gpo_level;

        default:
            if ((int)gpio_num >= 0 && (int)gpio_num < SOC_GPIO_PIN_COUNT) {
                return s_gpio[gpio_num].out_level;
            }
            return 1;
    }
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
 * ADC (Battery = 3150 mV Full; Dawn Accord AI Inhibitor = 450 mV Intact)
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
    if (chan == ADC_CHANNEL_0) {
        /* Battery sense (GPIO 1 -> ADC1_CH0): ~3150 mV full 2xAA alkaline */
        *out_raw = 3900;
    } else if (chan == ADC_CHANNEL_4) {
        /* Dawn Accord AI inhibitor (GPIO 5 -> ADC1_CH4): ~450 mV intact resistor */
        *out_raw = 560;
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
    if (handle && handle->chan == ADC_CHANNEL_4) {
        *voltage = 450;
    } else if (handle && handle->chan == ADC_CHANNEL_0) {
        *voltage = 3150;
    } else {
        *voltage = (raw * 3200) / 4095;
    }
    return ESP_OK;
}

/* ============================================================================
 * I2C Master + ST25DV04K Dynamic NFC Tag (0x53 User/Dyn + 0x57 System)
 * ============================================================================ */

#define ST25DV_I2C_DATA_ADDR 0x53
#define ST25DV_I2C_SYST_ADDR 0x57
#define ATECC608_I2C_ADDR    0x60

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

static uint8_t  s_st25_gpo_conf    = 0xC6;
static uint8_t  s_st25_pulse_dur   = 0x00;
static uint8_t  s_st25_gpo_ctrl    = 0x80;
static uint8_t  s_st25_eh_ctrl     = 0x00;
static uint8_t  s_st25_rf_mngt     = 0x00;
static uint8_t  s_st25_it_sts      = 0x00;
static uint8_t  s_st25_mb_ctrl     = 0x00;
static uint8_t  s_st25_ftm[256];
static uint16_t s_st25_data_cursor = 0;
static uint16_t s_st25_syst_cursor = 0;

static void st25dv_pulse_gpo_interrupt(void) {
    s_st25_it_sts = 0x86; /* RF_WRITE (0x80) | RF_INTTERUPT (0x04) | RF_ACTIVITY (0x02) */
    gpio_isr_t isr = s_gpio[CONFIG_NFC_GPO_PIN].isr;
    void *arg = s_gpio[CONFIG_NFC_GPO_PIN].isr_arg;
    if (isr) {
        s_nfc_gpo_level = 0;
        isr(arg);
        s_nfc_gpo_level = 1;
        isr(arg);
    }
}

static uint8_t st25dv_read_data_byte(uint16_t addr) {
    if (addr < STOCK25_EEPROM_SZ) {
        return s_eeprom[addr];
    }
    switch (addr) {
        case 0x2000: return s_st25_gpo_ctrl;
        case 0x2002: return s_st25_eh_ctrl;
        case 0x2003: return s_st25_rf_mngt;
        case 0x2004: return 0x01; /* I2C_SSO: security session open */
        case 0x2005: {
            uint8_t v = s_st25_it_sts;
            s_st25_it_sts = 0;
            return v;
        }
        case 0x2006: return s_st25_mb_ctrl;
        case 0x2007: return 0x00;
        default:
            if (addr >= 0x2008 && addr < 0x2108) {
                return s_st25_ftm[addr - 0x2008];
            }
            return 0x00;
    }
}

static void st25dv_write_data_byte(uint16_t addr, uint8_t val) {
    if (addr < STOCK25_EEPROM_SZ) {
        s_eeprom[addr] = val;
        s_eeprom_shadow[addr] = val;
        s_eeprom_dirty = 1;
        return;
    }
    switch (addr) {
        case 0x2000: s_st25_gpo_ctrl = val; break;
        case 0x2002: s_st25_eh_ctrl  = val; break;
        case 0x2003: s_st25_rf_mngt  = val; break;
        case 0x2006: s_st25_mb_ctrl  = val; break;
        default:
            if (addr >= 0x2008 && addr < 0x2108) {
                s_st25_ftm[addr - 0x2008] = val;
            }
            break;
    }
}

static uint8_t st25dv_read_syst_byte(uint16_t addr) {
    static const uint8_t k_uid[8] = {0xE0, 0x02, 0x25, 0x5C, 0x00, 0x00, 0x00, 0x01};
    switch (addr) {
        case 0x0000: return s_st25_gpo_conf;
        case 0x0001: return s_st25_pulse_dur;
        case 0x0005: return 0x7F;
        case 0x0007: return 0x7F;
        case 0x0009: return 0x7F;
        /* espp::St25dv::read_block_size_bytes reads 1B from 0x0014 (4 B/block),
         * and read_memory_size_blocks reads 2B BE from 0x0016..0x0017 (128 blocks -> 512 B). */
        case 0x0014: return 0x04;
        case 0x0015: return 0x00;
        case 0x0016: return 0x00;
        case 0x0017: return 0x80;
        case 0x0020: return 0x24;
        default:
            if (addr >= 0x0018 && addr < 0x0020) {
                return k_uid[addr - 0x0018];
            }
            if (addr >= 0x0900 && addr < 0x0908) {
                return 0x00;
            }
            return 0x00;
    }
}

static void st25dv_write_syst_byte(uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0x0000: s_st25_gpo_conf  = val; break;
        case 0x0001: s_st25_pulse_dur = val; break;
        default: break;
    }
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

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus_handle, uint16_t address, int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (!bus_handle) return ESP_ERR_INVALID_ARG;
    /* Bus A (port 0) hosts ST25DV04K (0x53 & 0x57) and ATECC608B (0x60).
     * Bus B (port 1) is the minibadge bus (nothing attached). */
    if (bus_handle->port == 0) {
        if (address == ST25DV_I2C_DATA_ADDR ||
            address == ST25DV_I2C_SYST_ADDR ||
            address == ATECC608_I2C_ADDR) {
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t i2c_dev,
                              const uint8_t *write_buffer,
                              size_t write_size,
                              int xfer_timeout_ms) {
    (void)xfer_timeout_ms;
    if (!i2c_dev || (!write_buffer && write_size > 0)) return ESP_ERR_INVALID_ARG;
    if (i2c_dev->bus_port != 0) return ESP_ERR_NOT_FOUND;

    if (i2c_dev->addr == ST25DV_I2C_DATA_ADDR) {
        if (write_size >= 2) {
            uint16_t reg = ((uint16_t)write_buffer[0] << 8) | write_buffer[1];
            for (size_t i = 2; i < write_size; i++) {
                st25dv_write_data_byte((uint16_t)(reg + (i - 2)), write_buffer[i]);
            }
            s_st25_data_cursor = (uint16_t)(reg + (write_size - 2));
        }
        return ESP_OK;
    }
    if (i2c_dev->addr == ST25DV_I2C_SYST_ADDR) {
        if (write_size >= 2) {
            uint16_t reg = ((uint16_t)write_buffer[0] << 8) | write_buffer[1];
            /* Ignore 19-byte present_password write to 0x0900 (session stays open) */
            if (!(reg == 0x0900 && write_size == 19)) {
                for (size_t i = 2; i < write_size; i++) {
                    st25dv_write_syst_byte((uint16_t)(reg + (i - 2)), write_buffer[i]);
                }
            }
            s_st25_syst_cursor = (uint16_t)(reg + (write_size - 2));
        }
        return ESP_OK;
    }
    if (i2c_dev->addr == ATECC608_I2C_ADDR) {
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
    if (i2c_dev->bus_port != 0) return ESP_ERR_NOT_FOUND;

    if (i2c_dev->addr == ST25DV_I2C_DATA_ADDR) {
        for (size_t i = 0; i < read_size; i++) {
            read_buffer[i] = st25dv_read_data_byte(s_st25_data_cursor++);
        }
        return ESP_OK;
    }
    if (i2c_dev->addr == ST25DV_I2C_SYST_ADDR) {
        for (size_t i = 0; i < read_size; i++) {
            read_buffer[i] = st25dv_read_syst_byte(s_st25_syst_cursor++);
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

/* Helper to write a valid Type-5 NDEF Nut tag (5-char code + optional NT:<TYPE>) into EEPROM */
void platform_nfc_inject_nut(const char *code5, const char *nut_type_str) {
    if (!code5 || strlen(code5) != 5) return;

    /* Record 1: NDEF Well-Known Text ("T"), UTF-8 lang "en", payload = 5-char code */
    const bool has_nt = (nut_type_str && nut_type_str[0] != '\0');
    uint8_t rec1[32];
    size_t r1_len = 0;
    rec1[r1_len++] = (uint8_t)(0x80U | (has_nt ? 0x00U : 0x40U) | 0x10U | 0x01U); /* MB | (ME) | SR | TNF_WELL_KNOWN */
    rec1[r1_len++] = 1;           /* type length: "T" */
    rec1[r1_len++] = 1 + 2 + 5;   /* payload length: status(1) + "en"(2) + code(5) */
    rec1[r1_len++] = 'T';
    rec1[r1_len++] = 0x02;        /* UTF-8, lang len = 2 */
    rec1[r1_len++] = 'e';
    rec1[r1_len++] = 'n';
    memcpy(rec1 + r1_len, code5, 5);
    r1_len += 5;

    uint8_t rec2[64];
    size_t r2_len = 0;
    if (has_nt) {
        char nt_buf[40];
        int nt_chars = snprintf(nt_buf, sizeof(nt_buf), "NT:%s", nut_type_str);
        if (nt_chars > 0 && nt_chars < (int)sizeof(nt_buf)) {
            rec2[r2_len++] = (uint8_t)(0x40U | 0x10U | 0x01U); /* ME | SR | TNF_WELL_KNOWN */
            rec2[r2_len++] = 1;
            rec2[r2_len++] = (uint8_t)(1 + 2 + nt_chars);
            rec2[r2_len++] = 'T';
            rec2[r2_len++] = 0x02;
            rec2[r2_len++] = 'e';
            rec2[r2_len++] = 'n';
            memcpy(rec2 + r2_len, nt_buf, (size_t)nt_chars);
            r2_len += (size_t)nt_chars;
        }
    }

    size_t ndef_total = r1_len + r2_len;
    if (4 + 2 + ndef_total + 1 > STOCK25_EEPROM_SZ) return;

    memset(s_eeprom, 0, sizeof(s_eeprom));
    size_t off = 0;
    s_eeprom[off++] = 0xE1;
    s_eeprom[off++] = 0x40;
    s_eeprom[off++] = 0x40;
    s_eeprom[off++] = 0x05;
    s_eeprom[off++] = 0x03; /* Type-5 NDEF Message TLV */
    s_eeprom[off++] = (uint8_t)ndef_total;
    memcpy(s_eeprom + off, rec1, r1_len);
    off += r1_len;
    if (r2_len > 0) {
        memcpy(s_eeprom + off, rec2, r2_len);
        off += r2_len;
    }
    s_eeprom[off++] = 0xFE; /* Terminator TLV */
    /* Leave s_eeprom_shadow unchanged so emu_step detects the external RF write and pulses GPO! */
}

/* ============================================================================
 * esp_lcd i80 + ST7789 (240x320 RGB565 LE) & LEDC Backlight
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

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io) { (void)io; return ESP_OK; }

esp_err_t esp_lcd_new_panel_st7789(const esp_lcd_panel_io_handle_t io,
                                   const esp_lcd_panel_dev_config_t *panel_dev_config,
                                   esp_lcd_panel_handle_t *ret_panel) {
    (void)panel_dev_config;
    if (!ret_panel) return ESP_ERR_INVALID_ARG;
    memset(&s_lcd_panel, 0, sizeof(s_lcd_panel));
    s_lcd_panel.io = io;
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

    if (!panel->swap_xy && !panel->mirror_x && !panel->mirror_y) {
        for (int row = 0; row < h; row++) {
            int dy = y_start + row;
            if (dy < 0 || dy >= STOCK25_FB_H) continue;
            for (int col = 0; col < w; col++) {
                int dx = x_start + col;
                if (dx >= 0 && dx < STOCK25_FB_W) {
                    s_fb[dy * STOCK25_FB_W + dx] = src[row * w + col];
                }
            }
        }
    } else {
        for (int row = 0; row < h; row++) {
            for (int col = 0; col < w; col++) {
                int sx = x_start + col;
                int sy = y_start + row;
                int dx = panel->swap_xy ? sy : sx;
                int dy = panel->swap_xy ? sx : sy;
                if (panel->mirror_x) dx = STOCK25_FB_W - 1 - dx;
                if (panel->mirror_y) dy = STOCK25_FB_H - 1 - dy;
                if (dx >= 0 && dx < STOCK25_FB_W && dy >= 0 && dy < STOCK25_FB_H) {
                    s_fb[dy * STOCK25_FB_W + dx] = src[row * w + col];
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
    return s_ledc_freq[timer_num] ? s_ledc_freq[timer_num] : 1000U;
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
 * Addressable LED Strip (20 WS2812s in 2025 chain order)
 * ============================================================================ */

struct led_strip_t {
    uint32_t max_leds;
};

static struct led_strip_t s_led_strip_dev;

esp_err_t led_strip_new_rmt_device(const led_strip_config_t *led_config,
                                   const led_strip_rmt_config_t *rmt_config,
                                   led_strip_handle_t *ret_strip) {
    (void)rmt_config;
    if (!led_config || !ret_strip) return ESP_ERR_INVALID_ARG;
    s_led_strip_dev.max_leds = led_config->max_leds;
    *ret_strip = &s_led_strip_dev;
    return ESP_OK;
}

esp_err_t led_strip_set_pixel(led_strip_handle_t strip,
                              uint32_t index,
                              uint32_t red,
                              uint32_t green,
                              uint32_t blue) {
    (void)strip;
    if (index >= STOCK25_LED_COUNT) return ESP_ERR_INVALID_ARG;
    s_leds_staging[index * 3 + 0] = (uint8_t)(red & 0xFFU);
    s_leds_staging[index * 3 + 1] = (uint8_t)(green & 0xFFU);
    s_leds_staging[index * 3 + 2] = (uint8_t)(blue & 0xFFU);
    return ESP_OK;
}

esp_err_t led_strip_refresh(led_strip_handle_t strip) {
    (void)strip;
    memcpy(s_leds, s_leds_staging, sizeof(s_leds));
    return ESP_OK;
}

esp_err_t led_strip_clear(led_strip_handle_t strip) {
    (void)strip;
    memset(s_leds_staging, 0, sizeof(s_leds_staging));
    memset(s_leds, 0, sizeof(s_leds));
    return ESP_OK;
}

esp_err_t led_strip_del(led_strip_handle_t strip) {
    (void)strip;
    return ESP_OK;
}

/* ============================================================================
 * Wi-Fi Manager Simulation (Pretends connected to "MUSEUM")
 * ============================================================================ */

static wifi_status_t s_wifi_status = WIFI_STATUS_DISCONNECTED;
static wifi_status_callback_t s_wifi_cbs[WIFI_STATUS_CALLBACK_MAX];
static int s_wifi_cb_count = 0;
static esp_timer_handle_t s_wifi_connect_timer = NULL;
static wifi_credentials_t s_saved_wifi[4];
static uint8_t s_saved_wifi_count = 1;
static wifi_ps_type_t s_wifi_ps = WIFI_PS_NONE;

static void notify_wifi_callbacks(wifi_status_t st) {
    s_wifi_status = st;
    for (int i = 0; i < s_wifi_cb_count; i++) {
        if (s_wifi_cbs[i]) {
            s_wifi_cbs[i](st);
        }
    }
}

static void wifi_connect_timer_cb(void *arg) {
    (void)arg;
    notify_wifi_callbacks(WIFI_STATUS_CONNECTED);
}

static void schedule_wifi_connect(uint64_t delay_us) {
    if (!s_wifi_connect_timer) {
        esp_timer_create_args_t args = {
            .callback = wifi_connect_timer_cb,
            .arg = NULL,
            .name = "wifi_sim_conn",
        };
        esp_timer_create(&args, &s_wifi_connect_timer);
    }
    if (s_wifi_connect_timer) {
        esp_timer_stop(s_wifi_connect_timer);
        esp_timer_start_once(s_wifi_connect_timer, delay_us);
    }
}

esp_err_t wifi_manager_init(void) {
    s_saved_wifi_count = 1;
    snprintf(s_saved_wifi[0].ssid, sizeof(s_saved_wifi[0].ssid), "%s", "MUSEUM");
    s_saved_wifi[0].password[0] = '\0';
    s_wifi_status = WIFI_STATUS_CONNECTING;
    /* Fire CONNECTED 150ms after wifi_manager_init() returns (after main.c sets CONNECTING) */
    schedule_wifi_connect(150000ULL);
    return ESP_OK;
}

void wifi_force_reconnect(void) {
    notify_wifi_callbacks(WIFI_STATUS_CONNECTING);
    schedule_wifi_connect(80000ULL);
}

wifi_status_t get_wifi_status(void) {
    return s_wifi_status;
}

void add_wifi_status_callback(wifi_status_callback_t cb) {
    if (!cb || s_wifi_cb_count >= WIFI_STATUS_CALLBACK_MAX) return;
    for (int i = 0; i < s_wifi_cb_count; i++) {
        if (s_wifi_cbs[i] == cb) return;
    }
    s_wifi_cbs[s_wifi_cb_count++] = cb;
}

esp_err_t get_saved_wifi_networks(wifi_credentials_t *creds, uint8_t *count) {
    if (!count) return ESP_ERR_INVALID_ARG;
    *count = s_saved_wifi_count;
    if (creds && s_saved_wifi_count > 0) {
        memcpy(creds, s_saved_wifi, s_saved_wifi_count * sizeof(wifi_credentials_t));
    }
    return ESP_OK;
}

esp_err_t save_wifi_network(wifi_credentials_t *creds) {
    if (!creds) return ESP_ERR_INVALID_ARG;
    for (uint8_t i = 0; i < s_saved_wifi_count; i++) {
        if (strcmp(s_saved_wifi[i].ssid, creds->ssid) == 0) {
            s_saved_wifi[i] = *creds;
            return ESP_OK;
        }
    }
    if (s_saved_wifi_count < 4) {
        s_saved_wifi[s_saved_wifi_count++] = *creds;
    }
    return ESP_OK;
}

esp_err_t delete_wifi_network(char *ssid) {
    if (!ssid) return ESP_ERR_INVALID_ARG;
    for (uint8_t i = 0; i < s_saved_wifi_count; i++) {
        if (strcmp(s_saved_wifi[i].ssid, ssid) == 0) {
            for (uint8_t j = i; j + 1 < s_saved_wifi_count; j++) {
                s_saved_wifi[j] = s_saved_wifi[j + 1];
            }
            s_saved_wifi_count--;
            break;
        }
    }
    return ESP_OK;
}

esp_err_t get_wifi_network_list(wifi_ap_record_t **networks, uint16_t *count) {
    if (!networks || !count) return ESP_ERR_INVALID_ARG;
    wifi_ap_record_t *list = (wifi_ap_record_t *)calloc(1, sizeof(wifi_ap_record_t));
    if (!list) return ESP_ERR_NO_MEM;
    snprintf((char *)list[0].ssid, sizeof(list[0].ssid), "%s", "MUSEUM");
    list[0].rssi = -42;
    list[0].primary = 6;
    list[0].authmode = WIFI_AUTH_OPEN;
    *networks = list;
    *count = 1;
    return ESP_OK;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap_info) {
    if (!ap_info) return ESP_ERR_INVALID_ARG;
    memset(ap_info, 0, sizeof(*ap_info));
    /* Before initial Wi-Fi credential provisioning completes, report the
     * provisioning SSID so badge.c runs provision_api() + provision_wifi().
     * Once provisioned, report "MUSEUM". */
    const char *active_ssid = se_slot_touched(SE_KEY_WIFI_SSID)
                                  ? "MUSEUM"
                                  : CONFIG_CONFERENCE_WIFI_SSID;
    snprintf((char *)ap_info->ssid, sizeof(ap_info->ssid), "%s", active_ssid);
    ap_info->rssi = -42;
    ap_info->primary = 6;
    ap_info->authmode = WIFI_AUTH_OPEN;
    return ESP_OK;
}

esp_err_t esp_wifi_get_ps(wifi_ps_type_t *type) {
    if (type) *type = s_wifi_ps;
    return ESP_OK;
}

esp_err_t esp_wifi_set_ps(wifi_ps_type_t type) {
    s_wifi_ps = type;
    return ESP_OK;
}

/* ============================================================================
 * Embedded SPIFFS (/spiffs) Virtual Filesystem
 * ============================================================================ */

static bool s_spiffs_mounted = false;

#ifndef __EMSCRIPTEN__
#define NATIVE_SPIFFS_DIR "emulator/build/2025-stock-spiffs"

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
 * 12-Function Museum Emulator Core ABI (emulator/core/emu.h)
 * ============================================================================ */

void emu_init(uint32_t seed) {
    const char *log_env = getenv("EMU25_LOG");
    s_log_enabled = (log_env && log_env[0] == '1');

    s_rng_state = ((uint64_t)(seed ? seed : 0x20255343U) << 32) ^ 0x9E3779B97F4A7C15ULL;
    memset(s_fb, 0, sizeof(s_fb));
    memset(s_leds, 0, sizeof(s_leds));
    memset(s_leds_staging, 0, sizeof(s_leds_staging));
    memset(s_eeprom, 0, sizeof(s_eeprom));
    memset(s_eeprom_shadow, 0, sizeof(s_eeprom_shadow));
    memset(s_gpio, 0, sizeof(s_gpio));
    memset(s_i2c_devs, 0, sizeof(s_i2c_devs));
    memset(s_event_subs, 0, sizeof(s_event_subs));
    memset(s_wifi_cbs, 0, sizeof(s_wifi_cbs));
    s_wifi_cb_count = 0;
    s_wifi_connect_timer = NULL;
    s_wifi_status = WIFI_STATUS_DISCONNECTED;

    s_eeprom_dirty = 0;
    s_backlight_u8 = 255;
    s_buttons = 0;
    s_nfc_gpo_level = 1;
    s_st25_it_sts = 0;
    s_st25_gpo_conf = 0xC6;
    s_st25_gpo_ctrl = 0x80;

#ifndef __EMSCRIPTEN__
    cli_fake_server_reset(seed);
#endif

    platform_storage_init(seed);
    platform_scheduler_init();
    platform_scheduler_start_app_main();
    platform_scheduler_step(20000ULL);
}

void emu_step(uint32_t ms, uint8_t buttons) {
    s_buttons = buttons;

    /* Detect external host/browser RF writes to the 512-byte ST25DV EEPROM */
    if (memcmp(s_eeprom, s_eeprom_shadow, STOCK25_EEPROM_SZ) != 0) {
        memcpy(s_eeprom_shadow, s_eeprom, STOCK25_EEPROM_SZ);
        st25dv_pulse_gpo_interrupt();
    }

    uint64_t total_us = (uint64_t)ms * 1000ULL;
    if (total_us == 0) {
        platform_scheduler_step(0);
        return;
    }

    /* Slice larger dt values into <= 5 ms ticks so LVGL & FreeRTOS tasks interleave smoothly */
    const uint64_t slice_us = 5000ULL;
    while (total_us > 0) {
        uint64_t step = (total_us > slice_us) ? slice_us : total_us;
        platform_scheduler_step(step);
        total_us -= step;
    }
}

uint16_t *emu_framebuffer(void) { return s_fb; }
uint8_t *emu_leds(void)         { return s_leds; }
uint8_t emu_backlight(void)           { return s_backlight_u8; }
uint8_t *emu_eeprom(void)             { return s_eeprom; }

int emu_eeprom_dirty(void) {
    int d = s_eeprom_dirty;
    s_eeprom_dirty = 0;
    return d;
}

void emu_set_speed(uint32_t mult) {
    s_speed_mult = mult ? mult : 1;
}

int emu_fb_width(void)    { return STOCK25_FB_W; }
int emu_fb_height(void)   { return STOCK25_FB_H; }
int emu_led_count(void)   { return STOCK25_LED_COUNT; }
int emu_eeprom_size(void) { return STOCK25_EEPROM_SZ; }
