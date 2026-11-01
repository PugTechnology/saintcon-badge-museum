#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Upstream entry point */
void app_main(void);

/* Virtual clock & cooperative FreeRTOS scheduler (freertos_sim.c) */
void platform_scheduler_init(void);
void platform_scheduler_start_app_main(void);
void platform_scheduler_step(uint64_t delta_us);
uint64_t platform_now_us(void);

/* Storage (storage_sim.c) */
void platform_storage_init(uint32_t seed);

/* Peripherals & PRNG (peripherals_sim.c) */
uint32_t platform_rand_u32(void);
void platform_schedule_badge_sync(uint64_t delay_us);

/* 2024 Core ABI extensions for capacitive touch, IR tower beacons, and accelerometer */
void emu_set_touch(int pressed, int x, int y);
void emu_send_ir(uint32_t code);
void emu_set_accel(int16_t x, int16_t y, int16_t z);

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
uint32_t cli_fake_server_tower_ir_code(int tower_id);
uint32_t cli_fake_server_beacon_ir_code(const char *kind);
#endif

#ifdef __cplusplus
}
#endif
