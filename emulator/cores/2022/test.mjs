#!/usr/bin/env node
/**
 * CLI Verification & 8 MHz vs 125 MHz Reference Comparison Suite
 * for the SAINTCON 2022 Clue-style Notepad Badge RP2040 Emulator.
 */

import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import {
  createBadge,
  buildKioskMessage,
  parseBadgeMessage,
  escapeSerialBytes,
  shareToBrightness,
  sharesToRgb,
  FLASH_LED_TABLES,
  STDIO_HOOKS,
} from './src/badge2022.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const UF2_PATH = path.join(__dirname, 'upstream', 'Saintcon2022BadgeFirmwareFix.uf2');
const uf2Bytes = new Uint8Array(fs.readFileSync(UF2_PATH));

function pressButton(badge, btn, holdMs = 80, releaseMs = 80) {
  badge.setButton(btn, true);
  badge.stepLockstep(holdMs * 1000);
  badge.setButton(btn, false);
  badge.stepLockstep(releaseMs * 1000);
}

function summarizeLedShares(report) {
  let activeChannels = 0;
  let totalShare = 0;
  let maxShare = 0;
  for (let i = 0; i < 156; i++) {
    const s = report.shares[i];
    if (s > 0.0001) {
      activeChannels++;
      totalShare += s;
      if (s > maxShare) maxShare = s;
    }
  }
  return {
    gpioSwitchesPerSec: Math.round(report.gpioSwitchCount / (report.windowNanos / 1e9)),
    activeChannels,
    totalSharePct: +(totalShare * 100).toFixed(3),
    maxSharePct: +(maxShare * 100).toFixed(3),
  };
}

function maxChannelShareDiffPct(repA, repB) {
  let maxDiff = 0;
  for (let i = 0; i < 156; i++) {
    const d = Math.abs(repA.shares[i] - repB.shares[i]) * 100;
    if (d > maxDiff) maxDiff = d;
  }
  return +maxDiff.toFixed(4);
}

function decodeThumbBlTarget(pcAddr, hw1, hw2) {
  const s = (hw1 >>> 10) & 1;
  const imm10 = hw1 & 0x3ff;
  const j1 = (hw2 >>> 13) & 1;
  const j2 = (hw2 >>> 11) & 1;
  const imm11 = hw2 & 0x7ff;
  const i1 = (~(j1 ^ s)) & 1;
  const i2 = (~(j2 ^ s)) & 1;
  let offset = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1);
  if (s) offset |= ~0x1ffffff;
  return (pcAddr + 4 + offset) >>> 0;
}

console.log('=== 1. Flash LED Tables, Pin Pair Uniqueness & Hooked Address Byte Checks ===');
{
  const badge = createBadge({ uf2Bytes, cycleNanos: 125 });
  assert.equal(badge.ledTable.length, 52, 'Expected 52 RGB LEDs total');
  const suspects = badge.ledTable.filter((e) => e.category === 'suspects');
  const attacks = badge.ledTable.filter((e) => e.category === 'attacks');
  const rooms = badge.ledTable.filter((e) => e.category === 'rooms');
  assert.equal(suspects.length, 21, 'Expected 21 Suspects LEDs at 0x1000b60c');
  assert.equal(attacks.length, 21, 'Expected 21 Attacks LEDs at 0x1000b314');
  assert.equal(rooms.length, 10, 'Expected 10 Rooms LEDs at 0x1000b514');

  const seenPairs = new Set();
  for (const e of badge.ledTable) {
    for (const ch of ['r', 'g', 'b']) {
      const { cathode, anode } = e[ch];
      assert.ok(cathode >= 0 && cathode <= 14, `Invalid cathode GP${cathode}`);
      assert.ok(anode >= 0 && anode <= 14, `Invalid anode GP${anode}`);
      assert.notEqual(cathode, anode, `Cathode == Anode on LED ${e.ledIndex} ${ch}`);
      const key = `${cathode}->${anode}`;
      assert.ok(!seenPairs.has(key), `Duplicate charlieplex pair ${key}`);
      seenPairs.add(key);
    }
  }
  assert.equal(seenPairs.size, 156, 'All 156 RGB charlieplex pairs must be unique');

  // Verify the bytes and call sites at the hooked firmware addresses in UF2 flash
  const flashView = new DataView(badge.mcu.flash.buffer);
  const readFlashU16 = (addr) => flashView.getUint16(addr - 0x10000000, true);
  const readFlashU32 = (addr) => flashView.getUint32(addr - 0x10000000, true);

  // Hook 1: GETCHAR_TIMEOUT_US at 0x10006228
  assert.equal(STDIO_HOOKS.GETCHAR_TIMEOUT_US, 0x10006228);
  assert.equal(readFlashU16(0x10006228), 0xb530, '0x10006228 must start with push {r4, r5, lr} (0xb530)');
  assert.equal(readFlashU16(0x1000622a), 0xb083, '0x1000622a must be sub sp, #0xc (0xb083)');
  assert.equal(readFlashU16(0x1000622c), 0x0004, '0x1000622c must be movs r4, r0 (0x0004)');
  assert.equal(readFlashU16(0x100018a4), 0x2000, 'Main loop call site 0x100018a4 must set r0 = 0 (0x2000)');
  const blGetcharTarget = decodeThumbBlTarget(
    0x100018a6,
    readFlashU16(0x100018a6),
    readFlashU16(0x100018a8),
  );
  assert.equal(
    blGetcharTarget,
    STDIO_HOOKS.GETCHAR_TIMEOUT_US,
    'BL at 0x100018a6 in main loop must target GETCHAR_TIMEOUT_US (0x10006228)',
  );

  // Hook 2: STDIO_USB_OUT_CHARS at 0x10006428
  assert.equal(STDIO_HOOKS.STDIO_USB_OUT_CHARS, 0x10006428);
  assert.equal(readFlashU16(0x10006428), 0xb5f0, '0x10006428 must start with push {r4, r5, r6, r7, lr} (0xb5f0)');
  assert.equal(readFlashU16(0x1000642a), 0x46c6, '0x1000642a must be mov lr, r8 (0x46c6)');
  assert.equal(readFlashU16(0x1000642c), 0xb500, '0x1000642c must be push {lr} (0xb500)');
  // Step 50 ms so CRT0 copies .data to SRAM and initializes stdio_usb at 0x20000c20
  badge.stepLockstep(50_000);
  const sramView = new DataView(badge.mcu.sram.buffer);
  const stdioUsbOutCharsPtr = sramView.getUint32(0x20000c20 - 0x20000000, true);
  assert.equal(
    stdioUsbOutCharsPtr,
    (STDIO_HOOKS.STDIO_USB_OUT_CHARS | 1) >>> 0,
    'stdio_usb.out_chars at 0x20000c20 must point to STDIO_USB_OUT_CHARS | 1 (0x10006429)',
  );

  // Hook 3: BOOTLOADER_ENTRY at 0x10000ed2
  assert.equal(STDIO_HOOKS.BOOTLOADER_ENTRY, 0x10000ed2);
  assert.equal(readFlashU16(0x10000ec6), 0x48af, '0x10000ec6 must load rom_table_code UB from 0x10001184');
  assert.equal(readFlashU32(0x10001184), 0x00004255, 'Literal at 0x10001184 must be 0x00004255 ("UB")');
  assert.equal(readFlashU16(0x10000ed2), 0x4798, '0x10000ed2 must be blx r3 (0x4798)');

  console.log('  PASS: 52 RGB LEDs (156 unique pairs) and all hooked addresses/call sites verified.');
}

console.log('\n=== 1B. First Boot on Erased Flash (0xFF), Reboot After First Boot, and Wipe Save ===');
let postFirstBootSectors = null;
{
  const badge = createBadge({ uf2Bytes, cycleNanos: 125 });
  // The 191-block UF2 payload spans 0x10000000..0x1000bf00 (48,896 bytes); flash from 0x1000bf00 upward starts erased (0xFF).
  assert.equal(badge.mcu.readUint8(0x1000bf00), 0xff, 'Flash at 0x1000bf00 (immediately after 191 UF2 blocks) must start erased (0xFF)');
  // Before stepping, flash outside the UF2 image at 0x1cc000..0x1cdfff must be 0xFF (erased)
  for (let i = 0; i < 4096; i++) {
    assert.equal(badge.mcu.flash[0x1cc000 + i], 0xff, `Expected 0x1cc000+${i} to start erased (0xFF)`);
    assert.equal(badge.mcu.flash[0x1cd000 + i], 0xff, `Expected 0x1cd000+${i} to start erased (0xFF)`);
  }

  // Step 150 ms: main sees [0x101cc000] == 0xffffffff, calls save_clues_flash(0) at 0x10000938,
  // and enters rgb_self_test (0x10000a3c..0x10000b2e).
  badge.stepLockstep(150_000);
  const stFirst = badge.getFirmwareState();
  assert.equal(stFirst.clueWordRaw, 0x00000000, 'save_clues_flash(0) must write 0x00000000 to 0x1cc000 on first boot');
  assert.equal(stFirst.inSelfTest, true, 'First boot on erased flash must enter rgb_self_test (0x10000a3c..0x10000b2e)');
  assert.equal(badge.isFlashDirty(), true, 'First boot save_clues_flash(0) must mark flash sector 0x1cc000 dirty');

  // Verify Red self-test phase lights all 52 LEDs on first boot
  badge.resetLedCounters();
  badge.stepLockstep(250_000);
  const firstBootRedRep = badge.getCumulativeLedReport();
  let redCount = 0;
  for (let i = 0; i < 52; i++) {
    if (firstBootRedRep.shares[i * 3] > 0.002) redCount++;
  }
  assert.equal(redCount, 52, 'All 52 LEDs must light Red during first-boot self-test');

  postFirstBootSectors = badge.consumeDirtyFlashSectors();
  assert.ok(postFirstBootSectors, 'Expected dirty flash sectors from first boot');

  // Reboot after first boot with the saved flash sector: must NOT enter self-test again
  const rebootedBadge = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  rebootedBadge.stepLockstep(1_200_000);
  const stReboot = rebootedBadge.getFirmwareState();
  assert.equal(stReboot.inSelfTest, false, 'Reboot after first boot must not run rgb_self_test again');
  assert.equal(stReboot.displayMode, 0, 'Reboot after first boot must enter Mode 0');
  assert.equal(stReboot.suspectsCursor, 0x800000, 'Initial Suspects cursor at bit 23');
  assert.equal(stReboot.attacksCursor, 0x800000, 'Initial Attacks cursor at bit 23');

  // Wipe Save (fresh badge with savedSectors: null): restores 0xFF across 0x1cc000..0x1cdfff and re-runs first-boot self-test
  const wipedBadge = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: null });
  assert.equal(wipedBadge.mcu.flash[0x1cc000], 0xff, 'Wipe Save must start with 0x1cc000 erased to 0xFF');
  wipedBadge.stepLockstep(150_000);
  const stWipe = wipedBadge.getFirmwareState();
  assert.equal(stWipe.inSelfTest, true, 'Wipe Save must re-enter first-boot rgb_self_test');
  assert.equal(stWipe.clueWordRaw, 0x00000000, 'Wipe Save first boot must re-initialize 0x1cc000 to 0');

  console.log('  PASS: First boot on 0xFF flash runs save_clues_flash(0) + self-test; next reboot enters Mode 0; Wipe re-runs first boot.');
}

console.log('\n=== 2. Boot, Button Pull-Ups (GP15..17), GP24/25 Wing Clock & I2C0 0x29 Dark Room Check ===');
{
  const badge = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  badge.stepLockstep(2_200_000); // 2.2 s simulated time
  const st = badge.getFirmwareState();
  assert.equal(st.gp15Input, true, 'GP15 (UP) should be high at boot');
  assert.equal(st.gp16Input, true, 'GP16 (DOWN) should be high at boot');
  assert.equal(st.gp17Input, true, 'GP17 (SELECT) should be high at boot');
  assert.equal(st.ledsEnabled, 1, 'LEDs should be enabled via I2C0 ALS sensor (>100)');
  assert.ok(st.gp24ToggleCount >= 3 && st.gp24ToggleCount <= 5, `Expected GP24 500ms toggles, got ${st.gp24ToggleCount}`);
  assert.ok(st.gp25ToggleCount >= 3 && st.gp25ToggleCount <= 5, `Expected GP25 500ms toggles, got ${st.gp25ToggleCount}`);

  // Verify GP26/27 input changes do NOT pass through to GP24/25 immediately (proving 500ms self-toggle vs pass-through).
  const beforeGp24 = (badge.mcu.sio.gpioValue >>> 24) & 1;
  badge.mcu.gpio[26].setInputValue(!badge.mcu.gpio[26].inputValue);
  badge.mcu.gpio[27].setInputValue(!badge.mcu.gpio[27].inputValue);
  badge.stepLockstep(50_000); // 50 ms (< 500 ms toggle period)
  const afterGp24 = (badge.mcu.sio.gpioValue >>> 24) & 1;
  assert.equal(afterGp24, beforeGp24, 'GP24 only toggles on its 500ms timer, not GP26 pass-through');

  // Test I2C0 0x29 light sensor threshold (polled at 0x10001dc2..0x10001df4 via read_ambient_light at 0x100020c0):
  // Reading 1..100 sets [0x20000788] = 0, causing core1_entry (0x1000031a..0x10000320) to spin with all LEDs off.
  badge.setAlsLux(10); // Dark room (10 <= 100)
  badge.stepLockstep(600_000); // Wait for 500ms repeating timer to poll I2C 0x29
  assert.equal(badge.getFirmwareState().ledsEnabled, 0, 'ALS reading 10 (<=100) must set ledsEnabled=0');
  badge.resetLedCounters();
  badge.stepLockstep(250_000);
  const darkSum = summarizeLedShares(badge.getCumulativeLedReport());
  assert.equal(darkSum.activeChannels, 0, 'All LEDs must be blanked when ledsEnabled=0 in Dark room');

  // Restore bright room (250 > 100)
  badge.setAlsLux(250);
  badge.stepLockstep(600_000);
  assert.equal(badge.getFirmwareState().ledsEnabled, 1, 'ALS reading 250 (>100) must restore ledsEnabled=1');
  badge.resetLedCounters();
  badge.stepLockstep(250_000);
  const brightSum = summarizeLedShares(badge.getCumulativeLedReport());
  assert.ok(brightSum.activeChannels >= 1, 'LEDs must light again when restored to bright room');

  console.log(
    `  PASS: GP15/16/17 high, GP24/25 toggled ${st.gp24ToggleCount}/${st.gp25ToggleCount} times, I2C 0x29 Dark room blanks LEDs (active=${darkSum.activeChannels}) and restores them (active=${brightSum.activeChannels}).`,
  );
}

console.log('\n=== 3. Kiosk Protocol Parity & Unstripped Serial Log (8 MHz Hooks vs 125 MHz USB) ===');
{
  function runKioskSuite(badge, stepUsAfterSend) {
    const results = [];
    const sendAndCollect = (label, msg) => {
      const prevLen = badge.serialLines.length;
      badge.sendSerial(msg);
      badge.stepLockstep(stepUsAfterSend);
      const lines = badge.serialLines.slice(prevLen);
      const st = badge.getFirmwareState();
      results.push({
        label,
        lines,
        clueNum: st.clueNum,
        suspectsMask: st.suspectsMask,
        attacksMask: st.attacksMask,
        roomsMask: st.roomsMask,
        gameWon: st.gameWon,
        displayMode: st.displayMode,
        winSuspectsMask: st.winSuspectsMask,
        winAttacksMask: st.winAttacksMask,
        winRoomsMask: st.winRoomsMask,
      });
    };

    sendAndCollect('ping', 'kiosk:ping\n\r');
    sendAndCollect('hi', 'kiosk:hi\n\r');
    sendAndCollect('bad_length', 'kiosk:short\n\r');
    sendAndCollect('bad_checksum', 'kiosk:E6614103E715222A01A000004000002000FF\n\r');
    sendAndCollect(
      'bad_serial',
      buildKioskMessage('0000000000000000', 1, 0xa00000, 0x400000, 0x2000),
    );
    sendAndCollect(
      'valid_clue',
      buildKioskMessage('e6614103e715222a', 3, 0xa00000, 0x400000, 0x2000),
    );
    sendAndCollect(
      'win_clue',
      buildKioskMessage('e6614103e715222a', 0xff, 0x800000, 0x800000, 0x8000),
    );
    return results;
  }

  const t0Hook = performance.now();
  const badgeHook8MHz = createBadge({
    uf2Bytes,
    cycleNanos: 125,
    stdioMode: 'hook',
    savedSectors: postFirstBootSectors,
  });
  badgeHook8MHz.stepLockstep(1_200_000);
  const resHook = runKioskSuite(badgeHook8MHz, 300_000);
  const hookMs = Math.round(performance.now() - t0Hook);

  const t0Usb = performance.now();
  const badgeUsb125MHz = createBadge({
    uf2Bytes,
    cycleNanos: 8,
    stdioMode: 'usb',
    savedSectors: postFirstBootSectors,
  });
  badgeUsb125MHz.stepLockstep(1_500_000);
  const resUsb = runKioskSuite(badgeUsb125MHz, 300_000);
  const usbMs = Math.round(performance.now() - t0Usb);

  // Verify both 8 MHz hook and 125 MHz USB emit the 36-hex badge: reply followed by
  // the 2 non-zero little-endian stack bytes of saved r4 at sp+0x28..0x2b in send_badge_message (0x100006c8),
  // where r4 holds the boot-time time_us_64() timestamp from 0x10000ffa (0x00004a20 -> " J" at 8 MHz,
  // 0x0000293f -> "?)" at 125 MHz USB).
  const hiLineHook = resHook[1].lines.find((l) => l.startsWith('badge:'));
  const hiLineUsb = resUsb[1].lines.find((l) => l.startsWith('badge:'));
  assert.equal(
    hiLineHook,
    'badge:8000000000E6614103E715222A018000002C J',
    '8 MHz hook must preserve trailing stack bytes " J" from saved r4 (0x00004a20)',
  );
  assert.equal(
    hiLineUsb,
    'badge:8000000000E6614103E715222A018000002C?)',
    '125 MHz USB must preserve trailing stack bytes "?)" from saved r4 (0x0000293f)',
  );
  assert.equal(
    hiLineHook.slice(0, 42),
    hiLineUsb.slice(0, 42),
    '42-char badge:<36-hex> prefix must be identical between 8 MHz hook and 125 MHz USB',
  );
  assert.equal(
    escapeSerialBytes('badge:test\x00\x1f\x7f\xff'),
    'badge:test\\x00\\x1f\\x7f\\xff',
    'escapeSerialBytes must escape non-printing bytes as \\xNN',
  );

  // Normalize the 2 clock-dependent r4 timestamp bytes on 'hi' before deepEqual of all 7 results
  const normHook = structuredClone(resHook);
  const normUsb = structuredClone(resUsb);
  normHook[1].lines = [hiLineHook.slice(0, 42)];
  normUsb[1].lines = [hiLineUsb.slice(0, 42)];
  assert.deepEqual(
    normHook,
    normUsb,
    '8 MHz stdio hooks must produce identical kiosk outputs and state to 125 MHz USB',
  );

  // Verify parsed badge:hi message and exact error strings
  assert.ok(resHook[0].lines.includes('badge:pong'), 'Expected badge:pong');
  const hiParsed = parseBadgeMessage(hiLineHook);
  assert.equal(hiParsed.valid, true);
  assert.equal(hiParsed.serial, 'e6614103e715222a');
  assert.equal(hiParsed.statusByte, 1);
  assert.equal(hiParsed.suspectsCursor, 0x800000);
  assert.equal(hiParsed.attacksCursor, 0x800000);
  assert.equal(hiParsed.roomsCursor, 0x0000);
  assert.ok(
    resHook[2].lines.includes('invalid clue length from kiosk'),
    'Expected invalid clue length from kiosk',
  );
  assert.ok(resHook[3].lines.includes('incorrect checksum'), 'Expected incorrect checksum');
  assert.ok(
    resHook[4].lines.includes("serial number doesn't match"),
    "Expected serial number doesn't match",
  );
  assert.ok(resHook[5].lines.includes('new clue!'), 'Expected new clue!');

  console.log(
    `  PASS: All 7 kiosk commands produced identical serial replies & state (8 MHz hook: ${hookMs} ms vs 125 MHz USB: ${usbMs} ms; stack leak bytes: 8MHz=${JSON.stringify(hiLineHook.slice(42))}, 125MHz=${JSON.stringify(hiLineUsb.slice(42))}).`,
  );
  for (const r of resHook) {
    console.log(`    [${r.label}] -> ${JSON.stringify(r.lines)}`);
  }
}

console.log('\n=== 4. Buttons, Short Tap Minimum Hold (<20ms), Cursor Wrap, and Flash Persistence ===');
{
  const badge = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  badge.stepLockstep(1_200_000);

  // Initial cursors at index 0 (bit 23 = 0x800000)
  let st = badge.getFirmwareState();
  assert.equal(st.activeCategory, 0);
  assert.equal(st.suspectsCursor, 0x800000);

  // Press UP at index 0 -> wraps to index 20 (bit 3 = 0x000008)
  pressButton(badge, 'UP');
  st = badge.getFirmwareState();
  assert.equal(st.suspectsCursor, 0x000008, 'UP at index 0 should wrap to bit 3 (0x000008)');

  // Press DOWN at index 20 -> wraps back to index 0 (bit 23 = 0x800000)
  pressButton(badge, 'DOWN');
  st = badge.getFirmwareState();
  assert.equal(st.suspectsCursor, 0x800000, 'DOWN at index 20 should wrap to bit 23 (0x800000)');

  // Verify a 1 ms short tap (which would fall inside a 20 ms sleep_ms without minimum hold) still registers!
  badge.setButton('DOWN', true);
  badge.stepLockstep(1_000); // 1 ms press
  badge.setButton('DOWN', false);
  badge.stepLockstep(80_000); // allow minimum hold (25 ms) + release poll to complete
  assert.equal(
    badge.getFirmwareState().suspectsCursor,
    0x400000,
    '1 ms short tap on DOWN must be held for >=25ms and advance Suspects cursor to index 1 (0x400000)',
  );

  // Press SELECT -> switch activeCategory from 0 (Suspects) to 1 (Attacks)
  pressButton(badge, 'SELECT');
  assert.equal(badge.getFirmwareState().activeCategory, 1);

  // Two rapid 1 ms short taps on DOWN queued back-to-back must both count in order -> index 2 (0x200000)
  badge.setButton('DOWN', true);
  badge.stepLockstep(1_000);
  badge.setButton('DOWN', false);
  badge.stepLockstep(2_000);
  badge.setButton('DOWN', true);
  badge.stepLockstep(1_000);
  badge.setButton('DOWN', false);
  badge.stepLockstep(180_000);
  assert.equal(
    badge.getFirmwareState().attacksCursor,
    0x200000,
    'Two rapid 1 ms DOWN taps must both register in order and advance Attacks cursor to index 2 (0x200000)',
  );

  // Press SELECT again -> toggles activeCategory back to 0 (Suspects); Rooms has no user cursor in 0x100013a4
  pressButton(badge, 'SELECT');
  assert.equal(badge.getFirmwareState().activeCategory, 0);
  assert.equal(badge.getFirmwareState().roomsCursor, 0x0000);

  // Verify UP/DOWN also emits a badge:<payload> serial message with the current cursors
  const lastBadgeLine = [...badge.serialLines].reverse().find((l) => l.startsWith('badge:'));
  const parsedCursorMsg = parseBadgeMessage(lastBadgeLine);
  assert.equal(parsedCursorMsg.valid, true);
  assert.equal(parsedCursorMsg.suspectsCursor, 0x400000);
  assert.equal(parsedCursorMsg.attacksCursor, 0x200000);

  // Send a clue (saves 0x1cc000 immediately); UP/DOWN already saved cursors to 0x1cd000
  badge.sendSerial(buildKioskMessage('e6614103e715222a', 5, 0xa00000, 0x400000, 0x2000));
  badge.stepLockstep(300_000);

  assert.equal(badge.isFlashDirty(), true, 'Flash sectors should be marked dirty');
  const savedSectors = badge.consumeDirtyFlashSectors();
  assert.ok(savedSectors, 'Expected saved sectors');
  assert.equal(badge.isFlashDirty(), false, 'Flash dirty flag should clear after consume');

  // Reboot into a fresh badge instance with savedSectors
  const rebooted = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors });
  rebooted.stepLockstep(1_200_000);
  const rst = rebooted.getFirmwareState();
  assert.equal(rst.clueNum, 0, '0x20001754 stays 0 for normal clues and becomes 0xff on win');
  assert.equal(rst.suspectsMask, 0xa00000, 'Restored suspectsMask');
  assert.equal(rst.attacksMask, 0x400000, 'Restored attacksMask');
  assert.equal(rst.roomsMask, 0x2000, 'Restored roomsMask');
  assert.equal(rst.suspectsCursor, 0x400000, 'Restored suspectsCursor from 0x1cd000');
  assert.equal(rst.attacksCursor, 0x200000, 'Restored attacksCursor from 0x1cd000');
  assert.equal(rst.roomsCursor, 0x0000, 'roomsCursor is 0x0000');
  console.log('  PASS: UP/DOWN/SELECT cursor wrap, 1 ms tap hold, double-tap queue, and flash persistence verified.');
}

console.log('\n=== 4B. Cumulative Kiosk Clue Progress (10 Visits + Reload + Deduction) & Rainbow Ratio Check ===');
{
  const badge = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  badge.stepLockstep(1_200_000);

  // Hidden answer: Suspect #20 (bit 3), Attack #20 (bit 3), Room #9 (bit 6)
  // Visits 1..10 each clear 2 Suspects (0..19), 2 Attacks (0..19), and 1 Room (0..8 for visits 1..9)
  let cumSuspects = 0;
  let cumAttacks = 0;
  let cumRooms = 0;

  function countLitGreenLeds(b) {
    b.resetLedCounters();
    // Sample across 550 ms (> one full 500 ms new-clue blink cycle: 250 ms ON + 250 ms OFF)
    b.stepLockstep(550_000);
    const rep = b.getCumulativeLedReport();
    let greenLeds = 0;
    for (let i = 0; i < 52; i++) {
      if (rep.shares[i * 3 + 1] > 0.001) greenLeds++;
    }
    return { greenLeds, shares: rep.shares };
  }

  for (let v = 1; v <= 10; v++) {
    const sIdx1 = (v - 1) * 2;
    const sIdx2 = (v - 1) * 2 + 1;
    const aIdx1 = (v - 1) * 2;
    const aIdx2 = (v - 1) * 2 + 1;
    cumSuspects = (cumSuspects | (1 << (23 - sIdx1)) | (1 << (23 - sIdx2))) >>> 0;
    cumAttacks = (cumAttacks | (1 << (23 - aIdx1)) | (1 << (23 - aIdx2))) >>> 0;
    if (v <= 9) {
      const rIdx = v - 1;
      cumRooms = (cumRooms | (1 << (15 - rIdx))) >>> 0;
    }

    // Send cumulative OR of all clues so far via the stand-in's sendWhenBlinkEven option
    const msg = buildKioskMessage('e6614103e715222a', v, cumSuspects, cumAttacks, cumRooms);
    badge.sendSerial(msg, { sendWhenBlinkEven: true });
    badge.stepLockstep(300_000);
    const { greenLeds } = countLitGreenLeds(badge);
    const expectedLit = v * 4 + Math.min(v, 9);
    assert.equal(
      greenLeds,
      expectedLit,
      `After visit ${v}/10, lit Green LED count (${greenLeds}) must equal cumulative cleared count (${expectedLit})`,
    );

    // At visit 5, verify that saving flash and reloading into a fresh badge instance preserves all lit Green LEDs
    if (v === 5) {
      const midSectors = badge.consumeDirtyFlashSectors();
      const reloaded = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: midSectors });
      reloaded.stepLockstep(1_200_000);
      const { greenLeds: reloadedGreen } = countLitGreenLeds(reloaded);
      assert.equal(reloadedGreen, expectedLit, `Reload at visit 5 must preserve all ${expectedLit} lit Green LEDs`);
    }
  }

  // After visit 10, 49 of 52 LEDs are lit Green; deduce the unique unlit Suspect, Attack, and Room
  const finalCheck = countLitGreenLeds(badge);
  assert.equal(finalCheck.greenLeds, 49, 'After 10 visits, 49 of 52 LEDs must be lit Green');
  const unlitSuspects = [];
  const unlitAttacks = [];
  const unlitRooms = [];
  for (let i = 0; i < 21; i++) {
    if (finalCheck.shares[i * 3 + 1] <= 0.001) unlitSuspects.push(i);
    if (finalCheck.shares[(21 + i) * 3 + 1] <= 0.001) unlitAttacks.push(i);
  }
  for (let i = 0; i < 10; i++) {
    if (finalCheck.shares[(42 + i) * 3 + 1] <= 0.001) unlitRooms.push(i);
  }
  assert.deepEqual(unlitSuspects, [20], 'Deduction must leave exactly Suspect #20 unlit');
  assert.deepEqual(unlitAttacks, [20], 'Deduction must leave exactly Attack #20 unlit');
  assert.deepEqual(unlitRooms, [9], 'Deduction must leave exactly Room #9 unlit');

  // Accuse the deduced triple by sending the 0xFF win clue
  const winMsg = buildKioskMessage(
    'e6614103e715222a',
    0xff,
    1 << (23 - 20),
    1 << (23 - 20),
    1 << (15 - 9),
  );
  badge.sendSerial(winMsg);
  badge.stepLockstep(300_000);
  const winSt = badge.getFirmwareState();
  assert.equal(winSt.gameWon, 1, 'Accusing the deduced triple must set gameWon=1');
  assert.equal(winSt.displayMode, 1, 'Accusing the deduced triple must enter Mode 1');

  // Verify sharesToRgb preserves R:G:B ratio and keeps scanned LEDs bright
  const testShares = new Float64Array(156);
  testShares[0] = 0.001; // 1x
  testShares[1] = 0.002; // 2x
  testShares[2] = 0.004; // 4x
  testShares[3] = 0.005; // single scanned channel (~50us / 10ms)
  const testRgb = sharesToRgb(testShares);
  assert.ok(testRgb[2] >= 200, `Expected 0.4% max share to have brightness >= 200, got ${testRgb[2]}`);
  assert.ok(Math.abs(testRgb[1] * 2 - testRgb[2]) <= 1, `Expected G = B/2 (${testRgb[1]} vs ${testRgb[2]})`);
  assert.ok(Math.abs(testRgb[0] * 4 - testRgb[2]) <= 2, `Expected R = B/4 (${testRgb[0]} vs ${testRgb[2]})`);
  assert.ok(testRgb[3] >= 215, `Expected 0.5% scanned LED share to stay bright (>=215), got ${testRgb[3]}`);

  console.log('  PASS: Cumulative 10-visit deduction game (49/52 Green LEDs, reload survival, win) & RGB ratio curve verified.');
}

console.log('\n=== 4C. Raw Odd-Blink Quirk vs Stand-In sendWhenBlinkEven Hold ===');
{
  const bRaw = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  const bStandIn = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  bRaw.stepLockstep(1_200_000);
  bStandIn.stepLockstep(1_200_000);

  // Clue 1 clears 5 LEDs (non-overlapping with row-0 cursors): Suspects #1..2 (0x600000), Attacks #1..2 (0x600000), Room #0 (0x8000)
  const clue1 = buildKioskMessage('e6614103e715222a', 1, 0x600000, 0x600000, 0x8000);
  bRaw.sendSerial(clue1);
  bStandIn.sendSerial(clue1, { sendWhenBlinkEven: true });
  bRaw.stepLockstep(100_000);
  bStandIn.stepLockstep(100_000);

  // Both badges are now in the first 250ms OFF phase of the blink (newClueBlinkCounter == 61, odd)
  assert.equal(bRaw.getFirmwareState().newClueBlinkCounter & 1, 1, 'bRaw must be on an odd blink count');
  assert.equal(bStandIn.getFirmwareState().newClueBlinkCounter & 1, 1, 'bStandIn must be on an odd blink count');

  // Clue 2 adds 5 more LEDs (cumulative 10 LEDs: Suspects #1..4, Attacks #1..4, Rooms #0..1)
  const clue2 = buildKioskMessage('e6614103e715222a', 2, 0x780000, 0x780000, 0xc000);
  bRaw.sendSerial(clue2); // Raw path: delivered immediately while blink count is odd
  bStandIn.sendSerial(clue2, { sendWhenBlinkEven: true }); // Stand-in path: waits for even blink count

  // Advance 16 seconds so the 61-tick (15.25s) blink finishes completely on both badges
  bRaw.stepLockstep(16_000_000);
  bStandIn.stepLockstep(16_000_000);
  assert.equal(bRaw.getFirmwareState().newClueBlinkCounter, 0, 'bRaw blink must have finished');
  assert.equal(bStandIn.getFirmwareState().newClueBlinkCounter, 0, 'bStandIn blink must have finished');

  bRaw.resetLedCounters();
  bStandIn.resetLedCounters();
  bRaw.stepLockstep(300_000);
  bStandIn.stepLockstep(300_000);

  const repRaw = bRaw.getCumulativeLedReport();
  const repStandIn = bStandIn.getCumulativeLedReport();
  let greenRaw = 0;
  let greenStandIn = 0;
  for (let i = 0; i < 52; i++) {
    if (repRaw.shares[i * 3 + 1] > 0.001) greenRaw++;
    if (repStandIn.shares[i * 3 + 1] > 0.001) greenStandIn++;
  }
  assert.equal(
    greenRaw,
    5,
    `Raw clue sent on odd blink count must leave Clue 1's 5 LEDs dark (only 5 of 10 lit, got ${greenRaw})`,
  );
  assert.equal(
    greenStandIn,
    10,
    `Stand-in sendWhenBlinkEven path must keep all 10 LEDs from Clues 1 and 2 lit (got ${greenStandIn})`,
  );
  console.log(
    `  PASS: Raw clue on odd blink count leaves earlier LEDs dark (${greenRaw}/10 lit); stand-in sendWhenBlinkEven keeps all lit (${greenStandIn}/10 lit).`,
  );
}

console.log('\n=== 5. Boot-Hold Modes: DOWN Self-Test & SELECT USB Bootloader ===');
{
  // SELECT held at boot -> enters USB bootloader (0x10000ed2)
  const bootSel = createBadge({
    uf2Bytes,
    cycleNanos: 125,
    savedSectors: postFirstBootSectors,
    holdButtonsAtBoot: ['SELECT'],
  });
  bootSel.stepLockstep(500_000);
  assert.equal(bootSel.getFirmwareState().inBootloader, true, 'Holding SELECT at boot must enter USB bootloader');

  // DOWN held at boot -> enters RGB self-test (500ms Red -> 500ms Green -> 500ms Blue)
  const bootDown = createBadge({
    uf2Bytes,
    cycleNanos: 125,
    savedSectors: postFirstBootSectors,
    holdButtonsAtBoot: ['DOWN'],
  });
  bootDown.stepLockstep(100_000); // Let self-test start (Red phase)
  bootDown.resetLedCounters();
  bootDown.stepLockstep(300_000); // 300ms inside Red phase
  const redRep = bootDown.getCumulativeLedReport();
  let redLit = 0;
  for (let i = 0; i < 52; i++) {
    if (redRep.shares[i * 3 + 0] > 0.002 && redRep.shares[i * 3 + 1] === 0 && redRep.shares[i * 3 + 2] === 0) {
      redLit++;
    }
  }
  assert.equal(redLit, 52, `Expected all 52 LEDs Red during self-test phase 1, got ${redLit}`);

  bootDown.stepLockstep(300_000); // Advance into Green phase
  bootDown.resetLedCounters();
  bootDown.stepLockstep(300_000);
  const greenRep = bootDown.getCumulativeLedReport();
  let greenLit = 0;
  for (let i = 0; i < 52; i++) {
    if (greenRep.shares[i * 3 + 1] > 0.002 && greenRep.shares[i * 3 + 0] === 0 && greenRep.shares[i * 3 + 2] === 0) {
      greenLit++;
    }
  }
  assert.equal(greenLit, 52, `Expected all 52 LEDs Green during self-test phase 2, got ${greenLit}`);

  bootDown.stepLockstep(150_000); // Advance into middle of Blue phase (1045ms..1545ms)
  bootDown.resetLedCounters();
  bootDown.stepLockstep(250_000);
  const blueRep = bootDown.getCumulativeLedReport();
  let blueLit = 0;
  for (let i = 0; i < 52; i++) {
    if (blueRep.shares[i * 3 + 2] > 0.002 && blueRep.shares[i * 3 + 0] === 0 && blueRep.shares[i * 3 + 1] === 0) {
      blueLit++;
    }
  }
  assert.equal(blueLit, 52, `Expected all 52 LEDs Blue during self-test phase 3, got ${blueLit}`);
  console.log('  PASS: SELECT boot-hold enters bootloader; DOWN boot-hold cycles all 52 LEDs R -> G -> B.');
}

console.log('\n=== 5B. Core 1 Reset Spinlock Ownership Tracking & Fixed-Size Serial History ===');
{
  const bSpin = createBadge({ uf2Bytes, cycleNanos: 125, savedSectors: postFirstBootSectors });
  bSpin.stepLockstep(500_000);

  // Core 0 takes SPINLOCK5 (0xd0000114, bit 5 = 0x20)
  bSpin.mcu.activeCore = bSpin.core0;
  bSpin.mcu.core = bSpin.core0;
  const lock5Claim = bSpin.mcu.readUint32(0xd0000114) >>> 0;
  assert.equal(lock5Claim, 1 << 5, 'Core 0 must claim SPINLOCK5');

  // Core 1 takes SPINLOCK18 (0xd0000148, bit 18 = 0x40000) and is parked at an infinite branch (0x10000b2e: b 0x10000a62)
  // so the 512-instruction grace loop does not release SPINLOCK18 via code, exercising the fallback mask clear.
  bSpin.mcu.activeCore = bSpin.core1;
  bSpin.mcu.core = bSpin.core1;
  const lock18Claim = bSpin.mcu.readUint32(0xd0000148) >>> 0;
  assert.equal(lock18Claim, 1 << 18, 'Core 1 must claim SPINLOCK18');
  bSpin.core1.PC = 0x10000b2e;

  const ownersBefore = bSpin.getSpinLockOwners();
  assert.equal(ownersBefore.core0SpinLockMask, 1 << 5, 'core0SpinLockMask must track bit 5');
  assert.equal(ownersBefore.core1SpinLockMask, 1 << 18, 'core1SpinLockMask must track bit 18');

  // Reset Core 1 via PSM_FRCE_OFF (0x40010004 bit 16)
  bSpin.mcu.activeCore = bSpin.core0;
  bSpin.mcu.core = bSpin.core0;
  bSpin.mcu.writeUint32(0x40010004, 1 << 16);

  const ownersAfter = bSpin.getSpinLockOwners();
  assert.equal(ownersAfter.core1SpinLockMask, 0, 'Core 1 spinlock mask must be cleared on Core 1 reset');
  assert.equal(ownersAfter.core0SpinLockMask, 1 << 5, 'Core 0 spinlock mask must NOT be cleared on Core 1 reset');
  assert.equal(ownersAfter.rawSpinLock & (1 << 18), 0, 'SPINLOCK18 held by Core 1 must be released');
  assert.equal(ownersAfter.rawSpinLock & (1 << 5), 1 << 5, 'SPINLOCK5 held by Core 0 must remain held');

  // Verify via SIO hardware registers that SPINLOCK5 is still held (reading returns 0) while SPINLOCK18 is free
  assert.equal(bSpin.mcu.readUint32(0xd0000114) >>> 0, 0, 'SPINLOCK5 must still be locked');
  assert.equal(bSpin.mcu.readUint32(0xd0000148) >>> 0, 1 << 18, 'SPINLOCK18 must be free to claim again');
  // Clean up both spinlocks
  bSpin.mcu.writeUint32(0xd0000114, 1);
  bSpin.mcu.writeUint32(0xd0000148, 1);
  bSpin.mcu.writeUint32(0x40010004, 0);

  // Verify fixed-size serial history cap in badge2022.mjs
  for (let i = 0; i < 260; i++) {
    bSpin.sendSerial('kiosk:hi\n\r');
    bSpin.stepLockstep(40_000);
  }
  assert.ok(bSpin.serialLines.length <= 200, `serialLines must be capped at 200 (got ${bSpin.serialLines.length})`);
  assert.ok(
    bSpin.getSerialOutput().length <= 8192,
    `serialHistory must be capped at 8192 chars (got ${bSpin.getSerialOutput().length})`,
  );
  console.log('  PASS: Core 1 reset clears only Core 1 spinlocks while preserving Core 0 spinlocks; serial history capped.');
}

console.log('\n=== 6. 8 MHz vs 125 MHz Clock Comparison Across All Firmware States & RPTimer Guard Check ===');
{
  function compareState(name, setupFn, windowUs = 500_000, extraOptions = {}) {
    const b8 = createBadge({ uf2Bytes, cycleNanos: 125, stdioMode: 'hook', ...extraOptions });
    const b125 = createBadge({ uf2Bytes, cycleNanos: 8, stdioMode: 'hook', ...extraOptions });
    setupFn(b8);
    setupFn(b125);
    b8.resetLedCounters();
    b125.resetLedCounters();
    const t8 = performance.now();
    b8.stepLockstep(windowUs);
    const wall8Ms = Math.round(performance.now() - t8);
    const t125 = performance.now();
    b125.stepLockstep(windowUs);
    const wall125Ms = Math.round(performance.now() - t125);

    const rep8 = b8.getCumulativeLedReport();
    const rep125 = b125.getCumulativeLedReport();
    const sum8 = summarizeLedShares(rep8);
    const sum125 = summarizeLedShares(rep125);
    const maxDiffPct = maxChannelShareDiffPct(rep8, rep125);

    console.log(
      `  [${name}] 8MHz (${wall8Ms}ms): sw/s=${sum8.gpioSwitchesPerSec}, active=${sum8.activeChannels}, total=${sum8.totalSharePct}% | ` +
        `125MHz (${wall125Ms}ms): sw/s=${sum125.gpioSwitchesPerSec}, active=${sum125.activeChannels}, total=${sum125.totalSharePct}% | ` +
        `maxChDiff=${maxDiffPct}%`,
    );
    assert.equal(
      sum8.activeChannels,
      sum125.activeChannels,
      `Active LED channel count must match in ${name} (8MHz=${sum8.activeChannels}, 125MHz=${sum125.activeChannels})`,
    );
    return { b8, b125, sum8, sum125, maxDiffPct };
  }

  // State 0A: First Boot on Erased Flash (0xFF -> save_clues_flash(0) + Red Self-Test)
  compareState(
    'First Boot (Erased Flash -> Red Self-Test)',
    (b) => {
      b.stepLockstep(120_000);
      assert.equal(b.getFirmwareState().inSelfTest, true);
    },
    250_000,
  );

  // State 0B: Boot-hold DOWN RGB Self-Test (Green phase)
  compareState(
    'Boot-Hold DOWN Self-Test (Green phase)',
    (b) => {
      b.stepLockstep(650_000);
      assert.equal(b.getFirmwareState().inSelfTest, true);
    },
    250_000,
    { savedSectors: postFirstBootSectors, holdButtonsAtBoot: ['DOWN'] },
  );

  // State 0C: Boot-hold SELECT USB Bootloader
  compareState(
    'Boot-Hold SELECT USB Bootloader',
    (b) => {
      b.stepLockstep(200_000);
      assert.equal(b.getFirmwareState().inBootloader, true);
    },
    200_000,
    { savedSectors: postFirstBootSectors, holdButtonsAtBoot: ['SELECT'] },
  );

  // State A: Mode 0 idle at boot (2 Blue cursors: Suspects blinking 250ms, Attacks solid)
  compareState(
    'Mode 0 Idle (2 Blue cursors, 250ms blink)',
    (b) => {
      b.stepLockstep(1_200_000);
    },
    500_000,
    { savedSectors: postFirstBootSectors },
  );

  // State B: Mode 0 new-clue blink (newly received clue bits blink Green every 250ms for 61 ticks)
  compareState(
    'Mode 0 New-Clue Blink (Green + Blue cursors)',
    (b) => {
      b.stepLockstep(1_200_000);
      b.sendSerial(buildKioskMessage('e6614103e715222a', 1, 0xe00000, 0xc00000, 0xc000));
      b.stepLockstep(100_000);
    },
    500_000,
    { savedSectors: postFirstBootSectors },
  );

  // State C: Mode 1 Win Solution Blink (61-tick countdown blinking Red winning triple + Green clues)
  let wonSavedSectors = null;
  compareState(
    'Mode 1 Win Solution Blink',
    (b) => {
      b.stepLockstep(1_200_000);
      b.sendSerial(buildKioskMessage('e6614103e715222a', 1, 0x7ffff8, 0x7ffff8, 0x7fc0));
      b.stepLockstep(100_000);
      b.sendSerial(buildKioskMessage('e6614103e715222a', 0xff, 0x800000, 0x800000, 0x8000));
      b.stepLockstep(200_000);
      assert.equal(b.getFirmwareState().displayMode, 1);
      if (!wonSavedSectors) wonSavedSectors = b.consumeDirtyFlashSectors();
    },
    500_000,
    { savedSectors: postFirstBootSectors },
  );

  // State D: Mode 2 Fast Rainbow Wave (reached automatically after 11-tick = 2.75s boot win blink + 250ms final frame)
  compareState(
    'Mode 2 Fast Rainbow Wave',
    (b) => {
      b.stepLockstep(3_600_000); // Boots in Mode 1 with countdown=11 (2.75s) -> transitions to Mode 2
      assert.equal(b.getFirmwareState().displayMode, 2);
    },
    400_000,
    { savedSectors: wonSavedSectors },
  );

  // State E: Mode 3 Slow Rainbow Wave (reached via DOWN from Mode 2)
  compareState(
    'Mode 3 Slow Rainbow Wave',
    (b) => {
      b.stepLockstep(3_600_000);
      pressButton(b, 'DOWN'); // Mode 2 -> Mode 3
      assert.equal(b.getFirmwareState().displayMode, 3);
    },
    400_000,
    { savedSectors: wonSavedSectors },
  );

  // State F: Mode 4 Random Sparkle & Mode 5 Random Fill at both 8 MHz and 125 MHz
  {
    const b8 = createBadge({
      uf2Bytes,
      cycleNanos: 125,
      stdioMode: 'hook',
      savedSectors: wonSavedSectors,
    });
    const b125 = createBadge({
      uf2Bytes,
      cycleNanos: 8,
      stdioMode: 'hook',
      savedSectors: wonSavedSectors,
    });
    b8.stepLockstep(3_600_000);
    b125.stepLockstep(3_600_000);
    pressButton(b8, 'DOWN'); // -> 3
    pressButton(b8, 'DOWN'); // -> 4
    pressButton(b125, 'DOWN'); // -> 3
    pressButton(b125, 'DOWN'); // -> 4
    assert.equal(b8.getFirmwareState().displayMode, 4);
    assert.equal(b125.getFirmwareState().displayMode, 4);

    b8.resetLedCounters();
    b125.resetLedCounters();
    const t4_8 = performance.now();
    b8.stepLockstep(300_000);
    const w4_8 = Math.round(performance.now() - t4_8);
    const t4_125 = performance.now();
    b125.stepLockstep(300_000);
    const w4_125 = Math.round(performance.now() - t4_125);
    const sum4_8 = summarizeLedShares(b8.getCumulativeLedReport());
    const sum4_125 = summarizeLedShares(b125.getCumulativeLedReport());
    assert.ok(sum4_8.activeChannels > 0 && sum4_125.activeChannels > 0, 'Mode 4 sparkle must light channels at 8MHz and 125MHz');
    console.log(
      `  [Mode 4 Random Sparkle] 8MHz (${w4_8}ms): sw/s=${sum4_8.gpioSwitchesPerSec}, active=${sum4_8.activeChannels}, total=${sum4_8.totalSharePct}% | ` +
        `125MHz (${w4_125}ms): sw/s=${sum4_125.gpioSwitchesPerSec}, active=${sum4_125.activeChannels}, total=${sum4_125.totalSharePct}%`,
    );

    pressButton(b8, 'DOWN'); // -> 5
    pressButton(b125, 'DOWN'); // -> 5
    assert.equal(b8.getFirmwareState().displayMode, 5);
    assert.equal(b125.getFirmwareState().displayMode, 5);
    b8.resetLedCounters();
    b125.resetLedCounters();
    const t5_8 = performance.now();
    b8.stepLockstep(300_000);
    const w5_8 = Math.round(performance.now() - t5_8);
    const t5_125 = performance.now();
    b125.stepLockstep(300_000);
    const w5_125 = Math.round(performance.now() - t5_125);
    const sum5_8 = summarizeLedShares(b8.getCumulativeLedReport());
    const sum5_125 = summarizeLedShares(b125.getCumulativeLedReport());
    assert.ok(sum5_8.activeChannels > 0 && sum5_125.activeChannels > 0, 'Mode 5 random fill must light channels at 8MHz and 125MHz');
    console.log(
      `  [Mode 5 Random Fill]    8MHz (${w5_8}ms): sw/s=${sum5_8.gpioSwitchesPerSec}, active=${sum5_8.activeChannels}, total=${sum5_8.totalSharePct}% | ` +
        `125MHz (${w5_125}ms): sw/s=${sum5_125.gpioSwitchesPerSec}, active=${sum5_125.activeChannels}, total=${sum5_125.totalSharePct}%`,
    );

    pressButton(b8, 'DOWN'); // -> wraps 5 -> 2
    assert.equal(b8.getFirmwareState().displayMode, 2);
    pressButton(b8, 'UP'); // -> wraps 2 -> 5
    assert.equal(b8.getFirmwareState().displayMode, 5);
  }

  // Test 125 MHz reference with RPTimer past-target guard turned ON vs OFF (Fix 9)
  {
    const bGuardOn = createBadge({
      uf2Bytes,
      cycleNanos: 8,
      stdioMode: 'hook',
      savedSectors: postFirstBootSectors,
      disableTimerPastGuard: false,
    });
    const bGuardOff = createBadge({
      uf2Bytes,
      cycleNanos: 8,
      stdioMode: 'hook',
      savedSectors: postFirstBootSectors,
      disableTimerPastGuard: true,
    });
    bGuardOn.stepLockstep(1_200_000);
    bGuardOff.stepLockstep(1_200_000);
    bGuardOn.sendSerial(buildKioskMessage('e6614103e715222a', 1, 0xe00000, 0xc00000, 0xc000));
    bGuardOff.sendSerial(buildKioskMessage('e6614103e715222a', 1, 0xe00000, 0xc00000, 0xc000));
    bGuardOn.resetLedCounters();
    bGuardOff.resetLedCounters();
    bGuardOn.stepLockstep(500_000);
    bGuardOff.stepLockstep(500_000);

    const repOn = bGuardOn.getCumulativeLedReport();
    const repOff = bGuardOff.getCumulativeLedReport();
    const diff125Guard = maxChannelShareDiffPct(repOn, repOff);
    assert.deepEqual(
      bGuardOn.getFirmwareState(),
      bGuardOff.getFirmwareState(),
      'At 125 MHz, firmware state must be identical with RPTimer past-target guard on vs off',
    );
    assert.equal(
      diff125Guard,
      0,
      'At 125 MHz, LED duty shares must be 100% identical with RPTimer past-target guard on vs off',
    );
    console.log(
      `  [125 MHz RPTimer Past-Target Guard On vs Off] state identical, maxChDiff=${diff125Guard}% (0.0000% difference at 125 MHz).`,
    );
  }
}

console.log('\nALL 2022 CORE VERIFICATION TESTS PASSED.');
