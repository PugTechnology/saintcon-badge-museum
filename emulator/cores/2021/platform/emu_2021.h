#ifndef SC21_EMU_2021_H
#define SC21_EMU_2021_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Generalized Badge Emulator Core ABI */
void            emu_init(uint32_t seed);
void            emu_step(uint32_t elapsed_ms, uint32_t buttons_mask);
const uint16_t *emu_framebuffer(void);
const uint8_t  *emu_leds(void);
uint8_t         emu_backlight(void);
uint8_t        *emu_eeprom(void);
int             emu_eeprom_dirty(void);
void            emu_set_speed(uint32_t multiplier);

int             emu_fb_width(void);
int             emu_fb_height(void);
int             emu_led_count(void);
int             emu_eeprom_size(void);
void            emu_set_touch(int pressed, int x, int y);

/* 2021 External 8 MB QSPI Flash & Registration vCard region (0x7f8000, 512 bytes) */
void            emu_2021_load_flash(const uint8_t *data, uint32_t len);
uint8_t        *emu_2021_vcard_ptr(void);
int             emu_2021_vcard_size(void);
int             emu_2021_vcard_dirty(void);
void            emu_2021_erase_vcard(void);

/* 2021 Combination Lock Shackle, Shim, and Direct Wheel Controls */
void            emu_2021_set_shackle(int open);
int             emu_2021_get_shackle(void);
void            emu_2021_pulse_shim(void);
void            emu_2021_set_wheel(int touched, int angle_deg);

/* 2021 Virtual ST25R95 NFC Reader/Tag, vCard Programmer/Reader, & Solo Museum Bot Trade */
int             emu_2021_nfc_quote_count(void);
const char     *emu_2021_nfc_quote_label(int index);
int             emu_2021_nfc_present_quote_tag(int index);
int             emu_2021_nfc_present_text_tag(const char *text);
void            emu_2021_nfc_clear_tag(void);

int             emu_2021_nfc_program_vcard(const char *vcard_body);
int             emu_2021_nfc_last_vcard_status(void);
int             emu_2021_nfc_read_emulated_tag(char *out_buf, int max_len);

int             emu_2021_nfc_start_bot_trade(int part0, int count0, int part1, int count1);
int             emu_2021_nfc_bot_trade_status(char *out_buf, int max_len);

/* 2021 Virtual I2C_0 Minibadge Bus (0x28) */
void            emu_2021_mb_set_present(int present);
int             emu_2021_mb_get_present(void);
void            emu_2021_mb_press_button(void);
void            emu_2021_mb_send_text(const char *ascii_text);

/* Internal crypto helpers implemented in crypto_shim.c */
void            sc21_aes128_ecb_encrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void            sc21_aes128_ecb_decrypt(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]);
void            sc21_sha1_compute(const void *data, size_t len, uint8_t digest[20]);

#ifdef __cplusplus
}
#endif

#endif /* SC21_EMU_2021_H */
