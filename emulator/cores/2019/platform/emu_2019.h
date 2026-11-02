#ifndef EMU_2019_H
#define EMU_2019_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMU_2019_MATRIX_W   64
#define EMU_2019_MATRIX_H   16
#define EMU_2019_DOT_SCALE  6
#define EMU_2019_FB_W       (EMU_2019_MATRIX_W * EMU_2019_DOT_SCALE) /* 384 */
#define EMU_2019_FB_H       (EMU_2019_MATRIX_H * EMU_2019_DOT_SCALE) /* 96  */
#define EMU_2019_LED_COUNT  26
#define EMU_2019_EEPROM_SZ  512

/* Standard museum core exports */
void emu_init(uint32_t seed);
void emu_reset(void);
void emu_step(uint32_t elapsed_ms, uint32_t buttons_mask);
void emu_set_speed(uint32_t multiplier);

const uint16_t *emu_framebuffer(void);
int emu_fb_width(void);
int emu_fb_height(void);

const uint8_t *emu_leds(void);
int emu_led_count(void);
uint8_t emu_backlight(void);

uint8_t *emu_eeprom(void);
int emu_eeprom_size(void);
int emu_eeprom_dirty(void);
void emu_eeprom_clear_dirty(void);

/* 2019 Enigma Badge specific exports */
void emu_2019_set_keys(uint32_t az_mask);
uint32_t emu_2019_get_keys(void);

void emu_2019_plugboard_clear(void);
int emu_2019_plugboard_connect(int a, int b);
int emu_2019_plugboard_disconnect(int a);
int emu_2019_plugboard_get_partner(int a);
int emu_2019_plugboard_get_state(char *out26);

int emu_2019_get_badge_level(void);
int emu_2019_get_menu_state(void);
void emu_2019_start_factory_test(void);
uint8_t emu_2019_scale_led_channel(uint8_t v);

#ifdef __cplusplus
}
#endif

#endif /* EMU_2019_H */
