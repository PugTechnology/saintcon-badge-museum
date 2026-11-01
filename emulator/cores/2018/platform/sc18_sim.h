#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * SAINTCON 2018 Badge Hardware & Simulation Constants
 * ============================================================================ */

#define SC18_FB_WIDTH       320
#define SC18_FB_HEIGHT      96
#define SC18_LED_COUNT      256
#define SC18_EEPROM_SIZE    4096

/* Button bitmask (matching badge.json & museum standard Core ABI) */
#define SC18_BTN_UP         (1u << 0)  /* SW1 -> GPIO 16 (active low) */
#define SC18_BTN_DOWN       (1u << 1)  /* SW2 -> GPIO 17 (active low) */
#define SC18_BTN_SELECT     (1u << 4)  /* SW3 -> GPIO 18 (active low) */

/* Minibadge I2C2 addresses polled by main.py:35 */
#define SC18_MB_SLOT_COUNT  5

typedef struct {
    uint8_t addr;               /* 7-bit I2C address: 0x01, 0x29, 0x40, 0x41, 0x42 */
    uint8_t present;            /* 1 if virtual minibadge is plugged in */
    uint8_t last_brightness;    /* Last 0x02 brightness byte written by badge */
    uint16_t last_score;        /* Last 0x01 16-bit score written by badge */
    uint32_t write_count;       /* Master->minibadge transactions */
    uint32_t poll_count;        /* Master<-minibadge status polls */

    /* Pending event response queued for next poll from main.py:minibadgeComms() */
    uint8_t pending_type;       /* 0=idle, 1=button, 2=text, 3=pixels(2s), 4=pixels(timed), 5=custom */
    uint8_t pending_arg;        /* button status byte (type 1) or delay_10ms (type 4) */
    uint8_t pending_len;        /* payload length */
    uint8_t pending_data[64];   /* ASCII text or 32-byte column bitmap */
} sc18_minibadge_t;

/* Core ABI (standard 12 functions) */
void emu_init(uint32_t seed);
void emu_step(uint32_t dt_ms, uint32_t buttons_mask);
const uint16_t *emu_framebuffer(void);
const uint8_t *emu_leds(void);
uint8_t emu_backlight(void);
uint8_t *emu_eeprom(void);
int emu_eeprom_dirty(void);
void emu_set_speed(uint32_t mult);
int emu_fb_width(void);
int emu_fb_height(void);
int emu_led_count(void);
int emu_eeprom_size(void);

/* 2018 Companion ABI (Wi-Fi AP / microWebSrv HTTP bridge / Minibadges / Score) */
int emu_2018_ap_info(char *out_json, int max_len);
int emu_2018_http_request(const uint8_t *raw_req, int req_len, uint8_t *out_resp, int max_resp);
int emu_2018_http_response_len(void);
void emu_2018_mb_set_present(int slot, int present);
void emu_2018_mb_queue_text(int slot, const char *ascii_msg);
void emu_2018_mb_queue_pixels(int slot, const uint8_t *cols32, int delay_10ms);
void emu_2018_mb_queue_button(int slot, int btn_status);
int emu_2018_mb_get_info(int slot, uint8_t *out_16b);
void emu_2018_set_cli_score(int score);

/* Internal simulation hooks called by modsc18.c */
uint32_t sc18_ticks_ms(void);
void sc18_fiber_sleep_ms(uint32_t ms);
void sc18_fiber_yield_once(void);
void sc18_vm_poll_hook(void);
const char *sc18_vfs_root(void);
void sc18_sync_mutable_fs_if_changed(void);

/* GPIO */
void sc18_gpio_init(int pin, int mode, int pull);
int sc18_gpio_read(int pin);
void sc18_gpio_write(int pin, int val);

/* I2C Bus 1 (IS31FL3730 matrix drivers @ 0x60, 0x61) & Bus 2 (Minibadges) */
int sc18_i2c1_writeto(uint8_t addr, const uint8_t *buf, size_t len);
void sc18_i2c2_start(void);
void sc18_i2c2_stop(void);
int sc18_i2c2_write(const uint8_t *buf, size_t len);
int sc18_i2c2_readinto(uint8_t *buf, size_t len, bool nack);
int sc18_i2c_scan(int bus_id, uint8_t *out_addrs, int max_addrs);

/* Network WLAN (STA_IF = 0, AP_IF = 1) */
void sc18_wlan_get_mac(uint8_t out_mac[6]);
void sc18_wlan_set_active(int iface, bool active);
bool sc18_wlan_get_active(int iface);
void sc18_wlan_sta_connect(const char *ssid, const char *password);
bool sc18_wlan_sta_isconnected(void);
void sc18_wlan_ap_config(const char *essid, const char *password, int authmode);

/* Virtual Port-80 HTTP Server Socket for microWebSrv */
void sc18_http_srv_listen(int port);
void sc18_http_srv_close(void);
int sc18_http_srv_accept_wait(void); /* returns 0 when client accepted, -1 if server closed */
int sc18_http_cli_readline(uint8_t *out_buf, int max_len);
int sc18_http_cli_read(uint8_t *out_buf, int max_len);
int sc18_http_cli_write(const uint8_t *buf, int len);
void sc18_http_cli_close(void);

/* Stand-in Hacker Challenge score HTTP GET */
int sc18_score_http_get(const char *url, const char *user_agent, char *out_body, int max_len);
