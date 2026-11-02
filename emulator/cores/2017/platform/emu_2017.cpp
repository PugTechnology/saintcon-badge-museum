/*
 * SAINTCON 2017 ("HITB Minibadge 2017") ATtiny45 emulator runtime.
 *
 * Runs `HITBUTT_Minibadge/HITB_Test.ino` unmodified against our own shims for:
 *   - `ADCTouch` (`ADCTouch.read(A3, ...)` for `TOUCH_BTN_1` on PB3/ADC3,
 *                 `ADCTouch.read(A2, ...)` for `TOUCH_BTN_2` on PB4/ADC2)
 *   - `APA102<0, 2>` (`ledStrip.write(colors, ledCount, brightness)` for `U2`, `U3`)
 *
 * Touch pad model:
 *   - Baseline untouched ADC capacitance reading: `A3 -> 512`, `A2 -> 508`.
 *     In `setup()`, `ref0 = ADCTouch.read(A3, 500)` and `ref1 = ADCTouch.read(A2, 500)`
 *     read the true current touch capacitance (so if a pad is held during `setup()` / Reset,
 *     `ref0` or `ref1` absorbs that held reading just as on real hardware).
 *   - When Pad 1 (`A3`, `buttons_mask & 1`) is held, `ADCTouch.read(A3)` returns
 *     `512 + 68 = 580` (`value0 = 68 > 40` at `HITB_Test.ino:43` when calibrated untouched).
 *   - When Pad 2 (`A2`, `buttons_mask & 2`) is held, `ADCTouch.read(A2)` returns
 *     `508 + 36 = 544` (`value1 = 36 > 10` at `HITB_Test.ino:49` when calibrated untouched).
 *   - Custom delta overrides (`emu_2017_set_pad_delta`) allow exact boundary
 *     verification around `value0 = 40/41` and `value1 = 10/11`.
 *
 * APA102 brightness model:
 *   - `HITB_Test.ino:18,56` calls `ledStrip.write(colors, ledCount, brightness)`
 *     with `brightness = 10` (out of 31).
 *   - Each RGB channel (`0..255`) is scaled by one fixed 32-point strictly
 *     monotonic curve `kApa102BrightnessCurve[b] = round(255 * (b / 31.0)^0.384)` for `b = 1..31`
 *     (`0` at `b = 0`, `165` at `b = 10`).
 */

#include "emu_2017.h"
#include "Arduino.h"
#include "ADCTouch.h"
#include "APA102.h"

#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

ADCTouchClass ADCTouch;

/* Baseline untouched ADC readings for A3 (PB3 / TOUCH_BTN_1) and A2 (PB4 / TOUCH_BTN_2) */
static const int kBaselineAdcPad0 = 512; /* A3 */
static const int kBaselineAdcPad1 = 508; /* A2 */

/* Default pressed capacitance deltas when held via buttons_mask (exceeding >40 and >10) */
static const int kPressedDeltaPad0 = 68; /* value0 = 68 > 40 */
static const int kPressedDeltaPad1 = 36; /* value1 = 36 > 10 */

/*
 * Fixed 32-entry strictly monotonic curve mapping 5-bit APA102 global brightness (0..31)
 * to a display multiplier (0..255): `round(255 * (b / 31.0)^0.384)` for b = 1..31 (0 at b = 0).
 * At brightness=10 (HITB_Test.ino:18), scale = 165.
 */
static const uint8_t kApa102BrightnessCurve[32] = {
    0,   68,  89,  104, 116, 127, 136, 144,
    152, 159, 165, 171, 177, 183, 188, 193,
    198, 202, 207, 211, 216, 220, 224, 227,
    231, 235, 238, 242, 245, 249, 252, 255
};

static uint16_t g_fb[EMU_2017_FB_W * EMU_2017_FB_H];
static uint8_t g_leds[EMU_2017_LED_COUNT * 3];
static uint8_t g_raw_leds[EMU_2017_LED_COUNT * 3];
static uint8_t g_dummy_eeprom[1];

static int g_in_setup = 0;
static uint32_t g_buttons_mask = 0;
static uint32_t g_speed_mult = 1;

static int g_override_active[2] = {0, 0};
static int g_override_delta[2] = {0, 0};
static int g_ref_recorded[2] = {kBaselineAdcPad0, kBaselineAdcPad1};
static int g_last_raw_adc[2] = {kBaselineAdcPad0, kBaselineAdcPad1};
static int g_last_delta[2] = {0, 0};

static uint8_t g_last_brightness = 0;
static uint32_t g_apa102_writes = 0;

static uint8_t apply_apa102_brightness(uint8_t c, uint8_t brightness) {
  uint8_t b = (uint8_t)(brightness & 0x1FU);
  uint32_t scale = (uint32_t)kApa102BrightnessCurve[b];
  return (uint8_t)(((uint32_t)c * scale + 127U) / 255U);
}

extern "C" int emu_2017_adctouch_read(uint8_t analog_pin, int samples) {
  (void)samples;
  if (analog_pin == A3) {
    int base = kBaselineAdcPad0;
    int delta = 0;
    if (g_override_active[0]) {
      delta = g_override_delta[0];
    } else if (g_buttons_mask & 1U) {
      delta = kPressedDeltaPad0;
    }
    int raw = base + delta;
    if (g_in_setup) {
      g_ref_recorded[0] = raw;
    }
    g_last_raw_adc[0] = raw;
    g_last_delta[0] = raw - g_ref_recorded[0];
    return raw;
  }
  if (analog_pin == A2) {
    int base = kBaselineAdcPad1;
    int delta = 0;
    if (g_override_active[1]) {
      delta = g_override_delta[1];
    } else if (g_buttons_mask & 2U) {
      delta = kPressedDeltaPad1;
    }
    int raw = base + delta;
    if (g_in_setup) {
      g_ref_recorded[1] = raw;
    }
    g_last_raw_adc[1] = raw;
    g_last_delta[1] = raw - g_ref_recorded[1];
    return raw;
  }
  return 500;
}

extern "C" void emu_2017_on_apa102_write(uint8_t data_pin, uint8_t clock_pin,
                                         const rgb_color *colors,
                                         uint16_t count,
                                         uint8_t brightness) {
  (void)data_pin;
  (void)clock_pin;
  g_last_brightness = (uint8_t)(brightness & 0x1FU);
  g_apa102_writes++;
  uint16_t n = (count < EMU_2017_LED_COUNT) ? count : EMU_2017_LED_COUNT;
  for (uint16_t i = 0; i < n; i++) {
    uint8_t r = colors[i].red;
    uint8_t g = colors[i].green;
    uint8_t b = colors[i].blue;
    g_raw_leds[i * 3 + 0] = r;
    g_raw_leds[i * 3 + 1] = g;
    g_raw_leds[i * 3 + 2] = b;
    g_leds[i * 3 + 0] = apply_apa102_brightness(r, g_last_brightness);
    g_leds[i * 3 + 1] = apply_apa102_brightness(g, g_last_brightness);
    g_leds[i * 3 + 2] = apply_apa102_brightness(b, g_last_brightness);
  }
}

extern "C" {

EMSCRIPTEN_KEEPALIVE
void emu_reset(void) {
  memset(g_fb, 0, sizeof(g_fb));
  memset(g_leds, 0, sizeof(g_leds));
  memset(g_raw_leds, 0, sizeof(g_raw_leds));
  g_last_brightness = 0;
  g_apa102_writes = 0;

  /*
   * Do NOT clear `g_buttons_mask` or `g_override_active` here so that resetting
   * while a pad is held lets `setup()` (`ref0 = ADCTouch.read(A3, 500)`,
   * `ref1 = ADCTouch.read(A2, 500)`) absorb the held touch capacitance into
   * `ref0` / `ref1` just as on real hardware.
   */
  g_in_setup = 1;
  setup();
  g_in_setup = 0;

  /* Run one initial loop() pass so the resting LED state is immediately available */
  loop();
}

EMSCRIPTEN_KEEPALIVE
void emu_init(uint32_t seed) {
  (void)seed;
  g_buttons_mask = 0;
  g_override_active[0] = 0;
  g_override_active[1] = 0;
  g_override_delta[0] = 0;
  g_override_delta[1] = 0;
  emu_reset();
}

EMSCRIPTEN_KEEPALIVE
int emu_step(uint32_t elapsed_ms, uint32_t buttons_mask) {
  g_buttons_mask = buttons_mask;
  uint32_t iters = elapsed_ms * g_speed_mult;
  if (iters == 0) {
    iters = 1;
  }
  if (iters > 64) {
    iters = 64;
  }
  for (uint32_t i = 0; i < iters; i++) {
    loop();
  }
  return 1;
}

EMSCRIPTEN_KEEPALIVE
void emu_set_speed(uint32_t mult) {
  g_speed_mult = (mult >= 1 && mult <= 16) ? mult : 1;
}

EMSCRIPTEN_KEEPALIVE
const uint16_t *emu_framebuffer(void) {
  return g_fb;
}

EMSCRIPTEN_KEEPALIVE
int emu_fb_width(void) {
  return EMU_2017_FB_W;
}

EMSCRIPTEN_KEEPALIVE
int emu_fb_height(void) {
  return EMU_2017_FB_H;
}

EMSCRIPTEN_KEEPALIVE
const uint8_t *emu_leds(void) {
  return g_leds;
}

EMSCRIPTEN_KEEPALIVE
int emu_led_count(void) {
  return EMU_2017_LED_COUNT;
}

EMSCRIPTEN_KEEPALIVE
int emu_backlight(void) {
  return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t *emu_eeprom(void) {
  return g_dummy_eeprom;
}

EMSCRIPTEN_KEEPALIVE
int emu_eeprom_size(void) {
  return EMU_2017_EEPROM_SZ;
}

EMSCRIPTEN_KEEPALIVE
int emu_eeprom_dirty(void) {
  return 0;
}

EMSCRIPTEN_KEEPALIVE
void emu_eeprom_clear_dirty(void) {
}

EMSCRIPTEN_KEEPALIVE
void emu_2017_set_pad_delta(int pad_index, int delta_override) {
  if (pad_index >= 0 && pad_index < 2) {
    g_override_active[pad_index] = 1;
    g_override_delta[pad_index] = delta_override;
  }
}

EMSCRIPTEN_KEEPALIVE
void emu_2017_clear_pad_overrides(void) {
  g_override_active[0] = 0;
  g_override_active[1] = 0;
  g_override_delta[0] = 0;
  g_override_delta[1] = 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_2017_get_ref(int pad_index) {
  if (pad_index >= 0 && pad_index < 2) {
    return g_ref_recorded[pad_index];
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_2017_get_last_raw(int pad_index) {
  if (pad_index >= 0 && pad_index < 2) {
    return g_last_raw_adc[pad_index];
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
int emu_2017_get_last_delta(int pad_index) {
  if (pad_index >= 0 && pad_index < 2) {
    return g_last_delta[pad_index];
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_2017_get_raw_led_channel(int led_index, int channel) {
  if (led_index >= 0 && led_index < EMU_2017_LED_COUNT && channel >= 0 && channel < 3) {
    return g_raw_leds[led_index * 3 + channel];
  }
  return 0;
}

EMSCRIPTEN_KEEPALIVE
uint8_t emu_2017_get_last_brightness(void) {
  return g_last_brightness;
}

EMSCRIPTEN_KEEPALIVE
uint32_t emu_2017_get_write_count(void) {
  return g_apa102_writes;
}

} /* extern "C" */
