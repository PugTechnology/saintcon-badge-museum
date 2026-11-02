#pragma once
/*
 * Public C ABI for the SAINTCON 2017 ("HITB Minibadge 2017") emulator core.
 * Exposes the standard museum WASM ABI (`emu_init`, `emu_step`, `emu_leds`, etc.)
 * plus inspection/control helpers for the two capacitive touch pads (`A3` / `A2`)
 * and two APA102 RGB LEDs (`U2`, `U3`).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMU_2017_FB_W 1
#define EMU_2017_FB_H 1
#define EMU_2017_LED_COUNT 2
#define EMU_2017_EEPROM_SZ 0

void emu_init(uint32_t seed);
int emu_step(uint32_t elapsed_ms, uint32_t buttons_mask);
void emu_reset(void);
void emu_set_speed(uint32_t mult);
const uint16_t *emu_framebuffer(void);
int emu_fb_width(void);
int emu_fb_height(void);
const uint8_t *emu_leds(void);
int emu_led_count(void);
int emu_backlight(void);
uint8_t *emu_eeprom(void);
int emu_eeprom_size(void);
int emu_eeprom_dirty(void);
void emu_eeprom_clear_dirty(void);

/* 2017-specific helpers for CLI tests and the web companion panel */
void emu_2017_set_pad_delta(int pad_index, int delta_override);
void emu_2017_clear_pad_overrides(void);
int emu_2017_get_ref(int pad_index);
int emu_2017_get_last_raw(int pad_index);
int emu_2017_get_last_delta(int pad_index);
uint8_t emu_2017_get_raw_led_channel(int led_index, int channel);
uint8_t emu_2017_get_last_brightness(void);
uint32_t emu_2017_get_write_count(void);

#ifdef __cplusplus
}
#endif
