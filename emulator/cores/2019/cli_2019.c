/*
 * Headless CLI runner, smoke screenshot generator, and Enigma verification test
 * for the SAINTCON 2019 Enigma Badge firmware core.
 */

#include "platform/emu_2019.h"
#include "main.h"
#include "enigma.h"
#include "plugboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

/* Write 384x96 RGB565 framebuffer as 24-bit RGB PNG */
static int write_png(const char *path, const uint16_t *fb, int w, int h) {
  FILE *fp = fopen(path, "wb");
  if (!fp) {
    perror(path);
    return -1;
  }

  size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
  uint8_t *raw = (uint8_t *)malloc(raw_len);
  if (!raw) {
    fclose(fp);
    return -1;
  }

  for (int y = 0; y < h; y++) {
    uint8_t *row = &raw[(size_t)y * (1 + (size_t)w * 3)];
    row[0] = 0; /* filter type 0 (None) */
    for (int x = 0; x < w; x++) {
      uint16_t px = fb[y * w + x];
      uint8_t r5 = (uint8_t)((px >> 11) & 0x1FU);
      uint8_t g6 = (uint8_t)((px >> 5) & 0x3FU);
      uint8_t b5 = (uint8_t)(px & 0x1FU);
      row[1 + x * 3 + 0] = (uint8_t)((r5 << 3) | (r5 >> 2));
      row[1 + x * 3 + 1] = (uint8_t)((g6 << 2) | (g6 >> 4));
      row[1 + x * 3 + 2] = (uint8_t)((b5 << 3) | (b5 >> 2));
    }
  }

  uLongf comp_cap = compressBound((uLong)raw_len);
  uint8_t *comp = (uint8_t *)malloc(comp_cap);
  if (!comp || compress2(comp, &comp_cap, raw, (uLong)raw_len, Z_BEST_COMPRESSION) != Z_OK) {
    free(raw);
    free(comp);
    fclose(fp);
    return -1;
  }
  free(raw);

  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, fp);

  #define WRITE_CHUNK(type_str, data_ptr, data_len) do { \
    uint32_t len_u = (uint32_t)(data_len); \
    uint8_t len_be[4] = {(uint8_t)(len_u >> 24), (uint8_t)(len_u >> 16), (uint8_t)(len_u >> 8), (uint8_t)len_u}; \
    fwrite(len_be, 1, 4, fp); \
    fwrite(type_str, 1, 4, fp); \
    uLong crc = crc32(0L, Z_NULL, 0); \
    crc = crc32(crc, (const Bytef *)(type_str), 4); \
    if ((data_len) > 0) { \
      fwrite(data_ptr, 1, data_len, fp); \
      crc = crc32(crc, (const Bytef *)(data_ptr), (uInt)(data_len)); \
    } \
    uint8_t crc_be[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc}; \
    fwrite(crc_be, 1, 4, fp); \
  } while (0)

  uint8_t ihdr[13] = {
    (uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
    (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h,
    8, 2, 0, 0, 0
  };
  WRITE_CHUNK("IHDR", ihdr, 13);
  WRITE_CHUNK("IDAT", comp, comp_cap);
  WRITE_CHUNK("IEND", NULL, 0);
  #undef WRITE_CHUNK

  free(comp);
  fclose(fp);
  return 0;
}

static void step_n(int steps) {
  for (int i = 0; i < steps; i++) {
    emu_step(0, 0);
  }
}

/* Tap a letter 'A'..'Z' for 2 30Hz frames (4 host steps), then release for 2 30Hz frames */
static void tap_letter(char c) {
  if (c < 'A' || c > 'Z') return;
  uint32_t bit = 1U << (c - 'A');
  emu_2019_set_keys(bit);
  for (int i = 0; i < 4; i++) emu_step(0, 0);
  emu_2019_set_keys(0);
  for (int i = 0; i < 4; i++) emu_step(0, 0);
}

/* Hold a letter 'A'..'Z' for 95 30Hz frames (>3 seconds) to trigger firmware back gesture */
static void hold_letter_3s(char c) {
  uint32_t bit = 1U << (c - 'A');
  emu_2019_set_keys(bit);
  for (int i = 0; i < 196; i++) emu_step(0, 0);
  emu_2019_set_keys(0);
  for (int i = 0; i < 6; i++) emu_step(0, 0);
}

/* ============================================================================
 * Independent Plain Enigma I Reference Implementation (written only for test)
 * ============================================================================ */

typedef struct {
  int rotors[3]; /* 0=left (slow), 1=middle, 2=right (fast), 1..9 */
  int rings[3];  /* 0..25 (Ringstellung: 1='A'->0 .. 26='Z'->25) */
  int pos[3];    /* 0..25 (current window letter: 0='A' .. 25='Z') */
  int plug[26];  /* 0..25 stecker partner for each letter */
} RefEnigma;

static const char *REF_WIRING[9] = {
  "EKMFLGDQVZNTOWYHXUSPAIBRCJ", /* I    */
  "AJDKSIRUXBLHWTMCQGZNPYFVOE", /* II   */
  "BDFHJLCPRTXVZNYEIWGAKMUSQO", /* III  */
  "ESOVPZJAYQUIRHXLNFTGKDCMWB", /* IV   */
  "VZBRGITYUPSDNHLXAWMJQOFECK", /* V    */
  "JPGVOUMFYQBENHZRDKASXLICTW", /* VI   */
  "NZJHGRCXMYSWBOUFAIVLPEKQDT", /* VII  */
  "FKQHTLXOCBJSPDZRAMEWNIUYGV", /* VIII */
  "BASHCOMPUKIDZERGYJWLQTFXVN"  /* IX (SAINTCON custom rotor in Src/enigma.c:40) */
};

/* Greek 4th rotors in Src/enigma.c:41-42 (used by firmware 4-rotor mode with thick UKW-B):
 *   Beta ('b'):  "LEYJVCNIXWPBQMDRTAKZGFUHOS"
 *   Gamma ('g'): "FSOKANUERHMBTIYCWLQPZXVGJD"
 */
static const char *REF_REFLECTOR_B = "YRUHQSLDPXNGOKMIEBFZCWVJAT";

static int ref_at_notch(int rotor_num_1_to_9, int pos_0_to_25) {
  char c = (char)('A' + pos_0_to_25);
  switch (rotor_num_1_to_9) {
    case 1: return c == 'Q';
    case 2: return c == 'E';
    case 3: return c == 'V';
    case 4: return c == 'J';
    case 5: return c == 'Z';
    case 6:
    case 7:
    case 8: return (c == 'Z' || c == 'M');
    case 9: return c == 'Y'; /* Src/enigma.c:47: rotor IX notch is "Y" */
    default: return 0;
  }
}

static int ref_mod26(int v) {
  int r = v % 26;
  return r < 0 ? r + 26 : r;
}

static int ref_rotor_fwd(int rotor_num, int ring, int pos, int ch) {
  const char *w = REF_WIRING[rotor_num - 1];
  int shift = pos - ring;
  int idx = ref_mod26(ch + shift);
  int wired = w[idx] - 'A';
  return ref_mod26(wired - shift);
}

static int ref_rotor_rev(int rotor_num, int ring, int pos, int ch) {
  const char *w = REF_WIRING[rotor_num - 1];
  int shift = pos - ring;
  int target = ref_mod26(ch + shift);
  int idx = 0;
  for (int i = 0; i < 26; i++) {
    if ((w[i] - 'A') == target) {
      idx = i;
      break;
    }
  }
  return ref_mod26(idx - shift);
}

static char ref_enigma_step_and_encode(RefEnigma *M, char in_char) {
  /* Standard Enigma I mechanical stepping (with middle-rotor double-stepping) */
  int mid_at_notch = ref_at_notch(M->rotors[1], M->pos[1]);
  int right_at_notch = ref_at_notch(M->rotors[2], M->pos[2]);

  if (mid_at_notch) {
    M->pos[0] = ref_mod26(M->pos[0] + 1);
    M->pos[1] = ref_mod26(M->pos[1] + 1);
  } else if (right_at_notch) {
    M->pos[1] = ref_mod26(M->pos[1] + 1);
  }
  M->pos[2] = ref_mod26(M->pos[2] + 1);

  int c = in_char - 'A';
  /* Plugboard in */
  c = M->plug[c];
  /* Right -> Middle -> Left rotors */
  c = ref_rotor_fwd(M->rotors[2], M->rings[2], M->pos[2], c);
  c = ref_rotor_fwd(M->rotors[1], M->rings[1], M->pos[1], c);
  c = ref_rotor_fwd(M->rotors[0], M->rings[0], M->pos[0], c);
  /* Reflector UKW-B */
  c = REF_REFLECTOR_B[c] - 'A';
  /* Left -> Middle -> Right inverse rotors */
  c = ref_rotor_rev(M->rotors[0], M->rings[0], M->pos[0], c);
  c = ref_rotor_rev(M->rotors[1], M->rings[1], M->pos[1], c);
  c = ref_rotor_rev(M->rotors[2], M->rings[2], M->pos[2], c);
  /* Plugboard out */
  c = M->plug[c];
  return (char)('A' + c);
}

static int run_enigma_tests(void) {
  printf("=== Running 2019 Firmware Enigma I & Challenge Verification ===\n");

  /* -------------------------------------------------------------------------
   * Part 1a: Firmware enigma_setup + GPIO plugboardUpdate + enigma_encode_char
   * vs. independent reference Enigma I across 40 characters including a true
   * dynamic middle-rotor double-step (rotors I II III, rings A A A, start A D U:
   * A D U -> A D V -> A E W -> B F X, plus 10 plugboard wires).
   * ------------------------------------------------------------------------- */
  emu_init(0);
  emu_2019_plugboard_clear();
  const char *wires1[10] = {"AM", "FI", "NV", "PS", "TU", "WZ", "BJ", "EY", "CQ", "DX"};
  for (int i = 0; i < 10; i++) {
    emu_2019_plugboard_connect(wires1[i][0] - 'A', wires1[i][1] - 'A');
  }

  /* Scan plugboard using the firmware's own GPIO open-drain matrix scan */
  extern char plugboard[26];
  plugboardUpdate();
  char fw_plug[27];
  memcpy(fw_plug, plugboard, 26);
  fw_plug[26] = '\0';

  char fw_rotors[4] = "123"; /* Left=I (notch Q), Middle=II (notch E), Right=III (notch V) */
  char fw_rings[4]  = "AAA";
  char fw_start[4]  = "ADU"; /* Dynamically reaches middle notch: ADU -> ADV -> AEW -> BFX */

  struct Enigma fw_enigma = {0};
  enigma_setup(&fw_enigma, fw_rotors, fw_start, fw_rings, fw_plug);

  RefEnigma ref = {
    .rotors = {1, 2, 3},
    .rings  = {0, 0, 0},
    .pos    = {0, 3, 20},
  };
  for (int i = 0; i < 26; i++) ref.plug[i] = i;
  for (int i = 0; i < 10; i++) {
    int a = wires1[i][0] - 'A';
    int b = wires1[i][1] - 'A';
    ref.plug[a] = b;
    ref.plug[b] = a;
  }

  const char *plaintext = "SAINTCONENIGMABADGEMUSEUMVERIFICATIONTST";
  char fw_cipher[64] = {0};
  char ref_cipher[64] = {0};
  int len = (int)strlen(plaintext);

  for (int i = 0; i < len; i++) {
    fw_cipher[i] = enigma_encode_char(&fw_enigma, plaintext[i]);
    ref_cipher[i] = ref_enigma_step_and_encode(&ref, plaintext[i]);

    /* Verify rotor window positions match after every single keypress:
     * rotors[2] = left (slow), rotors[1] = middle, rotors[0] = right (fast) */
    char fw_p0 = (char)('A' + ((fw_enigma.rotors[2].offset + (fw_rings[0] - 'A')) % 26));
    char fw_p1 = (char)('A' + ((fw_enigma.rotors[1].offset + (fw_rings[1] - 'A')) % 26));
    char fw_p2 = (char)('A' + ((fw_enigma.rotors[0].offset + (fw_rings[2] - 'A')) % 26));
    char ref_p0 = (char)('A' + ref.pos[0]);
    char ref_p1 = (char)('A' + ref.pos[1]);
    char ref_p2 = (char)('A' + ref.pos[2]);

    if (fw_cipher[i] != ref_cipher[i] || fw_p0 != ref_p0 || fw_p1 != ref_p1 || fw_p2 != ref_p2) {
      fprintf(stderr,
              "FAIL at index %d ('%c'): fw='%c' (%c%c%c) vs ref='%c' (%c%c%c)\n",
              i, plaintext[i], fw_cipher[i], fw_p0, fw_p1, fw_p2,
              ref_cipher[i], ref_p0, ref_p1, ref_p2);
      return 1;
    }
  }

  /* Now reset firmware Enigma rotors to A D U and decode the ciphertext back */
  memset(&fw_enigma, 0, sizeof(fw_enigma));
  enigma_setup(&fw_enigma, fw_rotors, fw_start, fw_rings, fw_plug);
  char fw_decoded[64] = {0};
  for (int i = 0; i < len; i++) {
    fw_decoded[i] = enigma_encode_char(&fw_enigma, fw_cipher[i]);
  }
  if (strcmp(fw_decoded, plaintext) != 0) {
    fprintf(stderr, "FAIL reciprocal decode: got '%s', expected '%s'\n", fw_decoded, plaintext);
    return 1;
  }

  printf("  [PASS] Plaintext  : %s\n", plaintext);
  printf("  [PASS] Ciphertext : %s (100%% match with independent Enigma I reference across dynamic double-step ADU -> ADV -> AEW -> BFX)\n", fw_cipher);
  printf("  [PASS] Decoded    : %s\n", fw_decoded);

  /* -------------------------------------------------------------------------
   * Part 1b: Verify firmware quirk when starting directly on BOTH notches
   * (rotors I II III, rings A A A, start A E V: middle II on notch E and right
   * III on notch V simultaneously). Real Enigma I steps AEV -> BFW, whereas
   * firmware Src/enigma.c:208-227 steps the middle rotor twice (AEV -> BGW).
   * ------------------------------------------------------------------------- */
  {
    struct Enigma both_fw = {0};
    char r_123[4] = "123";
    char g_aaa[4] = "AAA";
    char s_aev[4] = "AEV";
    enigma_setup(&both_fw, r_123, s_aev, g_aaa, fw_plug);
    (void)enigma_encode_char(&both_fw, 'A');
    char p0 = (char)('A' + (both_fw.rotors[2].offset % 26));
    char p1 = (char)('A' + (both_fw.rotors[1].offset % 26));
    char p2 = (char)('A' + (both_fw.rotors[0].offset % 26));

    RefEnigma both_ref = {
      .rotors = {1, 2, 3},
      .rings  = {0, 0, 0},
      .pos    = {0, 4, 21}, /* A, E, V */
    };
    for (int i = 0; i < 26; i++) both_ref.plug[i] = i;
    (void)ref_enigma_step_and_encode(&both_ref, 'A');
    char rp0 = (char)('A' + both_ref.pos[0]);
    char rp1 = (char)('A' + both_ref.pos[1]);
    char rp2 = (char)('A' + both_ref.pos[2]);

    if (p0 != 'B' || p1 != 'G' || p2 != 'W' || rp0 != 'B' || rp1 != 'F' || rp2 != 'W') {
      fprintf(stderr, "FAIL start-on-both-notches (AEV): fw=%c%c%c (expected BGW), ref=%c%c%c (expected BFW)\n",
              p0, p1, p2, rp0, rp1, rp2);
      return 1;
    }
    printf("  [PASS] Start on both notches (I-II-III @ AEV): firmware steps AEV -> %c%c%c (middle rotor steps twice in enigma.c:208-227; real Enigma I steps AEV -> %c%c%c)\n",
           p0, p1, p2, rp0, rp1, rp2);
  }

  /* -------------------------------------------------------------------------
   * Part 1c: Verify monotonic 0..255 brightness curve on both matrix & lamps
   * ------------------------------------------------------------------------- */
  {
    uint8_t prev_s = 0;
    for (int v = 0; v <= 255; v++) {
      uint8_t cur_s = emu_2019_scale_led_channel((uint8_t)v);
      if (cur_s < prev_s) {
        fprintf(stderr, "FAIL brightness curve non-monotonic at %d: %u < %u\n", v, cur_s, prev_s);
        return 1;
      }
      prev_s = cur_s;
    }
    printf("  [PASS] Brightness curve 0..255 is strictly non-decreasing (0 -> %u, 128 -> %u, 129 -> %u, 255 -> %u)\n",
           emu_2019_scale_led_channel(0), emu_2019_scale_led_channel(128),
           emu_2019_scale_led_channel(129), emu_2019_scale_led_channel(255));
  }

  /* -------------------------------------------------------------------------
   * Part 2: Decode Challenge Message 1 (10/1/1942 from Code Sheet.pdf and
   * Saintcon2019BadgeChallenge.pdf) through the full firmware UI using only
   * button & plugboard inputs, verifying badgeLevel advances 1 -> 2.
   * ------------------------------------------------------------------------- */
  memset(emu_eeprom(), 0, (size_t)emu_eeprom_size());
  emu_init(0);
  emu_2019_plugboard_clear();

  /* Datum 1 (10/1/1942) Steckerverbindungen: AV CK DL EU FH GI JO NX PW YZ */
  const char *msg1_wires[10] = {"AV", "CK", "DL", "EU", "FH", "GI", "JO", "NX", "PW", "YZ"};
  for (int i = 0; i < 10; i++) {
    emu_2019_plugboard_connect(msg1_wires[i][0] - 'A', msg1_wires[i][1] - 'A');
  }

  /* Decode the 3-letter encrypted message key "XXA" at indicator setting "AAA"
   * using the firmware's own enigma_setup + enigma_encode_char to find the real message key! */
  plugboardUpdate();
  char msg1_plug[27];
  memcpy(msg1_plug, plugboard, 26);
  msg1_plug[26] = '\0';
  struct Enigma key_machine = {0};
  char r_str[4] = "964";
  char i_str[4] = "AAA";
  char g_str[4] = "OKE";
  enigma_setup(&key_machine, r_str, i_str, g_str, msg1_plug);
  char k0 = enigma_encode_char(&key_machine, 'X');
  char k1 = enigma_encode_char(&key_machine, 'X');
  char k2 = enigma_encode_char(&key_machine, 'A');
  printf("  [INFO] Message 1 preamble 'AAA XXA' decodes message key 'XXA' -> '%c%c%c'\n", k0, k1, k2);
  step_n(160);
  if (emu_2019_get_badge_level() != 1) {
    fprintf(stderr, "FAIL initial badgeLevel=%d (expected 1)\n", emu_2019_get_badge_level());
    return 1;
  }

  /* Menu state 0 (Instructions) -> press S -> state 1 (Enigma Emulator) -> press K */
  tap_letter('S');
  tap_letter('K');

  /* Entering enigma_menu() opens enigma_settings() wizard:
   *   State 0: Machine Type (default 3 Rotor) -> press K */
  tap_letter('K');

  /*   State 1: Set Rotors (default "1 2 3" -> target "9 6 4" = IX VI IV; S increments, W decrements) */
  tap_letter('W'); /* Rotor 1 (slot 0): '1' -> '9' (IX) */
  tap_letter('D');
  for (int i = 0; i < 4; i++) tap_letter('S'); /* Rotor 2 (slot 1): '2' -> '6' (VI) */
  tap_letter('D');
  tap_letter('S'); /* Rotor 3 (slot 2): '3' -> '4' (IV) */
  tap_letter('K'); /* Next -> Set Rings */

  /*   State 2: Set Rings (default "A A A" -> target "O K E" = 15 11 05; selectedRotor is 2) */
  tap_letter('A');
  tap_letter('A'); /* Move selectedRotor back to 0 */
  for (int i = 0; i < 14; i++) tap_letter('S'); /* Ring 1 (slot 0): 'A' -> 'O' (15) */
  tap_letter('D');
  for (int i = 0; i < 10; i++) tap_letter('S'); /* Ring 2 (slot 1): 'A' -> 'K' (11) */
  tap_letter('D');
  for (int i = 0; i < 4; i++) tap_letter('S');  /* Ring 3 (slot 2): 'A' -> 'E' (05) */
  tap_letter('K'); /* Next -> Start Pos. */

  /*   State 3: Start Pos. (default "A A A" -> set to "S A I", decoded from preamble "AAA XXA")
   *   Currently selectedRotor is 2 (right slot, 'A').
   *   Set slot 2 ('A' -> 'I' = +8 with 'S'), slot 1 ('A'), slot 0 ('A' -> 'S' = -8 with 'W'). */
  for (int i = 0; i < ('I' - 'A'); i++) tap_letter('S'); /* Start 3 (slot 2): 'A' -> 'I' */
  tap_letter('A');
  tap_letter('A'); /* Move selectedRotor from 2 to 0 */
  for (int i = 0; i < (26 - ('S' - 'A')); i++) tap_letter('W'); /* Start 1 (slot 0): 'A' -> 'S' */
  tap_letter('K'); /* Next -> Plugboard screen */
  tap_letter('K'); /* Finish settings -> enter ENIGMA encoding screen */

  /* Type the encrypted body of Message 1 (after the 5-letter Kenngruppe block AAIMP): */
  const char *msg1_body = "UYZGYJKDJIKEKKAJOQHYQPRNGNYUGZAUWCGEKBNAPKGDPZURPQWLUWDEUWECVOYBDBJA";
  for (const char *p = msg1_body; *p; p++) {
    tap_letter(*p);
  }

  if (emu_2019_get_badge_level() != 2) {
    fprintf(stderr, "FAIL Message 1 challenge decode: badgeLevel=%d (expected 2)\n",
            emu_2019_get_badge_level());
    return 1;
  }
  printf("  [PASS] Challenge Message 1 (10/1/1942) decoded via firmware controls -> LEVELED UP (badgeLevel 1 -> %d)!\n",
         emu_2019_get_badge_level());
  return 0;
}

static int generate_smoke_screenshots(const char *out_dir) {
  mkdir(out_dir, 0755);
  char path[512];

  memset(emu_eeprom(), 0, (size_t)emu_eeprom_size());
  emu_init(0);
  emu_2019_plugboard_clear();

  /* 1. Boot splash (sc4 logo during 1000 ms boot delay after 510 ms ISSI init) */
  step_n(60);
  snprintf(path, sizeof(path), "%s/01-boot.png", out_dir);
  write_png(path, emu_framebuffer(), emu_fb_width(), emu_fb_height());
  printf("[smoke] Wrote %s\n", path);

  /* Advance into main menu -> select ENIGMA EMULATOR */
  step_n(80);
  tap_letter('S');
  snprintf(path, sizeof(path), "%s/02-menu.png", out_dir);
  write_png(path, emu_framebuffer(), emu_fb_width(), emu_fb_height());
  printf("[smoke] Wrote %s\n", path);

  /* 3. Enter Enigma Emulator (step through 5 settings screens with K, then encode letters) */
  tap_letter('K');
  for (int i = 0; i < 5; i++) tap_letter('K');
  step_n(590); /* Let "Start Encoding - Hold ANY key to exit" finish scrolling so "ENIGMA" header shows */
  tap_letter('S');
  tap_letter('A');
  tap_letter('I');
  tap_letter('N');
  tap_letter('T');
  snprintf(path, sizeof(path), "%s/03-enigma-emulator.png", out_dir);
  write_png(path, emu_framebuffer(), emu_fb_width(), emu_fb_height());
  printf("[smoke] Wrote %s\n", path);

  /* Exit Enigma Emulator (hold any key 3s returns to main menu at ENIGMA EMULATOR) */
  hold_letter_3s('K');

  /* 4. Snake game (menuState 1 -> S -> menuState 2 GAMES -> K starts Snake) */
  tap_letter('S');
  tap_letter('K');
  step_n(24);
  tap_letter('W');
  step_n(24);
  snprintf(path, sizeof(path), "%s/04-snake.png", out_dir);
  write_png(path, emu_framebuffer(), emu_fb_width(), emu_fb_height());
  printf("[smoke] Wrote %s\n", path);

  /* Exit Snake with K, then navigate menuState 2 -> 4 (CUSTOM MESSAGE) */
  tap_letter('K');
  tap_letter('S');
  tap_letter('S');
  tap_letter('K');
  /* Type "SAINTCON" in Custom Message editor */
  const char *custom_word = "SAINTCON";
  for (int c = 0; custom_word[c] != '\0'; c++) {
    if (c > 0) tap_letter('D');
    int diff = custom_word[c] - 'A';
    for (int i = 0; i < diff; i++) tap_letter('W');
  }
  step_n(12); /* Ensure cursor underline is visible (15 <= framecounter < 30) */
  snprintf(path, sizeof(path), "%s/05-custom-message.png", out_dir);
  write_png(path, emu_framebuffer(), emu_fb_width(), emu_fb_height());
  printf("[smoke] Wrote %s\n", path);
  tap_letter('K'); /* Save custom message */

  /* 6. At-home challenge step: decode Message 1 end-to-end and capture "LEVELED UP" */
  const char *msg1_wires[10] = {"AV", "CK", "DL", "EU", "FH", "GI", "JO", "NX", "PW", "YZ"};
  for (int i = 0; i < 10; i++) {
    emu_2019_plugboard_connect(msg1_wires[i][0] - 'A', msg1_wires[i][1] - 'A');
  }
  /* Navigate from menuState 4 -> menuState 1 (Enigma Emulator) */
  for (int i = 0; i < 3; i++) tap_letter('W');
  tap_letter('K');
  /* Settings are already 9 6 4 / O K E / S A I from run_enigma_tests(); step through 5 screens */
  for (int i = 0; i < 5; i++) tap_letter('K');

  const char *msg1_body = "UYZGYJKDJIKEKKAJOQHYQPRNGNYUGZAUWCGEKBNAPKGDPZURPQWLUWDEUWECVOYBDBJA";
  for (const char *p = msg1_body; *p; p++) {
    tap_letter(*p);
  }
  step_n(40); /* Let initial scrollCounter finish so "LEVELED UP" header is displayed in red */
  snprintf(path, sizeof(path), "%s/06-challenge-step.png", out_dir);
  write_png(path, emu_framebuffer(), emu_fb_width(), emu_fb_height());
  printf("[smoke] Wrote %s (badgeLevel=%d)\n", path, emu_2019_get_badge_level());

  return 0;
}

int main(int argc, char **argv) {
  if (argc >= 2 && strcmp(argv[1], "--test-enigma") == 0) {
    return run_enigma_tests();
  }
  if (argc >= 3 && strcmp(argv[1], "--smoke") == 0) {
    if (run_enigma_tests() != 0) return 1;
    return generate_smoke_screenshots(argv[2]);
  }
  printf("Usage: %s [--test-enigma | --smoke <out_dir>]\n", argv[0]);
  return run_enigma_tests();
}
