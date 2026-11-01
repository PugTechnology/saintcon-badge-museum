// emulator/web/mock-core.js
// Fallback JavaScript implementation of the SAINTCON 2025 badge core ABI.
// Implements the exact C/WASM ABI over a shared ArrayBuffer (HEAPU8 / HEAPU16):
//   void      emu_init(uint32_t seed)
//   void      emu_step(uint32_t dt_ms, uint8_t buttons)
//   uint16_t* emu_framebuffer(void)
//   uint8_t*  emu_leds(void)
//   uint8_t   emu_backlight(void)
//   uint8_t*  emu_eeprom(void)
//   int       emu_eeprom_dirty(void)
//   void      emu_set_speed(uint32_t mult)

(function (global) {
  'use strict';

  const SCREEN_W = 240;
  const SCREEN_H = 320;
  const NUM_LEDS = 20;
  const EEPROM_SIZE = 512;

  // Compact 5x7 ASCII bitmap font (codes 32..95)
  const FONT_5X7 = {
    ' ': [0x00, 0x00, 0x00, 0x00, 0x00],
    '!': [0x00, 0x00, 0x5f, 0x00, 0x00],
    '"': [0x00, 0x07, 0x00, 0x07, 0x00],
    '#': [0x14, 0x7f, 0x14, 0x7f, 0x14],
    '$': [0x24, 0x2a, 0x7f, 0x2a, 0x12],
    '%': [0x23, 0x13, 0x08, 0x64, 0x62],
    '&': [0x36, 0x49, 0x55, 0x22, 0x50],
    "'": [0x00, 0x05, 0x03, 0x00, 0x00],
    '(': [0x00, 0x1c, 0x22, 0x41, 0x00],
    ')': [0x00, 0x41, 0x22, 0x1c, 0x00],
    '*': [0x14, 0x08, 0x3e, 0x08, 0x14],
    '+': [0x08, 0x08, 0x3e, 0x08, 0x08],
    ',': [0x00, 0x50, 0x30, 0x00, 0x00],
    '-': [0x08, 0x08, 0x08, 0x08, 0x08],
    '.': [0x00, 0x60, 0x60, 0x00, 0x00],
    '/': [0x20, 0x10, 0x08, 0x04, 0x02],
    '0': [0x3e, 0x51, 0x49, 0x45, 0x3e],
    '1': [0x00, 0x42, 0x7f, 0x40, 0x00],
    '2': [0x42, 0x61, 0x51, 0x49, 0x46],
    '3': [0x21, 0x41, 0x45, 0x4b, 0x31],
    '4': [0x18, 0x14, 0x12, 0x7f, 0x10],
    '5': [0x27, 0x45, 0x45, 0x45, 0x39],
    '6': [0x3c, 0x4a, 0x49, 0x49, 0x30],
    '7': [0x01, 0x71, 0x09, 0x05, 0x03],
    '8': [0x36, 0x49, 0x49, 0x49, 0x36],
    '9': [0x06, 0x49, 0x49, 0x29, 0x1e],
    ':': [0x00, 0x36, 0x36, 0x00, 0x00],
    ';': [0x00, 0x56, 0x36, 0x00, 0x00],
    '<': [0x08, 0x14, 0x22, 0x41, 0x00],
    '=': [0x14, 0x14, 0x14, 0x14, 0x14],
    '>': [0x00, 0x41, 0x22, 0x14, 0x08],
    '?': [0x02, 0x01, 0x51, 0x09, 0x06],
    '@': [0x32, 0x49, 0x79, 0x41, 0x3e],
    'A': [0x7e, 0x11, 0x11, 0x11, 0x7e],
    'B': [0x7f, 0x49, 0x49, 0x49, 0x36],
    'C': [0x3e, 0x41, 0x41, 0x41, 0x22],
    'D': [0x7f, 0x41, 0x41, 0x22, 0x1c],
    'E': [0x7f, 0x49, 0x49, 0x49, 0x41],
    'F': [0x7f, 0x09, 0x09, 0x09, 0x01],
    'G': [0x3e, 0x41, 0x49, 0x49, 0x7a],
    'H': [0x7f, 0x08, 0x08, 0x08, 0x7f],
    'I': [0x00, 0x41, 0x7f, 0x41, 0x00],
    'J': [0x20, 0x40, 0x41, 0x3f, 0x01],
    'K': [0x7f, 0x08, 0x14, 0x22, 0x41],
    'L': [0x7f, 0x40, 0x40, 0x40, 0x40],
    'M': [0x7f, 0x02, 0x0c, 0x02, 0x7f],
    'N': [0x7f, 0x04, 0x08, 0x10, 0x7f],
    'O': [0x3e, 0x41, 0x41, 0x41, 0x3e],
    'P': [0x7f, 0x09, 0x09, 0x09, 0x06],
    'Q': [0x3e, 0x41, 0x51, 0x21, 0x5e],
    'R': [0x7f, 0x09, 0x19, 0x29, 0x46],
    'S': [0x46, 0x49, 0x49, 0x49, 0x31],
    'T': [0x01, 0x01, 0x7f, 0x01, 0x01],
    'U': [0x3f, 0x40, 0x40, 0x40, 0x3f],
    'V': [0x1f, 0x20, 0x40, 0x20, 0x1f],
    'W': [0x3f, 0x40, 0x38, 0x40, 0x3f],
    'X': [0x63, 0x14, 0x08, 0x14, 0x63],
    'Y': [0x07, 0x08, 0x70, 0x08, 0x07],
    'Z': [0x61, 0x51, 0x49, 0x45, 0x43],
    '[': [0x00, 0x7f, 0x41, 0x41, 0x00],
    '\\': [0x02, 0x04, 0x08, 0x10, 0x20],
    ']': [0x00, 0x41, 0x41, 0x7f, 0x00],
    '^': [0x04, 0x02, 0x01, 0x02, 0x04],
    '_': [0x40, 0x40, 0x40, 0x40, 0x40],
  };

  function rgb565(r, g, b) {
    return (((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3)) & 0xffff;
  }

  function hsvToRgb(h, s, v) {
    const i = Math.floor(h * 6);
    const f = h * 6 - i;
    const p = v * (1 - s);
    const q = v * (1 - f * s);
    const t = v * (1 - (1 - f) * s);
    let r = 0, g = 0, b = 0;
    switch (i % 6) {
      case 0: r = v; g = t; b = p; break;
      case 1: r = q; g = v; b = p; break;
      case 2: r = p; g = v; b = t; break;
      case 3: r = p; g = q; b = v; break;
      case 4: r = t; g = p; b = v; break;
      case 5: r = v; g = p; b = q; break;
    }
    return [Math.round(r * 255), Math.round(g * 255), Math.round(b * 255)];
  }

  function createMockCore() {
    // Memory layout in a single ArrayBuffer (256 KB):
    //   0x00000 .. 0x257FF : Framebuffer (240*320*2 = 153,600 bytes, 2-byte aligned)
    //   0x25800 .. 0x2583B : LEDs (20*3 = 60 bytes)
    //   0x25900 .. 0x25AFF : EEPROM (512 bytes)
    const FB_PTR = 0x00000;
    const LEDS_PTR = 0x25800;
    const EEPROM_PTR = 0x25900;
    const TOTAL_BYTES = 0x26000;

    const buffer = new ArrayBuffer(TOTAL_BYTES);
    const HEAPU8 = new Uint8Array(buffer);
    const HEAPU16 = new Uint16Array(buffer);
    const fbView = new Uint16Array(buffer, FB_PTR, SCREEN_W * SCREEN_H);
    const ledsView = new Uint8Array(buffer, LEDS_PTR, NUM_LEDS * 3);
    const eepromView = new Uint8Array(buffer, EEPROM_PTR, EEPROM_SIZE);

    let rngState = 0x12345678;
    let speedMult = 1;
    let backlight = 255;
    let eepromDirty = 0;
    let totalMs = 0;
    let petMs = 0;
    let barPos = 0;
    let ledPhase = 0;
    let prevButtons = 0;
    let lastDirText = 'NONE';
    let currentDirText = 'IDLE';
    let bootCount = 0;
    let eepromCounter = 0;

    const MAGIC_OFFSET = 256; // Binary metadata at bytes 256..271 so 0..255 holds NDEF message

    function writeNdefText(text) {
      // Encode NFC Forum Type 5 Tag (ST25DV04K) Capability Container + NDEF Text Record in bytes 0..255
      eepromView.fill(0, 0, 256);
      // 4-byte Capability Container (CC): Magic E1, Version 40, Size 40 (512B / 8), Read/Write 00
      eepromView[0] = 0xe1;
      eepromView[1] = 0x40;
      eepromView[2] = 0x40;
      eepromView[3] = 0x00;

      const encoder = new TextEncoder();
      const textBytes = encoder.encode(text.slice(0, 220));
      const langBytes = [0x65, 0x6e]; // "en"
      const payloadLen = 1 + langBytes.length + textBytes.length;
      // NDEF record header: MB=1, ME=1, CF=0, SR=1, IL=0, TNF=0x01 (Well-Known) -> 0xD1
      // Type Length = 1 ('T' = 0x54), Payload Length = payloadLen
      const ndefLen = 4 + payloadLen;

      let idx = 4;
      eepromView[idx++] = 0x03; // NDEF Message TLV Tag
      eepromView[idx++] = ndefLen; // TLV Length (short format < 255)
      eepromView[idx++] = 0xd1; // NDEF Header (MB|ME|SR|TNF_WELL_KNOWN)
      eepromView[idx++] = 0x01; // Type Length = 1
      eepromView[idx++] = payloadLen; // Payload Length
      eepromView[idx++] = 0x54; // Type = 'T' (Text)
      eepromView[idx++] = langBytes.length; // Status byte (UTF-8, lang length = 2)
      eepromView[idx++] = langBytes[0];
      eepromView[idx++] = langBytes[1];
      for (let i = 0; i < textBytes.length && idx < 254; i++) {
        eepromView[idx++] = textBytes[i];
      }
      eepromView[idx++] = 0xfe; // Terminator TLV
    }

    function syncEeprom() {
      // Save binary counters in 256..271
      eepromView[MAGIC_OFFSET + 0] = 0x53; // 'S'
      eepromView[MAGIC_OFFSET + 1] = 0x43; // 'C'
      eepromView[MAGIC_OFFSET + 2] = 0x32; // '2'
      eepromView[MAGIC_OFFSET + 3] = 0x35; // '5'
      eepromView[MAGIC_OFFSET + 4] = bootCount & 0xff;
      eepromView[MAGIC_OFFSET + 5] = (bootCount >> 8) & 0xff;
      eepromView[MAGIC_OFFSET + 6] = (bootCount >> 16) & 0xff;
      eepromView[MAGIC_OFFSET + 7] = (bootCount >> 24) & 0xff;
      eepromView[MAGIC_OFFSET + 8] = eepromCounter & 0xff;
      eepromView[MAGIC_OFFSET + 9] = (eepromCounter >> 8) & 0xff;
      eepromView[MAGIC_OFFSET + 10] = (eepromCounter >> 16) & 0xff;
      eepromView[MAGIC_OFFSET + 11] = (eepromCounter >> 24) & 0xff;

      const msg = `SAINTCON 2025 Badge | Counter: ${eepromCounter} | Boots: ${bootCount} | Last Input: ${lastDirText} | Speed: ${speedMult}x`;
      writeNdefText(msg);
      eepromDirty = 1;
    }

    function fillRect(x, y, w, h, color) {
      const x0 = Math.max(0, x | 0);
      const y0 = Math.max(0, y | 0);
      const x1 = Math.min(SCREEN_W, (x + w) | 0);
      const y1 = Math.min(SCREEN_H, (y + h) | 0);
      for (let py = y0; py < y1; py++) {
        const row = py * SCREEN_W;
        for (let px = x0; px < x1; px++) {
          fbView[row + px] = color;
        }
      }
    }

    function drawChar(ch, x, y, scale, color) {
      const glyph = FONT_5X7[ch] || FONT_5X7['?'];
      for (let col = 0; col < 5; col++) {
        const bits = glyph[col];
        for (let row = 0; row < 7; row++) {
          if ((bits >> row) & 1) {
            fillRect(x + col * scale, y + row * scale, scale, scale, color);
          }
        }
      }
    }

    function drawText(str, x, y, scale, color) {
      const upper = String(str).toUpperCase();
      let cx = x;
      for (let i = 0; i < upper.length; i++) {
        drawChar(upper[i], cx, y, scale, color);
        cx += 6 * scale;
      }
    }

    function drawTextCentered(str, y, scale, color) {
      const upper = String(str).toUpperCase();
      const w = upper.length * 6 * scale - scale;
      const x = Math.floor((SCREEN_W - w) / 2);
      drawText(upper, x, y, scale, color);
    }

    function buttonsToDirString(buttons) {
      const parts = [];
      if (buttons & 1) parts.push('UP');
      if (buttons & 2) parts.push('DOWN');
      if (buttons & 4) parts.push('LEFT');
      if (buttons & 8) parts.push('RIGHT');
      if (buttons & 16) parts.push('CENTER');
      return parts.length ? parts.join('+') : 'IDLE';
    }

    function renderFrame(buttons) {
      // Background dark navy
      fillRect(0, 0, SCREEN_W, SCREEN_H, rgb565(12, 18, 32));

      // Header bar
      fillRect(0, 0, SCREEN_W, 28, rgb565(28, 42, 74));
      drawTextCentered('SAINTCON 2025', 4, 2, rgb565(255, 210, 70));
      drawTextCentered('MOCK CORE TEST PATTERN', 19, 1, rgb565(170, 210, 255));

      // SMPTE / RGB565 test color bars (y = 32..56)
      const bars = [
        rgb565(255, 40, 40),
        rgb565(40, 255, 40),
        rgb565(60, 100, 255),
        rgb565(40, 255, 255),
        rgb565(255, 40, 255),
        rgb565(255, 255, 40),
        rgb565(255, 255, 255),
        rgb565(120, 128, 140),
      ];
      const barW = SCREEN_W / bars.length;
      for (let i = 0; i < bars.length; i++) {
        fillRect(i * barW, 32, barW, 20, bars[i]);
      }

      // Moving scanner bar track (y = 58..82)
      fillRect(10, 58, SCREEN_W - 20, 24, rgb565(22, 30, 48));
      const trackW = SCREEN_W - 24;
      const scannerW = 36;
      const maxOffset = trackW - scannerW;
      const cycle = (barPos % (maxOffset * 2));
      const pos = cycle <= maxOffset ? cycle : maxOffset * 2 - cycle;
      fillRect(12 + pos, 60, scannerW, 20, rgb565(0, 230, 180));
      fillRect(12 + pos + 8, 60, scannerW - 16, 20, rgb565(180, 255, 240));

      // Pressed direction banner (y = 90..150)
      const isPressed = buttons !== 0;
      fillRect(10, 90, SCREEN_W - 20, 56, isPressed ? rgb565(35, 85, 55) : rgb565(20, 28, 46));
      drawTextCentered('PRESSED INPUT:', 96, 1, rgb565(160, 185, 220));
      const scale = currentDirText.length > 9 ? 2 : 3;
      drawTextCentered(
        currentDirText,
        114,
        scale,
        isPressed ? rgb565(100, 255, 130) : rgb565(130, 150, 180)
      );

      // Visual 5-way joystick indicator on screen (center at 120, 195)
      const jx = 120, jy = 192;
      const inactiveCol = rgb565(38, 48, 72);
      const activeCol = rgb565(255, 190, 40);
      // UP
      fillRect(jx - 12, jy - 36, 24, 20, (buttons & 1) ? activeCol : inactiveCol);
      drawText('U', jx - 5, jy - 32, 2, rgb565(15, 20, 30));
      // DOWN
      fillRect(jx - 12, jy + 16, 24, 20, (buttons & 2) ? activeCol : inactiveCol);
      drawText('D', jx - 5, jy + 20, 2, rgb565(15, 20, 30));
      // LEFT
      fillRect(jx - 38, jy - 10, 22, 20, (buttons & 4) ? activeCol : inactiveCol);
      drawText('L', jx - 32, jy - 6, 2, rgb565(15, 20, 30));
      // RIGHT
      fillRect(jx + 16, jy - 10, 22, 20, (buttons & 8) ? activeCol : inactiveCol);
      drawText('R', jx + 22, jy - 6, 2, rgb565(15, 20, 30));
      // CENTER
      fillRect(jx - 12, jy - 10, 24, 20, (buttons & 16) ? rgb565(255, 90, 90) : rgb565(55, 68, 98));
      drawText('C', jx - 5, jy - 6, 2, rgb565(255, 255, 255));

      // Telemetry / EEPROM / Chain status panel (y = 240..314)
      fillRect(10, 240, SCREEN_W - 20, 72, rgb565(18, 26, 44));
      const headIdx = Math.floor(ledPhase) % NUM_LEDS;
      drawText(`EEPROM CNT: ${eepromCounter}`, 18, 248, 1, rgb565(255, 220, 100));
      drawText(`BOOT COUNT: ${bootCount}`, 132, 248, 1, rgb565(180, 220, 255));
      drawText(`LAST DIR  : ${lastDirText}`, 18, 262, 1, rgb565(140, 255, 170));
      drawText(`LED CHASE : ${headIdx} (D${headIdx + 1})`, 18, 276, 1, rgb565(255, 150, 220));
      drawText(`SPEED MULT: ${speedMult}X`, 18, 290, 1, rgb565(120, 230, 255));
      drawText(`BL:${backlight}`, 160, 290, 1, rgb565(200, 200, 220));
      drawText(`PET TIME  : ${(petMs / 1000).toFixed(1)}S`, 18, 302, 1, rgb565(160, 170, 195));
    }

    function updateLeds() {
      const head = Math.floor(ledPhase) % NUM_LEDS;
      for (let i = 0; i < NUM_LEDS; i++) {
        // Distance behind head in chain order 0..19
        const dist = (head - i + NUM_LEDS) % NUM_LEDS;
        let r = 0, g = 0, b = 0;
        if (dist === 0) {
          r = 255; g = 255; b = 255;
        } else if (dist < 7) {
          const intensity = (7 - dist) / 7;
          const hue = (i / NUM_LEDS + totalMs * 0.0002) % 1.0;
          const rgb = hsvToRgb(hue, 0.9, intensity * 0.95);
          r = rgb[0]; g = rgb[1]; b = rgb[2];
        }
        ledsView[i * 3 + 0] = r;
        ledsView[i * 3 + 1] = g;
        ledsView[i * 3 + 2] = b;
      }
    }

    function emu_init(seed) {
      rngState = (seed >>> 0) || 0x12345678;
      totalMs = 0;
      petMs = 0;
      barPos = 0;
      ledPhase = 0;
      prevButtons = 0;
      currentDirText = 'IDLE';

      // Check if EEPROM already has valid 'SC25' signature at bytes 256..259
      if (
        eepromView[MAGIC_OFFSET + 0] === 0x53 &&
        eepromView[MAGIC_OFFSET + 1] === 0x43 &&
        eepromView[MAGIC_OFFSET + 2] === 0x32 &&
        eepromView[MAGIC_OFFSET + 3] === 0x35
      ) {
        bootCount =
          (eepromView[MAGIC_OFFSET + 4] |
            (eepromView[MAGIC_OFFSET + 5] << 8) |
            (eepromView[MAGIC_OFFSET + 6] << 16) |
            (eepromView[MAGIC_OFFSET + 7] << 24)) >>> 0;
        eepromCounter =
          (eepromView[MAGIC_OFFSET + 8] |
            (eepromView[MAGIC_OFFSET + 9] << 8) |
            (eepromView[MAGIC_OFFSET + 10] << 16) |
            (eepromView[MAGIC_OFFSET + 11] << 24)) >>> 0;
      } else {
        bootCount = 0;
        eepromCounter = 0;
      }

      bootCount = (bootCount + 1) >>> 0;
      eepromCounter = (eepromCounter + 1) >>> 0;
      syncEeprom();
      updateLeds();
      renderFrame(0);
    }

    function emu_step(dt_ms, buttons) {
      const dt = Math.max(0, dt_ms >>> 0);
      const btns = buttons & 0x1f;
      totalMs += dt;
      petMs += dt * speedMult;

      // Advance moving bar and LED chase in chain order
      const effectiveRate = 1 + Math.log2(Math.max(1, speedMult));
      barPos = (barPos + dt * 0.14 * effectiveRate) % 10000;
      ledPhase = (ledPhase + dt * 0.012 * effectiveRate) % NUM_LEDS;

      currentDirText = buttonsToDirString(btns);

      // Rising edge on any button increments EEPROM counter and updates NDEF message
      const newlyPressed = btns & ~prevButtons;
      if (newlyPressed !== 0) {
        lastDirText = currentDirText;
        eepromCounter = (eepromCounter + 1) >>> 0;
        // Allow CENTER+UP / CENTER+DOWN to test backlight level dimming in mock
        if ((btns & 16) && (newlyPressed & 1)) {
          backlight = Math.min(255, backlight + 35);
        } else if ((btns & 16) && (newlyPressed & 2)) {
          backlight = Math.max(40, backlight - 35);
        }
        syncEeprom();
      }
      prevButtons = btns;

      updateLeds();
      renderFrame(btns);
    }

    function emu_framebuffer() {
      return FB_PTR;
    }

    function emu_leds() {
      return LEDS_PTR;
    }

    function emu_backlight() {
      return backlight & 0xff;
    }

    function emu_eeprom() {
      return EEPROM_PTR;
    }

    function emu_eeprom_dirty() {
      const d = eepromDirty;
      eepromDirty = 0;
      return d;
    }

    function emu_set_speed(mult) {
      speedMult = Math.max(1, mult >>> 0);
      syncEeprom();
    }

    function emu_fb_width() {
      return SCREEN_W;
    }

    function emu_fb_height() {
      return SCREEN_H;
    }

    function emu_led_count() {
      return NUM_LEDS;
    }

    function emu_eeprom_size() {
      return EEPROM_SIZE;
    }

    function setBacklightForTest(val) {
      backlight = Math.max(0, Math.min(255, val | 0));
    }

    return {
      isMock: true,
      HEAPU8,
      HEAPU16,
      memory: { buffer },
      _emu_init: emu_init,
      _emu_step: emu_step,
      _emu_framebuffer: emu_framebuffer,
      _emu_leds: emu_leds,
      _emu_backlight: emu_backlight,
      _emu_eeprom: emu_eeprom,
      _emu_eeprom_dirty: emu_eeprom_dirty,
      _emu_set_speed: emu_set_speed,
      _emu_fb_width: emu_fb_width,
      _emu_fb_height: emu_fb_height,
      _emu_led_count: emu_led_count,
      _emu_eeprom_size: emu_eeprom_size,
      emu_init,
      emu_step,
      emu_framebuffer,
      emu_leds,
      emu_backlight,
      emu_eeprom,
      emu_eeprom_dirty,
      emu_set_speed,
      emu_fb_width,
      emu_fb_height,
      emu_led_count,
      emu_eeprom_size,
      setBacklightForTest,
    };
  }

  global.createMockCore = createMockCore;
})(typeof window !== 'undefined' ? window : globalThis);
