/*
 * SAINTCON 2020 LED Badge — ATtiny84A Hardware & Peripheral Emulator Shim
 *
 * Runs upstream Firmware/Saintcon2020/main.cpp (558 lines) completely unpatched:
 *   - 1 kHz Timer1 Compare Match A ISR (`ISR(TIM1_COMPA_vect)`) driving PORTA (`PA0..PA7` -> `D1..D8`)
 *     in 20-step software PWM (`pwmcounter` stepping by 5 from 1 to 96 after the initial cycle).
 *   - LED brightness on the page is computed strictly from the real writes to `PORTA` over the
 *     20-tick (20 ms) PWM window, never from reading `LEDS[]`. Because no hardware file states an
 *     LED colour, `emu_leds()` maps each LED's 20-tick duty through `kPwmCurve20` to a neutral
 *     warm-white RGB triple `(I, round(I*232/255), round(I*168/255))`.
 *   - Mode button `SW1` on `PB2` (active-low with internal pull-up), debounced 50 ms by `isButtonPressed()`.
 *   - Test pads `TP1` (`PB0`) and `TP2` (`PB1`): holding `TP1` low at boot (`(PINB & (1<<TP1PIN)) == 0`
 *     at `main.cpp:153`) enters `specialModes()`, and `PORTB` toggles the `PB0` (`TP1`) pull-up
 *     (`main.cpp:89` leaves `DDRB` bit 0 commented out) `displayState + 1` times (`1..8` toggles) at 1 s
 *     intervals in both normal and special modes (`main.cpp:385-401, 540-556`).
 *   - 512-byte ATtiny84A EEPROM with address 0 storing `displayState`.
 */

#include "avr/io.h"
#include "avr/interrupt.h"
#include "avr/eeprom.h"
#include "emu_2020.h"

#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#define EMSCRIPTEN_KEEPALIVE
#endif

/* AVR I/O register instances referenced by main.cpp */
volatile uint8_t GIMSK = 0;
volatile uint8_t PCMSK1 = 0;
volatile uint8_t DDRA = 0;
volatile uint8_t DDRB = 0;
volatile AvrPortAReg PORTA = {0};
volatile AvrPortBReg PORTB = {0};
volatile AvrPinBReg PINB;
volatile uint8_t TCCR1A = 0;
volatile uint8_t TCCR1B = 0;
volatile uint16_t TCNT1 = 0;
volatile uint16_t OCR1A = 0;
volatile uint8_t TIMSK1 = 0;

/* Firmware globals from main.cpp (for clean power-on reset across emu_reset()) */
extern volatile uint8_t LEDS[8];
extern volatile avr_uint16_t millis;
extern volatile uint8_t milliscounter;
extern volatile avr_uint16_t lastIntTime;
extern volatile uint8_t pwmvalue;
extern volatile uint8_t ledcounter;
extern volatile uint8_t pwmcounter;
extern uint8_t displayState;
extern avr_uint16_t lastDisplayUpdate;
extern avr_uint16_t buttonDebounce;
extern uint8_t displayCounter;
extern uint8_t chaseDelay;
extern uint8_t ringDelay;
extern uint8_t waveDelay;
extern uint8_t pulseDelay;
extern avr_uint16_t alternateDelay;
extern avr_uint16_t alternate2Delay;
extern avr_uint16_t circleDelay;
extern bool countUp;
extern avr_uint16_t lastTpUpdate;
extern avr_uint16_t tpDelay;
extern bool tpState;
extern uint8_t tpCounter;
extern uint8_t numStates;
extern bool lastButtonState;

extern uint8_t isButtonPressed(void);
extern int badge_2020_fw_main(void);
extern "C" void __avr_isr_tim1_compa(void);

/* 21-point monotonic brightness curve mapping PWM high-tick count (0..20) to 8-bit intensity (0..255) */
static const uint8_t kPwmCurve20[21] = {
    0,   42,  58,  72,  85,  98,  110, 122, 134, 145, 156,
    167, 178, 188, 198, 208, 218, 228, 237, 246, 255
};

static uint16_t g_fb[EMU_2020_FB_W * EMU_2020_FB_H];
static uint8_t g_leds[EMU_2020_LED_COUNT * 3];
static uint8_t g_eeprom[EMU_2020_EEPROM_SZ];
static int g_eeprom_inited = 0;
static int g_eeprom_dirty = 0;

/* Sliding 20-tick (20 ms = 1 full 50 Hz PWM cycle at 1 kHz ISR) ring buffer of real PORTA writes */
static uint8_t g_porta_pwm_ring[20];
static int g_porta_pwm_head = 0;
static int g_porta_pwm_count = 0;
static uint8_t g_porta_latest = 0;
static uint8_t g_portb_latest = 0;
static int g_led_duty[EMU_2020_LED_COUNT];

/* Button & TP1/TP2 state */
static int g_button_pressed = 0;
static int g_tp1_boot_low = 0;
static int g_in_special_mode = 0;
static int g_tp_pulse_count = 0;
static int g_ie = 0;
static int g_in_reset_btn = 0;
static int g_pinb_reads_since_boot = 0;

/* Cooperative fiber & 1 kHz timing state */
static uint32_t g_speed_mult = 1;
static int g_ms_budget = 0;
static int g_polls_this_ms = 0;
static int g_fiber_started = 0;

#define FIBER_STACK_SZ (128 * 1024)
#ifdef __EMSCRIPTEN__
static uint8_t g_c_stack[FIBER_STACK_SZ] __attribute__((aligned(16)));
static uint8_t g_async_stack[FIBER_STACK_SZ] __attribute__((aligned(16)));
static uint8_t g_main_async_stack[4096] __attribute__((aligned(16)));
static emscripten_fiber_t g_fw_fiber;
static emscripten_fiber_t g_host_fiber;
static int g_host_fiber_inited = 0;
#else
static uint8_t g_c_stack[FIBER_STACK_SZ] __attribute__((aligned(16)));
static ucontext_t g_fw_ctx;
static ucontext_t g_host_ctx;
#endif

static void emu_yield_to_host(void) {
#ifdef __EMSCRIPTEN__
  emscripten_fiber_swap(&g_fw_fiber, &g_host_fiber);
#else
  swapcontext(&g_fw_ctx, &g_host_ctx);
#endif
}

static void sync_leds_from_porta_ring(void) {
  int n = g_porta_pwm_count;
  if (n <= 0) {
    memset(g_leds, 0, sizeof(g_leds));
    memset(g_led_duty, 0, sizeof(g_led_duty));
    return;
  }
  for (int bit = 0; bit < EMU_2020_LED_COUNT; bit++) {
    int high_ticks = 0;
    uint8_t mask = (uint8_t)(1U << bit);
    for (int k = 0; k < 20; k++) {
      if (g_porta_pwm_ring[k] & mask) {
        high_ticks++;
      }
    }
    int duty20 = high_ticks;
    if (duty20 < 0) duty20 = 0;
    if (duty20 > 20) duty20 = 20;
    g_led_duty[bit] = duty20;
    uint8_t lv = kPwmCurve20[duty20];
    g_leds[bit * 3 + 0] = lv;
    g_leds[bit * 3 + 1] = (uint8_t)((lv * 232U + 127U) / 255U);
    g_leds[bit * 3 + 2] = (uint8_t)((lv * 168U + 127U) / 255U);
  }
}

extern "C" void emu_2020_on_porta_write(uint8_t val) {
  g_porta_latest = val;
  g_porta_pwm_ring[g_porta_pwm_head] = val;
  g_porta_pwm_head = (g_porta_pwm_head + 1) % 20;
  if (g_porta_pwm_count < 20) {
    g_porta_pwm_count++;
  }
  sync_leds_from_porta_ring();
}

extern "C" void emu_2020_on_portb_write(uint8_t val) {
  uint8_t prev = g_portb_latest;
  g_portb_latest = val;
  /* Count TP1 (PB0) rising pulses driven by the main/special loop (after setup at boot) */
  if (g_pinb_reads_since_boot > 0) {
    if (((prev & 1U) == 0U) && ((val & 1U) != 0U)) {
      g_tp_pulse_count++;
    }
  }
}

extern "C" uint8_t emu_2020_read_pinb(void) {
  uint8_t pinb = 0xFFU;
  /* PB2 = BUTTONPIN (active low with pull-up) */
  if (g_button_pressed) {
    pinb &= (uint8_t)~(1U << 2);
  }
  /* PB0 = TP1PIN: sampled at boot (main.cpp:153: `if((PINB & 1<<TP1PIN) == 0) specialModes();`) */
  if (!g_in_reset_btn) {
    g_pinb_reads_since_boot++;
    if (g_tp1_boot_low) {
      pinb &= (uint8_t)~(1U << 0);
      if (g_pinb_reads_since_boot == 1) {
        g_in_special_mode = 1;
      }
    } else if ((g_portb_latest & 1U) == 0U && g_pinb_reads_since_boot > 1) {
      pinb &= (uint8_t)~(1U << 0);
    }
  }
  return pinb;
}

static void advance_one_badge_ms(void) {
  if (g_ie && (TIMSK1 & (1U << OCIE1A))) {
    __avr_isr_tim1_compa();
  } else {
    millis.v = static_cast<uint16_t>(millis.v + 1U);
  }
}

extern "C" uint16_t emu_2020_read_millis(volatile const uint16_t *ptr) {
  if (g_in_reset_btn) {
    return *ptr;
  }
  g_polls_this_ms++;
  if (g_polls_this_ms <= 8) {
    return *ptr;
  }
  g_polls_this_ms = 1;
  while (g_ms_budget <= 0) {
    emu_yield_to_host();
  }
  g_ms_budget--;
  advance_one_badge_ms();
  return *ptr;
}

extern "C" void emu_2020_delay_ms(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    while (g_ms_budget <= 0) {
      emu_yield_to_host();
    }
    g_ms_budget--;
    advance_one_badge_ms();
  }
}

extern "C" void emu_2020_sei(void) { g_ie = 1; }
extern "C" void emu_2020_cli(void) { g_ie = 0; }

extern "C" uint8_t eeprom_read_byte(const uint8_t *p) {
  uintptr_t addr = (uintptr_t)p;
  if (addr < EMU_2020_EEPROM_SZ) {
    return g_eeprom[addr];
  }
  return 0xFFU;
}

extern "C" void eeprom_write_byte(uint8_t *p, uint8_t v) {
  uintptr_t addr = (uintptr_t)p;
  if (addr < EMU_2020_EEPROM_SZ) {
    if (g_eeprom[addr] != v) {
      g_eeprom[addr] = v;
      g_eeprom_dirty = 1;
    }
  }
}

#ifdef __EMSCRIPTEN__
static void fw_fiber_entry(void *arg) {
  (void)arg;
  badge_2020_fw_main();
  while (1) {
    emu_yield_to_host();
  }
}
#else
static void fw_ucontext_trampoline(void) {
  badge_2020_fw_main();
  while (1) {
    emu_yield_to_host();
  }
}
#endif

static void start_or_restart_fiber(void) {
  if (!g_eeprom_inited) {
    memset(g_eeprom, 0xFF, sizeof(g_eeprom));
    g_eeprom_inited = 1;
  }

  /* Reset AVR registers and firmware globals to power-on state */
  GIMSK = 0;
  PCMSK1 = 0;
  DDRA = 0;
  DDRB = 0;
  PORTA.v = 0;
  PORTB.v = 0;
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 0;
  TIMSK1 = 0;

  for (int i = 0; i < 8; i++) {
    LEDS[i] = 0;
  }
  millis.v = 0;
  milliscounter = 0;
  lastIntTime.v = 0;
  pwmvalue = 0;
  ledcounter = 0;
  pwmcounter = 0;
  displayState = 0;
  lastDisplayUpdate.v = 0;
  buttonDebounce.v = 0;
  displayCounter = 0;
  chaseDelay = 50;
  ringDelay = 50;
  waveDelay = 100;
  pulseDelay = 10;
  alternateDelay.v = 500;
  alternate2Delay.v = 500;
  circleDelay.v = 100;
  countUp = true;
  lastTpUpdate.v = 0;
  tpDelay.v = 1000;
  tpState = false;
  tpCounter = 0;
  numStates = 8;
  lastButtonState = false;

  memset(g_porta_pwm_ring, 0, sizeof(g_porta_pwm_ring));
  g_porta_pwm_head = 0;
  g_porta_pwm_count = 0;
  g_porta_latest = 0;
  g_portb_latest = 0;
  memset(g_leds, 0, sizeof(g_leds));
  memset(g_led_duty, 0, sizeof(g_led_duty));
  g_in_special_mode = 0;
  g_tp_pulse_count = 0;
  g_ie = 0;
  g_pinb_reads_since_boot = 0;
  g_ms_budget = 0;
  g_polls_this_ms = 0;

  /* Reset static locals in isButtonPressed() (previousButtonState = 1<<2, lastDebounceTime = 0, sent = 0) */
  int saved_btn = g_button_pressed;
  g_in_reset_btn = 1;
  millis.v = 0;
  g_button_pressed = 1;
  (void)isButtonPressed();
  g_button_pressed = 0;
  (void)isButtonPressed();
  g_in_reset_btn = 0;
  g_button_pressed = saved_btn;

#ifdef __EMSCRIPTEN__
  if (!g_host_fiber_inited) {
    emscripten_fiber_init_from_current_context(&g_host_fiber, g_main_async_stack, sizeof(g_main_async_stack));
    g_host_fiber_inited = 1;
  }
  emscripten_fiber_init(&g_fw_fiber, fw_fiber_entry, NULL,
                        g_c_stack, sizeof(g_c_stack),
                        g_async_stack, sizeof(g_async_stack));
#else
  getcontext(&g_fw_ctx);
  g_fw_ctx.uc_stack.ss_sp = g_c_stack;
  g_fw_ctx.uc_stack.ss_size = sizeof(g_c_stack);
  g_fw_ctx.uc_link = &g_host_ctx;
  makecontext(&g_fw_ctx, fw_ucontext_trampoline, 0);
#endif
  g_fiber_started = 1;
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void emu_init(uint32_t seed) {
  (void)seed;
  g_button_pressed = 0;
  start_or_restart_fiber();
}

EMSCRIPTEN_KEEPALIVE
void emu_reset(void) {
  start_or_restart_fiber();
}

EMSCRIPTEN_KEEPALIVE
void emu_step(uint32_t elapsed_ms, uint32_t buttons_mask) {
  if (!g_fiber_started) {
    emu_init(0x20205343U);
  }
  g_button_pressed = (buttons_mask & 1U) ? 1 : 0;
  uint32_t dt = (elapsed_ms > 0U && elapsed_ms <= 5000U) ? (elapsed_ms * g_speed_mult) : 16U;
  g_ms_budget += (int)dt;
#ifdef __EMSCRIPTEN__
  emscripten_fiber_swap(&g_host_fiber, &g_fw_fiber);
#else
  swapcontext(&g_host_ctx, &g_fw_ctx);
#endif
}

EMSCRIPTEN_KEEPALIVE
void emu_set_speed(uint32_t multiplier) {
  g_speed_mult = (multiplier >= 1U && multiplier <= 600U) ? multiplier : 1U;
}

EMSCRIPTEN_KEEPALIVE
const uint16_t *emu_framebuffer(void) { return g_fb; }

EMSCRIPTEN_KEEPALIVE
int emu_fb_width(void) { return EMU_2020_FB_W; }

EMSCRIPTEN_KEEPALIVE
int emu_fb_height(void) { return EMU_2020_FB_H; }

EMSCRIPTEN_KEEPALIVE
const uint8_t *emu_leds(void) { return g_leds; }

EMSCRIPTEN_KEEPALIVE
int emu_led_count(void) { return EMU_2020_LED_COUNT; }

EMSCRIPTEN_KEEPALIVE
uint8_t emu_backlight(void) { return 255U; }

EMSCRIPTEN_KEEPALIVE
uint8_t *emu_eeprom(void) {
  if (!g_eeprom_inited) {
    memset(g_eeprom, 0xFF, sizeof(g_eeprom));
    g_eeprom_inited = 1;
  }
  return g_eeprom;
}

EMSCRIPTEN_KEEPALIVE
int emu_eeprom_size(void) { return EMU_2020_EEPROM_SZ; }

EMSCRIPTEN_KEEPALIVE
int emu_eeprom_dirty(void) {
  int d = g_eeprom_dirty;
  g_eeprom_dirty = 0;
  return d;
}

EMSCRIPTEN_KEEPALIVE
void emu_eeprom_clear_dirty(void) { g_eeprom_dirty = 0; }

EMSCRIPTEN_KEEPALIVE
void emu_2020_set_tp1_boot_low(int shorted) {
  g_tp1_boot_low = shorted ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_2020_get_tp1_boot_low(void) {
  return g_tp1_boot_low;
}

EMSCRIPTEN_KEEPALIVE
int emu_2020_get_in_special_mode(void) {
  return g_in_special_mode;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_2020_get_porta(void) {
  return g_porta_latest;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_2020_get_portb(void) {
  return g_portb_latest;
}

EMSCRIPTEN_KEEPALIVE
int emu_2020_get_led_duty(int index) {
  if (index < 0 || index >= EMU_2020_LED_COUNT) return 0;
  return g_led_duty[index];
}

EMSCRIPTEN_KEEPALIVE
int emu_2020_get_tp_pulse_count(void) {
  return g_tp_pulse_count;
}

EMSCRIPTEN_KEEPALIVE
uint16_t emu_2020_get_millis(void) {
  return millis.v;
}

} /* extern "C" */
