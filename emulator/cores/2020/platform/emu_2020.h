#ifndef SAINTCON_2020_EMU_H
#define SAINTCON_2020_EMU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMU_2020_FB_W       160
#define EMU_2020_FB_H       120
#define EMU_2020_LED_COUNT  8
#define EMU_2020_EEPROM_SZ  512

/* Standard Museum Core Exports */
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

/* 2020-Specific Exports (PORTA/PORTB register hooks, TP1 boot switch, PWM duty) */
void emu_2020_set_tp1_boot_low(int shorted);
int emu_2020_get_tp1_boot_low(void);
int emu_2020_get_in_special_mode(void);
uint8_t emu_2020_get_porta(void);
uint8_t emu_2020_get_portb(void);
int emu_2020_get_led_duty(int index);
int emu_2020_get_tp_pulse_count(void);
uint16_t emu_2020_get_millis(void);

/* Internal hooks called by the AVR register shims */
void emu_2020_on_porta_write(uint8_t val);
void emu_2020_on_portb_write(uint8_t val);
uint8_t emu_2020_read_pinb(void);
uint16_t emu_2020_read_millis(volatile const uint16_t *ptr);
void emu_2020_delay_ms(uint32_t ms);
void emu_2020_sei(void);
void emu_2020_cli(void);

#ifdef __cplusplus
}
#endif

#endif /* SAINTCON_2020_EMU_H */
