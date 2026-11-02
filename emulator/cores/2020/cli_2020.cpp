/*
 * Headless CLI runner, smoke screenshot generator, and hardware-level verification test
 * for the SAINTCON 2020 ATtiny84A LED Badge firmware core.
 */

#include "platform/emu_2020.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef SCRIPT_DIR_STR
#define SCRIPT_DIR_STR "emulator/cores/2020"
#endif

#ifndef ROOT_DIR_STR
#define ROOT_DIR_STR "."
#endif

/* Step badge time by `ms` milliseconds in 1-ms increments with `btn` (0 or 1) */
static void step_ms(int ms, uint32_t btn) {
  for (int i = 0; i < ms; i++) {
    emu_step(1, btn);
  }
}

/*
 * Press SW1 (PB2) just long enough for isButtonPressed() (main.cpp:473-489) to fire:
 *   - 1st ms of press sets `lastDebounceTime = T0`.
 *   - 51 ms later (`millis - lastDebounceTime == 51 > 50`), `isButtonPressed()` returns 1.
 *   - 1 ms later, release SW1 so we are 1 ms after the mode transition.
 */
static void press_mode_button(void) {
  step_ms(52, 1);
  step_ms(1, 0);
}

/* Return 8-bit mask of LEDs whose 20-tick PORTA PWM duty is >= threshold */
static uint8_t porta_duty_mask(int min_duty) {
  uint8_t m = 0;
  for (int i = 0; i < 8; i++) {
    if (emu_2020_get_led_duty(i) >= min_duty) {
      m |= (uint8_t)(1U << i);
    }
  }
  return m;
}

/* Render a smoke screenshot by compositing the live emu_leds() + PORTA state onto badges/2020/board.png */
static int write_smoke_png_2020(const char *out_path, const char *caption) {
  const uint8_t *leds = emu_leds();
  uint8_t porta = emu_2020_get_porta();
  uint8_t portb = emu_2020_get_portb();
  int d[8];
  for (int i = 0; i < 8; i++) {
    d[i] = emu_2020_get_led_duty(i);
  }

  char cmd[2560];
  snprintf(
      cmd, sizeof(cmd),
      "python3 -c '"
      "import json, sys\n"
      "from pathlib import Path\n"
      "from PIL import Image, ImageDraw\n"
      "root = Path(\"%s/emulator/web/badges/2020\")\n"
      "if not root.exists(): root = Path(\"emulator/web/badges/2020\")\n"
      "layout = json.loads((root / \"layout.json\").read_text())\n"
      "im = Image.open(root / \"board.png\").convert(\"RGBA\")\n"
      "glow = Image.new(\"RGBA\", im.size, (0, 0, 0, 0))\n"
      "gdraw = ImageDraw.Draw(glow)\n"
      "intensities = [%u,%u,%u,%u,%u,%u,%u,%u]\n"
      "duties = [%d,%d,%d,%d,%d,%d,%d,%d]\n"
      "for idx, led in enumerate(layout[\"leds\"]):\n"
      "  lv = intensities[idx]\n"
      "  if lv > 0:\n"
      "    lx, ly, lr = led[\"x_px\"], led[\"y_px\"], led[\"radius_px\"]\n"
      "    alpha = int(45 + (lv / 255.0) * 210)\n"
      "    hr = lr * 2.2\n"
      "    gdraw.ellipse([lx-hr, ly-hr, lx+hr, ly+hr], fill=(255, 224, 150, int(alpha * 0.42)))\n"
      "    gdraw.ellipse([lx-lr, ly-lr, lx+lr, ly+lr], fill=(255, 232, 168, alpha), outline=(255, 250, 235, alpha), width=2)\n"
      "    cr = lr * 0.45\n"
      "    gdraw.ellipse([lx-cr, ly-cr, lx+cr, ly+cr], fill=(255, 255, 245, alpha))\n"
      "out = Image.alpha_composite(im, glow)\n"
      "draw = ImageDraw.Draw(out)\n"
      "y0, y1 = out.height - 66, out.height - 14\n"
      "draw.rounded_rectangle([24, y0, out.width - 24, y1], radius=8, fill=(10, 12, 16, 230), outline=(70, 80, 96, 220), width=2)\n"
      "txt = f\"%s | PORTA=0x%02X | PORTB=0x%02X | Duty20={duties}\"\n"
      "draw.text((42, y0 + 18), txt, fill=(240, 244, 250, 255))\n"
      "Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
      "out.save(sys.argv[1])\n"
      "' \"%s\"",
      ROOT_DIR_STR,
      (unsigned)leds[0], (unsigned)leds[3], (unsigned)leds[6], (unsigned)leds[9],
      (unsigned)leds[12], (unsigned)leds[15], (unsigned)leds[18], (unsigned)leds[21],
      d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7],
      caption, (unsigned)porta, (unsigned)portb, out_path);

  int rc = system(cmd);
  if (rc != 0) {
    fprintf(stderr, "Failed to render smoke screenshot: %s\n", out_path);
    return -1;
  }
  printf("[smoke-2020] Wrote %s (PORTA=0x%02X, duties=%d,%d,%d,%d,%d,%d,%d,%d)\n",
         out_path, (unsigned)porta, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
  return 0;
}

/*
 * Parse `char messages[8][19]` directly from upstream `Firmware/Saintcon2020/main.cpp`
 * at test time so no message bytes are copied into this file.
 */
static int load_messages_from_upstream_main(uint8_t out_msgs[8][19]) {
  const char *candidates[2] = {
      SCRIPT_DIR_STR "/upstream/Firmware/Saintcon2020/main.cpp",
      "emulator/cores/2020/upstream/Firmware/Saintcon2020/main.cpp",
  };
  FILE *fp = NULL;
  for (int i = 0; i < 2; i++) {
    fp = fopen(candidates[i], "rb");
    if (fp) break;
  }
  if (!fp) {
    fprintf(stderr, "FAIL: Could not open upstream main.cpp to read messages[8][19]\n");
    return -1;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }
  long sz = ftell(fp);
  if (sz <= 0 || sz > 262144L || fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return -1;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(fp);
    return -1;
  }
  size_t nread = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  buf[nread] = '\0';

  const char *anchor = strstr(buf, "char messages[8][19]");
  if (!anchor) {
    free(buf);
    fprintf(stderr, "FAIL: Could not find `char messages[8][19]` in upstream main.cpp\n");
    return -1;
  }
  const char *p = strchr(anchor, '{');
  if (!p) {
    free(buf);
    return -1;
  }
  p++; /* enter outer '{' */
  memset(out_msgs, 0, 8 * 19);
  for (int row = 0; row < 8; row++) {
    p = strchr(p, '{');
    if (!p) {
      free(buf);
      return -1;
    }
    p++; /* enter row '{' */
    int col = 0;
    while (*p && *p != '}') {
      while (*p && !isdigit((unsigned char)*p) && *p != '}') p++;
      if (*p == '}' || !*p) break;
      char *endp = NULL;
      long val = strtol(p, &endp, 10);
      if (endp == p || col >= 19) {
        free(buf);
        return -1;
      }
      out_msgs[row][col++] = (uint8_t)(val & 0xFF);
      p = endp;
    }
    if (*p == '}') p++;
  }
  free(buf);
  return 0;
}

static int run_verification_tests(void) {
  printf("=== SAINTCON 2020 Firmware Verification Test (Real PORTA Writes) ===\n");

  /* 1. Fresh power-on boot (EEPROM[0] = 0xFF -> Mode 0: Ring, ringDelay = 50 ms) */
  memset(emu_eeprom(), 0xFF, (size_t)emu_eeprom_size());
  emu_eeprom_clear_dirty();
  emu_2020_set_tp1_boot_low(0);
  emu_init(0x20205343U);

  /*
   * Mode 0 (Ring, main.cpp:194-208):
   * Step 0 triggers at t = 51 ms (`millis - lastDisplayUpdate > 50`); we sample 25 ms into each 51-ms step
   * across 2 full 17-step Ring cycles so `displayCounter == 16` (`LEDS[8] = 0` at main.cpp:201) and the
   * transition into the next cycle's `displayCounter == 0` are both verified.
   */
  step_ms(25, 0);
  uint8_t expected_ring = 0;
  for (int cycle = 0; cycle < 2; cycle++) {
    for (int step = 0; step <= 16; step++) {
      step_ms(51, 0);
      if (step < 8) {
        expected_ring |= (uint8_t)(1U << step);
      } else if (step < 16) {
        expected_ring &= (uint8_t)~(1U << (step - 8));
      }
      uint8_t observed = porta_duty_mask(18);
      if (observed != expected_ring) {
        fprintf(stderr, "FAIL: Mode 0 (Ring) cycle %d step %d: expected PORTA mask 0x%02X, got 0x%02X\n",
                cycle, step, (unsigned)expected_ring, (unsigned)observed);
        return 1;
      }
    }
  }
  printf("  [PASS] Mode 0 (Ring): 2 full 17-step cycles (including displayCounter=16 -> 0) verified from real PORTA PWM writes.\n");

  /* 2. Button press test: advance Mode 0 -> Mode 1 -> ... -> Mode 7 -> Mode 0 and verify EEPROM[0] writes */
  for (int expected_mode = 1; expected_mode <= 8; expected_mode++) {
    uint8_t target = (uint8_t)(expected_mode % 8);
    emu_eeprom_clear_dirty();
    press_mode_button();
    if (emu_eeprom()[0] != target || !emu_eeprom_dirty()) {
      fprintf(stderr, "FAIL: Button press did not advance/save displayState=%u to EEPROM[0] (got %u)\n",
              (unsigned)target, (unsigned)emu_eeprom()[0]);
      return 1;
    }
    /* Wait 5 ms before next press so button release is observed by isButtonPressed() */
    step_ms(5, 0);
  }
  printf("  [PASS] Button & EEPROM: SW1 (PB2) 50 ms debounce cycles all 8 modes (0..7..0) and writes EEPROM[0].\n");

  /* 3. Mode 1 (Chase, main.cpp:209-226, chaseDelay = 50 ms) */
  emu_eeprom()[0] = 1;
  emu_reset();
  step_ms(25, 0);
  for (int step = 0; step < 8; step++) {
    step_ms(51, 0);
    uint8_t expected = (uint8_t)(1U << step);
    uint8_t observed = porta_duty_mask(18);
    if (observed != expected) {
      fprintf(stderr, "FAIL: Mode 1 (Chase) step %d: expected PORTA mask 0x%02X, got 0x%02X\n",
              step, (unsigned)expected, (unsigned)observed);
      return 1;
    }
  }
  printf("  [PASS] Mode 1 (Chase): all 8 steps (D1..D8 at 50 ms cadence) verified from real PORTA PWM writes.\n");

  /* 4. Mode 2 (Blink, main.cpp:227-246, waveDelay = 100 ms) */
  emu_eeprom()[0] = 2;
  emu_reset();
  step_ms(30, 0);
  step_ms(101, 0);
  if (porta_duty_mask(18) != 0xFFU) {
    fprintf(stderr, "FAIL: Mode 2 (Blink) ON phase: expected 0xFF, got 0x%02X\n", (unsigned)porta_duty_mask(18));
    return 1;
  }
  step_ms(101, 0);
  if (porta_duty_mask(1) != 0x00U) {
    fprintf(stderr, "FAIL: Mode 2 (Blink) OFF phase: expected 0x00, got 0x%02X\n", (unsigned)porta_duty_mask(1));
    return 1;
  }
  printf("  [PASS] Mode 2 (Blink): 100 ms all-on (0xFF) / all-off (0x00) phases verified from real PORTA PWM writes.\n");

  /* 5. Mode 3 (Pulse, main.cpp:247-267, pulseDelay = 10 ms, software PWM 0..100..0) */
  emu_eeprom()[0] = 3;
  emu_reset();
  int prev_duty = 0;
  int max_duty_seen = 0;
  int saw_mid_pwm = 0;
  /* Ramp up 101 steps (11 ms per step) */
  for (int s = 0; s <= 102; s++) {
    step_ms(11, 0);
    int d0 = emu_2020_get_led_duty(0);
    if (d0 + 1 < prev_duty) {
      fprintf(stderr, "FAIL: Mode 3 (Pulse) ramp-up non-monotonic at step %d: %d -> %d\n", s, prev_duty, d0);
      return 1;
    }
    if (d0 >= 6 && d0 <= 14) {
      saw_mid_pwm = 1;
    }
    if (d0 > max_duty_seen) {
      max_duty_seen = d0;
    }
    prev_duty = d0;
  }
  if (!saw_mid_pwm || max_duty_seen != 20) {
    fprintf(stderr, "FAIL: Mode 3 (Pulse) ramp-up did not reach full 20/20 PWM duty (max=%d, mid=%d)\n",
            max_duty_seen, saw_mid_pwm);
    return 1;
  }
  /* Ramp down 100 steps */
  for (int s = 0; s < 100; s++) {
    step_ms(11, 0);
    int d0 = emu_2020_get_led_duty(0);
    if (d0 > prev_duty + 1) {
      fprintf(stderr, "FAIL: Mode 3 (Pulse) ramp-down non-monotonic at step %d: %d -> %d\n", s, prev_duty, d0);
      return 1;
    }
    prev_duty = d0;
  }
  step_ms(25, 0);
  if (emu_2020_get_led_duty(0) > 1) {
    fprintf(stderr, "FAIL: Mode 3 (Pulse) did not return to 0/1 duty at bottom of ramp (got %d)\n",
            emu_2020_get_led_duty(0));
    return 1;
  }
  printf("  [PASS] Mode 3 (Pulse): 1 kHz software PWM ramp 0/20 -> 20/20 -> 0/20 verified from real PORTA writes.\n");

  /* 6. Mode 4 (Alternate, main.cpp:268-297, alternateDelay = 500 ms: 0x0F <-> 0xF0) */
  emu_eeprom()[0] = 4;
  emu_reset();
  step_ms(40, 0);
  step_ms(501, 0);
  if (porta_duty_mask(18) != 0x0FU) {
    fprintf(stderr, "FAIL: Mode 4 (Alternate) phase 0: expected 0x0F, got 0x%02X\n", (unsigned)porta_duty_mask(18));
    return 1;
  }
  step_ms(501, 0);
  if (porta_duty_mask(18) != 0xF0U) {
    fprintf(stderr, "FAIL: Mode 4 (Alternate) phase 1: expected 0xF0, got 0x%02X\n", (unsigned)porta_duty_mask(18));
    return 1;
  }
  printf("  [PASS] Mode 4 (Alternate): 500 ms 0x0F <-> 0xF0 alternation verified from real PORTA PWM writes.\n");

  /* 7. Mode 5 (Alternate 2, main.cpp:298-327, alternate2Delay = 500 ms: 0x55 <-> 0xAA) */
  emu_eeprom()[0] = 5;
  emu_reset();
  step_ms(40, 0);
  step_ms(501, 0);
  if (porta_duty_mask(18) != 0x55U) {
    fprintf(stderr, "FAIL: Mode 5 (Alternate 2) phase 0: expected 0x55, got 0x%02X\n", (unsigned)porta_duty_mask(18));
    return 1;
  }
  step_ms(501, 0);
  if (porta_duty_mask(18) != 0xAAU) {
    fprintf(stderr, "FAIL: Mode 5 (Alternate 2) phase 1: expected 0xAA, got 0x%02X\n", (unsigned)porta_duty_mask(18));
    return 1;
  }
  printf("  [PASS] Mode 5 (Alternate 2): 500 ms 0x55 <-> 0xAA alternation verified from real PORTA PWM writes.\n");

  /* 8. Mode 6 (Circle, main.cpp:328-375, circleDelay = 100 ms: 0x11 -> 0x22 -> 0x44 -> 0x88) */
  emu_eeprom()[0] = 6;
  emu_reset();
  step_ms(30, 0);
  static const uint8_t kCircleMasks[4] = {0x11U, 0x22U, 0x44U, 0x88U};
  for (int q = 0; q < 4; q++) {
    step_ms(101, 0);
    if (porta_duty_mask(18) != kCircleMasks[q]) {
      fprintf(stderr, "FAIL: Mode 6 (Circle) step %d: expected 0x%02X, got 0x%02X\n",
              q, (unsigned)kCircleMasks[q], (unsigned)porta_duty_mask(18));
      return 1;
    }
  }
  printf("  [PASS] Mode 6 (Circle): 100 ms quadrant pairs (0x11, 0x22, 0x44, 0x88) verified from real PORTA PWM writes.\n");

  /* 9. Mode 7 (On, main.cpp:376-380: all 8 LEDs continuously at 100% duty) */
  emu_eeprom()[0] = 7;
  emu_reset();
  step_ms(25, 0);
  if (porta_duty_mask(20) != 0xFFU) {
    fprintf(stderr, "FAIL: Mode 7 (On): expected all 8 LEDs at 20/20 duty (0xFF), got 0x%02X\n",
            (unsigned)porta_duty_mask(20));
    return 1;
  }
  printf("  [PASS] Mode 7 (On): all 8 LEDs at 20/20 PWM duty (0xFF) verified from real PORTA writes.\n");

  /* 10. Reload persistence test: advance via button from Mode 4 -> Mode 5, reset MCU, verify Mode 5 resumes */
  emu_eeprom()[0] = 4;
  emu_reset();
  step_ms(10, 0);
  press_mode_button();
  if (emu_eeprom()[0] != 5) {
    fprintf(stderr, "FAIL: Expected EEPROM[0]=5 after button press from Mode 4, got %u\n", (unsigned)emu_eeprom()[0]);
    return 1;
  }
  emu_reset();
  step_ms(540, 0);
  if (porta_duty_mask(18) != 0x55U) {
    fprintf(stderr, "FAIL: Reload persistence: expected Mode 5 phase 0 (0x55) after reset, got 0x%02X\n",
            (unsigned)porta_duty_mask(18));
    return 1;
  }
  step_ms(501, 0);
  if (porta_duty_mask(18) != 0xAAU) {
    fprintf(stderr, "FAIL: Reload persistence: expected Mode 5 phase 1 (0xAA) after reset, got 0x%02X\n",
            (unsigned)porta_duty_mask(18));
    return 1;
  }
  printf("  [PASS] EEPROM Reload Persistence: button-selected mode survives reload and resumes from EEPROM[0].\n");

  /* 10b. Wipe EEPROM to 0xFF (fresh EEPROM state) and confirm Mode 0 fallback (main.cpp:150-151) */
  memset(emu_eeprom(), 0xFF, (size_t)emu_eeprom_size());
  emu_eeprom_clear_dirty();
  emu_reset();
  step_ms(25, 0);
  step_ms(51, 0);
  if (porta_duty_mask(18) != 0x01U) {
    fprintf(stderr, "FAIL: Wipe EEPROM (0xFF) fallback to Mode 0 (Ring) step 0: expected 0x01, got 0x%02X\n",
            (unsigned)porta_duty_mask(18));
    return 1;
  }
  printf("  [PASS] EEPROM Wipe (0xFF): fresh/wiped 0xFF EEPROM falls back to Mode 0 (Ring) at main.cpp:150-151.\n");

  /*
   * 11. Special Mode (`if((PINB & 1<<TP1PIN) == 0) specialModes();` at main.cpp:153):
   * Read `char messages[8][19]` directly from upstream `main.cpp:499-506` at test time
   * and verify all 8 messages byte-for-byte from real PORTA writes without printing or decoding the text.
   */
  uint8_t raw_messages[8][19];
  if (load_messages_from_upstream_main(raw_messages) != 0) {
    return 1;
  }

  emu_2020_set_tp1_boot_low(1);
  emu_reset();
  /* Advance 50 ms into the initial 1000-ms pre-byte window so we sample 50 ms into each 1001-ms byte window */
  step_ms(50, 0);
  if (!emu_2020_get_in_special_mode()) {
    fprintf(stderr, "FAIL: Booting with TP1 low did not enter specialModes()\n");
    return 1;
  }

  for (int msg_idx = 0; msg_idx < 8; msg_idx++) {
    int msg_len = (int)strlen((const char *)raw_messages[msg_idx]);
    for (int b_idx = 0; b_idx <= msg_len; b_idx++) {
      step_ms(1001, 0);
      uint8_t raw_byte = raw_messages[msg_idx][b_idx];
      uint8_t expected_porta = 0;
      for (int x = 0; x < 7; x++) {
        if (raw_byte & (1U << x)) {
          expected_porta |= (uint8_t)(1U << (7 - x));
        }
      }
      uint8_t observed_porta = emu_2020_get_porta();
      uint8_t observed_duty = porta_duty_mask(18);
      if (observed_porta != expected_porta || observed_duty != expected_porta) {
        fprintf(stderr,
                "FAIL: specialModes() message %d byte index %d: expected PORTA=0x%02X, got PORTA=0x%02X (duty=0x%02X)\n",
                msg_idx, b_idx, (unsigned)expected_porta, (unsigned)observed_porta, (unsigned)observed_duty);
        return 1;
      }
    }
    printf("  [PASS] Special Mode Message %d: all %d bytes (+ terminator) verified byte-for-byte on PORTA.\n",
           msg_idx, msg_len);

    if (msg_idx == 3) {
      /*
       * Messages 0..3 take ~58.5 s of badge time, so continuing into Message 4 without
       * a fresh boot crosses `lastDisplayUpdate >= 64536` (the 16-bit wrap racing window
       * tested in section 13 below). Reboot into specialModes() and advance 4 button
       * presses (~0.4 s) to start Message 4 cleanly at the start of the 16-bit cycle.
       */
      emu_reset();
      step_ms(50, 0);
      for (int p = 0; p < 4; p++) {
        press_mode_button();
        step_ms(49, 0);
      }
    } else if (msg_idx < 7) {
      /*
       * Advance to next message via SW1 button press (main.cpp:509-522).
       * In specialModes(), line 512 sets `lastDisplayUpdate = millis` when the button fires,
       * and press_mode_button() releases 1 ms later, so stepping 49 ms puts us 50 ms into the new message window.
       */
      press_mode_button();
      step_ms(49, 0);
    }
  }

  /*
   * 12. 16-bit `millis` wrap at 65536 ms in normal modes + button past the wrap:
   * On AVR, `millis` and `lastDisplayUpdate` are 16-bit `uint16_t` (`0..65535`), and
   * `(millis - lastDisplayUpdate) > ringDelay` wraps modulo 65536 without glitching.
   * Also verify on every 1-ms tick that `millis` never goes backwards (except at 65535 -> 0)
   * and that every D8-off -> D1-on all-off gap in Ring mode has the true 102-ms length.
   */
  emu_2020_set_tp1_boot_low(0);
  memset(emu_eeprom(), 0xFF, (size_t)emu_eeprom_size());
  emu_reset();
  uint16_t prev_millis = emu_2020_get_millis();
  uint8_t prev_ring_porta = emu_2020_get_porta();
  int d8_off_t = -1;
  int gap_count = 0;
  /* Step to millis = 65535 (1285 Ring steps = 65535 ms) */
  for (int t = 1; t <= 65535; t++) {
    emu_step(1, 0);
    uint16_t cur_millis = emu_2020_get_millis();
    if (cur_millis < prev_millis) {
      fprintf(stderr, "FAIL: Ring mode millis went backwards before 65536 wrap at t=%d: %u -> %u\n",
              t, (unsigned)prev_millis, (unsigned)cur_millis);
      return 1;
    }
    prev_millis = cur_millis;
    uint8_t cur_porta = emu_2020_get_porta();
    if (prev_ring_porta == 0x80U && cur_porta == 0x00U) {
      d8_off_t = t;
    } else if (prev_ring_porta == 0x00U && cur_porta == 0x01U && d8_off_t >= 0) {
      int gap_ms = t - d8_off_t;
      if (gap_ms != 102) {
        fprintf(stderr, "FAIL: Ring mode D8-off -> D1-on all-off gap at t=%d was %d ms (expected 102 ms)\n",
                t, gap_ms);
        return 1;
      }
      gap_count++;
      d8_off_t = -1;
    }
    prev_ring_porta = cur_porta;
  }
  if (gap_count < 70) {
    fprintf(stderr, "FAIL: Expected >= 70 Ring cycles with 102-ms all-off gap before wrap, saw %d\n", gap_count);
    return 1;
  }
  if (emu_2020_get_millis() != 65535U) {
    fprintf(stderr, "FAIL: Expected 16-bit millis == 65535 at 65535 ms, got %u\n",
            (unsigned)emu_2020_get_millis());
    return 1;
  }
  /* Step 1 ms across the 16-bit boundary: 65535 -> 0 */
  step_ms(1, 0);
  if (emu_2020_get_millis() != 0U) {
    fprintf(stderr, "FAIL: Expected 16-bit millis to wrap to 0 at 65536 ms, got %u\n",
            (unsigned)emu_2020_get_millis());
    return 1;
  }
  /*
   * At t = 65535 ms (1285 * 51 ms), Ring just executed step 1284 (1285 % 17 == 10).
   * Step 25 ms into step 10, then verify the next 17 Ring steps across t = 65561..66428 ms
   * and at 70 s still advance at exact 51-ms intervals.
   */
  step_ms(24, 0);
  uint8_t ring_post_wrap = porta_duty_mask(18);
  int transitions_seen = 0;
  for (int s = 0; s < 17; s++) {
    step_ms(51, 0);
    uint8_t cur = porta_duty_mask(18);
    if (cur != ring_post_wrap) {
      transitions_seen++;
      ring_post_wrap = cur;
    }
  }
  if (transitions_seen != 16) {
    fprintf(stderr, "FAIL: Mode 0 (Ring) after 65536 ms wrap expected 16 transitions in 17 steps, got %d\n",
            transitions_seen);
    return 1;
  }
  /* Advance past 70 s total (~4500 ms after wrap) and test SW1 button press past the wrap */
  step_ms(3600, 0);
  emu_eeprom_clear_dirty();
  press_mode_button();
  if (emu_eeprom()[0] != 1 || !emu_eeprom_dirty()) {
    fprintf(stderr, "FAIL: Button press after 65536 ms wrap did not advance to Mode 1 (EEPROM[0]=%u)\n",
            (unsigned)emu_eeprom()[0]);
    return 1;
  }
  printf("  [PASS] 16-bit millis Wrap (Normal Mode & Button): wraps 65535 -> 0; Ring 51-ms cadence and SW1 button work past 65.5 s.\n");

  /*
   * 13. Special Mode 16-bit wrap racing (`if(millis > (lastDisplayUpdate + binaryDelay))` at main.cpp:523):
   * In `specialModes()`, `lastDisplayUpdate` advances by 1001 ms per byte (`1001, 2002, ..., 64064, 65065`).
   * At `millis = 65065`, `(uint16_t)(65065 + 1000)` wraps modulo 65536 to `529`.
   * From `millis = 65065` through `65535` (471 ms, ~0.47 s every 65.536 s), `millis > (lastDisplayUpdate + 1000)`
   * is true on every loop pass, so the message races and changes PORTA many times inside that 0.47 s window
   * until `millis` wraps to 0 (where `(uint16_t)(65535 + 1000) == 999` and normal 1 s cadence resumes).
   */
  emu_2020_set_tp1_boot_low(1);
  emu_reset();
  /* Step to 65064 ms (just before the 65th update at 65065 ms) */
  step_ms(65064, 0);
  uint8_t porta_before_race = emu_2020_get_porta();
  int race_porta_changes = 0;
  uint8_t prev_porta = porta_before_race;
  /* Step through the 471-ms racing window: millis = 65065..65535 */
  for (int m = 65065; m <= 65535; m++) {
    step_ms(1, 0);
    uint8_t p = emu_2020_get_porta();
    if (p != prev_porta) {
      race_porta_changes++;
      prev_porta = p;
    }
  }
  if (race_porta_changes < 50) {
    fprintf(stderr,
            "FAIL: Expected specialModes() to race across 65065..65535 ms wrap window, but saw only %d PORTA changes\n",
            race_porta_changes);
    return 1;
  }
  /* Step 1 ms to wrap millis from 65535 -> 0; racing must stop for the next 999 ms */
  step_ms(1, 0);
  if (emu_2020_get_millis() != 0U) {
    fprintf(stderr, "FAIL: Expected millis == 0 after 65535 in specialModes(), got %u\n",
            (unsigned)emu_2020_get_millis());
    return 1;
  }
  uint8_t porta_settled = emu_2020_get_porta();
  int post_wrap_changes = 0;
  for (int m = 1; m <= 999; m++) {
    step_ms(1, 0);
    if (emu_2020_get_porta() != porta_settled) {
      post_wrap_changes++;
    }
  }
  if (post_wrap_changes != 0) {
    fprintf(stderr, "FAIL: Expected specialModes() to stop racing at millis = 0..999, saw %d changes\n",
            post_wrap_changes);
    return 1;
  }
  printf("  [PASS] 16-bit millis Wrap (Special Mode main.cpp:523): races across 65065..65535 ms (%d PORTA changes in 471 ms) and settles at wrap.\n",
         race_porta_changes);

  emu_2020_set_tp1_boot_low(0);
  printf("=== ALL SAINTCON 2020 TESTS PASSED ===\n");
  return 0;
}

static int run_smoke_screenshots(const char *out_dir) {
  mkdir(out_dir, 0755);
  char path[512];

  /* 1. Mode 0 (Ring) with 5 LEDs lit */
  memset(emu_eeprom(), 0xFF, (size_t)emu_eeprom_size());
  emu_2020_set_tp1_boot_low(0);
  emu_init(0x20205343U);
  step_ms(25 + 51 * 5, 0);
  snprintf(path, sizeof(path), "%s/mode0-ring.png", out_dir);
  if (write_smoke_png_2020(path, "Mode 0: Ring (5/8 LEDs lit)") != 0) return 1;

  /* 2. Mode 3 (Pulse) at ~55% software PWM duty */
  emu_eeprom()[0] = 3;
  emu_reset();
  step_ms(11 * 55, 0);
  snprintf(path, sizeof(path), "%s/mode3-pulse.png", out_dir);
  if (write_smoke_png_2020(path, "Mode 3: Pulse (Software PWM mid-ramp)") != 0) return 1;

  /* 3. Mode 6 (Circle) quadrant pair */
  emu_eeprom()[0] = 6;
  emu_reset();
  step_ms(30 + 101 * 2, 0);
  snprintf(path, sizeof(path), "%s/mode6-circle.png", out_dir);
  if (write_smoke_png_2020(path, "Mode 6: Circle (Quadrant Pair)") != 0) return 1;

  /* 4. Special Mode (TP1 held low at boot) displaying first byte of message 0 */
  emu_2020_set_tp1_boot_low(1);
  emu_reset();
  step_ms(1050, 0);
  snprintf(path, sizeof(path), "%s/special-tp1-binary.png", out_dir);
  if (write_smoke_png_2020(path, "Special Mode (TP1 low at boot: Binary Message 0 Byte 0)") != 0) return 1;
  emu_2020_set_tp1_boot_low(0);

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
    int rc = run_verification_tests();
    if (rc != 0) return rc;
  }
  if (smoke_dir) {
    int rc = run_smoke_screenshots(smoke_dir);
    if (rc != 0) return rc;
  }
  return 0;
}
