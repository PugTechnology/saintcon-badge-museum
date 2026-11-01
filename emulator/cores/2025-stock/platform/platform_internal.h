#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream entry point renamed via -Dapp_main=stock2025_app_main or called directly */
void app_main(void);

/* Virtual clock & cooperative FreeRTOS scheduler (freertos_sim.c) */
void platform_scheduler_init(void);
void platform_scheduler_start_app_main(void);
void platform_scheduler_step(uint64_t delta_us);
uint64_t platform_now_us(void);
void platform_nudge_status_sync_timer(void);

/* Crypto helpers (crypto_sim.c) */
void platform_hmac_sha256(const uint8_t *key, size_t key_len,
                          const uint8_t *msg, size_t msg_len,
                          uint8_t out_digest[32]);

/* Storage (storage_sim.c) */
void platform_storage_init(uint32_t seed);

/* Peripherals & PRNG (peripherals_sim.c) */
uint32_t platform_rand_u32(void);
void platform_nfc_inject_nut(const char *code5, const char *nut_type_str);

/* Native CLI fake server (cli_fake_server.cpp, only linked in native emu-cli) */
#ifndef __EMSCRIPTEN__
int cli_fake_server_handle(const char *method,
                           const char *url,
                           const char *body,
                           size_t body_len,
                           char **out_headers_json,
                           char **out_body,
                           size_t *out_body_len);
void cli_fake_server_reset(uint32_t seed);
void cli_fake_server_register_nut(const char *code5, const char *nut_type_str, int faction_override);
#endif

#ifdef __cplusplus
}
#endif
