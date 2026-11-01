#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generalized Multi-Year Emulator Core ABI
void      emu_init(uint32_t seed);
void      emu_step(uint32_t dt_ms, uint8_t buttons);
uint16_t *emu_framebuffer(void);
uint8_t  *emu_leds(void);
uint8_t   emu_backlight(void);
uint8_t  *emu_eeprom(void);
int       emu_eeprom_dirty(void);
void      emu_set_speed(uint32_t mult);
int       emu_fb_width(void);
int       emu_fb_height(void);
int       emu_led_count(void);
int       emu_eeprom_size(void);

// Optional CLI / debug hooks
void platform_2026_set_log_enabled(bool enabled);
void platform_2026_usb_push_str(const char *str);

#ifdef __cplusplus
}
#endif
