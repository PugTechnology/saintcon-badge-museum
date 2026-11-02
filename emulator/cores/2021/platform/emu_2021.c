/*
 * First-party hardware platform shim for the SAINTCON 2021 combination lock badge.
 *
 * Replaces Microchip's proprietary QTouch binary library and Atmel Start / ASF4 /
 * CMSIS HAL with clean C implementations of:
 *   - Cooperative fiber execution (Emscripten fibers on WASM, ucontext on Linux)
 *   - GPIO, External Interrupts (PIN_PA27 Back button, PIN_PB09 ST25R95 IRQ_OUT)
 *   - SysTick / MS_Timer & 20 ms periodic Timer (touch_process)
 *   - Round 240x240 RGB565 ILI9331 LCD & PB17 backlight + sleep/wake
 *   - TCC0 PWM RGB LED (D1: WO[2]=R, WO[1]=G, WO[3]=B, active-low)
 *   - QTouch 3-electrode capacitive wheel (scroller 0), shackle sensors (3..5),
 *     and padlock shim sensor (6)
 *   - 512-byte NVMCTRL SmartEEPROM & 8 MB QUAD_SPI_0 external flash
 *   - ST25R95 SPI NFC reader/writer & card-emulation transceiver, 21 upstream
 *     quote unlock tags, external vCard programmer/reader, and solo "Museum Bot"
 *     AES-128 / SHA-1 trade partner
 *   - SERCOM1 I2C_0 minibadge bus (virtual minibadge at 0x28)
 */
#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "emu_2021.h"
#include "atmel_start.h"

#include "FrameBuffer.h"
#include "ILI9331.h"
#include "flash.h"
#include "machine_common.h"
#pragma pack()
#include "main.h"
#include "nfc.h"
#pragma pack()

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#endif

_Static_assert(sizeof(requirement) == 2, "Compile with -fshort-enums so requirement is 2 bytes (ARM EABI)");

/* Upstream main() renamed via -Dmain=sc21_firmware_main */
extern int sc21_firmware_main(void);

/* Globals defined in upstream main.c / nfc.c */
extern volatile uint32_t MS_Timer;
extern const uint8_t     touch_lut[256];
extern volatile uint8_t  measurement_done_touch;
extern uint8_t           key_status;
extern uint8_t           keys[4];
extern ext_irq_cb_t      nfc_write_callback;
extern uint8_t           no_field_overflow;
extern uint32_t          minibadge_delay;

/* ============================================================================
 * Constants & Peripheral Descriptors
 * ============================================================================ */

#define SC21_FB_W          240
#define SC21_FB_H          240
#define SC21_QSPI_SIZE     (8u * 1024u * 1024u)
#define SC21_VCARD_OFFSET  0x7F8000u
#define SC21_VCARD_BYTES   512u
#define SC21_EEPROM_BYTES  512u

/* Button bitmasks from badge.json / index.html */
#define SC21_BTN_UP        (1u << 0)
#define SC21_BTN_DOWN      (1u << 1)
#define SC21_BTN_LEFT      (1u << 2)
#define SC21_BTN_RIGHT     (1u << 3)
#define SC21_BTN_CENTER    (1u << 4)
#define SC21_BTN_BACK      (1u << 5) /* bit 32: SW3 PA27 Back */
#define SC21_BTN_SHACKLE   (1u << 6) /* bit 64: Pull Shackle Open */

/* Scrubbed deterministic museum AES-128 trade key (matches fetch-upstream.sh) */
static const uint8_t k_museum_trade_key[16] = {
    0x4d, 0x55, 0x53, 0x45, 0x55, 0x4d, 0x5f, 0x54,
    0x52, 0x41, 0x44, 0x45, 0x32, 0x30, 0x32, 0x31,
};

struct timer_descriptor      Timer;
struct pwm_descriptor        PWM_0;
struct rand_sync_desc        RAND_0;
struct spi_m_sync_descriptor SPI_1;
struct i2c_m_sync_desc       I2C_0;
struct qspi_sync_descriptor  QUAD_SPI_0;
struct sha_sync_descriptor   HASH_ALGORITHM_0;
struct aes_sync_descriptor   CRYPTOGRAPHY_0;
struct io_descriptor        *io = NULL;

uint8_t            sc21_seeprom_buf[SC21_EEPROM_BYTES];
sc21_nvmctrl_regs_t sc21_nvmctrl;

/* ============================================================================
 * Emulator State
 * ============================================================================ */

static uint16_t s_lcd_fb[SC21_FB_W * SC21_FB_H];
static bool     s_lcd_sleeping = false;
static uint8_t  s_rgb_led[3]   = {0, 0, 0};
static uint32_t s_speed_mult   = 1;

static uint8_t  s_seeprom_shadow[SC21_EEPROM_BYTES];
static uint8_t  s_qspi_flash[SC21_QSPI_SIZE];
static bool     s_qspi_flash_loaded = false;
static uint8_t  s_vcard_shadow[SC21_VCARD_BYTES];

/* GPIO & External Interrupt state */
static bool         s_gpio_level[SC21_MAX_GPIO_PINS];
static ext_irq_cb_t s_ext_irq_cb[SC21_MAX_GPIO_PINS];
static bool         s_ext_irq_en[SC21_MAX_GPIO_PINS];
static bool         s_pending_back_irq = false;

/* QTouch Wheel, Shackle, & Shim sensor state */
static uint8_t  s_inv_touch_lut[256];
static bool     s_inv_lut_ready = false;

static uint32_t s_buttons_mask      = 0;
static uint32_t s_prev_buttons_mask = 0;

static bool     s_ptr_touched     = false;
static uint8_t  s_ptr_wheel_pos   = 0; /* 0..255 post-LUT clockwise from 12 o'clock */
static bool     s_api_touched     = false;
static uint8_t  s_api_wheel_pos   = 0;

static double   s_kb_wheel_pos    = 0.0; /* 0..256 continuous post-LUT position */
static int      s_kb_spin_dir     = 0;   /* -1 = CCW (LEFT), +1 = CW (RIGHT), 0 = none */
static uint32_t s_kb_next_step_ms = 0;
static bool     s_kb_first_step   = false;

static uint8_t  s_touch_tick_div  = 0;
static uint8_t  s_latched_raw_pos = 0;
static uint8_t  s_latched_touched = 0;

static bool     s_shackle_latched_open  = false;
static bool     s_shackle_pulse_pending = false;
static bool     s_shim_pulse_pending    = false;
static bool     s_frame_saw_trade_img   = false;
static bool     s_frame_saw_trade2_img  = false;
static bool     s_in_trading_ui         = false;
static bool     s_in_trade_complete_ui  = false;

/* Cooperative Fiber State */
#define FW_C_STACK_SZ  (128 * 1024)
#define FW_AS_STACK_SZ (64 * 1024)

_Alignas(16) static uint8_t s_fw_c_stack[FW_C_STACK_SZ];
#ifdef __EMSCRIPTEN__
_Alignas(16) static uint8_t s_fw_async_stack[FW_AS_STACK_SZ];
_Alignas(16) static uint8_t s_host_async_stack[32 * 1024];
static emscripten_fiber_t   s_host_fiber;
static emscripten_fiber_t   s_fw_fiber;
static bool                 s_host_fiber_inited = false;
#else
static ucontext_t           s_host_ctx;
static ucontext_t           s_fw_ctx;
#endif

static bool     s_in_fw_fiber          = false;
static bool     s_in_irq_dispatch      = false;
static bool     s_soft_reset_requested = false;
static uint32_t s_soft_reset_count     = 0;
static uint32_t s_step_target_ms       = 0;
static uint32_t s_us_accum             = 0;

/* ============================================================================
 * Virtual ST25R95 NFC State, Quote Tags, & Solo "Museum Bot" Trade Partner
 * ============================================================================ */

/* All 21 real upstream quote preimages from Scenes/nfcScene.c:24-46 */
static const char *const k_nfc_quotes[21] = {
    "Fear is the mind-killer.",
    "Remember...All I'm Offering Is The Truth. Nothing More.",
    "I fight for the Users!",
    "Their crime is curiosity.",
    "Roads? Where we're going, we don't need roads",
    "Too Many Secrets",
    "A strange game. The only winning move is not to play.",
    "Life moves pretty fast. If you don't stop and look around once in a while, you could miss it.",
    "The only privacy that's left is the inside of your head.",
    "We must keep our faith in the Republic. The day we stop believing democracy can work is the day we lose it.",
    "Well, if droids could think, there'd be none of us here, would there?",
    "One ring to rule them all, one ring to find them, one ring the bring them all, and in the darkness bind them.",
    "I'm sorry, Dave. I'm afraid I can't do that.",
    "If we knew what it was we were doing, it would not be called research, would it?",
    "What... is the air-speed velocity of an unladen swallow?",
    "I am a leaf on the wind. Watch how I soar.",
    "I only speak two languages: English and bad English.",
    "We have no names, man. No names. We are nameless!",
    "I aim to misbehave.",
    "Curse your sudden but inevitable betrayal!",
    "Never gonna give you up!",
};

#define ST25_MAX_RESP   64
#define ST25_MAX_RF_Q   96
#define VTAG_MEM_BYTES  540

typedef struct {
    uint8_t len;
    uint8_t bytes[24];
} st25_rf_frame_t;

static bool            s_st25_cs_low         = false;
static uint8_t         s_st25_spi_ctrl       = 0xFF;
static uint16_t        s_st25_cs_pos         = 0;
static uint8_t         s_st25_cmd_buf[64];
static uint16_t        s_st25_cmd_len        = 0;

static uint8_t         s_st25_resp_buf[ST25_MAX_RESP];
static uint16_t        s_st25_resp_len       = 0;
static uint16_t        s_st25_resp_pos       = 0;
static bool            s_st25_resp_ready     = false;
static bool            s_st25_listening      = false;

/* Queue of incoming RF frames delivered to card-emulation IRQ (PIN_PB09) */
static st25_rf_frame_t s_rf_queue[ST25_MAX_RF_Q];
static uint16_t        s_rf_q_head           = 0;
static uint16_t        s_rf_q_tail           = 0;
static uint16_t        s_rf_q_count          = 0;

/* Captured pages sent back by card-emulation 0x06 Send command */
static uint8_t         s_captured_tag[304];
static uint16_t        s_captured_read_idx   = 0;

/* Virtual Type-2 NTAG215 tag presented to reader mode */
static bool            s_vtag_present        = false;
static uint8_t         s_vtag_uid[7]         = {'M', 'U', 'S', 'E', 'U', 'M', '1'};
static uint8_t         s_vtag_mem[VTAG_MEM_BYTES];

/* Solo "Museum Bot" trade state: 0=idle, 1=waiting for badge msg2, 2=confirmation queued, 3=completed (latched until read) */
#pragma pack(push, 1)
typedef struct {
    uint16_t    magic;
    uint16_t    msg_id;
    uint16_t    nonce1;
    uint16_t    nonce2;
    requirement reqparts[4];
} sc21_trade_msg_t;
#pragma pack(pop)

_Static_assert(sizeof(sc21_trade_msg_t) == 16, "sc21_trade_msg_t must be 16 bytes");

static int         s_bot_trade_state       = 0;
static int         s_last_vcard_program_rc = 0;
static uint16_t    s_bot_nonce          = 0x2021u;
static requirement s_bot_offer[4];
static requirement s_bot_received[4];
static char        s_bot_status_msg[192] = "Idle — select parts for Museum Bot to offer, then open Trading on the badge.";

/* Virtual I2C_0 Minibadge at 0x28 (40) */
#define SC21_MB_ADDR 40

static bool     s_mb_present          = false;
static uint8_t  s_mb_last_reg[2]      = {0, 0};
static bool     s_mb_btn_pending      = false;
static bool     s_mb_msg_pending      = false;
static char     s_mb_msg_buf[128]     = "";
static uint8_t  s_mb_msg_len          = 0;
static uint8_t  s_mb_read_stage       = 0; /* 0=event code, 1=msg len, 2=msg body */

/* ============================================================================
 * Inverse touch_lut Builder & Helpers
 * ============================================================================ */

static void ensure_inv_touch_lut(void) {
    if (s_inv_lut_ready) {
        return;
    }
    for (int target = 0; target < 256; target++) {
        int best_raw = 0;
        int best_err = 9999;
        for (int raw = 0; raw < 256; raw++) {
            int err = (int)touch_lut[raw] - target;
            if (err < 0) err = -err;
            if (err < best_err) {
                best_err = err;
                best_raw = raw;
            }
        }
        s_inv_touch_lut[target] = (uint8_t)best_raw;
    }
    s_inv_lut_ready = true;
}

static void vtag_init_header(void) {
    memset(s_vtag_mem, 0, sizeof(s_vtag_mem));
    memcpy(&s_vtag_mem[0], &s_vtag_uid[0], 3);
    s_vtag_mem[3] = (uint8_t)(0x88u ^ s_vtag_uid[0] ^ s_vtag_uid[1] ^ s_vtag_uid[2]);
    memcpy(&s_vtag_mem[4], &s_vtag_uid[3], 4);
    s_vtag_mem[8] = (uint8_t)(s_vtag_uid[3] ^ s_vtag_uid[4] ^ s_vtag_uid[5] ^ s_vtag_uid[6]);
    s_vtag_mem[12] = 0xE1u;
    s_vtag_mem[13] = 0x10u;
    s_vtag_mem[14] = 0x3Eu;
}

static void vtag_write_ndef_mime(const char *mime_type, const uint8_t *payload, uint8_t payload_len) {
    vtag_init_header();
    uint8_t mlen = (uint8_t)strlen(mime_type);
    uint8_t *dst = &s_vtag_mem[16];
    dst[0] = 0x03u;
    dst[1] = (uint8_t)(mlen + payload_len + 3u);
    dst[2] = 0xD2u; /* MB | ME | SR | TNF_MIME */
    dst[3] = mlen;
    dst[4] = payload_len;
    memcpy(&dst[5], mime_type, mlen);
    memcpy(&dst[5 + mlen], payload, payload_len);
    dst[5 + mlen + payload_len] = 0xFEu;
}

/* ============================================================================
 * Fiber Switching & Runtime Reset
 * ============================================================================ */

static void sc21_yield_to_host(void) {
    if (!s_in_fw_fiber) {
        return;
    }
    s_in_fw_fiber = false;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&s_fw_fiber, &s_host_fiber);
#else
    swapcontext(&s_fw_ctx, &s_host_ctx);
#endif
    s_in_fw_fiber = true;
}

static void fw_fiber_trampoline(void) {
    s_in_fw_fiber = true;
    (void)sc21_firmware_main();
    while (1) {
        sc21_yield_to_host();
    }
}

#ifdef __EMSCRIPTEN__
static void fw_em_fiber_entry(void *arg) {
    (void)arg;
    fw_fiber_trampoline();
}
#endif

static void sc21_init_fw_fiber(void) {
#ifdef __EMSCRIPTEN__
    if (!s_host_fiber_inited) {
        emscripten_fiber_init_from_current_context(
            &s_host_fiber, s_host_async_stack, sizeof(s_host_async_stack));
        s_host_fiber_inited = true;
    }
    emscripten_fiber_init(
        &s_fw_fiber,
        fw_em_fiber_entry,
        NULL,
        s_fw_c_stack,
        sizeof(s_fw_c_stack),
        s_fw_async_stack,
        sizeof(s_fw_async_stack));
#else
    getcontext(&s_fw_ctx);
    s_fw_ctx.uc_stack.ss_sp   = s_fw_c_stack;
    s_fw_ctx.uc_stack.ss_size = sizeof(s_fw_c_stack);
    s_fw_ctx.uc_link          = &s_host_ctx;
    makecontext(&s_fw_ctx, fw_fiber_trampoline, 0);
#endif
}

static void sc21_resume_fw_fiber(void) {
    s_in_fw_fiber = true;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&s_host_fiber, &s_fw_fiber);
#else
    swapcontext(&s_host_ctx, &s_fw_ctx);
#endif
    s_in_fw_fiber = false;
}

static void sc21_reset_runtime_state(bool clear_lcd_and_led) {
    ensure_inv_touch_lut();

    if (clear_lcd_and_led) {
        memset(s_lcd_fb, 0, sizeof(s_lcd_fb));
        memset(s_rgb_led, 0, sizeof(s_rgb_led));
    }
    s_lcd_sleeping = false;

    memset(&Timer, 0, sizeof(Timer));
    memset(&PWM_0, 0, sizeof(PWM_0));
    memset(&sc21_nvmctrl, 0, sizeof(sc21_nvmctrl));
    sc21_nvmctrl.SEESTAT.bit.SBLK = 1;
    sc21_nvmctrl.SEESTAT.bit.BUSY = 0;
    sc21_nvmctrl.INTFLAG.bit.DONE = 1;

    memset(s_gpio_level, 0, sizeof(s_gpio_level));
    memset(s_ext_irq_cb, 0, sizeof(s_ext_irq_cb));
    memset(s_ext_irq_en, 0, sizeof(s_ext_irq_en));
    s_gpio_level[PIN_PA14] = true; /* NFC_CS_PIN idle high */
    s_gpio_level[PIN_PB08] = true; /* NFC_IRQ_IN_PIN idle high */
    s_gpio_level[PIN_PB09] = true; /* NFC_IRQ_OUT_PIN idle high */
    s_pending_back_irq     = false;

    s_st25_cs_low     = false;
    s_st25_spi_ctrl   = 0xFF;
    s_st25_cs_pos     = 0;
    s_st25_cmd_len    = 0;
    s_st25_resp_len   = 0;
    s_st25_resp_pos   = 0;
    s_st25_resp_ready = false;
    s_st25_listening  = false;
    s_rf_q_head       = 0;
    s_rf_q_tail       = 0;
    s_rf_q_count      = 0;

    s_mb_read_stage         = 0;
    s_frame_saw_trade_img   = false;
    s_frame_saw_trade2_img  = false;
    s_in_trading_ui         = false;
    s_in_trade_complete_ui  = false;
    s_shackle_pulse_pending = false;
    s_shim_pulse_pending    = false;

    /* Reset upstream firmware globals for clean soft/hard reboot */
    MS_Timer               = 100u;
    s_us_accum             = 0;
    measurement_done_touch = 0;
    scroller_status        = 0;
    scroller_position      = 0;
    key_status             = 0;
    memset(keys, 0, sizeof(keys));
    back_event             = false;
    unlock_event           = false;
    claspopen              = false;
    rouge_event            = false;
    minibadge_delay        = 0;

    memset((void *)TAG_BUFF, 0, TAG_BUFF_LEN);
    NFC_BADGE_READ         = false;
    NFC_BADGE_WRITE[0]     = NWRITE_IDLE;
    NFC_BADGE_WRITE[1]     = 0;
    nfc_write_callback     = NULL;
    no_field_overflow      = 0;

    s_in_irq_dispatch      = false;
    s_soft_reset_requested = false;
}

/* ============================================================================
 * Virtual Clock, Keyboard Wheel Synthesis, Timer Tasks, & IRQ Dispatch
 * ============================================================================ */

static void sc21_update_keyboard_wheel(uint32_t now_ms) {
    if (s_in_trading_ui) {
        s_kb_spin_dir = 0;
        if ((s_buttons_mask & SC21_BTN_LEFT) && !(s_buttons_mask & SC21_BTN_RIGHT)) {
            s_kb_wheel_pos = 192.0; /* 9 o'clock (< prev part in Trading) */
        } else if ((s_buttons_mask & SC21_BTN_RIGHT) && !(s_buttons_mask & SC21_BTN_LEFT)) {
            s_kb_wheel_pos = 64.0;  /* 3 o'clock (> next part in Trading) */
        }
        return;
    }

    int want_dir = 0;
    if ((s_buttons_mask & SC21_BTN_LEFT) && !(s_buttons_mask & SC21_BTN_RIGHT)) {
        want_dir = -1; /* CCW -> combo_scroll > 0 */
    } else if ((s_buttons_mask & SC21_BTN_RIGHT) && !(s_buttons_mask & SC21_BTN_LEFT)) {
        want_dir = +1; /* CW  -> combo_scroll < 0 */
    }

    if (want_dir == 0) {
        s_kb_spin_dir = 0;
        return;
    }

    if (want_dir != s_kb_spin_dir) {
        /* First frame touches at current angle so comboScene/menuScene latches baseline;
         * 25 ms later, first motion step jumps 11 units (~15.4 deg, >10 and <20) so
         * comboScene stage transitions trigger reliably on every reversal. */
        s_kb_spin_dir     = want_dir;
        s_kb_first_step   = true;
        s_kb_next_step_ms = now_ms + 25u;
        return;
    }

    if ((int32_t)(now_ms - s_kb_next_step_ms) >= 0) {
        double delta = s_kb_first_step ? (11.0 * (double)want_dir) : (6.4 * (double)want_dir);
        s_kb_first_step = false;
        s_kb_wheel_pos += delta;
        while (s_kb_wheel_pos < 0.0)    s_kb_wheel_pos += 256.0;
        while (s_kb_wheel_pos >= 256.0) s_kb_wheel_pos -= 256.0;
        s_kb_next_step_ms = now_ms + 55u;
    }
}

static void sc21_service_timers_and_irqs(void) {
    /* 1. Periodic Timer tasks (20 ms touch_process) */
    if (Timer.running) {
        for (uint8_t i = 0; i < Timer.task_count; i++) {
            struct timer_task *t = Timer.tasks[i];
            if (t && t->active && (int32_t)(MS_Timer - t->next_due_ms) >= 0) {
                if (t->mode == TIMER_TASK_REPEAT && t->interval > 0) {
                    uint32_t next = t->next_due_ms + t->interval;
                    if ((int32_t)(MS_Timer - next) >= (int32_t)t->interval) {
                        next = MS_Timer + t->interval;
                    }
                    t->next_due_ms = next;
                } else {
                    t->active = false;
                }
                if (t->cb) {
                    t->cb(t);
                }
            }
        }
    }

    if (s_in_irq_dispatch) {
        return;
    }

    /* 2. PIN_PA27 Back button external interrupt */
    if (s_pending_back_irq && s_ext_irq_cb[PIN_PA27]) {
        s_pending_back_irq = false;
        s_in_irq_dispatch  = true;
        s_ext_irq_cb[PIN_PA27]();
        s_in_irq_dispatch  = false;
    }

    /* 3. PIN_PB09 ST25R95 card-emulation RF frame external interrupts */
    while (s_rf_q_count > 0 &&
           s_st25_listening &&
           !s_st25_resp_ready &&
           s_ext_irq_en[PIN_PB09] &&
           s_ext_irq_cb[PIN_PB09] != NULL) {
        st25_rf_frame_t rf = s_rf_queue[s_rf_q_head];
        s_rf_q_head = (uint16_t)((s_rf_q_head + 1u) % ST25_MAX_RF_Q);
        s_rf_q_count--;

        memcpy(s_st25_resp_buf, rf.bytes, rf.len);
        s_st25_resp_len   = rf.len;
        s_st25_resp_pos   = 0;
        s_st25_resp_ready = true;
        s_gpio_level[PIN_PB09] = false; /* Active-low IRQ_OUT */

        s_in_irq_dispatch = true;
        s_ext_irq_cb[PIN_PB09]();
        s_in_irq_dispatch = false;

        if (s_soft_reset_requested) {
            return;
        }
    }
}

static void sc21_advance_time_ms(uint32_t delta_ms) {
    if (delta_ms == 0) {
        sc21_service_timers_and_irqs();
        return;
    }

    uint32_t rem = delta_ms;
    while (rem > 0) {
        uint32_t step = (rem > 5u) ? 5u : rem;
        MS_Timer += step;
        rem -= step;

        sc21_update_keyboard_wheel(MS_Timer);
        sc21_service_timers_and_irqs();
        if (s_soft_reset_requested) {
            sc21_yield_to_host();
            return;
        }
    }

    if (s_in_fw_fiber && !s_in_irq_dispatch && (int32_t)(MS_Timer - s_step_target_ms) >= 0) {
        sc21_yield_to_host();
    }
}

/* ============================================================================
 * Atmel Start / Driver Init / GPIO / ExtIRQ / Timer / Delay / PWM / RNG
 * ============================================================================ */

void atmel_start_init(void) {
    /* Hardware descriptors are initialized in sc21_reset_runtime_state(). */
}

uint32_t SysTick_Config(uint32_t ticks) {
    (void)ticks;
    return 0;
}

void NVIC_SystemReset(void) {
    s_soft_reset_requested = true;
    sc21_yield_to_host();
}

void delay_ms(const uint16_t ms) {
    if (ms == 0) {
        sc21_advance_time_ms(0);
        return;
    }
    sc21_advance_time_ms((uint32_t)ms);
}

void delay_us(const uint16_t us) {
    s_us_accum += (uint32_t)us;
    if (s_us_accum >= 1000u) {
        uint32_t ms = s_us_accum / 1000u;
        s_us_accum %= 1000u;
        sc21_advance_time_ms(ms);
    }
}

static void st25_on_cs_change(bool new_level);

void gpio_set_pin_direction(const uint32_t pin, const enum gpio_direction direction) {
    (void)pin;
    (void)direction;
}

void gpio_set_pin_level(const uint32_t pin, const bool level) {
    if (pin >= SC21_MAX_GPIO_PINS) {
        return;
    }
    bool prev = s_gpio_level[pin];
    s_gpio_level[pin] = level;
    if (pin == PIN_PA14 && prev != level) {
        st25_on_cs_change(level);
    }
}

bool gpio_get_pin_level(const uint32_t pin) {
    if (pin >= SC21_MAX_GPIO_PINS) {
        return false;
    }
    if (pin == PIN_PB09) {
        return s_st25_resp_ready ? false : true;
    }
    return s_gpio_level[pin];
}

void gpio_toggle_pin_level(const uint32_t pin) {
    if (pin < SC21_MAX_GPIO_PINS) {
        gpio_set_pin_level(pin, !s_gpio_level[pin]);
    }
}

int32_t ext_irq_register(const uint32_t pin, ext_irq_cb_t cb) {
    if (pin < SC21_MAX_GPIO_PINS) {
        s_ext_irq_cb[pin] = cb;
        s_ext_irq_en[pin] = (cb != NULL);
    }
    return 0;
}

int32_t ext_irq_enable(const uint32_t pin) {
    if (pin < SC21_MAX_GPIO_PINS) {
        s_ext_irq_en[pin] = true;
    }
    return 0;
}

int32_t ext_irq_disable(const uint32_t pin) {
    if (pin < SC21_MAX_GPIO_PINS) {
        s_ext_irq_en[pin] = false;
    }
    return 0;
}

int32_t io_write(struct io_descriptor *const io_descr, const uint8_t *const buf, const uint16_t length) {
    if (!io_descr || !io_descr->write) {
        return -1;
    }
    return io_descr->write(io_descr, buf, length);
}

int32_t io_read(struct io_descriptor *const io_descr, uint8_t *const buf, const uint16_t length) {
    if (!io_descr || !io_descr->read) {
        return -1;
    }
    return io_descr->read(io_descr, buf, length);
}

int32_t timer_add_task(struct timer_descriptor *const descr, struct timer_task *const task) {
    if (!descr || !task) {
        return -1;
    }
    for (uint8_t i = 0; i < descr->task_count; i++) {
        if (descr->tasks[i] == task) {
            task->active      = true;
            task->next_due_ms = MS_Timer + task->interval;
            return 0;
        }
    }
    if (descr->task_count < 4u) {
        task->active                        = true;
        task->next_due_ms                   = MS_Timer + task->interval;
        descr->tasks[descr->task_count++] = task;
    }
    return 0;
}

int32_t timer_start(struct timer_descriptor *const descr) {
    if (descr) {
        descr->running = true;
    }
    return 0;
}

int32_t pwm_enable(struct pwm_descriptor *const descr) {
    if (descr) {
        descr->enabled = true;
    }
    return 0;
}

int32_t pwm_set_parameters(struct pwm_descriptor *const descr, const uint32_t period, const uint32_t duty_cycle) {
    if (descr) {
        descr->period = period;
        descr->duty   = duty_cycle;
    }
    return 0;
}

void hri_tcc_write_CC_reg(const void *const hw, uint8_t index, uint32_t data) {
    (void)hw;
    uint8_t level = (uint8_t)(255u - (data & 0xFFu));
    if (index == 2u) {
        s_rgb_led[0] = level; /* PA22 Red */
    } else if (index == 1u) {
        s_rgb_led[1] = level; /* PA21 Green */
    } else if (index == 3u) {
        s_rgb_led[2] = level; /* PA23 Blue */
    }
}

int32_t rand_sync_enable(struct rand_sync_desc *const desc) {
    if (desc && desc->state == 0u) {
        desc->state = 0x20215343u;
    }
    return 0;
}

uint32_t rand_sync_read32(struct rand_sync_desc *const desc) {
    uint32_t x = (desc && desc->state) ? desc->state : 0x20215343u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    if (desc) {
        desc->state = x;
    }
    return x;
}

/* ============================================================================
 * Crypto Accelerators (CRYPTOGRAPHY_0 AES-128-ECB, HASH_ALGORITHM_0 SHA-1)
 * ============================================================================ */

int32_t sha_sync_enable(struct sha_sync_descriptor *const desc) {
    if (desc) {
        desc->enabled = true;
    }
    return 0;
}

int32_t sha_sync_sha1_compute(
    struct sha_sync_descriptor *const desc,
    struct sha_context *const         ctx,
    const void                       *buf,
    uint32_t                          len,
    uint8_t                           digest[20]) {
    (void)desc;
    (void)ctx;
    sc21_sha1_compute(buf, (size_t)len, digest);
    return 0;
}

int32_t aes_sync_enable(struct aes_sync_descriptor *const desc) {
    if (desc) {
        desc->enabled = true;
    }
    return 0;
}

int32_t aes_sync_set_encrypt_key(
    struct aes_sync_descriptor *const desc,
    const uint8_t                    *key,
    const enum aes_keysize            size) {
    (void)size;
    if (desc && key) {
        memcpy(desc->key, key, 16);
    }
    return 0;
}

int32_t aes_sync_ecb_crypt(
    struct aes_sync_descriptor *const desc,
    const enum aes_action             enc,
    const uint8_t                    *input,
    uint8_t                          *output) {
    if (!desc || !input || !output) {
        return -1;
    }
    if (enc == AES_ENCRYPT) {
        sc21_aes128_ecb_encrypt(desc->key, input, output);
    } else {
        sc21_aes128_ecb_decrypt(desc->key, input, output);
    }
    return 0;
}

/* ============================================================================
 * Round 240x240 RGB565 ILI9331 LCD Shim
 * ============================================================================ */

void LCD_Init(void) {
    s_lcd_sleeping = false;
}

void LCD_Reset(void) {
    s_lcd_sleeping = false;
}

void LCD_Sleep(void) {
    s_lcd_sleeping = true;
}

void LCD_Wake(void) {
    s_lcd_sleeping = false;
}

void LCD_DrawPixel(uint8_t x, uint8_t y, uint16_t color) {
    if (x < SC21_FB_W && y < SC21_FB_H) {
        s_lcd_fb[(size_t)y * SC21_FB_W + (size_t)x] = color;
    }
}

void LCD_DrawImage(int x, int y, int w, int h, uint16_t *data) {
    if (!data || w <= 0 || h <= 0) {
        return;
    }
    bool prev_trading        = s_in_trading_ui;
    bool prev_trade_complete = s_in_trade_complete_ui;
    s_in_trading_ui          = s_frame_saw_trade_img;
    s_in_trade_complete_ui   = s_frame_saw_trade2_img;
    s_frame_saw_trade_img    = false;
    s_frame_saw_trade2_img   = false;

    if (s_bot_trade_state == 0) {
        if (prev_trade_complete && !s_in_trade_complete_ui) {
            snprintf(
                s_bot_status_msg,
                sizeof(s_bot_status_msg),
                "Left Trading — select parts for Museum Bot to offer, then open Trading on the badge.");
        } else if (!prev_trading && s_in_trading_ui) {
            snprintf(
                s_bot_status_msg,
                sizeof(s_bot_status_msg),
                "Trading open — use A/D to pick a part and W/S to set count, then click Arm Museum Bot Trade.");
        } else if (prev_trading && !s_in_trading_ui && !s_in_trade_complete_ui) {
            snprintf(
                s_bot_status_msg,
                sizeof(s_bot_status_msg),
                "Idle — select parts for Museum Bot to offer, then open Trading on the badge.");
        }
    }
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= SC21_FB_H) {
            continue;
        }
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx >= 0 && dx < SC21_FB_W) {
                s_lcd_fb[dy * SC21_FB_W + dx] = data[row * w + col];
            }
        }
    }
    /* Advance virtual clock by realistic 16-bit parallel blit time and yield if frame budget elapsed */
    sc21_advance_time_ms(14u);
}

void LCD_FillRect(int x, int y, int w, int h, uint16_t color) {
    for (int row = 0; row < h; row++) {
        int dy = y + row;
        if (dy < 0 || dy >= SC21_FB_H) {
            continue;
        }
        for (int col = 0; col < w; col++) {
            int dx = x + col;
            if (dx >= 0 && dx < SC21_FB_W) {
                s_lcd_fb[dy * SC21_FB_W + dx] = color;
            }
        }
    }
}

void LCD_DrawLine(int x0, int y0, int x1, int y1, uint16_t color) {
    int dx  = abs(x1 - x0);
    int sx  = (x0 < x1) ? 1 : -1;
    int dy  = -abs(y1 - y0);
    int sy  = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (1) {
        if (x0 >= 0 && x0 < SC21_FB_W && y0 >= 0 && y0 < SC21_FB_H) {
            s_lcd_fb[y0 * SC21_FB_W + x0] = color;
        }
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* ============================================================================
 * Clean First-Party QTouch Replacement (Wheel Scroller 0 + Sensors 3..6)
 * ============================================================================ */

void touch_process(void) {
    ensure_inv_touch_lut();
    measurement_done_touch = 1;
    s_touch_tick_div++;

    bool    now_touched = false;
    uint8_t target_pos  = 0;

    if (s_api_touched) {
        now_touched = true;
        target_pos  = s_api_wheel_pos;
    } else if (s_ptr_touched) {
        now_touched = true;
        target_pos  = s_ptr_wheel_pos;
    } else if (s_buttons_mask & SC21_BTN_UP) {
        now_touched    = true;
        target_pos     = 0u;   /* 12 o'clock (+ in Trade, top in Game) */
        s_kb_wheel_pos = 0.0;
    } else if (s_buttons_mask & SC21_BTN_DOWN) {
        now_touched    = true;
        target_pos     = 128u; /* 6 o'clock (- in Trade, bottom in Game) */
        s_kb_wheel_pos = 128.0;
    } else if (s_in_trading_ui && (s_buttons_mask & SC21_BTN_LEFT) && !(s_buttons_mask & SC21_BTN_RIGHT)) {
        now_touched    = true;
        target_pos     = 192u; /* 9 o'clock (< prev part in Trade) */
        s_kb_wheel_pos = 192.0;
    } else if (s_in_trading_ui && (s_buttons_mask & SC21_BTN_RIGHT) && !(s_buttons_mask & SC21_BTN_LEFT)) {
        now_touched    = true;
        target_pos     = 64u;  /* 3 o'clock (> next part in Trade) */
        s_kb_wheel_pos = 64.0;
    } else if (s_buttons_mask & (SC21_BTN_LEFT | SC21_BTN_RIGHT | SC21_BTN_CENTER)) {
        now_touched = true;
        int ipos    = ((int)llround(s_kb_wheel_pos)) & 0xFF;
        target_pos  = (uint8_t)ipos;
    }

    if (!now_touched) {
        s_latched_touched = 0;
        return;
    }

    /* Update position immediately on touch-down or keyboard/API wheel input;
     * coalesce pointer-move angle updates to 40 ms (every 2nd 20 ms tick) so
     * normal mouse dial reversals exceed comboScene's 10-degree per-frame threshold. */
    bool was_touched = (s_latched_touched != 0);
    s_latched_touched = 1;
    if (!was_touched || !s_ptr_touched || (s_touch_tick_div & 1u) == 0u) {
        s_latched_raw_pos = s_inv_touch_lut[target_pos];
    }
}

uint8_t get_sensor_state(uint16_t sensor_node) {
    bool shackle_open =
        s_shackle_latched_open || s_shackle_pulse_pending || ((s_buttons_mask & SC21_BTN_SHACKLE) != 0u);

    switch (sensor_node) {
        case 3:
            return shackle_open ? KEY_TOUCHED_MASK : 0u;
        case 4:
            return shackle_open ? 0u : KEY_TOUCHED_MASK;
        case 5:
            s_shackle_pulse_pending = false;
            return shackle_open ? KEY_TOUCHED_MASK : 0u;
        case 6:
            if (s_shim_pulse_pending) {
                s_shim_pulse_pending = false;
                return KEY_TOUCHED_MASK;
            }
            return 0u;
        default:
            return 0u;
    }
}

uint8_t get_scroller_state(uint16_t sensor_node) {
    (void)sensor_node;
    return s_latched_touched;
}

uint16_t get_scroller_position(uint16_t sensor_node) {
    (void)sensor_node;
    return (uint16_t)s_latched_raw_pos;
}

/* ============================================================================
 * 8 MB External QSPI Flash (QUAD_SPI_0)
 * ============================================================================ */

static void ensure_qspi_initialized(void) {
    if (!s_qspi_flash_loaded) {
        memset(s_qspi_flash, 0xFF, sizeof(s_qspi_flash));
        memset(s_vcard_shadow, 0xFF, sizeof(s_vcard_shadow));
        s_qspi_flash_loaded = true;
    }
}

int32_t qspi_sync_enable(struct qspi_sync_descriptor *const descr) {
    ensure_qspi_initialized();
    if (descr) {
        descr->enabled = true;
    }
    return 0;
}

int32_t qspi_sync_serial_run_command(struct qspi_sync_descriptor *const descr, const struct _qspi_command *cmd) {
    (void)descr;
    ensure_qspi_initialized();
    if (!cmd) {
        return -1;
    }

    switch (cmd->instruction) {
        case 0x9Fu: /* JEDEC Read ID (W25Q64JV: EF 40 17) */
            if (cmd->rx_buf && cmd->buf_len >= 3u) {
                uint8_t *rx = (uint8_t *)cmd->rx_buf;
                rx[0] = 0xEFu;
                rx[1] = 0x40u;
                rx[2] = 0x17u;
            }
            break;

        case FLASH_STATUS: /* 0x05 Read Status Register-1: bit 0 = BUSY (0) */
            if (cmd->rx_buf && cmd->buf_len >= 1u) {
                ((uint8_t *)cmd->rx_buf)[0] = 0x00u;
            }
            break;

        case 0x35u: /* Read Status Register-2: QE bit set */
            if (cmd->rx_buf && cmd->buf_len >= 1u) {
                ((uint8_t *)cmd->rx_buf)[0] = 0x02u;
            }
            break;

        case FLASH_WRITE_EN:
        case FLASH_WRITE_DE:
        case FLASH_QPI_EN:
            break;

        case FLASH_READ: /* 0xEB Quad I/O Fast Read */
            if (cmd->address == TRADE_IMG) {
                s_frame_saw_trade_img = true;
            } else if (cmd->address == TRADE2_IMG) {
                s_frame_saw_trade2_img = true;
            }
            if (cmd->rx_buf && cmd->buf_len > 0u) {
                if (cmd->address < SC21_QSPI_SIZE) {
                    size_t avail = SC21_QSPI_SIZE - (size_t)cmd->address;
                    size_t n     = (cmd->buf_len <= avail) ? cmd->buf_len : avail;
                    memcpy(cmd->rx_buf, &s_qspi_flash[cmd->address], n);
                    if (n < cmd->buf_len) {
                        memset((uint8_t *)cmd->rx_buf + n, 0xFF, cmd->buf_len - n);
                    }
                } else {
                    memset(cmd->rx_buf, 0xFF, cmd->buf_len);
                }
            }
            break;

        case FLASH_WRITE_PAGE: /* 0x32 Quad Page Program (AND-mask onto erased 0xFF flash) */
            if (cmd->tx_buf && cmd->buf_len > 0u && cmd->address < SC21_QSPI_SIZE) {
                const uint8_t *tx = (const uint8_t *)cmd->tx_buf;
                size_t avail      = SC21_QSPI_SIZE - (size_t)cmd->address;
                size_t n          = (cmd->buf_len <= avail) ? cmd->buf_len : avail;
                for (size_t i = 0; i < n; i++) {
                    s_qspi_flash[cmd->address + i] &= tx[i];
                }
            }
            break;

        case FLASH_ERASE_4K: /* 0x20 4 KB Sector Erase */
            if (cmd->address < SC21_QSPI_SIZE) {
                uint32_t base = cmd->address & ~0xFFFu;
                memset(&s_qspi_flash[base], 0xFF, 4096u);
            }
            break;

        case FLASH_ERASE_32K: /* 0x52 32 KB Block Erase */
            if (cmd->address < SC21_QSPI_SIZE) {
                uint32_t base = cmd->address & ~0x7FFFu;
                memset(&s_qspi_flash[base], 0xFF, 32768u);
            }
            break;

        case FLASH_ERASE_ALL: /* 0xC7 Chip Erase */
            memset(s_qspi_flash, 0xFF, sizeof(s_qspi_flash));
            break;

        default:
            break;
    }
    return 0;
}

/* ============================================================================
 * Virtual ST25R95 SPI Transceiver & Solo "Museum Bot" Handshake
 * ============================================================================ */

static void st25_set_response(const uint8_t *bytes, uint16_t len) {
    if (len > ST25_MAX_RESP) {
        len = ST25_MAX_RESP;
    }
    memcpy(s_st25_resp_buf, bytes, len);
    s_st25_resp_len        = len;
    s_st25_resp_pos        = 0;
    s_st25_resp_ready      = true;
    s_gpio_level[PIN_PB09] = false;
}

static void st25_bot_on_page14_written(void) {
    if (s_bot_trade_state != 1) {
        return;
    }
    /* NDEF application/encrypted record starts at vtag_mem[16]:
     * [0]=0x03, [1]=40, [2]=0xD2, [3]=21, [4]=16, [5..25]="application/encrypted",
     * [26..41]=16-byte AES-128 ciphertext of msg_id=2. */
    if (memcmp(&s_vtag_mem[16 + 5], "application/encrypted", 21) != 0) {
        return;
    }

    uint8_t plain[16];
    sc21_aes128_ecb_decrypt(k_museum_trade_key, &s_vtag_mem[16 + 26], plain);
    const sc21_trade_msg_t *msg = (const sc21_trade_msg_t *)plain;
    if (msg->magic != 0x6575u || msg->msg_id != 2u || msg->nonce1 != s_bot_nonce) {
        return;
    }

    for (int i = 0; i < 4; i++) {
        s_bot_received[i] = msg->reqparts[i];
    }

    /* Compute confirmation SHA-1(nonce1 || nonce2 || bot_offer[8B] || badge_outgoing[8B]) */
    uint8_t hash_in[20];
    uint16_t nonces[2] = {msg->nonce1, msg->nonce2};
    memcpy(&hash_in[0], nonces, 4);
    memcpy(&hash_in[4], s_bot_offer, 8);
    memcpy(&hash_in[12], s_bot_received, 8);

    uint8_t sha_out[20];
    sc21_sha1_compute(hash_in, 20u, sha_out);

    /* Place first 16 bytes of SHA-1 into virtual tag as application/encrypted confirmation */
    vtag_write_ndef_mime("application/encrypted", sha_out, 16u);
    s_bot_trade_state = 2;

    char sent_desc[64] = "nothing";
    int  s_pos         = 0;
    for (int i = 0; i < 4; i++) {
        if (s_bot_received[i].part < 12 && s_bot_received[i].count > 0) {
            s_pos += snprintf(
                sent_desc + s_pos,
                sizeof(sent_desc) - (size_t)s_pos,
                "%s%ux %s",
                (s_pos > 0) ? ", " : "",
                (unsigned)s_bot_received[i].count,
                part_names[s_bot_received[i].part]);
        }
    }
    char got_desc[64] = "nothing";
    int  g_pos        = 0;
    for (int i = 0; i < 4; i++) {
        if (s_bot_offer[i].part < 12 && s_bot_offer[i].count > 0) {
            g_pos += snprintf(
                got_desc + g_pos,
                sizeof(got_desc) - (size_t)g_pos,
                "%s%ux %s",
                (g_pos > 0) ? ", " : "",
                (unsigned)s_bot_offer[i].count,
                part_names[s_bot_offer[i].part]);
        }
    }
    snprintf(
        s_bot_status_msg,
        sizeof(s_bot_status_msg),
        (g_state.badge_bitmask & UNLOCK_TRADE) == 0u
            ? "Trade complete! Sent %s, received %s (press Back on badge to claim Trade unlock and return to Menu)."
            : "Trade complete! Sent %s, received %s (press Back on badge to return to Menu).",
        sent_desc,
        got_desc);
}

static void st25_handle_sendrecv(const uint8_t *payload, uint8_t plen) {
    static const uint8_t k_timeout[3] = {0x00u, 0x87u, 0x00u};
    if (!s_vtag_present || plen == 0u) {
        st25_set_response(k_timeout, sizeof(k_timeout));
        return;
    }

    /* 1. REQA (26 07) */
    if (plen == 2u && payload[0] == 0x26u) {
        const uint8_t resp[8] = {0x00u, 0x80u, 0x05u, 0x44u, 0x00u, 0x28u, 0x00u, 0x00u};
        st25_set_response(resp, sizeof(resp));
        return;
    }
    /* 2. ANTICOLL CL1 (93 20 08) */
    if (plen >= 2u && payload[0] == 0x93u && payload[1] == 0x20u) {
        uint8_t bcc1 = (uint8_t)(0x88u ^ s_vtag_uid[0] ^ s_vtag_uid[1] ^ s_vtag_uid[2]);
        const uint8_t resp[9] = {
            0x00u, 0x80u, 0x06u, 0x88u, s_vtag_uid[0], s_vtag_uid[1], s_vtag_uid[2], bcc1, 0x28u,
        };
        st25_set_response(resp, sizeof(resp));
        return;
    }
    /* 3. SELECT CL1 (93 70 ...) */
    if (plen >= 2u && payload[0] == 0x93u && payload[1] == 0x70u) {
        const uint8_t resp[7] = {0x00u, 0x80u, 0x04u, 0x04u, 0xDAu, 0x17u, 0x08u};
        st25_set_response(resp, sizeof(resp));
        return;
    }
    /* 4. ANTICOLL CL2 (95 20 08) */
    if (plen >= 2u && payload[0] == 0x95u && payload[1] == 0x20u) {
        uint8_t bcc2 = (uint8_t)(s_vtag_uid[3] ^ s_vtag_uid[4] ^ s_vtag_uid[5] ^ s_vtag_uid[6]);
        const uint8_t resp[9] = {
            0x00u, 0x80u, 0x06u, s_vtag_uid[3], s_vtag_uid[4], s_vtag_uid[5], s_vtag_uid[6], bcc2, 0x28u,
        };
        st25_set_response(resp, sizeof(resp));
        return;
    }
    /* 5. SELECT CL2 (95 70 ...) */
    if (plen >= 2u && payload[0] == 0x95u && payload[1] == 0x70u) {
        const uint8_t resp[7] = {0x00u, 0x80u, 0x04u, 0x00u, 0xFEu, 0x51u, 0x08u};
        st25_set_response(resp, sizeof(resp));
        return;
    }
    /* 6. Type-2 READ 4 pages = 16 bytes (30 <page> 28) */
    if (plen >= 2u && payload[0] == 0x30u) {
        uint16_t byte_off = (uint16_t)payload[1] * 4u;
        uint8_t  resp[24];
        memset(resp, 0, sizeof(resp));
        resp[0] = 0x00u;
        resp[1] = 0x80u;
        resp[2] = 0x15u;
        if (byte_off + 16u <= VTAG_MEM_BYTES) {
            memcpy(&resp[3], &s_vtag_mem[byte_off], 16u);
        }
        resp[21] = 0x08u;
        st25_set_response(resp, sizeof(resp));
        /* If Museum Bot confirmation hash was just read past its final page, auto-clear bot tag
         * and latch state 3 ("completed") until the host/UI reads emu_2021_nfc_bot_trade_status(). */
        if (s_bot_trade_state == 2 && payload[1] >= 12u) {
            s_vtag_present    = false;
            s_bot_trade_state = 3;
        }
        return;
    }
    /* 7. Type-2 WRITE 1 page = 4 bytes (A2 <page> <b0..b3> 28) */
    if (plen >= 6u && payload[0] == 0xA2u) {
        uint8_t  page     = payload[1];
        uint16_t byte_off = (uint16_t)page * 4u;
        if (byte_off + 4u <= VTAG_MEM_BYTES) {
            memcpy(&s_vtag_mem[byte_off], &payload[2], 4u);
        }
        if (s_bot_trade_state == 1 && page == 14u) {
            st25_bot_on_page14_written();
        }
        const uint8_t resp[5] = {0x00u, 0x90u, 0x02u, 0x0Au, 0x24u};
        st25_set_response(resp, sizeof(resp));
        return;
    }

    st25_set_response(k_timeout, sizeof(k_timeout));
}

static void st25_process_command(const uint8_t *buf, uint16_t len) {
    static const uint8_t k_ok[3] = {0x00u, 0x00u, 0x00u};
    if (len < 2u) {
        return;
    }
    uint8_t cmd = buf[1];
    if (cmd == 0x55u) {
        /* Echo */
        const uint8_t echo_resp[3] = {0x00u, 0x55u, 0x00u};
        st25_set_response(echo_resp, sizeof(echo_resp));
        return;
    }

    uint8_t plen = (len >= 3u) ? buf[2] : 0u;
    const uint8_t *payload = (len >= 3u) ? &buf[3] : NULL;
    if ((uint16_t)plen + 3u > len) {
        plen = (len >= 3u) ? (uint8_t)(len - 3u) : 0u;
    }

    switch (cmd) {
        case 0x01u: { /* IDN: 13-char device name + 2-byte ROM CRC = 15 bytes */
            const uint8_t idn[18] = {
                0x00u, 0x00u, 15u,
                'N', 'F', 'C', ' ', 'F', 'S', '2', 'J', 'A', 'S', 'T', '4', 0x00u,
                0x35u, 0x22u,
            };
            st25_set_response(idn, sizeof(idn));
            break;
        }
        case 0x02u: /* ProtocolSelect */
            s_st25_listening = false;
            st25_set_response(k_ok, sizeof(k_ok));
            break;
        case 0x04u: /* SendRecv (Reader mode) */
            s_st25_listening = false;
            st25_handle_sendrecv(payload, plen);
            break;
        case 0x05u: /* Listen (Card-emulation mode) */
            s_st25_listening = true;
            st25_set_response(k_ok, sizeof(k_ok));
            break;
        case 0x06u: /* Send (Card-emulation reply) */
            if (plen == 17u && payload != NULL) {
                uint16_t dst_off = 16u + s_captured_read_idx * 16u;
                if (dst_off + 16u <= sizeof(s_captured_tag)) {
                    memcpy(&s_captured_tag[dst_off], payload, 16u);
                    s_captured_read_idx++;
                }
            }
            st25_set_response(k_ok, sizeof(k_ok));
            break;
        case 0x09u: /* WrReg */
        case 0x0Du: /* AcFilter */
        default:
            st25_set_response(k_ok, sizeof(k_ok));
            break;
    }
}

static void st25_on_cs_change(bool new_level) {
    if (!new_level) {
        /* CS falling edge: start new SPI transaction */
        s_st25_cs_low   = true;
        s_st25_cs_pos   = 0;
        s_st25_spi_ctrl = 0xFF;
        s_st25_cmd_len  = 0;
    } else if (s_st25_cs_low) {
        /* CS rising edge: finalize SPI transaction */
        s_st25_cs_low = false;
        if (s_st25_spi_ctrl == 0x00u) {
            st25_process_command(s_st25_cmd_buf, s_st25_cmd_len);
        } else if (s_st25_spi_ctrl == 0x01u) {
            s_st25_listening       = false;
            s_st25_resp_ready      = false;
            s_st25_resp_len        = 0;
            s_st25_resp_pos        = 0;
            s_gpio_level[PIN_PB09] = true;
        } else if (s_st25_spi_ctrl == 0x02u) {
            s_st25_resp_ready      = false;
            s_st25_resp_pos        = 0;
            s_gpio_level[PIN_PB09] = true;
        }
        s_st25_spi_ctrl = 0xFF;
    }
}

static int32_t spi1_io_write(struct io_descriptor *const io_descr, const uint8_t *const buf, const uint16_t length) {
    (void)io_descr;
    struct spi_xfer xfer = {
        .txbuf = (uint8_t *)buf,
        .rxbuf = NULL,
        .size  = length,
    };
    return spi_m_sync_transfer(&SPI_1, &xfer);
}

static int32_t spi1_io_read(struct io_descriptor *const io_descr, uint8_t *const buf, const uint16_t length) {
    (void)io_descr;
    struct spi_xfer xfer = {
        .txbuf = NULL,
        .rxbuf = buf,
        .size  = length,
    };
    return spi_m_sync_transfer(&SPI_1, &xfer);
}

int32_t spi_m_sync_get_io_descriptor(struct spi_m_sync_descriptor *const spi, struct io_descriptor **out_io) {
    if (!spi || !out_io) {
        return -1;
    }
    spi->io.write = spi1_io_write;
    spi->io.read  = spi1_io_read;
    *out_io       = &spi->io;
    return 0;
}

int32_t spi_m_sync_enable(struct spi_m_sync_descriptor *const spi) {
    if (spi) {
        spi->enabled = true;
    }
    return 0;
}

int32_t spi_m_sync_transfer(struct spi_m_sync_descriptor *const spi, const struct spi_xfer *xfer) {
    (void)spi;
    if (!xfer) {
        return -1;
    }
    for (uint32_t i = 0; i < xfer->size; i++) {
        uint8_t tx = xfer->txbuf ? xfer->txbuf[i] : 0x00u;
        uint8_t rx = 0x00u;

        if (s_st25_cs_pos == 0u) {
            s_st25_spi_ctrl = tx;
            if (s_st25_spi_ctrl == 0x02u) {
                s_st25_resp_pos = 0;
            }
        }

        if (s_st25_spi_ctrl == 0x00u) {
            if (s_st25_cmd_len < sizeof(s_st25_cmd_buf)) {
                s_st25_cmd_buf[s_st25_cmd_len++] = tx;
            }
        } else if (s_st25_spi_ctrl == 0x02u) {
            if (s_st25_resp_pos < s_st25_resp_len) {
                rx = s_st25_resp_buf[s_st25_resp_pos++];
            }
        }

        if (xfer->rxbuf) {
            xfer->rxbuf[i] = rx;
        }
        s_st25_cs_pos++;
    }
    return (int32_t)xfer->size;
}

/* ============================================================================
 * Virtual I2C_0 Minibadge Bus (0x28)
 * ============================================================================ */

static int32_t i2c0_io_write(struct io_descriptor *const io_descr, const uint8_t *const buf, const uint16_t length) {
    (void)io_descr;
    if (I2C_0.slave_addr == SC21_MB_ADDR && s_mb_present) {
        if (buf && length >= 2u) {
            s_mb_last_reg[0] = buf[0];
            s_mb_last_reg[1] = buf[1];
            s_mb_read_stage  = 0;
        }
        return (int32_t)length;
    }
    return -1; /* NACK unpopulated addresses */
}

static int32_t i2c0_io_read(struct io_descriptor *const io_descr, uint8_t *const buf, const uint16_t length) {
    (void)io_descr;
    if (I2C_0.slave_addr != SC21_MB_ADDR || !s_mb_present || !buf || length == 0u) {
        return -1;
    }
    if (s_mb_last_reg[0] == 0x00u && s_mb_last_reg[1] == 0x01u) {
        if (s_mb_read_stage == 0u && length == 1u) {
            if ((g_state.badge_bitmask & UNLOCK_MINI) == 0u) {
                /* Let initial discovery award UNLOCK_MINI REWARD before consuming queued button/text */
                buf[0] = 0u;
            } else if (s_mb_btn_pending) {
                s_mb_btn_pending = false;
                buf[0]           = 1u;
            } else if (s_mb_msg_pending) {
                buf[0]          = 2u;
                s_mb_read_stage = 1u;
            } else {
                buf[0] = 0u;
            }
            return 1;
        }
        if (s_mb_read_stage == 1u && length == 1u) {
            buf[0]          = s_mb_msg_len;
            s_mb_read_stage = 2u;
            return 1;
        }
        if (s_mb_read_stage == 2u) {
            uint8_t n = (length <= s_mb_msg_len) ? (uint8_t)length : s_mb_msg_len;
            memcpy(buf, s_mb_msg_buf, n);
            s_mb_msg_pending = false;
            s_mb_read_stage  = 0u;
            return (int32_t)n;
        }
    }
    return -1;
}

int32_t i2c_m_sync_get_io_descriptor(struct i2c_m_sync_desc *const i2c, struct io_descriptor **out_io) {
    if (!i2c || !out_io) {
        return -1;
    }
    i2c->io.write = i2c0_io_write;
    i2c->io.read  = i2c0_io_read;
    *out_io       = &i2c->io;
    return 0;
}

int32_t i2c_m_sync_enable(struct i2c_m_sync_desc *const i2c) {
    if (i2c) {
        i2c->enabled = true;
    }
    return 0;
}

int32_t i2c_m_sync_set_slaveaddr(struct i2c_m_sync_desc *const i2c, int16_t addr, int32_t addr_len) {
    (void)addr_len;
    if (i2c) {
        i2c->slave_addr = addr;
    }
    return 0;
}

/* ============================================================================
 * USB CDC Disabled Stubs (cdcd_loop drives idle virtual clock & yield)
 * ============================================================================ */

void cdcd_loop(void) {
    sc21_advance_time_ms(2u);
}

int32_t cdcWrite(const char *const buf, const uint16_t length) {
    (void)buf;
    return (int32_t)length;
}

/* ============================================================================
 * Generalized Badge Emulator Core ABI
 * ============================================================================ */

static void sc21_run_for_ms(uint32_t duration_ms) {
    s_step_target_ms = MS_Timer + duration_ms;
    int guard = 0;
    while ((int32_t)(MS_Timer - s_step_target_ms) < 0 && guard < 64) {
        guard++;
        uint32_t remaining = s_step_target_ms - MS_Timer;
        sc21_resume_fw_fiber();
        if (s_soft_reset_requested) {
            s_soft_reset_count++;
            sc21_reset_runtime_state(false);
            sc21_init_fw_fiber();
            s_step_target_ms = MS_Timer + remaining;
        }
    }
}

void emu_init(uint32_t seed) {
    ensure_qspi_initialized();
    RAND_0.state = (seed != 0u) ? seed : 0x20215343u;
    srand((unsigned int)RAND_0.state);

    memcpy(s_seeprom_shadow, sc21_seeprom_buf, SC21_EEPROM_BYTES);
    memcpy(s_vcard_shadow, &s_qspi_flash[SC21_VCARD_OFFSET], SC21_VCARD_BYTES);

    s_buttons_mask       = 0;
    s_prev_buttons_mask  = 0;
    s_ptr_touched        = false;
    s_api_touched        = false;
    s_kb_spin_dir        = 0;
    s_latched_touched       = 0;
    s_latched_raw_pos       = 0;
    s_shackle_pulse_pending = false;
    s_shim_pulse_pending    = false;
    s_soft_reset_count      = 0;
    s_bot_trade_state       = 0;
    s_vtag_present          = false;

    sc21_reset_runtime_state(true);
    sc21_init_fw_fiber();

    /* Run initial boot + first frame (~60 ms covers nfc_reset delays + first canvas_blt) */
    sc21_run_for_ms(60u);
}

void emu_step(uint32_t elapsed_ms, uint32_t buttons_mask) {
    if (elapsed_ms == 0u) {
        elapsed_ms = 1u;
    }
    if (elapsed_ms > 250u) {
        elapsed_ms = 250u;
    }

    uint32_t rising = buttons_mask & ~s_prev_buttons_mask;
    s_prev_buttons_mask = buttons_mask;
    s_buttons_mask      = buttons_mask;

    if (rising & SC21_BTN_BACK) {
        s_pending_back_irq = true;
    }
    if (rising & SC21_BTN_SHACKLE) {
        s_shackle_pulse_pending = true;
    }
    sc21_update_keyboard_wheel(MS_Timer);

    uint32_t scaled_ms = elapsed_ms * (s_speed_mult > 0u ? s_speed_mult : 1u);
    sc21_run_for_ms(scaled_ms);
}

const uint16_t *emu_framebuffer(void) {
    return s_lcd_fb;
}

const uint8_t *emu_leds(void) {
    return s_rgb_led;
}

uint8_t emu_backlight(void) {
    return (s_gpio_level[PIN_PB17] && !s_lcd_sleeping) ? 255u : 0u;
}

uint8_t *emu_eeprom(void) {
    return sc21_seeprom_buf;
}

int emu_eeprom_dirty(void) {
    if (memcmp(s_seeprom_shadow, sc21_seeprom_buf, SC21_EEPROM_BYTES) != 0) {
        memcpy(s_seeprom_shadow, sc21_seeprom_buf, SC21_EEPROM_BYTES);
        return 1;
    }
    return 0;
}

void emu_set_speed(uint32_t multiplier) {
    s_speed_mult = (multiplier >= 1u && multiplier <= 600u) ? multiplier : 1u;
}

int emu_fb_width(void) {
    return SC21_FB_W;
}

int emu_fb_height(void) {
    return SC21_FB_H;
}

int emu_led_count(void) {
    return 1;
}

int emu_eeprom_size(void) {
    return (int)SC21_EEPROM_BYTES;
}

void emu_set_touch(int pressed, int x, int y) {
    if (!pressed) {
        s_ptr_touched = false;
        return;
    }
    double dx = (double)x - 120.0;
    double dy = (double)y - 120.0;
    double deg = atan2(dx, -dy) * (180.0 / 3.14159265358979323846);
    if (deg < 0.0) {
        deg += 360.0;
    }
    int pos = ((int)llround(deg * 256.0 / 360.0)) & 0xFF;
    s_ptr_wheel_pos = (uint8_t)pos;
    s_kb_wheel_pos  = (double)pos;
    s_ptr_touched   = true;
}

/* ============================================================================
 * 2021-Specific Companion Exports
 * ============================================================================ */

void emu_2021_load_flash(const uint8_t *data, uint32_t len) {
    ensure_qspi_initialized();
    /* Preserve the 512-byte registration vCard sector at 0x7F8000 across sprite flash load */
    uint8_t saved_vcard[SC21_VCARD_BYTES];
    memcpy(saved_vcard, &s_qspi_flash[SC21_VCARD_OFFSET], SC21_VCARD_BYTES);

    memset(s_qspi_flash, 0xFF, sizeof(s_qspi_flash));
    if (data && len > 0u) {
        uint32_t n = (len <= SC21_VCARD_OFFSET) ? len : SC21_VCARD_OFFSET;
        memcpy(s_qspi_flash, data, n);
    }
    memcpy(&s_qspi_flash[SC21_VCARD_OFFSET], saved_vcard, SC21_VCARD_BYTES);
}

uint8_t *emu_2021_vcard_ptr(void) {
    ensure_qspi_initialized();
    return &s_qspi_flash[SC21_VCARD_OFFSET];
}

int emu_2021_vcard_size(void) {
    return (int)SC21_VCARD_BYTES;
}

int emu_2021_vcard_dirty(void) {
    ensure_qspi_initialized();
    if (memcmp(s_vcard_shadow, &s_qspi_flash[SC21_VCARD_OFFSET], SC21_VCARD_BYTES) != 0) {
        memcpy(s_vcard_shadow, &s_qspi_flash[SC21_VCARD_OFFSET], SC21_VCARD_BYTES);
        return 1;
    }
    return 0;
}

void emu_2021_erase_vcard(void) {
    ensure_qspi_initialized();
    memset(&s_qspi_flash[SC21_VCARD_OFFSET], 0xFF, 4096u);
}

void emu_2021_set_shackle(int open) {
    if (open > 0) {
        s_shackle_latched_open  = true;
        s_shackle_pulse_pending = true;
    } else {
        s_shackle_latched_open = false;
        if (open < 0) {
            s_shackle_pulse_pending = false;
        }
    }
}

int emu_2021_get_shackle(void) {
    return (s_shackle_latched_open || s_shackle_pulse_pending || ((s_buttons_mask & SC21_BTN_SHACKLE) != 0u)) ? 1 : 0;
}

void emu_2021_pulse_shim(void) {
    s_shim_pulse_pending = true;
}

void emu_2021_set_wheel(int touched, int angle_deg) {
    ensure_inv_touch_lut();
    if (!touched) {
        s_api_touched = false;
        return;
    }
    int deg = angle_deg % 360;
    if (deg < 0) {
        deg += 360;
    }
    int pos = ((int)llround((double)deg * 256.0 / 360.0)) & 0xFF;
    s_api_wheel_pos   = (uint8_t)pos;
    s_kb_wheel_pos    = (double)pos;
    s_api_touched     = true;
    s_latched_touched = 1;
    s_latched_raw_pos = s_inv_touch_lut[pos];
}

int emu_2021_nfc_quote_count(void) {
    return 21;
}

const char *emu_2021_nfc_quote_label(int index) {
    if (index < 0 || index >= 21) {
        return "";
    }
    return k_nfc_quotes[index];
}

int emu_2021_nfc_present_text_tag(const char *text) {
    if (!text) {
        return -1;
    }
    size_t tlen = strlen(text);
    if (tlen > 220u) {
        tlen = 220u;
    }

    s_bot_trade_state = 0;
    vtag_init_header();

    uint8_t payload_len = (uint8_t)(3u + tlen);
    uint8_t tlv_len     = (uint8_t)(4u + payload_len);
    uint8_t *dst        = &s_vtag_mem[16];
    dst[0] = 0x03u;       /* NDEF Message TLV */
    dst[1] = tlv_len;
    dst[2] = 0xD1u;       /* MB | ME | SR | TNF_WELL_KNOWN */
    dst[3] = 0x01u;       /* Type length = 1 */
    dst[4] = payload_len;
    dst[5] = (uint8_t)'T';
    dst[6] = 0x02u;       /* UTF-8, 2-byte language code */
    dst[7] = (uint8_t)'e';
    dst[8] = (uint8_t)'n';
    memcpy(&dst[9], text, tlen);
    dst[9 + tlen] = 0xFEu;

    s_vtag_present = true;
    return 0;
}

int emu_2021_nfc_present_quote_tag(int index) {
    if (index < 0 || index >= 21) {
        return -1;
    }
    return emu_2021_nfc_present_text_tag(k_nfc_quotes[index]);
}

void emu_2021_nfc_clear_tag(void) {
    s_vtag_present    = false;
    s_bot_trade_state = 0;
}

static bool rf_queue_push(const uint8_t *bytes, uint8_t len) {
    if (s_rf_q_count >= ST25_MAX_RF_Q || len > sizeof(s_rf_queue[0].bytes)) {
        return false;
    }
    s_rf_queue[s_rf_q_tail].len = len;
    memcpy(s_rf_queue[s_rf_q_tail].bytes, bytes, len);
    s_rf_q_tail = (uint16_t)((s_rf_q_tail + 1u) % ST25_MAX_RF_Q);
    s_rf_q_count++;
    return true;
}

int emu_2021_nfc_last_vcard_status(void) {
    return s_last_vcard_program_rc;
}

int emu_2021_nfc_program_vcard(const char *vcard_body) {
    /* Build NDEF text/vcard payload containing upstream registration magic:
     *   BEGIN:VCARD
     *   VERSION:3.0
     *   <caller lines, stripped of BEGIN/END/VERSION/PRODID>
     *   PRODID:-//saintcon_2021_magic
     *   END:VCARD
     * Derive limit from main.c:83-87:
     *   uint8_t lc[256]; memcpy(lc, TAG_BUFF, 256);
     *   vcard_write_callback(&lc[0x15+10]); // offset 31
     * With 0xFE (NDEF TLV end) and NUL after END:VCARD inside lc[0..255],
     * full_vcard can be at most 256 - 31 - 2 = 223 bytes (64-byte wrapper +
     * up to 159 UTF-8 bytes of caller field text). */
    char clean_lines[256];
    size_t cpos = 0;
    if (vcard_body && vcard_body[0] != '\0') {
        const char *p = vcard_body;
        while (*p != '\0') {
            const char *eol = p;
            while (*eol != '\0' && *eol != '\r' && *eol != '\n') {
                eol++;
            }
            size_t llen = (size_t)(eol - p);
            if (llen > 0u &&
                strncmp(p, "BEGIN:VCARD", 11) != 0 &&
                strncmp(p, "END:VCARD", 9) != 0 &&
                strncmp(p, "VERSION:", 8) != 0 &&
                strncmp(p, "PRODID:", 7) != 0) {
                if (cpos + llen + 1u >= sizeof(clean_lines)) {
                    s_last_vcard_program_rc = -1;
                    return -1;
                }
                memcpy(&clean_lines[cpos], p, llen);
                cpos += llen;
                clean_lines[cpos++] = '\n';
            }
            p = eol;
            while (*p == '\r' || *p == '\n') {
                p++;
            }
        }
    }
    if (cpos == 0u) {
        const char *fallback =
            "FN:Badge Visitor\n"
            "ORG:SAINTCON Badge Museum\n"
            "EMAIL:visitor@example.invalid\n";
        cpos = strlen(fallback);
        memcpy(clean_lines, fallback, cpos);
    }
    clean_lines[cpos] = '\0';

    char full_vcard[320];
    int vlen = snprintf(
        full_vcard,
        sizeof(full_vcard),
        "BEGIN:VCARD\nVERSION:3.0\n%sPRODID:-//saintcon_2021_magic\nEND:VCARD",
        clean_lines);
    if (vlen <= 0 || vlen > 223) {
        s_last_vcard_program_rc = -1;
        return -1;
    }

    uint8_t ndef_pages[256];
    memset(ndef_pages, 0, sizeof(ndef_pages));
    uint8_t mlen    = 10u; /* "text/vcard" */
    uint8_t dlen    = (uint8_t)vlen;
    uint8_t tlv_len = (uint8_t)(mlen + dlen + 3u);

    ndef_pages[0] = 0x03u;
    ndef_pages[1] = tlv_len;
    ndef_pages[2] = 0xD2u;
    ndef_pages[3] = mlen;
    ndef_pages[4] = dlen;
    memcpy(&ndef_pages[5], "text/vcard", mlen);
    memcpy(&ndef_pages[5 + mlen], full_vcard, dlen);
    ndef_pages[5 + mlen + dlen] = 0xFEu;

    /* Match nfc.c:93 end-page formula: (tlv_len/4) + ((tlv_len-1)%4 > 0) + 4 */
    uint8_t last_page = (uint8_t)((tlv_len / 4u) + (((tlv_len - 1u) % 4u) > 0u ? 1u : 0u) + 4u);
    s_rf_q_head  = 0;
    s_rf_q_tail  = 0;
    s_rf_q_count = 0;

    /* Reject immediately if ST25R95 is not in card-emulation Listen mode (e.g. Read Tag
     * reader mode or finished Trade screen) without leaving stale RF frames queued. */
    if (!(s_st25_listening && s_ext_irq_en[PIN_PB09] && s_ext_irq_cb[PIN_PB09] != NULL)) {
        s_last_vcard_program_rc = -2;
        return -2;
    }

    /* Zero the page immediately after last_page before writing page 4..last_page so
     * TAG_BUFF always has a NUL byte after 0xFE even if TAG_BUFF previously held a
     * longer NDEF payload and tlv_len % 4 == 1. */
    if ((uint16_t)(last_page + 1u) < (uint16_t)(TAG_BUFF_LEN / 4u)) {
        uint8_t zero_rf[12] = {
            0x00u, 0x80u, 0x09u,
            0xA2u, (uint8_t)(last_page + 1u),
            0x00u, 0x00u, 0x00u, 0x00u,
            0x00u, 0x00u, 0x08u,
        };
        if (!rf_queue_push(zero_rf, sizeof(zero_rf))) {
            s_rf_q_head             = 0;
            s_rf_q_tail             = 0;
            s_rf_q_count            = 0;
            s_last_vcard_program_rc = -1;
            return -1;
        }
    }

    for (uint8_t page = 4u; page <= last_page; page++) {
        uint16_t off = (uint16_t)(page - 4u) * 4u;
        uint8_t  rf[12] = {
            0x00u, 0x80u, 0x09u,
            0xA2u, page,
            ndef_pages[off + 0u],
            ndef_pages[off + 1u],
            ndef_pages[off + 2u],
            ndef_pages[off + 3u],
            0x00u, 0x00u, 0x08u,
        };
        if (!rf_queue_push(rf, sizeof(rf))) {
            s_rf_q_head             = 0;
            s_rf_q_tail             = 0;
            s_rf_q_count            = 0;
            s_last_vcard_program_rc = -1;
            return -1;
        }
    }

    uint32_t resets_before = s_soft_reset_count;

    /* Step firmware so PIN_PB09 IRQ writes all pages -> vcard_write_callback -> NVIC_SystemReset -> MENU */
    sc21_run_for_ms(80u);

    /* Never leave undelivered RF frames queued */
    s_rf_q_head  = 0;
    s_rf_q_tail  = 0;
    s_rf_q_count = 0;

    if (s_soft_reset_count == resets_before || s_qspi_flash[SC21_VCARD_OFFSET] == 0xFFu) {
        s_last_vcard_program_rc = -2;
        return -2;
    }
    s_last_vcard_program_rc = 0;
    return 0;
}

int emu_2021_nfc_read_emulated_tag(char *out_buf, int max_len) {
    if (!out_buf || max_len <= 1) {
        return 0;
    }
    out_buf[0] = '\0';

    memset(s_captured_tag, 0, sizeof(s_captured_tag));
    s_captured_read_idx = 0;
    s_rf_q_head         = 0;
    s_rf_q_tail         = 0;
    s_rf_q_count        = 0;

    /* Queue Type-2 READ commands for pages 4, 8, ..., 72 (18 * 16 = 288 bytes) */
    for (uint8_t page = 4u; page <= 72u; page = (uint8_t)(page + 4u)) {
        uint8_t rf[8] = {0x00u, 0x80u, 0x05u, 0x30u, page, 0x00u, 0x00u, 0x08u};
        rf_queue_push(rf, sizeof(rf));
    }

    sc21_run_for_ms(24u);

    /* Never leave undelivered READ frames queued when badge is in reader mode */
    s_rf_q_head  = 0;
    s_rf_q_tail  = 0;
    s_rf_q_count = 0;

    const uint8_t *ndef = &s_captured_tag[16];
    if (ndef[0] != 0x03u) {
        return snprintf(
            out_buf,
            (size_t)max_len,
            "(No active emulated NDEF tag — badge is in NFC reader mode; open Read Tag and press Back, or use Reset Badge)");
    }

    uint8_t flags = ndef[2];
    uint8_t tnf   = flags & 0x07u;
    uint8_t tlen  = ndef[3];
    uint8_t plen  = ndef[4];

    if (tnf == 0x01u && tlen == 1u && ndef[5] == 'U' && plen >= 1u) {
        const char *prefix = "";
        switch (ndef[6]) {
            case 0x01u: prefix = "http://www."; break;
            case 0x02u: prefix = "https://www."; break;
            case 0x03u: prefix = "http://"; break;
            case 0x04u: prefix = "https://"; break;
            default: break;
        }
        int body_len = (int)plen - 1;
        if (body_len < 0) body_len = 0;
        return snprintf(out_buf, (size_t)max_len, "URI: %s%.*s", prefix, body_len, (const char *)&ndef[7]);
    }

    if (tnf == 0x02u && (size_t)(5u + tlen + plen) <= sizeof(s_captured_tag) - 16u) {
        if (tlen == 10u && memcmp(&ndef[5], "text/vcard", 10) == 0) {
            return snprintf(out_buf, (size_t)max_len, "%.*s", (int)plen, (const char *)&ndef[15]);
        }
        if (tlen == 21u && memcmp(&ndef[5], "application/encrypted", 21) == 0) {
            return snprintf(out_buf, (size_t)max_len, "Encrypted Trade Offer (application/encrypted, %u bytes)", (unsigned)plen);
        }
        return snprintf(
            out_buf,
            (size_t)max_len,
            "MIME (%.*s): %.*s",
            (int)tlen,
            (const char *)&ndef[5],
            (int)plen,
            (const char *)&ndef[5 + tlen]);
    }

    return snprintf(out_buf, (size_t)max_len, "NDEF TLV (%u bytes)", (unsigned)ndef[1]);
}

int emu_2021_nfc_start_bot_trade(int part0, int count0, int part1, int count1) {
    for (int i = 0; i < 4; i++) {
        s_bot_offer[i].part     = none;
        s_bot_offer[i].count    = 0;
        s_bot_received[i].part  = none;
        s_bot_received[i].count = 0;
    }

    int slot = 0;
    if (part0 >= 0 && part0 < 12 && count0 > 0) {
        s_bot_offer[slot].part  = (part_enum)part0;
        s_bot_offer[slot].count = (uint8_t)(count0 > 20 ? 20 : count0);
        slot++;
    }
    if (part1 >= 0 && part1 < 12 && count1 > 0 && part1 != part0) {
        s_bot_offer[slot].part  = (part_enum)part1;
        s_bot_offer[slot].count = (uint8_t)(count1 > 20 ? 20 : count1);
        slot++;
    }
    if (slot == 0) {
        s_bot_offer[0].part  = circuit;
        s_bot_offer[0].count = 2;
    }

    s_bot_nonce = (uint16_t)((rand_sync_read32(&RAND_0) & 0xFFFEu) | 1u);

    sc21_trade_msg_t msg;
    msg.magic  = 0x6575u;
    msg.msg_id = 1u;
    msg.nonce1 = s_bot_nonce;
    msg.nonce2 = 0u;
    for (int i = 0; i < 4; i++) {
        msg.reqparts[i] = s_bot_offer[i];
    }

    uint8_t enc[16];
    sc21_aes128_ecb_encrypt(k_museum_trade_key, (const uint8_t *)&msg, enc);
    vtag_write_ndef_mime("application/encrypted", enc, 16u);

    s_bot_trade_state = 1;
    s_vtag_present    = true;

    if (s_bot_offer[1].part < 12 && s_bot_offer[1].count > 0) {
        snprintf(
            s_bot_status_msg,
            sizeof(s_bot_status_msg),
            "Museum Bot tag ready: offering +%ux %s, +%ux %s (in Trading, trade runs automatically; press Back when done).",
            (unsigned)s_bot_offer[0].count,
            part_names[s_bot_offer[0].part],
            (unsigned)s_bot_offer[1].count,
            part_names[s_bot_offer[1].part]);
    } else {
        snprintf(
            s_bot_status_msg,
            sizeof(s_bot_status_msg),
            "Museum Bot tag ready: offering +%ux %s (in Trading, trade runs automatically; press Back when done).",
            (unsigned)s_bot_offer[0].count,
            part_names[s_bot_offer[0].part]);
    }
    return 0;
}

int emu_2021_nfc_bot_trade_status(char *out_buf, int max_len) {
    if (out_buf && max_len > 0) {
        snprintf(out_buf, (size_t)max_len, "%s", s_bot_status_msg);
    }
    int st = s_bot_trade_state;
    if (s_bot_trade_state == 3) {
        s_bot_trade_state = 0;
    }
    return st;
}

void emu_2021_mb_set_present(int present) {
    s_mb_present = (present != 0);
    if (!s_mb_present) {
        s_mb_btn_pending = false;
        s_mb_msg_pending = false;
        s_mb_read_stage  = 0;
    }
}

int emu_2021_mb_get_present(void) {
    return s_mb_present ? 1 : 0;
}

void emu_2021_mb_press_button(void) {
    s_mb_present     = true;
    s_mb_btn_pending = true;
}

void emu_2021_mb_send_text(const char *ascii_text) {
    s_mb_present = true;
    if (!ascii_text || ascii_text[0] == '\0') {
        ascii_text = "SAINTCON 2021";
    }
    size_t len = strlen(ascii_text);
    if (len > 120u) {
        len = 120u;
    }
    memcpy(s_mb_msg_buf, ascii_text, len);
    s_mb_msg_buf[len] = '\0';
    s_mb_msg_len      = (uint8_t)len;
    s_mb_msg_pending  = true;
    s_mb_read_stage   = 0;
}
