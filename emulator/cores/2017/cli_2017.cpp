/*
 * Headless CLI verification harness and smoke-screenshot generator for
 * SAINTCON 2017 ("HITB Minibadge 2017", `HITBUTT_Minibadge/HITB_Test.ino`).
 *
 * Verifies:
 *   1. `setup()` calibration (`ref0 = ADCTouch.read(A3, 500)`, `ref1 = ADCTouch.read(A2, 500)`).
 *   2. Rest state (`value0 = 0 <= 40`, `value1 = 0 <= 10`): APA102 color `(0, 255, 0)`
 *      at `brightness = 10` (`scaled = (0, 165, 0)`).
 *   3. Exact threshold boundary on Pad 1 (`A3`, `value0 > 40` at `HITB_Test.ino:43`):
 *      `value0 = 40` stays green `(0, 255, 0)`; `value0 = 41` and `buttons_mask = 1`
 *      (`value0 = 68`) turn both LEDs red `(255, 0, 0)` (`scaled = (165, 0, 0)`).
 *   4. Exact threshold boundary on Pad 2 (`A2`, `value1 > 10` at `HITB_Test.ino:49`):
 *      `value1 = 10` stays green `(0, 255, 0)`; `value1 = 11` and `buttons_mask = 2`
 *      (`value1 = 36`) turn both LEDs blue `(0, 0, 255)` (`scaled = (0, 0, 165)`).
 *   5. Both pads held together (`buttons_mask = 3`, `value0 = 68 > 40`, `value1 = 36 > 10`):
 *      `HITB_Test.ino:43-52` sets `r = 255` and `b = 255, g = 0`, producing magenta
 *      `(255, 0, 255)` (`scaled = (165, 0, 165)`).
 */

#include "platform/emu_2017.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ROOT_DIR_STR
#define ROOT_DIR_STR "."
#endif

static int assert_leds_2017(const char *label,
                            uint8_t exp_raw_r, uint8_t exp_raw_g, uint8_t exp_raw_b,
                            uint8_t exp_scaled_r, uint8_t exp_scaled_g, uint8_t exp_scaled_b,
                            uint8_t exp_brightness) {
  const uint8_t *scaled = emu_leds();
  uint8_t b = emu_2017_get_last_brightness();
  if (b != exp_brightness) {
    fprintf(stderr, "FAIL [%s]: expected APA102 brightness %u, got %u\n",
            label, (unsigned)exp_brightness, (unsigned)b);
    return 1;
  }
  for (int i = 0; i < 2; i++) {
    uint8_t rr = emu_2017_get_raw_led_channel(i, 0);
    uint8_t rg = emu_2017_get_raw_led_channel(i, 1);
    uint8_t rb = emu_2017_get_raw_led_channel(i, 2);
    if (rr != exp_raw_r || rg != exp_raw_g || rb != exp_raw_b) {
      fprintf(stderr,
              "FAIL [%s]: LED %d raw RGB expected (%u,%u,%u), got (%u,%u,%u)\n",
              label, i,
              (unsigned)exp_raw_r, (unsigned)exp_raw_g, (unsigned)exp_raw_b,
              (unsigned)rr, (unsigned)rg, (unsigned)rb);
      return 1;
    }
    uint8_t sr = scaled[i * 3 + 0];
    uint8_t sg = scaled[i * 3 + 1];
    uint8_t sb = scaled[i * 3 + 2];
    if (sr != exp_scaled_r || sg != exp_scaled_g || sb != exp_scaled_b) {
      fprintf(stderr,
              "FAIL [%s]: LED %d scaled RGB expected (%u,%u,%u), got (%u,%u,%u)\n",
              label, i,
              (unsigned)exp_scaled_r, (unsigned)exp_scaled_g, (unsigned)exp_scaled_b,
              (unsigned)sr, (unsigned)sg, (unsigned)sb);
      return 1;
    }
  }
  return 0;
}

static int write_smoke_png_2017(const char *out_path, const char *caption, uint32_t btn_mask) {
  const uint8_t *leds = emu_leds();
  uint8_t r0 = emu_2017_get_raw_led_channel(0, 0);
  uint8_t g0 = emu_2017_get_raw_led_channel(0, 1);
  uint8_t b0 = emu_2017_get_raw_led_channel(0, 2);
  int v0 = emu_2017_get_last_delta(0);
  int v1 = emu_2017_get_last_delta(1);
  uint8_t br = emu_2017_get_last_brightness();

  char cmd[2560];
  snprintf(
      cmd, sizeof(cmd),
      "python3 -c '"
      "import json, sys\n"
      "from pathlib import Path\n"
      "from PIL import Image, ImageDraw\n"
      "root = Path(\"%s/emulator/web/badges/2017\")\n"
      "if not root.exists(): root = Path(\"emulator/web/badges/2017\")\n"
      "layout = json.loads((root / \"layout.json\").read_text())\n"
      "im = Image.open(root / \"board.png\").convert(\"RGBA\")\n"
      "glow = Image.new(\"RGBA\", im.size, (0, 0, 0, 0))\n"
      "gdraw = ImageDraw.Draw(glow)\n"
      "s_rgb = [(%u, %u, %u), (%u, %u, %u)]\n"
      "btn_mask = %u\n"
      "for idx, pad in enumerate(layout.get(\"touch_pads\", [])):\n"
      "  if btn_mask & pad.get(\"bit\", (1 << idx)):\n"
      "    px, py = pad[\"x_px\"], pad[\"y_px\"]\n"
      "    if pad.get(\"shape\") == \"circle\":\n"
      "      pr = pad[\"radius_px\"]\n"
      "      gdraw.ellipse([px-pr, py-pr, px+pr, py+pr], fill=(255, 220, 80, 52), outline=(255, 230, 110, 235), width=4)\n"
      "    else:\n"
      "      pw, ph = pad[\"w_px\"], pad[\"h_px\"]\n"
      "      gdraw.rounded_rectangle([px-pw/2, py-ph/2, px+pw/2, py+ph/2], radius=12, fill=(255, 220, 80, 52), outline=(255, 230, 110, 235), width=4)\n"
      "for idx, led in enumerate(layout.get(\"leds\", [])):\n"
      "  lr, lg, lb = s_rgb[idx]\n"
      "  if lr > 0 or lg > 0 or lb > 0:\n"
      "    lx, ly, rad = led[\"x_px\"], led[\"y_px\"], led[\"radius_px\"]\n"
      "    peak = max(lr, lg, lb)\n"
      "    hr = rad * 1.45\n"
      "    mr = rad * 0.78\n"
      "    dr = int(lr * 255 / peak)\n"
      "    dg = int(lg * 255 / peak)\n"
      "    db = int(lb * 255 / peak)\n"
      "    gdraw.ellipse([lx-hr, ly-hr, lx+hr, ly+hr], fill=(dr, dg, db, 88))\n"
      "    gdraw.ellipse([lx-mr, ly-mr, lx+mr, ly+mr], fill=(dr, dg, db, 225), outline=(255, 255, 255, 230), width=2)\n"
      "    cr = rad * 0.34\n"
      "    gdraw.ellipse([lx-cr, ly-cr, lx+cr, ly+cr], fill=(255, 255, 255, 238))\n"
      "out = Image.alpha_composite(im, glow)\n"
      "draw = ImageDraw.Draw(out)\n"
      "y0, y1 = out.height - 58, out.height - 12\n"
      "draw.rounded_rectangle([18, y0, out.width - 18, y1], radius=8, fill=(10, 12, 16, 232), outline=(72, 82, 98, 220), width=2)\n"
      "txt = f\"%s | value0(A3)=%d value1(A2)=%d | raw=(%u,%u,%u) b=%u scaled=(%u,%u,%u)\"\n"
      "draw.text((32, y0 + 17), txt, fill=(240, 244, 250, 255))\n"
      "Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
      "out.save(sys.argv[1])\n"
      "' \"%s\"",
      ROOT_DIR_STR,
      (unsigned)leds[0], (unsigned)leds[1], (unsigned)leds[2],
      (unsigned)leds[3], (unsigned)leds[4], (unsigned)leds[5],
      (unsigned)btn_mask,
      caption, v0, v1,
      (unsigned)r0, (unsigned)g0, (unsigned)b0, (unsigned)br,
      (unsigned)leds[0], (unsigned)leds[1], (unsigned)leds[2],
      out_path);
  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "Failed to render 2017 smoke screenshot: %s\n", out_path);
    return 1;
  }
  printf("[smoke-2017] Wrote %s (value0=%d, value1=%d, raw=(%u,%u,%u), scaled=(%u,%u,%u))\n",
         out_path, v0, v1,
         (unsigned)r0, (unsigned)g0, (unsigned)b0,
         (unsigned)leds[0], (unsigned)leds[1], (unsigned)leds[2]);
  return 0;
}

static int run_tests(void) {
  printf("=== HITB Minibadge 2017 Verification Test ===\n");

  emu_init(0);
  if (emu_2017_get_ref(0) != 512 || emu_2017_get_ref(1) != 508) {
    fprintf(stderr, "FAIL: setup() ADCTouch calibration expected ref0=512, ref1=508, got %d, %d\n",
            emu_2017_get_ref(0), emu_2017_get_ref(1));
    return 1;
  }
  printf("  [PASS] setup() calibration: ref0(A3)=%d, ref1(A2)=%d (500 samples).\n",
         emu_2017_get_ref(0), emu_2017_get_ref(1));

  /* 1. Rest state (neither pad touched) */
  emu_step(16, 0);
  if (emu_2017_get_last_delta(0) != 0 || emu_2017_get_last_delta(1) != 0) {
    fprintf(stderr, "FAIL: rest state expected value0=0, value1=0\n");
    return 1;
  }
  if (assert_leds_2017("Rest (neither pad)", 0, 255, 0, 0, 165, 0, 10) != 0) {
    return 1;
  }
  printf("  [PASS] Rest state: value0=0 (<=40), value1=0 (<=10) -> APA102 green (0,255,0) @ brightness 10 -> scaled (0,165,0).\n");

  /* 2. Exact boundary check for Pad 1 (A3): threshold `value0 > 40` (HITB_Test.ino:43) */
  emu_2017_set_pad_delta(0, 40);
  emu_2017_set_pad_delta(1, 0);
  emu_step(1, 0);
  if (assert_leds_2017("Pad 1 boundary value0=40", 0, 255, 0, 0, 165, 0, 10) != 0) {
    return 1;
  }
  emu_2017_set_pad_delta(0, 41);
  emu_step(1, 0);
  if (assert_leds_2017("Pad 1 boundary value0=41", 255, 0, 0, 165, 0, 0, 10) != 0) {
    return 1;
  }
  emu_2017_clear_pad_overrides();
  emu_step(16, 1);
  if (emu_2017_get_last_delta(0) <= 40 || emu_2017_get_last_delta(1) != 0) {
    fprintf(stderr, "FAIL: Pad 1 held expected value0 > 40 and value1 == 0\n");
    return 1;
  }
  if (assert_leds_2017("Pad 1 held (A3)", 255, 0, 0, 165, 0, 0, 10) != 0) {
    return 1;
  }
  printf("  [PASS] Pad 1 (A3 / TOUCH_BTN_1): threshold `value0 > 40` verified (40 -> green, 41 & held=%d -> red (255,0,0) -> scaled (165,0,0)).\n",
         emu_2017_get_last_delta(0));

  /* 3. Exact boundary check for Pad 2 (A2): threshold `value1 > 10` (HITB_Test.ino:49) */
  emu_2017_set_pad_delta(0, 0);
  emu_2017_set_pad_delta(1, 10);
  emu_step(1, 0);
  if (assert_leds_2017("Pad 2 boundary value1=10", 0, 255, 0, 0, 165, 0, 10) != 0) {
    return 1;
  }
  emu_2017_set_pad_delta(1, 11);
  emu_step(1, 0);
  if (assert_leds_2017("Pad 2 boundary value1=11", 0, 0, 255, 0, 0, 165, 10) != 0) {
    return 1;
  }
  emu_2017_clear_pad_overrides();
  emu_step(16, 2);
  if (emu_2017_get_last_delta(0) != 0 || emu_2017_get_last_delta(1) <= 10) {
    fprintf(stderr, "FAIL: Pad 2 held expected value0 == 0 and value1 > 10\n");
    return 1;
  }
  if (assert_leds_2017("Pad 2 held (A2)", 0, 0, 255, 0, 0, 165, 10) != 0) {
    return 1;
  }
  printf("  [PASS] Pad 2 (A2 / TOUCH_BTN_2): threshold `value1 > 10` verified (10 -> green, 11 & held=%d -> blue (0,0,255) -> scaled (0,0,165)).\n",
         emu_2017_get_last_delta(1));

  /* 4. Both pads held together (buttons_mask = 3 -> r=255, g=0, b=255 -> magenta) */
  emu_2017_clear_pad_overrides();
  emu_step(16, 3);
  if (emu_2017_get_last_delta(0) <= 40 || emu_2017_get_last_delta(1) <= 10) {
    fprintf(stderr, "FAIL: Both pads held expected value0 > 40 and value1 > 10\n");
    return 1;
  }
  if (assert_leds_2017("Both pads held (A3 + A2)", 255, 0, 255, 165, 0, 165, 10) != 0) {
    return 1;
  }
  printf("  [PASS] Both pads held (A3 + A2): value0=%d (>40) & value1=%d (>10) -> APA102 magenta (255,0,255) -> scaled (165,0,165).\n",
         emu_2017_get_last_delta(0), emu_2017_get_last_delta(1));

  /*
   * 5. Reset while holding a pad (Item 14):
   * Hold Pad 1 (`buttons_mask = 1`), call `emu_reset()` while Pad 1 is held, and verify:
   *   - `setup()` reads the held capacitance (`512 + 68 = 580`) into `ref0`, so `value0 == 0 <= 40`
   *     and the LEDs are green `(0, 255, 0)` (`scaled = (0, 165, 0)`).
   *   - Releasing Pad 1 (`value0 == -68 <= 40`) and pressing Pad 1 again at the same level
   *     (`value0 == 0 <= 40`) both stay green (untriggered).
   *   - Pressing Pad 1 in a new way with a higher capacitance (`delta = 68 + 41 = 109` -> `value0 = 41 > 40`)
   *     triggers red `(255, 0, 0)`.
   */
  emu_step(16, 1);
  if (assert_leds_2017("Pre-reset Pad 1 held", 255, 0, 0, 165, 0, 0, 10) != 0) {
    return 1;
  }
  emu_reset();
  if (emu_2017_get_ref(0) != 580 || emu_2017_get_ref(1) != 508) {
    fprintf(stderr, "FAIL: Reset while holding Pad 1 expected ref0=580, ref1=508, got %d, %d\n",
            emu_2017_get_ref(0), emu_2017_get_ref(1));
    return 1;
  }
  if (emu_2017_get_last_delta(0) != 0 || emu_2017_get_last_delta(1) != 0) {
    fprintf(stderr, "FAIL: Reset while holding Pad 1 expected value0=0, value1=0, got %d, %d\n",
            emu_2017_get_last_delta(0), emu_2017_get_last_delta(1));
    return 1;
  }
  if (assert_leds_2017("Reset while holding Pad 1 (absorbed into ref0 -> green)", 0, 255, 0, 0, 165, 0, 10) != 0) {
    return 1;
  }
  /* Release Pad 1: value0 becomes 512 - 580 = -68 <= 40 -> still green */
  emu_step(16, 0);
  if (emu_2017_get_last_delta(0) != -68) {
    fprintf(stderr, "FAIL: Release after held-reset expected value0=-68, got %d\n",
            emu_2017_get_last_delta(0));
    return 1;
  }
  if (assert_leds_2017("Released after held-reset (value0=-68 -> green)", 0, 255, 0, 0, 165, 0, 10) != 0) {
    return 1;
  }
  /* Press Pad 1 again at the same level: value0 = 580 - 580 = 0 <= 40 -> still green */
  emu_step(16, 1);
  if (emu_2017_get_last_delta(0) != 0) {
    fprintf(stderr, "FAIL: Re-press at same level after held-reset expected value0=0, got %d\n",
            emu_2017_get_last_delta(0));
    return 1;
  }
  if (assert_leds_2017("Re-press at same level after held-reset (value0=0 -> green)", 0, 255, 0, 0, 165, 0, 10) != 0) {
    return 1;
  }
  /* Press Pad 1 with higher capacitance (> ref0 + 40 = 620, i.e. base + 109 = 621 -> value0 = 41 > 40) -> red */
  emu_2017_set_pad_delta(0, 68 + 41);
  emu_step(1, 1);
  if (emu_2017_get_last_delta(0) != 41) {
    fprintf(stderr, "FAIL: Higher capacitance press after held-reset expected value0=41, got %d\n",
            emu_2017_get_last_delta(0));
    return 1;
  }
  if (assert_leds_2017("Higher capacitance press after held-reset (value0=41 -> red)", 255, 0, 0, 165, 0, 0, 10) != 0) {
    return 1;
  }
  emu_2017_clear_pad_overrides();
  emu_step(1, 0);
  emu_reset();
  printf("  [PASS] Reset while holding Pad 1: setup() absorbs held finger into ref0=580 -> green; stays green on release/re-press until pressed higher (value0=41 -> red).\n");

  printf("=== ALL HITB MINIBADGE 2017 TESTS PASSED ===\n");
  return 0;
}

static int run_smoke(const char *out_dir) {
  char path[512];

  emu_init(0);
  emu_step(16, 0);
  snprintf(path, sizeof(path), "%s/rest-green.png", out_dir);
  if (write_smoke_png_2017(path, "Rest (no pad held): Green", 0) != 0) return 1;

  emu_step(16, 1);
  snprintf(path, sizeof(path), "%s/pad1-a3-red.png", out_dir);
  if (write_smoke_png_2017(path, "Pad 1 (A3 > 40 held): Red", 1) != 0) return 1;

  emu_step(16, 2);
  snprintf(path, sizeof(path), "%s/pad2-a2-blue.png", out_dir);
  if (write_smoke_png_2017(path, "Pad 2 (A2 > 10 held): Blue", 2) != 0) return 1;

  emu_step(16, 3);
  snprintf(path, sizeof(path), "%s/both-pads-magenta.png", out_dir);
  if (write_smoke_png_2017(path, "Both Pads (A3 + A2 held): Magenta", 3) != 0) return 1;

  return 0;
}

int main(int argc, char **argv) {
  int do_test = 0;
  const char *smoke_dir = NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--test") == 0) {
      do_test = 1;
    } else if (strcmp(argv[i], "--smoke") == 0 && i + 1 < argc) {
      smoke_dir = argv[++i];
    } else {
      fprintf(stderr, "Usage: %s [--test] [--smoke <dir>]\n", argv[0]);
      return 1;
    }
  }
  if (!do_test && !smoke_dir) {
    do_test = 1;
  }
  if (do_test) {
    if (run_tests() != 0) return 1;
  }
  if (smoke_dir) {
    if (run_smoke(smoke_dir) != 0) return 1;
  }
  return 0;
}
