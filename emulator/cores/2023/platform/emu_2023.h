#ifndef SC23_EMU_2023_H
#define SC23_EMU_2023_H

#include "../../../core/emu.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 2023-specific WASM & CLI peripheral hooks */
void emu_send_ir(uint32_t code);
void emu_set_accel(int16_t x, int16_t y, int16_t z);
void emu_set_minibadge(int slot, int catalog_index);
void emu_set_minibadge_label(int slot, const char *label8, int is_redactd);

/* Internal cooperative fiber / virtual-clock hooks */
void sc23_sched_yield_ms(uint32_t ms);
void sc23_on_gfx_flush(void);
void sc23_sync_leds(void);
void sc23_set_backlight_raw(uint8_t level);
void sc23_mark_eeprom_dirty(void);

/* Native CLI fake-server entry point (implemented in cli_fake_server.cpp) */
int sc23_cli_fake_server_handle(const char *method,
                                const char *url,
                                const char *headers,
                                const char *body,
                                char **out_body);
void sc23_cli_fake_server_reset(void);
uint32_t sc23_cli_issue_levelup_code(void);
void sc23_cli_set_bot_allies(int enabled);

#ifdef __cplusplus
}
#endif

#endif /* SC23_EMU_2023_H */
