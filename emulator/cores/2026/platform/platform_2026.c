#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "platform_2026.h"

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "pico/stdio_usb.h"
#include "pico/time.h"
#include "pico/rand.h"
#include "pico/unique_id.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/pio.h"

#include "bsp/buttons.h"
#include "bsp/config.h"
#include "bsp/bsp_display.h"
#include "bsp/bsp_st77916.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#endif

// ============================================================================
// Hardware Constants for SAINTCON 2026 ("Orion Trail" / Apollo)
// ============================================================================

#define EMU26_FB_WIDTH     360
#define EMU26_FB_HEIGHT    360
#define EMU26_LED_COUNT    9
#define EMU26_EEPROM_BYTES 1024
#define EMU26_BTN_MASK     0x7Fu

// Approximate QSPI DMA frame transfer duration (360*360*2 bytes over PIO QSPI)
#define EMU26_DMA_FRAME_US 8000ULL

_Alignas(4) static uint16_t s_fb[EMU26_FB_WIDTH * EMU26_FB_HEIGHT];
_Alignas(4) static uint8_t  s_leds[EMU26_LED_COUNT * 3];
_Alignas(4) static uint8_t  s_eeprom[EMU26_EEPROM_BYTES];

static int      s_eeprom_dirty  = 0;
static uint16_t s_eeprom_addr   = 0;
static uint8_t  s_backlight_pct = 100;
static uint8_t  s_backlight_u8  = 255;
static uint8_t  s_buttons       = 0;
static uint32_t s_speed_mult    = 1;
static uint32_t s_seed          = 0x20265343u;
static uint64_t s_rng_state     = 0x9E3779B97F4A7C15ULL;
static uint64_t s_now_us        = 10000ULL;
static uint64_t s_target_us     = 10000ULL;
static bool     s_skip_yield    = false;
static bool     s_in_timer_cb   = false;
static bool     s_log_enabled   = false;

// Upstream globals that need clean re-initialization across emu_init() resets
extern int wayfarer_main(void);
extern bool flush_done_flag;
extern char *achieve_str;
extern int game_running;
extern int crew_count;
extern bool usb_banner_printed;
extern int alert;
extern uint8_t led_pattern_index;
extern int dock_state;

// ============================================================================
// Cooperative Fiber Context (Emscripten Asyncify Fiber / POSIX ucontext)
// ============================================================================

static void game_fiber_run(void);

#ifdef __EMSCRIPTEN__
_Alignas(16) static uint8_t s_c_stack[256 * 1024];
_Alignas(16) static uint8_t s_async_stack[256 * 1024];
_Alignas(16) static uint8_t s_main_async_stack[32 * 1024];
static emscripten_fiber_t s_main_fiber;
static emscripten_fiber_t s_game_fiber;
static bool s_main_fiber_inited = false;

static void game_fiber_entry(void *arg) {
    (void)arg;
    game_fiber_run();
}

static void fiber_reset(void) {
    if (!s_main_fiber_inited) {
        emscripten_fiber_init_from_current_context(
            &s_main_fiber, s_main_async_stack, sizeof(s_main_async_stack));
        s_main_fiber_inited = true;
    }
    emscripten_fiber_init(
        &s_game_fiber,
        game_fiber_entry,
        NULL,
        s_c_stack,
        sizeof(s_c_stack),
        s_async_stack,
        sizeof(s_async_stack));
}

static void fiber_resume_game(void) {
    emscripten_fiber_swap(&s_main_fiber, &s_game_fiber);
}

static void fiber_yield_to_host(void) {
    emscripten_fiber_swap(&s_game_fiber, &s_main_fiber);
}
#else
_Alignas(16) static uint8_t s_c_stack[512 * 1024];
static ucontext_t s_main_ctx;
static ucontext_t s_game_ctx;

static void fiber_reset(void) {
    getcontext(&s_game_ctx);
    s_game_ctx.uc_stack.ss_sp   = s_c_stack;
    s_game_ctx.uc_stack.ss_size = sizeof(s_c_stack);
    s_game_ctx.uc_link          = &s_main_ctx;
    makecontext(&s_game_ctx, game_fiber_run, 0);
}

static void fiber_resume_game(void) {
    swapcontext(&s_main_ctx, &s_game_ctx);
}

static void fiber_yield_to_host(void) {
    swapcontext(&s_game_ctx, &s_main_ctx);
}
#endif

// ============================================================================
// Software Repeating Timer Scheduler
// ============================================================================

#define MAX_REPEATING_TIMERS 8

typedef struct {
    bool active;
    uint64_t period_us;
    uint64_t next_fire_us;
    struct repeating_timer *rt;
} soft_timer_t;

static soft_timer_t s_timers[MAX_REPEATING_TIMERS];

static void timers_reset(void) {
    memset(s_timers, 0, sizeof(s_timers));
}

bool cancel_repeating_timer(struct repeating_timer *timer) {
    if (!timer) {
        return false;
    }
    bool found = false;
    for (int i = 0; i < MAX_REPEATING_TIMERS; i++) {
        if (s_timers[i].active && s_timers[i].rt == timer) {
            s_timers[i].active = false;
            s_timers[i].rt     = NULL;
            found = true;
        }
    }
    return found;
}

bool add_repeating_timer_ms(
    int32_t delay_ms,
    repeating_timer_callback_t callback,
    void *user_data,
    struct repeating_timer *out) {
    if (!callback || !out) {
        return false;
    }
    cancel_repeating_timer(out);

    int64_t abs_ms = (delay_ms < 0) ? -(int64_t)delay_ms : (int64_t)delay_ms;
    if (abs_ms < 1) {
        abs_ms = 1;
    }
    uint64_t period_us = (uint64_t)abs_ms * 1000ULL;

    for (int i = 0; i < MAX_REPEATING_TIMERS; i++) {
        if (!s_timers[i].active) {
            out->delay_us  = (int64_t)period_us;
            out->callback  = callback;
            out->user_data = user_data;
            out->slot_id   = i;

            s_timers[i].active       = true;
            s_timers[i].period_us    = period_us;
            s_timers[i].next_fire_us = s_now_us + period_us;
            s_timers[i].rt           = out;
            return true;
        }
    }
    return false;
}

static void advance_clock_and_fire_timers(uint64_t delta_us) {
    uint64_t end_us = s_now_us + delta_us;
    while (true) {
        int earliest_idx = -1;
        uint64_t earliest_us = end_us + 1ULL;
        for (int i = 0; i < MAX_REPEATING_TIMERS; i++) {
            if (s_timers[i].active && s_timers[i].next_fire_us <= end_us) {
                if (s_timers[i].next_fire_us < earliest_us) {
                    earliest_us  = s_timers[i].next_fire_us;
                    earliest_idx = i;
                }
            }
        }
        if (earliest_idx < 0) {
            break;
        }

        s_now_us = earliest_us;
        struct repeating_timer *rt = s_timers[earliest_idx].rt;
        uint64_t period = s_timers[earliest_idx].period_us;
        s_timers[earliest_idx].next_fire_us = s_now_us + period;

        if (rt && rt->callback) {
            bool prev_cb = s_in_timer_cb;
            s_in_timer_cb = true;
            bool keep = rt->callback(rt);
            s_in_timer_cb = prev_cb;
            if (!keep && s_timers[earliest_idx].rt == rt) {
                s_timers[earliest_idx].active = false;
                s_timers[earliest_idx].rt     = NULL;
            }
        }
    }
    s_now_us = end_us;
}

static void consume_virtual_us(uint64_t duration_us) {
    if (s_skip_yield || s_in_timer_cb) {
        s_now_us += duration_us;
        return;
    }
    uint64_t remaining = duration_us;
    if (remaining == 0) {
        if (s_now_us >= s_target_us) {
            fiber_yield_to_host();
        }
        return;
    }
    while (remaining > 0) {
        if (s_now_us >= s_target_us) {
            fiber_yield_to_host();
            continue;
        }
        uint64_t budget = s_target_us - s_now_us;
        uint64_t slice  = (remaining < budget) ? remaining : budget;
        advance_clock_and_fire_timers(slice);
        remaining -= slice;
    }
}

uint64_t time_us_64(void) {
    return s_now_us;
}

absolute_time_t get_absolute_time(void) {
    return s_now_us;
}

uint32_t to_ms_since_boot(absolute_time_t t) {
    return (uint32_t)(t / 1000ULL);
}

int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to - from);
}

absolute_time_t make_timeout_time_ms(uint32_t ms) {
    return s_now_us + (uint64_t)ms * 1000ULL;
}

void sleep_us(uint64_t us) {
    consume_virtual_us(us);
}

void sleep_ms(uint32_t ms) {
    consume_virtual_us((uint64_t)ms * 1000ULL);
}

// ============================================================================
// PRNG & Unique Board ID
// ============================================================================

uint32_t get_rand_32(void) {
    // SplitMix64 -> 32-bit output
    s_rng_state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = s_rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z = z ^ (z >> 31);
    return (uint32_t)(z & 0xFFFFFFFFu);
}

uint64_t get_rand_64(void) {
    uint64_t hi = (uint64_t)get_rand_32();
    uint64_t lo = (uint64_t)get_rand_32();
    return (hi << 32) | lo;
}

void pico_get_unique_board_id(pico_unique_board_id_t *id_out) {
    if (!id_out) {
        return;
    }
    static const uint8_t k_default_uid[PICO_UNIQUE_BOARD_ID_SIZE_BYTES] = {
        0x53, 0x43, 0x32, 0x36, 0x4F, 0x52, 0x4E, 0x01
    };
    memcpy(id_out->id, k_default_uid, PICO_UNIQUE_BOARD_ID_SIZE_BYTES);
}

// ============================================================================
// GPIO (Active-Low Buttons + GPIO29 Offline Mode)
// ============================================================================

void gpio_init(uint gpio) { (void)gpio; }
void gpio_set_dir(uint gpio, bool out) { (void)gpio; (void)out; }
void gpio_pull_up(uint gpio) { (void)gpio; }
void gpio_pull_down(uint gpio) { (void)gpio; }
void gpio_put(uint gpio, bool value) { (void)gpio; (void)value; }
void gpio_set_function(uint gpio, gpio_function_t fn) { (void)gpio; (void)fn; }

bool gpio_get(uint gpio) {
    // Buttons are active-low with internal pull-ups (0 = pressed, 1 = released).
    // GPIO_OFFLINE (GPIO 29, JP2) is pulled low (0) when no dock station is attached,
    // causing Wayfarer.c to select offline_mode = true.
    switch (gpio) {
        case GPIO_JOY_UP:    return (s_buttons & BUTTON_UP) == 0;
        case GPIO_JOY_DOWN:  return (s_buttons & BUTTON_DOWN) == 0;
        case GPIO_JOY_LEFT:  return (s_buttons & BUTTON_LEFT) == 0;
        case GPIO_JOY_RIGHT: return (s_buttons & BUTTON_RIGHT) == 0;
        case GPIO_JOY_PRESS: return (s_buttons & BUTTON_PRESS) == 0;
        case GPIO_BTN1:      return (s_buttons & BUTTON_B) == 0;
        case GPIO_BTN2:      return (s_buttons & BUTTON_A) == 0;
        case GPIO_OFFLINE:   return false; // 0 = offline mode selected
        default:             return true;
    }
}

// ============================================================================
// I2C & 24CS08 1 KB EEPROM (0x50..0x53 10-bit block addressing)
// ============================================================================

i2c_inst_t emu_i2c0_inst = { 0 };
i2c_inst_t emu_i2c1_inst = { 1 };

uint i2c_init(i2c_inst_t *i2c, uint baudrate) {
    (void)i2c;
    return baudrate;
}

int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop) {
    (void)i2c;
    (void)nostop;
    if (addr < 0x50 || addr > 0x53 || !src || len == 0) {
        return -1;
    }
    uint16_t block_hi = (uint16_t)(addr - 0x50u) << 8;
    s_eeprom_addr = (block_hi | (uint16_t)src[0]) & (EMU26_EEPROM_BYTES - 1u);

    if (len > 1) {
        for (size_t i = 1; i < len; i++) {
            uint8_t val = src[i];
            if (s_eeprom[s_eeprom_addr] != val) {
                s_eeprom[s_eeprom_addr] = val;
                s_eeprom_dirty = 1;
            }
            s_eeprom_addr = (uint16_t)((s_eeprom_addr + 1u) & (EMU26_EEPROM_BYTES - 1u));
        }
    }
    return (int)len;
}

int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop) {
    (void)i2c;
    (void)nostop;
    if (addr < 0x50 || addr > 0x53 || !dst || len == 0) {
        return -1;
    }
    for (size_t i = 0; i < len; i++) {
        dst[i] = s_eeprom[s_eeprom_addr];
        s_eeprom_addr = (uint16_t)((s_eeprom_addr + 1u) & (EMU26_EEPROM_BYTES - 1u));
    }
    return (int)len;
}

// ============================================================================
// PIO & 9x WS2812C RGB LEDs (GPIO 24, PIO0 SM1)
// ============================================================================

pio_hw_t emu_pio0_hw;
pio_hw_t emu_pio1_hw;
static uint32_t s_ws2812_write_idx = 0;

uint pio_add_program(PIO pio, const pio_program_t *program) {
    (void)pio;
    (void)program;
    return 0;
}

void pio_sm_set_enabled(PIO pio, uint sm, bool enabled) {
    (void)pio;
    (void)sm;
    (void)enabled;
}

static uint8_t scale_ws2812_channel(uint8_t c) {
    // Firmware drives WS2812C at low indoor duty (16/255 normal, 32/255 alert).
    // Map linear LED PWM to visible sRGB brightness while preserving 0=off and hue ratios.
    if (c == 0) {
        return 0;
    }
    uint32_t scaled = (uint32_t)c * 9u + 32u;
    return (scaled > 255u) ? 255u : (uint8_t)scaled;
}

void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data) {
    (void)pio;
    if (sm != 1u) {
        return;
    }
    uint32_t grb = data >> 8u;
    uint8_t g = (uint8_t)((grb >> 16) & 0xFFu);
    uint8_t r = (uint8_t)((grb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(grb & 0xFFu);

    uint32_t idx = s_ws2812_write_idx;
    s_leds[idx * 3u + 0u] = scale_ws2812_channel(r);
    s_leds[idx * 3u + 1u] = scale_ws2812_channel(g);
    s_leds[idx * 3u + 2u] = scale_ws2812_channel(b);

    s_ws2812_write_idx = (idx + 1u) % EMU26_LED_COUNT;
}

// ============================================================================
// ST77916 360x360 QSPI Display Replacement
// ============================================================================

static channel_irq_callback_t s_dma_done_cb = NULL;
static uint16_t s_display_rotation = 0;

static void st77916_emu_init(void) {
    // Allow 40 ms power-on reset window so holding BUTTON_B at boot triggers self-test
    sleep_ms(40);
}

static void st77916_emu_reset(void) {}

static void st77916_emu_set_rotation(uint16_t rotation) {
    s_display_rotation = rotation;
}

static uint16_t st77916_emu_get_rotation(void) {
    return s_display_rotation;
}

static void st77916_emu_set_brightness(uint8_t pct) {
    if (pct > 100u) {
        pct = 100u;
    }
    s_backlight_pct = pct;
    s_backlight_u8  = (uint8_t)(((uint32_t)pct * 255u + 50u) / 100u);
}

static uint8_t st77916_emu_get_brightness(void) {
    return s_backlight_pct;
}

static void st77916_emu_set_window(bsp_display_area_t *area) {
    (void)area;
}

static void st77916_emu_copy_fb(uint16_t *color_p) {
    if (!color_p) {
        return;
    }
    const size_t total = (size_t)EMU26_FB_WIDTH * (size_t)EMU26_FB_HEIGHT;
    for (size_t i = 0; i < total; i++) {
        // Upstream framebuffer.c stores big-endian RGB565 for QSPI DMA;
        // convert back to little-endian RGB565 for the Core ABI.
        s_fb[i] = __builtin_bswap16(color_p[i]);
    }
}

static void st77916_emu_flush(bsp_display_area_t *area, uint16_t *color_p) {
    (void)area;
    st77916_emu_copy_fb(color_p);
}

static void st77916_emu_flush_dma(bsp_display_area_t *area, uint16_t *color_p) {
    (void)area;
    st77916_emu_copy_fb(color_p);
    if (s_dma_done_cb) {
        s_dma_done_cb();
    }
    // Pace tight rendering loops that don't call sleep_ms() (bootscreen, menus, ship anim)
    consume_virtual_us(EMU26_DMA_FRAME_US);
}

static bsp_display_interface_t s_st77916_if = {
    .init           = st77916_emu_init,
    .reset          = st77916_emu_reset,
    .set_rotation   = st77916_emu_set_rotation,
    .set_brightness = st77916_emu_set_brightness,
    .set_window     = st77916_emu_set_window,
    .get_rotation   = st77916_emu_get_rotation,
    .get_brightness = st77916_emu_get_brightness,
    .flush          = st77916_emu_flush,
    .flush_dma      = st77916_emu_flush_dma,
};

bool bsp_display_new_st77916(bsp_display_interface_t **interface, bsp_display_info_t *info) {
    if (!interface || !info) {
        return false;
    }
    s_dma_done_cb = info->dma_flush_done_cb;
    st77916_emu_set_brightness(info->brightness);
    *interface = &s_st77916_if;
    return true;
}

// ============================================================================
// USB CDC Console (forwarded to browser console / CLI log) & UART0 Dock Stub
// ============================================================================

uart_inst_t emu_uart0_inst = { 0 };
uart_inst_t emu_uart1_inst = { 1 };

uint uart_init(uart_inst_t *uart, uint baudrate) {
    (void)uart;
    return baudrate;
}

void uart_set_irq_enables(uart_inst_t *uart, bool rx_has_data, bool tx_needs_data) {
    (void)uart;
    (void)rx_has_data;
    (void)tx_needs_data;
}

bool uart_is_readable(uart_inst_t *uart) {
    (void)uart;
    return false; // No dock station attached
}

char uart_getc(uart_inst_t *uart) {
    (void)uart;
    return 0;
}

void uart_putc(uart_inst_t *uart, char c) {
    (void)uart;
    (void)c;
}

void uart_write_blocking(uart_inst_t *uart, const uint8_t *src, size_t len) {
    (void)uart;
    (void)src;
    (void)len;
}

void irq_set_exclusive_handler(uint num, irq_handler_t handler) {
    (void)num;
    (void)handler;
}

void irq_set_enabled(uint num, bool enabled) {
    (void)num;
    (void)enabled;
}

static char s_usb_line_buf[256];
static size_t s_usb_line_len = 0;

static char s_usb_rx_fifo[256];
static size_t s_usb_rx_head = 0;
static size_t s_usb_rx_tail = 0;
static void (*s_usb_rx_cb)(void *) = NULL;
static void *s_usb_rx_cb_param     = NULL;

static void usb_flush_line(void) {
    if (s_usb_line_len == 0) {
        return;
    }
    s_usb_line_buf[s_usb_line_len] = '\0';
#ifdef __EMSCRIPTEN__
    EM_ASM({
        console.log("[badge-usb]", UTF8ToString($0));
    }, s_usb_line_buf);
#else
    if (s_log_enabled) {
        fprintf(stderr, "[badge-usb] %s\n", s_usb_line_buf);
    }
#endif
    s_usb_line_len = 0;
}

static void usb_emit_char(char c) {
    if (c == '\0' || c == '\r') {
        return;
    }
    if (c == '\n') {
        usb_flush_line();
        return;
    }
    if (s_usb_line_len + 1 < sizeof(s_usb_line_buf)) {
        s_usb_line_buf[s_usb_line_len++] = c;
    } else {
        usb_flush_line();
    }
}

bool stdio_init_all(void) { return true; }
bool stdio_usb_init(void) { return true; }
bool stdio_usb_connected(void) { return true; }

int stdio_putchar(int c) {
    usb_emit_char((char)c);
    return c;
}

int stdio_puts(const char *s) {
    if (!s) {
        return 0;
    }
    int count = 0;
    while (*s) {
        usb_emit_char(*s++);
        count++;
    }
    usb_flush_line();
    return count;
}

int stdio_printf(const char *fmt, ...) {
    if (!fmt) {
        return 0;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (int i = 0; i < n && i < (int)sizeof(buf) - 1; i++) {
        usb_emit_char(buf[i]);
    }
    return n;
}

int getchar_timeout_us(uint32_t timeout_us) {
    (void)timeout_us;
    if (s_usb_rx_head == s_usb_rx_tail) {
        return PICO_ERROR_TIMEOUT;
    }
    char c = s_usb_rx_fifo[s_usb_rx_head];
    s_usb_rx_head = (s_usb_rx_head + 1u) % sizeof(s_usb_rx_fifo);
    return (unsigned char)c;
}

void stdio_set_chars_available_callback(void (*fn)(void *), void *param) {
    s_usb_rx_cb       = fn;
    s_usb_rx_cb_param = param;
}

void platform_2026_usb_push_str(const char *str) {
    if (!str) {
        return;
    }
    while (*str) {
        size_t next = (s_usb_rx_tail + 1u) % sizeof(s_usb_rx_fifo);
        if (next != s_usb_rx_head) {
            s_usb_rx_fifo[s_usb_rx_tail] = *str;
            s_usb_rx_tail = next;
        }
        str++;
    }
    if (s_usb_rx_cb) {
        s_usb_rx_cb(s_usb_rx_cb_param);
    }
}

void platform_2026_set_log_enabled(bool enabled) {
    s_log_enabled = enabled;
}

// ============================================================================
// Game Fiber Entry & Generalized Core ABI
// ============================================================================

static bool eeprom_is_blank(void) {
    for (size_t i = 0; i < EMU26_EEPROM_BYTES; i++) {
        if (s_eeprom[i] != 0u) {
            return false;
        }
    }
    return true;
}

static void game_fiber_run(void) {
    // If EEPROM is completely blank (first visit or after Wipe Save), initialize
    // a valid factory pilot profile via the firmware's own config_clear() routine
    // so the badge boots into the Orion Trail title screen like an attendee badge.
    if (eeprom_is_blank()) {
        s_skip_yield = true;
        config_clear();
        s_skip_yield = false;
    }

    (void)wayfarer_main();

    while (true) {
        fiber_yield_to_host();
    }
}

void emu_init(uint32_t seed) {
    // Note: s_eeprom is intentionally NOT zeroed here because the host loads
    // persisted EEPROM bytes into emu_eeprom() prior to calling emu_init(seed).
    memset(s_fb, 0, sizeof(s_fb));
    memset(s_leds, 0, sizeof(s_leds));
    s_eeprom_dirty     = 0;
    s_eeprom_addr      = 0;
    s_backlight_pct    = 100;
    s_backlight_u8     = 255;
    s_buttons          = 0;
    s_speed_mult       = 1;
    s_seed             = (seed != 0u) ? seed : 0x20265343u;
    s_rng_state        = ((uint64_t)s_seed << 32) ^ 0x9E3779B97F4A7C15ULL;
    s_now_us           = 10000ULL;
    s_target_us        = 10000ULL;
    s_skip_yield       = false;
    s_in_timer_cb      = false;
    s_ws2812_write_idx = 0;
    s_usb_line_len     = 0;
    s_usb_rx_head      = 0;
    s_usb_rx_tail      = 0;
    s_usb_rx_cb        = NULL;
    s_usb_rx_cb_param  = NULL;

    timers_reset();

    // Reset upstream firmware globals for clean reboots
    flush_done_flag    = true;
    achieve_str        = NULL;
    game_running       = 0;
    crew_count         = 0;
    usb_banner_printed = false;
    alert              = 0;
    led_pattern_index  = 0;
    dock_state         = 0;

    fiber_reset();
}

void emu_step(uint32_t dt_ms, uint8_t buttons) {
    s_buttons = buttons & EMU26_BTN_MASK;
    uint64_t step_us = (dt_ms == 0u) ? 1000ULL : ((uint64_t)dt_ms * 1000ULL);
    // Cap effective per-step multiplier so interactive menus/minigames never stall
    uint64_t eff_mult = (s_speed_mult > 4u) ? 4u : (uint64_t)s_speed_mult;
    s_target_us = s_now_us + step_us * eff_mult;
    fiber_resume_game();
}

uint16_t *emu_framebuffer(void) {
    return s_fb;
}

uint8_t *emu_leds(void) {
    return s_leds;
}

uint8_t emu_backlight(void) {
    return s_backlight_u8;
}

uint8_t *emu_eeprom(void) {
    return s_eeprom;
}

int emu_eeprom_dirty(void) {
    int d = s_eeprom_dirty;
    s_eeprom_dirty = 0;
    return d;
}

void emu_set_speed(uint32_t mult) {
    if (mult < 1u) {
        mult = 1u;
    } else if (mult > 600u) {
        mult = 600u;
    }
    s_speed_mult = mult;
}

int emu_fb_width(void) {
    return EMU26_FB_WIDTH;
}

int emu_fb_height(void) {
    return EMU26_FB_HEIGHT;
}

int emu_led_count(void) {
    return EMU26_LED_COUNT;
}

int emu_eeprom_size(void) {
    return EMU26_EEPROM_BYTES;
}
