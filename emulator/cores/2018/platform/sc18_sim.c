#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "sc18_sim.h"
#include "sc18_assets.h"

#include "py/builtin.h"
#include "py/compile.h"
#include "py/gc.h"
#include "py/mperrno.h"
#include "py/mphal.h"
#include "py/mpstate.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "py/stackctrl.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <setjmp.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#endif

/* ============================================================================
 * Cooperative Multi-Fiber Scheduler & MicroPython Thread Port
 * ============================================================================ */

#define MAX_FIBERS          8
#define FIBER_C_STACK_SZ    (256 * 1024)
#define FIBER_AS_STACK_SZ   (128 * 1024)
#define MPY_HEAP_SIZE       (8 * 1024 * 1024)

typedef enum {
    FIBER_UNUSED = 0,
    FIBER_READY,
    FIBER_SLEEPING,
    FIBER_WAIT_HTTP,
    FIBER_WAIT_MUTEX,
    FIBER_ZOMBIE,
} fiber_state_t;

typedef struct {
    fiber_state_t state;
    void *(*entry)(void *);
    void *arg;
    struct _mp_state_thread_t *mp_ts;
    void *c_stack_base;
    void *c_stack_top;
    void *last_sp;
    uint64_t wake_ms;
    uint32_t spin_polls;
    bool yielded_zero_this_ms;
    mp_thread_mutex_t *wait_mutex;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_t fiber;
#else
    ucontext_t ctx;
#endif
} sc18_fiber_t;

static sc18_fiber_t s_fibers[MAX_FIBERS];
_Alignas(16) static uint8_t s_fiber_c_stacks[MAX_FIBERS][FIBER_C_STACK_SZ];
_Alignas(16) static uint8_t s_mpy_heap[MPY_HEAP_SIZE];

#ifdef __EMSCRIPTEN__
_Alignas(16) static uint8_t s_fiber_as_stacks[MAX_FIBERS][FIBER_AS_STACK_SZ];
_Alignas(16) static uint8_t s_host_as_stack[32 * 1024];
static emscripten_fiber_t s_host_fiber;
static bool s_host_fiber_inited = false;
#else
static ucontext_t s_host_ctx;
#endif

static int s_cur_fiber = -1;
static uint64_t s_now_ms = 0;
static uint32_t s_rng_state = 0x20185C18u;
static uint32_t s_speed_mult = 1;
static bool s_vm_started = false;
static bool s_in_http_exchange = false;
#ifdef __EMSCRIPTEN__
static bool s_wasm_gc_pending = false;
#endif

static void switch_to_host(void) {
    int idx = s_cur_fiber;
    if (idx < 0) {
        return;
    }
    volatile uintptr_t sp_marker = 0;
    s_fibers[idx].last_sp = (void *)&sp_marker;
    s_cur_fiber = -1;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&s_fibers[idx].fiber, &s_host_fiber);
#else
    swapcontext(&s_fibers[idx].ctx, &s_host_ctx);
#endif
}

static void switch_to_fiber(int idx) {
#ifdef __EMSCRIPTEN__
    if (!s_host_fiber_inited) {
        emscripten_fiber_init_from_current_context(&s_host_fiber, s_host_as_stack, sizeof(s_host_as_stack));
        s_host_fiber_inited = true;
    }
#endif
    s_cur_fiber = idx;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&s_host_fiber, &s_fibers[idx].fiber);
#else
    swapcontext(&s_host_ctx, &s_fibers[idx].ctx);
#endif
}

#ifdef __EMSCRIPTEN__
static void fiber_entry_em(void *arg) {
    int idx = (int)(intptr_t)arg;
    sc18_fiber_t *f = &s_fibers[idx];
    if (f->entry) {
        f->entry(f->arg);
    }
    f->state = FIBER_ZOMBIE;
    s_cur_fiber = -1;
    emscripten_fiber_swap(&f->fiber, &s_host_fiber);
}
#else
static void fiber_entry_native(int idx) {
    sc18_fiber_t *f = &s_fibers[idx];
    if (f->entry) {
        f->entry(f->arg);
    }
    f->state = FIBER_ZOMBIE;
    s_cur_fiber = -1;
    setcontext(&s_host_ctx);
}
#endif

static int spawn_fiber(void *(*entry)(void *), void *arg, struct _mp_state_thread_t *initial_ts) {
    int idx = -1;
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (s_fibers[i].state == FIBER_UNUSED || s_fibers[i].state == FIBER_ZOMBIE) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return -1;
    }

    sc18_fiber_t *f = &s_fibers[idx];
    memset(f, 0, sizeof(*f));
    f->state = FIBER_READY;
    f->entry = entry;
    f->arg = arg;
    f->mp_ts = initial_ts;
    f->c_stack_base = s_fiber_c_stacks[idx];
    f->c_stack_top = s_fiber_c_stacks[idx] + FIBER_C_STACK_SZ;
    f->last_sp = f->c_stack_top;

#ifdef __EMSCRIPTEN__
    emscripten_fiber_init(
        &f->fiber,
        fiber_entry_em,
        (void *)(intptr_t)idx,
        s_fiber_c_stacks[idx],
        FIBER_C_STACK_SZ,
        s_fiber_as_stacks[idx],
        FIBER_AS_STACK_SZ
    );
#else
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp = s_fiber_c_stacks[idx];
    f->ctx.uc_stack.ss_size = FIBER_C_STACK_SZ;
    f->ctx.uc_link = &s_host_ctx;
    makecontext(&f->ctx, (void (*)(void))fiber_entry_native, 1, idx);
#endif
    return idx;
}

/* MicroPython mpthreadport implementation */
struct _mp_state_thread_t *mp_thread_get_state(void) {
    if (s_cur_fiber >= 0 && s_fibers[s_cur_fiber].mp_ts != NULL) {
        return s_fibers[s_cur_fiber].mp_ts;
    }
    return &mp_state_ctx.thread;
}

void mp_thread_set_state(struct _mp_state_thread_t *state) {
    if (s_cur_fiber >= 0) {
        s_fibers[s_cur_fiber].mp_ts = state;
    }
}

mp_uint_t mp_thread_create(void *(*entry)(void *), void *arg, size_t *stack_size) {
    if (*stack_size == 0) {
        *stack_size = FIBER_C_STACK_SZ;
    }
    int idx = spawn_fiber(entry, arg, NULL);
    if (idx < 0) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("no free fibers"));
    }
    return (mp_uint_t)(idx + 1);
}

mp_uint_t mp_thread_get_id(void) {
    return (mp_uint_t)(s_cur_fiber >= 0 ? (s_cur_fiber + 1) : 1);
}

void mp_thread_start(void) {
}

void mp_thread_finish(void) {
    if (s_cur_fiber >= 0) {
        s_fibers[s_cur_fiber].state = FIBER_ZOMBIE;
    }
}

void mp_thread_mutex_init(mp_thread_mutex_t *mutex) {
    mutex->locked = 0;
    mutex->owner_fiber = -1;
}

int mp_thread_mutex_lock(mp_thread_mutex_t *mutex, int wait) {
    while (mutex->locked) {
        if (!wait || s_cur_fiber < 0) {
            return 0;
        }
        s_fibers[s_cur_fiber].state = FIBER_WAIT_MUTEX;
        s_fibers[s_cur_fiber].wait_mutex = mutex;
        switch_to_host();
    }
    mutex->locked = 1;
    mutex->owner_fiber = s_cur_fiber;
    return 1;
}

void mp_thread_mutex_unlock(mp_thread_mutex_t *mutex) {
    mutex->locked = 0;
    mutex->owner_fiber = -1;
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (s_fibers[i].state == FIBER_WAIT_MUTEX && s_fibers[i].wait_mutex == mutex) {
            s_fibers[i].wait_mutex = NULL;
            s_fibers[i].state = FIBER_READY;
            break;
        }
    }
}

static void scan_ptr_range(const void *p_a, const void *p_b) {
    uintptr_t lo = (uintptr_t)p_a;
    uintptr_t hi = (uintptr_t)p_b;
    if (lo > hi) {
        uintptr_t tmp = lo;
        lo = hi;
        hi = tmp;
    }
    lo &= ~(uintptr_t)(sizeof(void *) - 1);
    if (hi > lo) {
        size_t n_ptrs = (hi - lo) / sizeof(void *);
        if (n_ptrs > 0) {
            gc_collect_root((void **)lo, n_ptrs);
        }
    }
}

void mp_thread_gc_others(void) {
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (i == s_cur_fiber) {
            continue;
        }
        if (s_fibers[i].state == FIBER_UNUSED || s_fibers[i].state == FIBER_ZOMBIE) {
            continue;
        }
        scan_ptr_range(s_fibers[i].last_sp, s_fibers[i].c_stack_top);
#ifdef __EMSCRIPTEN__
        if (s_fibers[i].fiber.asyncify_data.stack_ptr != NULL) {
            scan_ptr_range(s_fiber_as_stacks[i], s_fibers[i].fiber.asyncify_data.stack_ptr);
        }
#endif
        if (s_fibers[i].mp_ts && s_fibers[i].mp_ts != &mp_state_ctx.thread) {
            void **root_start = (void **)&s_fibers[i].mp_ts->dict_locals;
            size_t root_words = (sizeof(mp_state_thread_t) - offsetof(mp_state_thread_t, dict_locals)) / sizeof(void *);
            gc_collect_root(root_start, root_words);
        }
    }
}

#ifdef __EMSCRIPTEN__
/* In Emscripten Asyncify fibers, emscripten_scan_registers() invokes Asyncify.handleSleep
 * + setTimeout(..., 0), which cannot be nested inside emscripten_fiber_swap().
 * Instead, yield the requesting fiber via switch_to_host() so all WASM locals/registers
 * spill synchronously into s_fiber_as_stacks[], then run GC from s_host_fiber. */
static void sc18_run_host_gc(void) {
    s_wasm_gc_pending = false;
    gc_collect_start();
    mp_thread_gc_others(); /* s_cur_fiber == -1, so this scans ALL active fibers */
    gc_collect_end();
}
#endif

void gc_collect(void) {
#ifdef __EMSCRIPTEN__
    if (s_cur_fiber >= 0) {
        s_wasm_gc_pending = true;
        s_fibers[s_cur_fiber].state = FIBER_READY;
        switch_to_host();
        return;
    }
    sc18_run_host_gc();
#else
    gc_collect_start();
    jmp_buf regs;
    setjmp(regs);
    if (s_cur_fiber >= 0) {
        scan_ptr_range(&regs, s_fibers[s_cur_fiber].c_stack_top);
    }
    if (s_cur_fiber >= 0 && s_fibers[s_cur_fiber].mp_ts && s_fibers[s_cur_fiber].mp_ts != &mp_state_ctx.thread) {
        void **root_start = (void **)&s_fibers[s_cur_fiber].mp_ts->dict_locals;
        size_t root_words = (sizeof(mp_state_thread_t) - offsetof(mp_state_thread_t, dict_locals)) / sizeof(void *);
        gc_collect_root(root_start, root_words);
    }
    mp_thread_gc_others();
    gc_collect_end();
#endif
}

void nlr_jump_fail(void *val) {
    (void)val;
    fprintf(stderr, "FATAL: MicroPython nlr_jump_fail\n");
    abort();
}

void NORETURN __fatal_error(const char *msg) {
    fprintf(stderr, "FATAL: MicroPython %s\n", msg ? msg : "");
    abort();
}

/* ============================================================================
 * MicroPython HAL Timing & Cooperative Yield Hooks
 * ============================================================================ */

static void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    (void)fwrite(str, 1, len, stderr);
}

const mp_print_t mp_stderr_print = { NULL, stderr_print_strn };

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    /* Suppress noisy boot/polling stdout unless SC18_VERBOSE=1 is set */
    static int s_verbose = -1;
    if (s_verbose < 0) {
        const char *v = getenv("SC18_VERBOSE");
        s_verbose = (v && v[0] == '1') ? 1 : 0;
    }
    if (s_verbose) {
        (void)fwrite(str, 1, len, stdout);
    }
    return len;
}

uint32_t sc18_ticks_ms(void) {
    return (uint32_t)(s_now_ms & 0x3fffffffULL);
}

void sc18_fiber_sleep_ms(uint32_t ms) {
    if (s_cur_fiber < 0) {
        return;
    }
    sc18_fiber_t *f = &s_fibers[s_cur_fiber];
    f->spin_polls = 0;
    if (ms == 0) {
        if (f->yielded_zero_this_ms) {
            f->state = FIBER_SLEEPING;
            f->wake_ms = s_now_ms + 1;
        } else {
            f->yielded_zero_this_ms = true;
            f->state = FIBER_READY;
        }
    } else {
        f->state = FIBER_SLEEPING;
        f->wake_ms = s_now_ms + ms;
    }
    switch_to_host();
}

void sc18_fiber_yield_once(void) {
    sc18_fiber_sleep_ms(0);
}

void sc18_vm_poll_hook(void) {
    if (s_cur_fiber < 0) {
        return;
    }
    sc18_fiber_t *f = &s_fibers[s_cur_fiber];
    f->spin_polls++;
    /* Allow generous budget during t==0 boot imports, then cap tight spin loops
     * (like main.py:563 `while 1: if wifiConfigMode == 1:`) to 48 polls (12k branches)
     * before sleeping 5 virtual ms. */
    uint32_t limit = (s_now_ms == 0 || s_in_http_exchange) ? 8192u : 48u;
    if (f->spin_polls >= limit) {
        f->spin_polls = 0;
        f->state = FIBER_SLEEPING;
        f->wake_ms = s_now_ms + ((s_now_ms == 0 || s_in_http_exchange) ? 1ULL : 5ULL);
        switch_to_host();
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    sc18_fiber_sleep_ms((uint32_t)ms);
}

void mp_hal_delay_us(mp_uint_t us) {
    uint32_t ms = (uint32_t)((us + 999u) / 1000u);
    sc18_fiber_sleep_ms(ms > 0 ? ms : 1u);
}

mp_uint_t mp_hal_ticks_ms(void) {
    return (mp_uint_t)sc18_ticks_ms();
}

mp_uint_t mp_hal_ticks_us(void) {
    return (mp_uint_t)((s_now_ms * 1000ULL) & 0x3fffffffULL);
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return 0;
}

uint64_t mp_hal_time_ms(void) {
    /* Anchor virtual epoch at SAINTCON 2018 opening day: 2018-10-23T09:00:00Z */
    return 1540285200000ULL + s_now_ms;
}

uint64_t mp_hal_time_ns(void) {
    return mp_hal_time_ms() * 1000000ULL;
}

extern int mp_interrupt_char;
int mp_hal_get_interrupt_char(void) {
    return mp_interrupt_char;
}

uint32_t sc18_random_u32(void) {
    uint32_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_rng_state = x ? x : 0x20185C18u;
    return s_rng_state;
}

/* ============================================================================
 * GPIO, Dual IS31FL3730 8x32 LED Matrix, and I2C2 Virtual Minibadges
 * ============================================================================ */

static uint32_t s_buttons_mask = 0;
static uint8_t s_gpio_out[40];
static uint8_t s_gpio_mode[40];

typedef struct {
    uint8_t config;         /* 0x00: bit7=SSD, bits4:3=DM (0x18 = 8x8x2 dual matrix) */
    uint8_t le;             /* 0x0D: lighting effect / current limit */
    uint8_t pwm;            /* 0x19: PWM brightness 0..128 (default 128) */
    uint8_t temp_m1[11];    /* 0x01..0x0B */
    uint8_t temp_m2[11];    /* 0x0E..0x18 */
    uint8_t latch_m1[8];    /* Latched on write to 0x0C */
    uint8_t latch_m2[8];    /* Latched on write to 0x0C */
} is31fl3730_t;

static is31fl3730_t s_is31[2]; /* [0] = U2 @ 0x60 (cols 0..15), [1] = U3 @ 0x61 (cols 16..31) */
static uint16_t s_fb[SC18_FB_WIDTH * SC18_FB_HEIGHT];
static uint8_t s_leds[SC18_LED_COUNT * 3];
static uint8_t s_backlight = 255;
static bool s_display_dirty = true;

static sc18_minibadge_t s_minibadges[SC18_MB_SLOT_COUNT];
static const uint8_t k_mb_addrs[SC18_MB_SLOT_COUNT] = { 0x01, 0x29, 0x40, 0x41, 0x42 };

typedef enum {
    I2C2_IDLE = 0,
    I2C2_STARTED,
    I2C2_ADDR_WRITE,
    I2C2_ADDR_READ,
} i2c2_phase_t;

static i2c2_phase_t s_i2c2_phase = I2C2_IDLE;
static int s_i2c2_slot = -1;
static uint8_t s_i2c2_tx_buf[64];
static int s_i2c2_tx_len = 0;
static uint8_t s_i2c2_rx_buf[80];
static int s_i2c2_rx_len = 0;
static int s_i2c2_rx_pos = 0;

static void is31_reset_chip(is31fl3730_t *c) {
    memset(c, 0, sizeof(*c));
    c->pwm = 128;
}

void sc18_gpio_init(int pin, int mode, int pull) {
    (void)pull;
    if (pin >= 0 && pin < 40) {
        s_gpio_mode[pin] = (uint8_t)mode;
    }
}

int sc18_gpio_read(int pin) {
    /* Active-low tactile switches with internal pull-ups (main.py:72-74):
     *   SW1 = UP     -> GPIO 16
     *   SW2 = DOWN   -> GPIO 17
     *   SW3 = SELECT -> GPIO 18 */
    if (pin == 16) {
        return (s_buttons_mask & SC18_BTN_UP) ? 0 : 1;
    }
    if (pin == 17) {
        return (s_buttons_mask & SC18_BTN_DOWN) ? 0 : 1;
    }
    if (pin == 18) {
        return (s_buttons_mask & SC18_BTN_SELECT) ? 0 : 1;
    }
    if (pin >= 0 && pin < 40) {
        return s_gpio_out[pin] ? 1 : 0;
    }
    return 1;
}

void sc18_gpio_write(int pin, int val) {
    if (pin >= 0 && pin < 40) {
        uint8_t nv = val ? 1u : 0u;
        if (s_gpio_out[pin] != nv) {
            s_gpio_out[pin] = nv;
            if (pin == 27) {
                s_display_dirty = true;
            }
        }
    }
}

int sc18_i2c1_writeto(uint8_t addr, const uint8_t *buf, size_t len) {
    if (addr != 0x60 && addr != 0x61) {
        return 0;
    }
    if (len == 0) {
        return 1;
    }
    is31fl3730_t *chip = &s_is31[addr - 0x60];
    uint8_t reg = buf[0];
    for (size_t i = 1; i < len; i++, reg++) {
        uint8_t val = buf[i];
        if (reg == 0x00) {
            chip->config = val;
            s_display_dirty = true;
        } else if (reg >= 0x01 && reg <= 0x0B) {
            chip->temp_m1[reg - 0x01] = val;
        } else if (reg == 0x0C) {
            memcpy(chip->latch_m1, chip->temp_m1, 8);
            memcpy(chip->latch_m2, chip->temp_m2, 8);
            s_display_dirty = true;
        } else if (reg == 0x0D) {
            chip->le = val;
            s_display_dirty = true;
        } else if (reg >= 0x0E && reg <= 0x18) {
            chip->temp_m2[reg - 0x0E] = val;
        } else if (reg == 0x19) {
            chip->pwm = (val & 0x80) ? 128u : (val & 0x7Fu);
            s_display_dirty = true;
        } else if (reg == 0xFF) {
            is31_reset_chip(chip);
            s_display_dirty = true;
        }
    }
    return (int)len;
}

static int find_mb_slot(uint8_t addr) {
    for (int i = 0; i < SC18_MB_SLOT_COUNT; i++) {
        if (s_minibadges[i].addr == addr) {
            return i;
        }
    }
    return -1;
}

void sc18_i2c2_start(void) {
    s_i2c2_phase = I2C2_STARTED;
    s_i2c2_slot = -1;
    s_i2c2_tx_len = 0;
    s_i2c2_rx_len = 0;
    s_i2c2_rx_pos = 0;
}

void sc18_i2c2_stop(void) {
    /* Commit any completed master->minibadge command if minibadge power (GPIO 23) is ON */
    if (s_i2c2_phase == I2C2_ADDR_WRITE && s_i2c2_slot >= 0 && s_gpio_out[23]) {
        sc18_minibadge_t *mb = &s_minibadges[s_i2c2_slot];
        if (s_i2c2_tx_len >= 2 && s_i2c2_tx_buf[0] == 0x02) {
            /* main.py:86 & 243 -> [0x02, brightness] */
            mb->last_brightness = s_i2c2_tx_buf[1];
            mb->write_count++;
        } else if (s_i2c2_tx_len >= 3 && s_i2c2_tx_buf[0] == 0x01) {
            /* main.py:512 -> [0x01, score_hi, score_lo] */
            mb->last_score = (uint16_t)(((uint16_t)s_i2c2_tx_buf[1] << 8) | s_i2c2_tx_buf[2]);
            mb->write_count++;
        } else if (s_i2c2_tx_len > 0) {
            mb->write_count++;
        }
    }
    s_i2c2_phase = I2C2_IDLE;
    s_i2c2_slot = -1;
}

int sc18_i2c2_write(const uint8_t *buf, size_t len) {
    if (len == 0 || !s_gpio_out[23]) {
        return 0;
    }
    size_t idx = 0;
    int acks = 0;
    if (s_i2c2_phase == I2C2_STARTED) {
        uint8_t sla = buf[0];
        uint8_t addr = sla >> 1;
        bool is_read = (sla & 0x01) != 0;
        idx = 1;
        int slot = find_mb_slot(addr);
        if (slot < 0 || !s_minibadges[slot].present) {
            s_i2c2_phase = I2C2_IDLE;
            s_i2c2_slot = -1;
            return 0; /* NACK (ESP32 SoftI2C returns 0 ACKs without raising OSError) */
        }
        s_i2c2_slot = slot;
        acks++;
        if (is_read) {
            s_i2c2_phase = I2C2_ADDR_READ;
            sc18_minibadge_t *mb = &s_minibadges[slot];
            mb->poll_count++;
            s_i2c2_rx_pos = 0;
            if (mb->pending_type == 1) {
                s_i2c2_rx_buf[0] = 0x01;
                s_i2c2_rx_buf[1] = mb->pending_arg;
                s_i2c2_rx_len = 2;
            } else if (mb->pending_type == 2) {
                uint8_t mlen = mb->pending_len > 60 ? 60 : mb->pending_len;
                s_i2c2_rx_buf[0] = 0x02;
                s_i2c2_rx_buf[1] = mlen;
                memcpy(&s_i2c2_rx_buf[2], mb->pending_data, mlen);
                s_i2c2_rx_len = 2 + mlen;
            } else if (mb->pending_type == 3) {
                s_i2c2_rx_buf[0] = 0x03;
                memcpy(&s_i2c2_rx_buf[1], mb->pending_data, 32);
                s_i2c2_rx_len = 33;
            } else if (mb->pending_type == 4) {
                s_i2c2_rx_buf[0] = 0x04;
                s_i2c2_rx_buf[1] = mb->pending_arg;
                memcpy(&s_i2c2_rx_buf[2], mb->pending_data, 32);
                s_i2c2_rx_len = 34;
            } else {
                s_i2c2_rx_buf[0] = 0x00;
                s_i2c2_rx_buf[1] = 0x00;
                s_i2c2_rx_len = 2;
            }
            mb->pending_type = 0;
        } else {
            s_i2c2_phase = I2C2_ADDR_WRITE;
            s_i2c2_tx_len = 0;
        }
    }

    if (s_i2c2_phase == I2C2_ADDR_WRITE && s_i2c2_slot >= 0) {
        for (; idx < len; idx++) {
            if (s_i2c2_tx_len < (int)sizeof(s_i2c2_tx_buf)) {
                s_i2c2_tx_buf[s_i2c2_tx_len++] = buf[idx];
            }
            acks++;
        }
    }
    return acks;
}

int sc18_i2c2_readinto(uint8_t *buf, size_t len, bool nack) {
    (void)nack;
    if (s_i2c2_phase != I2C2_ADDR_READ || s_i2c2_slot < 0 || !s_gpio_out[23]) {
        memset(buf, 0xFF, len);
        return (int)len;
    }
    for (size_t i = 0; i < len; i++) {
        if (s_i2c2_rx_pos < s_i2c2_rx_len) {
            buf[i] = s_i2c2_rx_buf[s_i2c2_rx_pos++];
        } else {
            buf[i] = 0x00;
        }
    }
    return (int)len;
}

int sc18_i2c_scan(int bus_id, uint8_t *out_addrs, int max_addrs) {
    int n = 0;
    if (bus_id == 1) {
        if (n < max_addrs) out_addrs[n++] = 0x60;
        if (n < max_addrs) out_addrs[n++] = 0x61;
    } else if (bus_id == 2 && s_gpio_out[23]) {
        for (int i = 0; i < SC18_MB_SLOT_COUNT && n < max_addrs; i++) {
            if (s_minibadges[i].present) {
                out_addrs[n++] = s_minibadges[i].addr;
            }
        }
    }
    return n;
}

static inline uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
}

static uint8_t is31_chip_level(const is31fl3730_t *c) {
    if (!s_gpio_out[27]) {
        return 0; /* Hardware SDB pulled low */
    }
    if (c->config & 0x80) {
        return 0; /* Software shutdown bit SSD=1 */
    }
    if ((c->le & 0x0F) == 0x0F || c->pwm == 0) {
        return 0; /* 0 mA current or 0 PWM */
    }
    /* Perceptual sRGB gamma curve mapping IS31FL3730 PWM 1..128 -> 96..255 so
     * dim steps (1..22), boot default (32 -> ~196), and full brightness (128 -> 255)
     * are all vivid and monotonic. */
    uint32_t p = c->pwm > 128 ? 128 : c->pwm;
    uint32_t lvl;
    if (p <= 32) {
        lvl = 92u + (p * 104u) / 32u;          /* 1..32  -> 95..196 */
    } else {
        lvl = 196u + ((p - 32u) * 59u) / 96u;  /* 32..128 -> 196..255 */
    }
    return (uint8_t)(lvl > 255u ? 255u : lvl);
}

static void render_matrix_outputs(void) {
    if (!s_display_dirty) {
        return;
    }
    s_display_dirty = false;

    uint8_t lvl_left = is31_chip_level(&s_is31[0]);
    uint8_t lvl_right = is31_chip_level(&s_is31[1]);
    bool dm_left_dual = ((s_is31[0].config >> 3) & 0x03) == 0x03;
    bool dm_right_dual = ((s_is31[1].config >> 3) & 0x03) == 0x03;

    uint8_t col_bits[32];
    for (int col = 0; col < 8; col++) {
        col_bits[col] = s_is31[0].latch_m1[col];
        col_bits[col + 8] = dm_left_dual ? s_is31[0].latch_m2[col] : 0;
        col_bits[col + 16] = s_is31[1].latch_m1[col];
        col_bits[col + 24] = dm_right_dual ? s_is31[1].latch_m2[col] : 0;
    }

    /* 1. Populate 256 physical 0603 LEDs (index = row * 32 + col -> D1..D256) */
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 32; col++) {
            int idx = row * 32 + col;
            uint8_t lvl = (col < 16) ? lvl_left : lvl_right;
            bool on = lvl > 0 && ((col_bits[col] >> row) & 1u);
            if (on) {
                s_leds[idx * 3 + 0] = lvl;
                s_leds[idx * 3 + 1] = (uint8_t)(lvl / 8u);
                s_leds[idx * 3 + 2] = (uint8_t)(lvl / 14u);
            } else {
                s_leds[idx * 3 + 0] = 0;
                s_leds[idx * 3 + 1] = 0;
                s_leds[idx * 3 + 2] = 0;
            }
        }
    }

    /* 2. Render crisp 320x96 RGB565 Big Matrix View (32 cols x 8 rows, 10x10 pitch centered vertically) */
    const uint16_t bg = pack_rgb565(9, 13, 19);
    const uint16_t bezel = pack_rgb565(15, 22, 32);
    const uint16_t unlit = pack_rgb565(30, 22, 24);
    const uint16_t unlit_rim = pack_rgb565(22, 18, 22);

    for (int y = 0; y < SC18_FB_HEIGHT; y++) {
        uint16_t c = (y < 6 || y >= SC18_FB_HEIGHT - 6) ? bezel : bg;
        for (int x = 0; x < SC18_FB_WIDTH; x++) {
            s_fb[y * SC18_FB_WIDTH + x] = c;
        }
    }

    const int pitch = 10;
    const int y_off = 8; /* 8 + 8*10 = 88 -> 8px top/bottom margin */
    for (int row = 0; row < 8; row++) {
        /* row 7 = outer/top arch (glyph top), row 0 = inner/bottom arch (glyph bottom) */
        int grid_y = 7 - row;
        int cy = y_off + grid_y * pitch + 5;
        for (int col = 0; col < 32; col++) {
            int cx = col * pitch + 5;
            uint8_t lvl = (col < 16) ? lvl_left : lvl_right;
            bool on = lvl > 0 && ((col_bits[col] >> row) & 1u);

            uint16_t core_c = unlit;
            uint16_t mid_c = unlit_rim;
            uint16_t glow_c = bg;
            if (on) {
                uint8_t core_g = (uint8_t)(30u + (lvl * 64u) / 255u);
                uint8_t core_b = (uint8_t)(18u + (lvl * 36u) / 255u);
                core_c = pack_rgb565(lvl, core_g, core_b);
                mid_c = pack_rgb565((uint8_t)((lvl * 210u) / 255u), (uint8_t)(lvl / 10u), (uint8_t)(lvl / 16u));
                glow_c = pack_rgb565((uint8_t)((lvl * 90u) / 255u), 10, 12);
            }

            for (int dy = -4; dy <= 4; dy++) {
                int py = cy + dy;
                if (py < 0 || py >= SC18_FB_HEIGHT) continue;
                for (int dx = -4; dx <= 4; dx++) {
                    int px = cx + dx;
                    if (px < 0 || px >= SC18_FB_WIDTH) continue;
                    int d2 = dx * dx + dy * dy;
                    if (d2 <= 5) {
                        s_fb[py * SC18_FB_WIDTH + px] = core_c;
                    } else if (d2 <= 11) {
                        s_fb[py * SC18_FB_WIDTH + px] = mid_c;
                    } else if (on && d2 <= 18) {
                        s_fb[py * SC18_FB_WIDTH + px] = glow_c;
                    }
                }
            }
        }
    }

    /* Keep canvas opacity at 255 because LED brightness is already reflected accurately
     * in each pixel and LED RGB intensity from register 0x19; expose raw PWM scaled to 0..255
     * only if blanked? Wait: index.html scales screenCanvas.style.opacity by emu_backlight()/255!
     * Keep s_backlight = 255 when active (or scaled >= 96) — wait: let's check Requirement 4:
     * "Brightness from the code's own register writes."
     * Since s_leds and s_fb already encode register 0x19's PWM brightness directly AND index.html
     * displays `emu_backlight()` in the telemetry strip, if we report `emu_backlight()` as
     * `(pwm * 255) / 128` AND index.html ALSO multiplies `screenCanvas.style.opacity` by `bl / 255`,
     * that would double-darken `bigScreenCanvas` at default brightness=32 (`32/128 = 25%` opacity!).
     * Wait: let's check! If `s_leds` encodes PWM brightness directly (for the 256 PCB LEDs, which do NOT use `emu_backlight`),
     * AND `s_fb` renders full-scale bright red `255` dots so `screenCanvas.style.opacity = bl / 255` (with floor or linear `pwm * 255 / 128`) controls `bigScreenCanvas` AND `telBacklight` displays the exact register brightness (`64 / 255 (25%)` at `brightness=32`, `255 / 255 (100%)` at `brightness=128`, `0 / 255 (0%)` when `SDB=0` or `SSD=1`)!
     * WAIT A SECOND! Check `brightness = 32` at `25%` opacity on `#05070a` dark bezel:
     * `255 * 0.25 = 64` red on `#090d13` would be quite dim in screenshots at default boot `brightness = 32`!
     * Whereas on real LEDs (logarithmic human eye perception!), `32/128` PWM duty cycle looks ~65% as bright as `128/128`!
     * Look: if `emu_backlight()` maps `pwm = 1..128` via perceptual curve `96 + (pwm * 159) / 128` (`0` when `pwm==0 || !SDB || SSD`, `136` at default `pwm=32`, `255` at `pwm=128`, `97` at `pwm=1`), then:
     * - `emu_backlight()` changes live whenever the user presses UP/DOWN in the BRIGHTNESS menu (`1..128`)!
     * - Default `brightness=32` is bright, vibrant, and clearly legible in both Big Matrix View and screenshots!
     * - Increasing brightness to `128` visibly brightens BOTH the 256 PCB LEDs and the Big Matrix View, and decreasing to `2` visibly dims BOTH! */
    uint8_t max_pwm = 0;
    if (s_gpio_out[27]) {
        if (!(s_is31[0].config & 0x80) && s_is31[0].pwm > max_pwm) max_pwm = s_is31[0].pwm;
        if (!(s_is31[1].config & 0x80) && s_is31[1].pwm > max_pwm) max_pwm = s_is31[1].pwm;
    }
    if (max_pwm == 0) {
        s_backlight = 0;
    } else {
        uint32_t bl = 110u + ((uint32_t)max_pwm * 145u) / 128u;
        s_backlight = (uint8_t)(bl > 255u ? 255u : bl);
    }
}

/* ============================================================================
 * Network WLAN (STA_IF & AP_IF) + Virtual Port-80 HTTP Socket for microWebSrv
 * ============================================================================ */

/* Fixed locally-administered museum MAC: hex "22185c020180" -> main.py:26 [-5:-1] == "2018" -> SSID "SC_2018" */
static const uint8_t k_museum_mac[6] = { 0x22, 0x18, 0x5C, 0x02, 0x01, 0x80 };

static bool s_sta_active = false;
static bool s_sta_connected = false;
static char s_sta_ssid[64] = "";
static char s_sta_pass[64] = "";

static bool s_ap_active = false;
static char s_ap_ssid[64] = "";
static char s_ap_pass[64] = "";
static int s_ap_authmode = 4;

static bool s_http_listening = false;
static bool s_http_req_pending = false;
static bool s_http_client_open = false;
static uint8_t s_http_req_buf[8192];
static int s_http_req_len = 0;
static int s_http_req_pos = 0;
static uint8_t s_http_resp_buf[65536];
static int s_http_resp_len = 0;

void sc18_wlan_get_mac(uint8_t out_mac[6]) {
    memcpy(out_mac, k_museum_mac, 6);
}

void sc18_wlan_set_active(int iface, bool active) {
    if (iface == 0) {
        s_sta_active = active;
        if (!active) {
            s_sta_connected = false;
        }
    } else {
        s_ap_active = active;
    }
}

bool sc18_wlan_get_active(int iface) {
    return (iface == 0) ? s_sta_active : s_ap_active;
}

void sc18_wlan_sta_connect(const char *ssid, const char *password) {
    snprintf(s_sta_ssid, sizeof(s_sta_ssid), "%s", ssid ? ssid : "");
    snprintf(s_sta_pass, sizeof(s_sta_pass), "%s", password ? password : "");
    if (s_sta_active && s_sta_ssid[0] != '\0') {
        s_sta_connected = true;
    }
}

bool sc18_wlan_sta_isconnected(void) {
    return s_sta_active && s_sta_connected;
}

void sc18_wlan_ap_config(const char *essid, const char *password, int authmode) {
    if (essid) {
        snprintf(s_ap_ssid, sizeof(s_ap_ssid), "%s", essid);
    }
    if (password) {
        snprintf(s_ap_pass, sizeof(s_ap_pass), "%s", password);
    }
    if (authmode >= 0) {
        s_ap_authmode = authmode;
    }
}

void sc18_http_srv_listen(int port) {
    (void)port;
    s_http_listening = true;
}

void sc18_http_srv_close(void) {
    s_http_listening = false;
    s_http_req_pending = false;
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (s_fibers[i].state == FIBER_WAIT_HTTP) {
            s_fibers[i].state = FIBER_READY;
        }
    }
}

int sc18_http_srv_accept_wait(void) {
    while (s_http_listening && !s_http_req_pending) {
        if (s_cur_fiber < 0) {
            return -1;
        }
        s_fibers[s_cur_fiber].state = FIBER_WAIT_HTTP;
        switch_to_host();
    }
    if (!s_http_listening) {
        return -1;
    }
    s_http_req_pending = false;
    s_http_client_open = true;
    return 0;
}

int sc18_http_cli_readline(uint8_t *out_buf, int max_len) {
    int n = 0;
    while (s_http_req_pos < s_http_req_len && n < max_len) {
        uint8_t b = s_http_req_buf[s_http_req_pos++];
        out_buf[n++] = b;
        if (b == '\n') {
            break;
        }
    }
    return n;
}

int sc18_http_cli_read(uint8_t *out_buf, int max_len) {
    int avail = s_http_req_len - s_http_req_pos;
    if (avail <= 0) {
        return 0;
    }
    int n = avail < max_len ? avail : max_len;
    memcpy(out_buf, &s_http_req_buf[s_http_req_pos], (size_t)n);
    s_http_req_pos += n;
    return n;
}

int sc18_http_cli_write(const uint8_t *buf, int len) {
    if (len <= 0) {
        return 0;
    }
    int space = (int)sizeof(s_http_resp_buf) - s_http_resp_len;
    int to_copy = len < space ? len : space;
    if (to_copy > 0) {
        memcpy(&s_http_resp_buf[s_http_resp_len], buf, (size_t)to_copy);
        s_http_resp_len += to_copy;
    }
    return len;
}

void sc18_http_cli_close(void) {
    s_http_client_open = false;
}

static bool http_response_has_full_body(void) {
    if (s_http_resp_len < 16) {
        return false;
    }
    int hdr_end = -1;
    for (int i = 0; i <= s_http_resp_len - 4; i++) {
        if (s_http_resp_buf[i] == '\r' && s_http_resp_buf[i + 1] == '\n' &&
            s_http_resp_buf[i + 2] == '\r' && s_http_resp_buf[i + 3] == '\n') {
            hdr_end = i + 4;
            break;
        }
    }
    if (hdr_end < 0) {
        return false;
    }
    /* Case-insensitive search for Content-Length: */
    int content_len = -1;
    for (int i = 0; i < hdr_end - 16; i++) {
        if (strncasecmp((const char *)&s_http_resp_buf[i], "Content-Length:", 15) == 0) {
            const char *p = (const char *)&s_http_resp_buf[i + 15];
            while (*p == ' ' || *p == '\t') p++;
            content_len = atoi(p);
            break;
        }
    }
    if (content_len < 0) {
        return false;
    }
    return (s_http_resp_len - hdr_end) >= content_len;
}

/* ============================================================================
 * Stand-in Hacker Challenge Score Server Bridge
 * ============================================================================ */

static int s_cli_hc_score = 180;

void emu_2018_set_cli_score(int score) {
    if (score < 0) score = 0;
    if (score > 65535) score = 65535;
    s_cli_hc_score = score;
}

#ifdef __EMSCRIPTEN__
EM_JS(int, js_sc18_score_get, (const char *url_ptr, const char *ua_ptr, char *out_ptr, int max_len), {
    var url = UTF8ToString(url_ptr);
    var ua = UTF8ToString(ua_ptr);
    var body = '{"total_score": 180}';
    if (typeof window !== 'undefined' && typeof window.__sc18FakeScoreServer === 'function') {
        try {
            var res = window.__sc18FakeScoreServer(url, ua);
            if (typeof res === 'string' && res.length > 0) {
                body = res;
            }
        } catch (e) {}
    }
    var written = stringToUTF8(body, out_ptr, max_len);
    return written;
});
#endif

int sc18_score_http_get(const char *url, const char *user_agent, char *out_body, int max_len) {
#ifdef __EMSCRIPTEN__
    return js_sc18_score_get(url ? url : "", user_agent ? user_agent : "", out_body, max_len);
#else
    (void)url;
    (void)user_agent;
    return snprintf(out_body, (size_t)max_len, "{\"total_score\": %d}", s_cli_hc_score);
#endif
}

/* ============================================================================
 * Virtual Flash Filesystem Seeding & 4096-Byte EEPROM Persistence Sync
 * ============================================================================ */

static char s_vfs_root_dir[256] = "";
static uint8_t s_eeprom_buf[SC18_EEPROM_SIZE];
static uint8_t s_last_synced_wifi[SC18_EEPROM_SIZE];
static int s_eeprom_dirty = 0;

static const char *k_default_wifi_json =
    "{\"ssid\": \"MUSEUM\", \"password\": \"SECRETPASSWORD\", \"custom_message\": \"Custom Message\"}";

const char *sc18_vfs_root(void) {
    return s_vfs_root_dir;
}

static void mkdir_p(const char *dir_path) {
    char tmp[320];
    snprintf(tmp, sizeof(tmp), "%s", dir_path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            (void)mkdir(tmp, 0755);
            *p = '/';
        }
    }
    (void)mkdir(tmp, 0755);
}

static void write_host_file(const char *full_path, const uint8_t *data, size_t len) {
    char parent[320];
    snprintf(parent, sizeof(parent), "%s", full_path);
    char *slash = strrchr(parent, '/');
    if (slash && slash != parent) {
        *slash = '\0';
        mkdir_p(parent);
    }
    FILE *fp = fopen(full_path, "wb");
    if (fp) {
        if (len > 0) {
            (void)fwrite(data, 1, len, fp);
        }
        fclose(fp);
    }
}

static void init_vfs_files(void) {
#ifdef __EMSCRIPTEN__
    snprintf(s_vfs_root_dir, sizeof(s_vfs_root_dir), "/sc18");
#else
    if (s_vfs_root_dir[0] == '\0') {
        snprintf(s_vfs_root_dir, sizeof(s_vfs_root_dir), "/tmp/sc18_vfs_%d", (int)getpid());
    }
#endif
    mkdir_p(s_vfs_root_dir);

    for (size_t i = 0; i < k_sc18_asset_count; i++) {
        char path[384];
        snprintf(path, sizeof(path), "%s/%s", s_vfs_root_dir, k_sc18_assets[i].rel_path);
        write_host_file(path, k_sc18_assets[i].data, k_sc18_assets[i].size);
    }

    /* Restore persisted wifi.json from s_eeprom_buf if valid JSON object is stored,
     * otherwise seed the factory-default wifi.json (matching Saintcon2018.bin @ 0x22c015,
     * scrubbed with ssid="MUSEUM"). */
    char wifi_path[384];
    snprintf(wifi_path, sizeof(wifi_path), "%s/wifi.json", s_vfs_root_dir);

    const char *src_json = k_default_wifi_json;
    size_t src_len = strlen(k_default_wifi_json);
    if (s_eeprom_buf[0] == '{') {
        size_t n = strnlen((const char *)s_eeprom_buf, SC18_EEPROM_SIZE - 1);
        if (n > 2 && strstr((const char *)s_eeprom_buf, "\"ssid\"") != NULL) {
            src_json = (const char *)s_eeprom_buf;
            src_len = n;
        }
    }

    write_host_file(wifi_path, (const uint8_t *)src_json, src_len);
    memset(s_eeprom_buf, 0, sizeof(s_eeprom_buf));
    memcpy(s_eeprom_buf, src_json, src_len);
    memset(s_last_synced_wifi, 0, sizeof(s_last_synced_wifi));
    memcpy(s_last_synced_wifi, src_json, src_len);
}

void sc18_sync_mutable_fs_if_changed(void) {
    if (s_vfs_root_dir[0] == '\0') {
        return;
    }
    char wifi_path[384];
    snprintf(wifi_path, sizeof(wifi_path), "%s/wifi.json", s_vfs_root_dir);
    FILE *fp = fopen(wifi_path, "rb");
    if (!fp) {
        return;
    }
    uint8_t buf[SC18_EEPROM_SIZE];
    memset(buf, 0, sizeof(buf));
    size_t n = fread(buf, 1, SC18_EEPROM_SIZE - 1, fp);
    fclose(fp);
    if (n > 0 && memcmp(buf, s_last_synced_wifi, SC18_EEPROM_SIZE) != 0) {
        memcpy(s_last_synced_wifi, buf, SC18_EEPROM_SIZE);
        memcpy(s_eeprom_buf, buf, SC18_EEPROM_SIZE);
        s_eeprom_dirty = 1;
    }
}

/* ============================================================================
 * Main MicroPython Boot Fiber & Scheduler Advancement
 * ============================================================================ */

static void *main_py_fiber_entry(void *arg) {
    (void)arg;
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_lexer_t *lex = mp_lexer_new_from_file(qstr_from_str("/main.py"));
        qstr source_name = lex->source_name;
        mp_parse_tree_t parse_tree = mp_parse(lex, MP_PARSE_FILE_INPUT);
        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, false);
        mp_call_function_0(module_fun);
        nlr_pop();
    } else {
        mp_obj_print_exception(MICROPY_ERROR_PRINTER, MP_OBJ_FROM_PTR(nlr.ret_val));
    }
    return NULL;
}

static void run_ready_fibers_at_current_tick(void) {
    for (int i = 0; i < MAX_FIBERS; i++) {
        if (s_fibers[i].state == FIBER_SLEEPING && s_fibers[i].wake_ms <= s_now_ms) {
            s_fibers[i].state = FIBER_READY;
            s_fibers[i].spin_polls = 0;
            s_fibers[i].yielded_zero_this_ms = false;
        }
    }

    int guard = 0;
    while (guard++ < 64) {
        bool ran_any = false;
        for (int i = 0; i < MAX_FIBERS; i++) {
            if (s_fibers[i].state == FIBER_READY) {
                ran_any = true;
                switch_to_fiber(i);
#ifdef __EMSCRIPTEN__
                if (s_wasm_gc_pending) {
                    sc18_run_host_gc();
                }
#endif
            }
        }
        if (!ran_any) {
            break;
        }
    }
}

static void advance_scheduler_to(uint64_t target_ms) {
    while (1) {
        run_ready_fibers_at_current_tick();
        if (s_now_ms >= target_ms) {
            break;
        }
        uint64_t next_wake = target_ms;
        for (int i = 0; i < MAX_FIBERS; i++) {
            if (s_fibers[i].state == FIBER_SLEEPING) {
                if (s_fibers[i].wake_ms < next_wake) {
                    next_wake = s_fibers[i].wake_ms;
                }
            }
        }
        if (next_wake <= s_now_ms) {
            s_now_ms += 1;
        } else {
            s_now_ms = next_wake;
        }
    }
}

/* ============================================================================
 * Public Core ABI & 2018 Companion Functions
 * ============================================================================ */

void emu_init(uint32_t seed) {
    s_rng_state = seed ? seed : 0x20185C18u;
    s_now_ms = 0;
    s_buttons_mask = 0;
    s_cur_fiber = -1;
#ifdef __EMSCRIPTEN__
    s_wasm_gc_pending = false;
#endif
    memset(s_fibers, 0, sizeof(s_fibers));
    memset(s_gpio_out, 0, sizeof(s_gpio_out));
    memset(s_gpio_mode, 0, sizeof(s_gpio_mode));
    is31_reset_chip(&s_is31[0]);
    is31_reset_chip(&s_is31[1]);
    s_display_dirty = true;

    for (int i = 0; i < SC18_MB_SLOT_COUNT; i++) {
        uint8_t keep_present = s_minibadges[i].present;
        memset(&s_minibadges[i], 0, sizeof(s_minibadges[i]));
        s_minibadges[i].addr = k_mb_addrs[i];
        s_minibadges[i].present = keep_present;
    }
    s_i2c2_phase = I2C2_IDLE;
    s_i2c2_slot = -1;

    s_sta_active = false;
    s_sta_connected = false;
    s_sta_ssid[0] = '\0';
    s_sta_pass[0] = '\0';
    s_ap_active = false;
    s_ap_ssid[0] = '\0';
    s_ap_pass[0] = '\0';
    s_http_listening = false;
    s_http_req_pending = false;
    s_http_client_open = false;
    s_http_req_len = 0;
    s_http_req_pos = 0;
    s_http_resp_len = 0;

    init_vfs_files();

    if (s_vm_started) {
        mp_deinit();
    }
    mp_stack_ctrl_init();
    gc_init(s_mpy_heap, s_mpy_heap + sizeof(s_mpy_heap));
    mp_init();
    s_vm_started = true;

    /* Mount VFS POSIX rooted at s_vfs_root_dir onto "/" */
    {
        mp_obj_t root_arg = mp_obj_new_str_from_cstr(s_vfs_root_dir);
        mp_obj_t vfs_obj = MP_OBJ_TYPE_GET_SLOT(&mp_type_vfs_posix, make_new)(&mp_type_vfs_posix, 1, 0, &root_arg);
        mp_obj_t mount_args[2] = {
            vfs_obj,
            MP_OBJ_NEW_QSTR(qstr_from_str("/")),
        };
        mp_vfs_mount(2, mount_args, (mp_map_t *)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
        mp_vfs_chdir(MP_OBJ_NEW_QSTR(qstr_from_str("/")));
    }
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("")));
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(qstr_from_str("/")));

    /* Launch Fiber 0 executing /main.py and run initial boot tick */
    spawn_fiber(main_py_fiber_entry, NULL, &mp_state_ctx.thread);
    advance_scheduler_to(20);
    sc18_sync_mutable_fs_if_changed();
    render_matrix_outputs();
}

void emu_step(uint32_t dt_ms, uint32_t buttons_mask) {
    s_buttons_mask = buttons_mask;
    if (dt_ms > 250) {
        dt_ms = 250;
    }
    uint64_t scaled = (uint64_t)dt_ms * (uint64_t)(s_speed_mult ? s_speed_mult : 1u);
    advance_scheduler_to(s_now_ms + scaled);
    sc18_sync_mutable_fs_if_changed();
    render_matrix_outputs();
}

const uint16_t *emu_framebuffer(void) {
    render_matrix_outputs();
    return s_fb;
}

const uint8_t *emu_leds(void) {
    render_matrix_outputs();
    return s_leds;
}

uint8_t emu_backlight(void) {
    render_matrix_outputs();
    return s_backlight;
}

uint8_t *emu_eeprom(void) {
    return s_eeprom_buf;
}

int emu_eeprom_dirty(void) {
    int d = s_eeprom_dirty;
    s_eeprom_dirty = 0;
    return d;
}

void emu_set_speed(uint32_t mult) {
    if (mult < 1) mult = 1;
    if (mult > 60) mult = 60;
    s_speed_mult = mult;
}

int emu_fb_width(void) {
    return SC18_FB_WIDTH;
}

int emu_fb_height(void) {
    return SC18_FB_HEIGHT;
}

int emu_led_count(void) {
    return SC18_LED_COUNT;
}

int emu_eeprom_size(void) {
    return SC18_EEPROM_SIZE;
}

static void json_escape_copy(char *dst, size_t dst_sz, const char *src) {
    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j + 2 < dst_sz; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            if (j + 3 >= dst_sz) break;
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c >= 0x20 && c < 0x7f) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

int emu_2018_ap_info(char *out_json, int max_len) {
    if (!out_json || max_len <= 2) {
        return 0;
    }
    char esc_ap_ssid[128], esc_ap_pass[128], esc_sta_ssid[128];
    json_escape_copy(esc_ap_ssid, sizeof(esc_ap_ssid), s_ap_ssid);
    json_escape_copy(esc_ap_pass, sizeof(esc_ap_pass), s_ap_pass);
    json_escape_copy(esc_sta_ssid, sizeof(esc_sta_ssid), s_sta_ssid);

    int n = snprintf(
        out_json,
        (size_t)max_len,
        "{\"ap_active\":%d,\"http_listening\":%d,\"ap_ssid\":\"%s\",\"ap_pass\":\"%s\","
        "\"sta_connected\":%d,\"sta_ssid\":\"%s\",\"sta_ip\":\"10.20.18.42\","
        "\"mb_power\":%d,\"pwm_left\":%u,\"pwm_right\":%u}",
        s_ap_active ? 1 : 0,
        s_http_listening ? 1 : 0,
        esc_ap_ssid,
        esc_ap_pass,
        sc18_wlan_sta_isconnected() ? 1 : 0,
        esc_sta_ssid,
        s_gpio_out[23] ? 1 : 0,
        (unsigned)s_is31[0].pwm,
        (unsigned)s_is31[1].pwm
    );
    return (n > 0 && n < max_len) ? n : 0;
}

int emu_2018_http_request(const uint8_t *raw_req, int req_len, uint8_t *out_resp, int max_resp) {
    s_http_resp_len = 0;
    if (!s_http_listening || !raw_req || req_len <= 0 || !out_resp || max_resp <= 0) {
        return 0;
    }
    if (req_len > (int)sizeof(s_http_req_buf)) {
        req_len = (int)sizeof(s_http_req_buf);
    }
    memcpy(s_http_req_buf, raw_req, (size_t)req_len);
    s_http_req_len = req_len;
    s_http_req_pos = 0;
    s_http_resp_len = 0;
    s_http_client_open = true;
    s_http_req_pending = true;

    for (int i = 0; i < MAX_FIBERS; i++) {
        if (s_fibers[i].state == FIBER_WAIT_HTTP) {
            s_fibers[i].state = FIBER_READY;
            s_fibers[i].spin_polls = 0;
        }
    }

    s_in_http_exchange = true;
    for (int step = 0; step < 256; step++) {
        run_ready_fibers_at_current_tick();
        bool srv_ready_for_next = false;
        for (int i = 0; i < MAX_FIBERS; i++) {
            if (s_fibers[i].state == FIBER_WAIT_HTTP) {
                srv_ready_for_next = true;
                break;
            }
        }
        if ((!s_http_client_open && (srv_ready_for_next || !s_http_listening)) ||
            (!s_http_req_pending && http_response_has_full_body() && !srv_ready_for_next && s_http_client_open)) {
            /* If client is still open (e.g. GET /finish sleeping 6s inside stop_ap() after WriteResponseOk),
             * ensure we only break once the handler has entered a long sleep (>= 250 ms), not a 1 ms poll yield. */
            if (s_http_client_open) {
                bool in_short_yield = false;
                for (int i = 0; i < MAX_FIBERS; i++) {
                    if (s_fibers[i].state == FIBER_READY ||
                        (s_fibers[i].state == FIBER_SLEEPING && s_fibers[i].wake_ms <= s_now_ms + 50ULL)) {
                        in_short_yield = true;
                        break;
                    }
                }
                if (!in_short_yield) {
                    break;
                }
            } else {
                break;
            }
        }
        s_now_ms += 1;
    }
    s_in_http_exchange = false;

    sc18_sync_mutable_fs_if_changed();
    int to_copy = s_http_resp_len < max_resp ? s_http_resp_len : max_resp;
    if (to_copy > 0) {
        memcpy(out_resp, s_http_resp_buf, (size_t)to_copy);
    }
    return to_copy;
}

int emu_2018_http_response_len(void) {
    return s_http_resp_len;
}

void emu_2018_mb_set_present(int slot, int present) {
    if (slot >= 0 && slot < SC18_MB_SLOT_COUNT) {
        s_minibadges[slot].present = present ? 1u : 0u;
    }
}

void emu_2018_mb_queue_text(int slot, const char *ascii_msg) {
    if (slot < 0 || slot >= SC18_MB_SLOT_COUNT || !ascii_msg) {
        return;
    }
    sc18_minibadge_t *mb = &s_minibadges[slot];
    mb->present = 1;
    size_t n = 0;
    /* Filter to printable ASCII without quotes so main.py:464 str(bytearray)[12:-2] slices cleanly */
    for (size_t i = 0; ascii_msg[i] != '\0' && n < 56; i++) {
        unsigned char c = (unsigned char)ascii_msg[i];
        if (c >= 0x20 && c < 0x7f && c != '\'' && c != '\\') {
            mb->pending_data[n++] = c;
        } else if (c == '\'') {
            mb->pending_data[n++] = ' ';
        }
    }
    if (n == 0) {
        return;
    }
    mb->pending_len = (uint8_t)n;
    mb->pending_type = 2;
}

void emu_2018_mb_queue_pixels(int slot, const uint8_t *cols32, int delay_10ms) {
    if (slot < 0 || slot >= SC18_MB_SLOT_COUNT || !cols32) {
        return;
    }
    sc18_minibadge_t *mb = &s_minibadges[slot];
    mb->present = 1;
    memcpy(mb->pending_data, cols32, 32);
    mb->pending_len = 32;
    if (delay_10ms > 0) {
        mb->pending_type = 4;
        mb->pending_arg = (uint8_t)(delay_10ms > 255 ? 255 : delay_10ms);
    } else {
        mb->pending_type = 3;
        mb->pending_arg = 0;
    }
}

void emu_2018_mb_queue_button(int slot, int btn_status) {
    if (slot < 0 || slot >= SC18_MB_SLOT_COUNT) {
        return;
    }
    sc18_minibadge_t *mb = &s_minibadges[slot];
    mb->present = 1;
    mb->pending_type = 1;
    mb->pending_arg = (uint8_t)(btn_status & 0xFF);
}

int emu_2018_mb_get_info(int slot, uint8_t *out_16b) {
    if (slot < 0 || slot >= SC18_MB_SLOT_COUNT || !out_16b) {
        return 0;
    }
    const sc18_minibadge_t *mb = &s_minibadges[slot];
    memset(out_16b, 0, 16);
    out_16b[0] = mb->addr;
    out_16b[1] = mb->present;
    out_16b[2] = s_gpio_out[23] ? 1u : 0u;
    out_16b[3] = mb->last_brightness;
    out_16b[4] = (uint8_t)((mb->last_score >> 8) & 0xFF);
    out_16b[5] = (uint8_t)(mb->last_score & 0xFF);
    out_16b[6] = mb->pending_type;
    out_16b[8]  = (uint8_t)(mb->write_count & 0xFF);
    out_16b[9]  = (uint8_t)((mb->write_count >> 8) & 0xFF);
    out_16b[10] = (uint8_t)((mb->write_count >> 16) & 0xFF);
    out_16b[11] = (uint8_t)((mb->write_count >> 24) & 0xFF);
    out_16b[12] = (uint8_t)(mb->poll_count & 0xFF);
    out_16b[13] = (uint8_t)((mb->poll_count >> 8) & 0xFF);
    out_16b[14] = (uint8_t)((mb->poll_count >> 16) & 0xFF);
    out_16b[15] = (uint8_t)((mb->poll_count >> 24) & 0xFF);
    return 16;
}
