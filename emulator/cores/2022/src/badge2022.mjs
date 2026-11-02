/**
 * SAINTCON 2022 Clue-style Notepad Badge RP2040 Dual-Core Emulator Wrapper
 *
 * Uses wokwi/rp2040js (MIT License, Copyright (c) 2021 Uri Shaked) with:
 * - Lockstep dual Cortex-M0+ cores (Core 0 + Core 1), inter-core mailboxes, and PSM reset
 * - Configurable CPU clock (default 8 MHz, cycleNanos = 125) with true 1 MHz hardware timer
 * - Emulator-level stdio hooks at getchar_timeout_us (0x10006228) and
 *   stdio_usb_out_chars (0x10006428) so the UF2 binary runs 100% unpatched without USB overhead
 * - Real SIO GPIO pin-time integration across all 52 RGB charlieplexed LEDs (156 channels)
 * - SSI + IO_QSPI flash erase/program tracking for persistent sectors 0x1cc000 and 0x1cd000
 */

import { RP2040 } from '../upstream/rp2040js/src/rp2040.js';
import { USBCDC } from '../upstream/rp2040js/src/usb/cdc.js';
import { CortexM0Core } from '../upstream/rp2040js/src/cortex-m0-core.js';
import { bootromB1 } from '../upstream/rp2040js/demo/bootrom.js';

export const DEFAULT_UNIQUE_ID = new Uint8Array([0xe6, 0x61, 0x41, 0x03, 0xe7, 0x15, 0x22, 0x2a]);

export const STDIO_HOOKS = Object.freeze({
  GETCHAR_TIMEOUT_US: 0x10006228,
  STDIO_USB_OUT_CHARS: 0x10006428,
  BOOTLOADER_ENTRY: 0x10000ed2,
});

export const FLASH_LED_TABLES = Object.freeze([
  {
    category: 'suspects',
    categoryIndex: 0,
    flashAddr: 0x1000b60c,
    ramAddr: 0x20000888,
    count: 21,
    cursorRamAddr: 0x20000884,
    clueMaskRamAddr: 0x20001c14,
    winMaskRamAddr: 0x20001ec8,
    msbBit: 23,
  },
  {
    category: 'attacks',
    categoryIndex: 1,
    flashAddr: 0x1000b314,
    ramAddr: 0x20000590,
    count: 21,
    cursorRamAddr: 0x20000880,
    clueMaskRamAddr: 0x20001c0c,
    winMaskRamAddr: 0x20001ec0,
    msbBit: 23,
  },
  {
    category: 'rooms',
    categoryIndex: 2,
    flashAddr: 0x1000b514,
    ramAddr: 0x20000790,
    count: 10,
    cursorRamAddr: 0x20001d48,
    clueMaskRamAddr: 0x20001c10,
    winMaskRamAddr: 0x20001ec4,
    msbBit: 15,
  },
]);

export const BUTTON_PINS = Object.freeze({
  UP: 15,
  DOWN: 16,
  SELECT: 17,
});

const textDecoder = new TextDecoder();

/**
 * Single fixed strictly-increasing monotonic curve mapping a pin-time duty share
 * in [0, 1] to an 8-bit brightness value [0..255].
 *
 * A standard scanned LED in the firmware is driven for 50 us once per ~10 ms
 * scan (~0.5% duty share -> ~219/255), while rainbow modes modulate the pulse
 * width across 0..255 us per scan, preserving smooth color gradients.
 */
export function shareToBrightness(share) {
  if (!(share > 0)) return 0;
  if (share >= 1) return 255;
  const v = Math.pow(share / (share + 0.002), 0.45);
  return Math.min(255, Math.max(1, Math.round(v * 255)));
}

/**
 * Convert 156 channel duty shares (52 LEDs * R,G,B) into 8-bit RGB values while
 * preserving the R:G:B ratio of each LED: overall brightness comes from applying
 * shareToBrightness() to the LED's strongest channel, then scaling R, G, and B
 * by their share relative to that strongest channel.
 */
export function sharesToRgb(shares, rgbOut = new Uint8Array(156)) {
  for (let i = 0; i < 52; i++) {
    const base = i * 3;
    const rShare = shares[base];
    const gShare = shares[base + 1];
    const bShare = shares[base + 2];
    const maxShare = Math.max(rShare, gShare, bShare);
    if (maxShare > 0) {
      const peak = shareToBrightness(maxShare);
      rgbOut[base] = rShare > 0 ? Math.min(255, Math.round((peak * rShare) / maxShare)) : 0;
      rgbOut[base + 1] = gShare > 0 ? Math.min(255, Math.round((peak * gShare) / maxShare)) : 0;
      rgbOut[base + 2] = bShare > 0 ? Math.min(255, Math.round((peak * bShare) / maxShare)) : 0;
    } else {
      rgbOut[base] = 0;
      rgbOut[base + 1] = 0;
      rgbOut[base + 2] = 0;
    }
  }
  return rgbOut;
}

export function escapeSerialBytes(rawStr) {
  let out = '';
  for (let i = 0; i < rawStr.length; i++) {
    const c = rawStr.charCodeAt(i) & 0xff;
    if (c >= 0x20 && c <= 0x7e) {
      out += rawStr[i];
    } else {
      out += '\\x' + c.toString(16).padStart(2, '0');
    }
  }
  return out;
}

export function loadUF2(mcu, uf2Bytes) {
  const view = new DataView(uf2Bytes.buffer, uf2Bytes.byteOffset, uf2Bytes.byteLength);
  for (let off = 0; off + 512 <= uf2Bytes.byteLength; off += 512) {
    if (view.getUint32(off, true) !== 0x0a324655) continue;
    const addr = view.getUint32(off + 12, true);
    const len = view.getUint32(off + 16, true);
    if (addr >= 0x10000000 && addr < 0x11000000) {
      mcu.flash.set(uf2Bytes.subarray(off + 32, off + 32 + len), addr - 0x10000000);
    }
  }
}

export function extractLedTableFromFlash(flashBytes) {
  const leds = [];
  for (const tbl of FLASH_LED_TABLES) {
    const baseOff = tbl.flashAddr - 0x10000000;
    for (let i = 0; i < tbl.count; i++) {
      const off = baseOff + i * 24;
      const bCathode = flashBytes[off + 0];
      const bAnode = flashBytes[off + 1];
      const rCathode = flashBytes[off + 8];
      const rAnode = flashBytes[off + 9];
      const gCathode = flashBytes[off + 16];
      const gAnode = flashBytes[off + 17];
      leds.push({
        ledIndex: leds.length,
        category: tbl.category,
        categoryIndex: tbl.categoryIndex,
        rowIndex: i,
        maskBit: tbl.msbBit - i,
        r: { cathode: rCathode, anode: rAnode },
        g: { cathode: gCathode, anode: gAnode },
        b: { cathode: bCathode, anode: bAnode },
      });
    }
  }
  return leds;
}

export function buildKioskMessage(serialHex, clueNum, suspectsMask24, attacksMask24, roomsMask16) {
  const payload34 =
    serialHex.toUpperCase() +
    (clueNum & 0xff).toString(16).toUpperCase().padStart(2, '0') +
    (suspectsMask24 & 0xffffff).toString(16).toUpperCase().padStart(6, '0') +
    (attacksMask24 & 0xffffff).toString(16).toUpperCase().padStart(6, '0') +
    (roomsMask16 & 0xffff).toString(16).toUpperCase().padStart(4, '0');
  let sum = 0;
  for (let i = 0; i < 17; i++) {
    sum = (sum + parseInt(payload34.slice(i * 2, i * 2 + 2), 16)) & 0xff;
  }
  const chk = (-sum) & 0xff;
  const shift = chk % 34;
  const rotated =
    shift === 0 ? payload34 : payload34.slice(34 - shift) + payload34.slice(0, 34 - shift);
  return `kiosk:${rotated}${chk.toString(16).toUpperCase().padStart(2, '0')}\n\r`;
}

export function parseBadgeMessage(line) {
  if (typeof line !== 'string') return { valid: false, raw: '' };
  const m = line.match(/badge:([0-9A-Fa-f]{34})([0-9A-Fa-f]{2})/);
  if (!m) {
    return { valid: false, raw: line.trim() };
  }
  const rotated = m[1];
  const chk = parseInt(m[2], 16);
  const shift = chk % 34;
  const unrotated = shift === 0 ? rotated : rotated.slice(shift) + rotated.slice(0, shift);
  let sum = 0;
  for (let i = 0; i < 34; i++) {
    sum = (sum + unrotated.charCodeAt(i)) & 0xff;
  }
  if (((sum + chk) & 0xff) !== 0) {
    return { valid: false, raw: m[0], checksumMismatch: true };
  }
  return {
    valid: true,
    raw: m[0],
    serial: unrotated.slice(0, 16).toLowerCase(),
    statusByte: parseInt(unrotated.slice(16, 18), 16),
    suspectsCursor: parseInt(unrotated.slice(18, 24), 16),
    attacksCursor: parseInt(unrotated.slice(24, 30), 16),
    roomsCursor: parseInt(unrotated.slice(30, 34), 16),
  };
}

export function createBadge(options = {}) {
  const mcu = new RP2040();
  const cycleNanos = options.cycleNanos ?? 125; // Default 8 MHz CPU clock (125 ns/cycle)
  const stdioMode = options.stdioMode ?? 'hook'; // 'hook' (default) or 'usb'
  const logs = [];
  const maxLogs = options.maxLogs ?? 200;

  // Cap rp2040js console logging and intercept CortexM0Core "SEV" instruction.
  mcu.logger = {
    debug() {},
    info(name, msg) {
      if (name === 'CortexM0Core' && msg === 'SEV') {
        mcu.core0.eventRegistered = true;
        mcu.core0.waiting = false;
        mcu.core1.eventRegistered = true;
        mcu.core1.waiting = false;
        return;
      }
      if (logs.length < maxLogs) logs.push({ level: 'info', name, msg });
    },
    warn(name, msg) {
      if (logs.length < maxLogs) logs.push({ level: 'warn', name, msg });
    },
    error(name, msg) {
      if (logs.length < maxLogs) logs.push({ level: 'error', name, msg });
    },
  };

  let watchdogTriggers = 0;
  mcu.onWatchdogTrigger = () => {
    watchdogTriggers++;
    mcu.watchdog.enable = false;
  };

  // Sub-microsecond scheduling and SDK race guard for RP2040 hardware alarms:
  // Real RP2040 TIMER_ALARM0..3 registers also fire only on an exact 32-bit match with TIMERAWL.
  // In Pico SDK 1.3.1 hardware_alarm_set_target (0x10002f7c..0x10003040), the SDK checks
  // `if (target <= now) return true;` at 0x10002f8e, writes TIMER_ALARM at 0x10002fc8 (18 instructions
  // later), and re-checks TIMER_ARMED / TIMER_INTS / time_reached at 0x10002fd2..0x10003016.
  // At the reduced 8 MHz CPU clock (125 ns/cycle), those instructions take several microseconds,
  // exposing a race on short 10..25 us rainbow sleeps where TIMERAWL passes `target` before the
  // alarm is armed and before the late-target check completes. Firing when signedDelta <= 0
  // (unless options.disableTimerPastGuard is set for 125 MHz reference testing) prevents a 71.5-minute
  // wrap at 8 MHz.
  const disableTimerPastGuard = Boolean(options.disableTimerPastGuard);
  const timer = mcu.peripherals[0x40054];
  const origTimerWrite = timer.writeUint32.bind(timer);
  timer.writeUint32 = (offset, value) => {
    if (offset >= 0x10 && offset <= 0x1c) {
      const alarmIndex = (offset - 0x10) >>> 2;
      const alarm = timer.alarms[alarmIndex];
      const target = value >>> 0;
      const nowMicros = Math.floor(mcu.clock.nanos / 1000) >>> 0;
      const signedDelta = (target - nowMicros) | 0;
      if (!disableTimerPastGuard && signedDelta <= 0) {
        alarm.targetMicros = target;
        timer.fireAlarm(alarmIndex);
        return;
      }
      const deltaMicros = (target - nowMicros) >>> 0;
      alarm.armed = true;
      alarm.targetMicros = target;
      const subMicrosNanos = mcu.clock.nanos % 1000;
      const deltaNanos = Math.max(0, deltaMicros * 1000 - subMicrosNanos);
      alarm.clockAlarm.schedule(deltaNanos);
      return;
    }
    origTimerWrite(offset, value);
  };

  // Load bootrom and UF2 image. Flash outside the UF2 stays erased (0xFF) as on a fresh chip.
  mcu.loadBootrom(bootromB1);
  if (!options.uf2Bytes) {
    throw new Error('options.uf2Bytes is required');
  }
  mcu.flash.fill(0xff);
  loadUF2(mcu, options.uf2Bytes);
  if (options.savedSectors) {
    if (options.savedSectors.sector1cc && options.savedSectors.sector1cc.byteLength === 4096) {
      mcu.flash.set(options.savedSectors.sector1cc, 0x1cc000);
    }
    if (options.savedSectors.sector1cd && options.savedSectors.sector1cd.byteLength === 4096) {
      mcu.flash.set(options.savedSectors.sector1cd, 0x1cd000);
    }
  }

  // Extract the 52 RGB LED pin definitions from flash tables.
  const ledTable = extractLedTableFromFlash(mcu.flash);
  const pairToChannel = new Int16Array(15 * 15).fill(-1);
  for (let i = 0; i < ledTable.length; i++) {
    const entry = ledTable[i];
    pairToChannel[entry.r.cathode * 15 + entry.r.anode] = i * 3 + 0;
    pairToChannel[entry.g.cathode * 15 + entry.g.anode] = i * 3 + 1;
    pairToChannel[entry.b.cathode * 15 + entry.b.anode] = i * 3 + 2;
  }

  // Pull-ups on GP15 (UP), GP16 (DOWN), GP17 (SELECT): active LOW.
  for (const pin of [15, 16, 17]) {
    mcu.gpio[pin].setInputValue(true);
  }
  if (options.holdButtonsAtBoot) {
    for (const btn of options.holdButtonsAtBoot) {
      const pin = typeof btn === 'number' ? btn : BUTTON_PINS[btn];
      if (pin !== undefined) mcu.gpio[pin].setInputValue(false);
    }
  }

  // Dual-core setup: Core 0 and Core 1 on shared bus.
  const core0 = mcu.core;
  const core1 = new CortexM0Core(mcu);
  mcu.core0 = core0;
  mcu.core1 = core1;
  mcu.activeCore = core0;

  const resetCore1ToBootrom = () => {
    core1.VTOR = 0;
    core1.SP = mcu.readUint32(0);
    core1.PC = mcu.readUint32(4) & ~1;
    core1.waiting = false;
    core1.eventRegistered = false;
    core1.pendingInterrupts = 0;
    core1.enabledInterrupts = 0;
  };
  resetCore1ToBootrom();

  // Route IRQ 15 -> Core 0, IRQ 16 -> Core 1, other peripheral IRQs -> both cores.
  mcu.setInterrupt = (irq, value) => {
    if (irq === 15) {
      core0.setInterrupt(15, value);
    } else if (irq === 16) {
      core1.setInterrupt(16, value);
    } else {
      core0.setInterrupt(irq, value);
      core1.setInterrupt(irq, value);
    }
  };

  // Real SIO GPIO pin-time integration for all 156 LED channels (52 LEDs * 3 colors).
  const cumulativeChannelNanos = new Float64Array(156);
  let cumulativeWindowNanos = 0;
  let gpioSwitchCount = 0;

  // Rolling 50 ms window (5 sub-buckets of 10 ms) for smooth ~30 fps UI updates.
  const NUM_SUB_BUCKETS = 5;
  const SUB_BUCKET_NANOS = 10_000_000; // 10 ms
  const bucketChannelNanos = Array.from({ length: NUM_SUB_BUCKETS }, () => new Float64Array(156));
  const bucketWindowNanos = new Float64Array(NUM_SUB_BUCKETS);
  let curBucketIdx = 0;

  let lastGpioNanos = 0;
  let activeLowMask = 0;
  let activeHighMask = 0;
  let gp24ToggleCount = 0;
  let gp25ToggleCount = 0;
  let lastGp24High = false;
  let lastGp25High = false;

  const addNanosToActiveChannels = (dt) => {
    if (dt <= 0) return;
    cumulativeWindowNanos += dt;
    let rem = dt;
    while (rem > 0) {
      const space = SUB_BUCKET_NANOS - bucketWindowNanos[curBucketIdx];
      const step = rem < space ? rem : space;
      bucketWindowNanos[curBucketIdx] += step;
      if (activeLowMask !== 0 && activeHighMask !== 0) {
        const bArr = bucketChannelNanos[curBucketIdx];
        if (
          (activeLowMask & (activeLowMask - 1)) === 0 &&
          (activeHighMask & (activeHighMask - 1)) === 0
        ) {
          const c = 31 - Math.clz32(activeLowMask);
          const a = 31 - Math.clz32(activeHighMask);
          const ch = pairToChannel[c * 15 + a];
          if (ch >= 0) {
            cumulativeChannelNanos[ch] += step;
            bArr[ch] += step;
          }
        } else {
          let low = activeLowMask;
          while (low) {
            const cBit = low & -low;
            const c = 31 - Math.clz32(cBit);
            let high = activeHighMask;
            while (high) {
              const aBit = high & -high;
              const a = 31 - Math.clz32(aBit);
              const ch = pairToChannel[c * 15 + a];
              if (ch >= 0) {
                cumulativeChannelNanos[ch] += step;
                bArr[ch] += step;
              }
              high ^= aBit;
            }
            low ^= cBit;
          }
        }
      }
      rem -= step;
      if (bucketWindowNanos[curBucketIdx] >= SUB_BUCKET_NANOS) {
        curBucketIdx = (curBucketIdx + 1) % NUM_SUB_BUCKETS;
        bucketWindowNanos[curBucketIdx] = 0;
        bucketChannelNanos[curBucketIdx].fill(0);
      }
    }
  };

  const flushGpioTime = () => {
    const now = mcu.clock.nanos;
    const dt = now - lastGpioNanos;
    if (dt > 0) {
      addNanosToActiveChannels(dt);
      lastGpioNanos = now;
    }
  };

  // Per-core SIO: CPUID (0x00), inter-core FIFOs (0x50, 0x54, 0x58), and per-core Hardware Divider (0x60..0x78).
  const fifo0to1 = [];
  const fifo1to0 = [];
  let fifoSt0 = 0;
  let fifoSt1 = 0;
  const divState = [
    { dividend: 0, divisor: 1, quotient: 0, remainder: 0, csr: 0 },
    { dividend: 0, divisor: 1, quotient: 0, remainder: 0, csr: 0 },
  ];
  const updateCoreDivider = (d, signed, core) => {
    if (d.divisor === 0) {
      d.quotient = d.dividend > 0 ? -1 : 1;
      d.remainder = d.dividend;
    } else if (signed) {
      d.quotient = ((d.dividend | 0) / (d.divisor | 0)) | 0;
      d.remainder = ((d.dividend | 0) % (d.divisor | 0)) | 0;
    } else {
      d.quotient = ((d.dividend >>> 0) / (d.divisor >>> 0)) >>> 0;
      d.remainder = ((d.dividend >>> 0) % (d.divisor >>> 0)) >>> 0;
    }
    d.csr = 3;
    core.cycles += 8;
  };
  const origSioRead = mcu.sio.readUint32.bind(mcu.sio);
  const origSioWrite = mcu.sio.writeUint32.bind(mcu.sio);

  const updateFifoIrqs = () => {
    core0.setInterrupt(15, fifo1to0.length > 0);
    core1.setInterrupt(16, fifo0to1.length > 0);
  };

  let core0SpinLockMask = 0;
  let core1SpinLockMask = 0;

  mcu.sio.readUint32 = (offset) => {
    const isCore1 = mcu.activeCore === core1;
    if (offset === 0x00) return isCore1 ? 1 : 0;
    if (offset === 0x04) {
      // On real RP2040 silicon, reading SIO_GPIO_IN on an output-enabled pin
      // reflects the driven pad level alongside external inputs on input pins.
      const inVal = origSioRead(0x04);
      const oe = mcu.sio.gpioOutputEnable;
      return ((inVal & ~oe) | (mcu.sio.gpioValue & oe)) >>> 0;
    }
    if (offset === 0x50) {
      const rx = isCore1 ? fifo0to1 : fifo1to0;
      const tx = isCore1 ? fifo1to0 : fifo0to1;
      const sticky = isCore1 ? fifoSt1 : fifoSt0;
      return sticky | (tx.length < 8 ? 2 : 0) | (rx.length > 0 ? 1 : 0);
    }
    if (offset === 0x58) {
      const rx = isCore1 ? fifo0to1 : fifo1to0;
      if (rx.length === 0) {
        if (isCore1) fifoSt1 |= 8;
        else fifoSt0 |= 8;
        return 0;
      }
      const val = rx.shift() >>> 0;
      updateFifoIrqs();
      return val;
    }
    if (offset >= 0x60 && offset <= 0x78) {
      const d = divState[isCore1 ? 1 : 0];
      if (offset === 0x60 || offset === 0x68) return d.dividend >>> 0;
      if (offset === 0x64 || offset === 0x6c) return d.divisor >>> 0;
      if (offset === 0x70) {
        d.csr &= ~2;
        return d.quotient >>> 0;
      }
      if (offset === 0x74) return d.remainder >>> 0;
      if (offset === 0x78) return d.csr >>> 0;
    }
    if (offset >= 0x100 && offset <= 0x17c) {
      const res = origSioRead(offset) >>> 0;
      if (res !== 0) {
        if (isCore1) {
          core1SpinLockMask = (core1SpinLockMask | res) >>> 0;
          core0SpinLockMask = (core0SpinLockMask & ~res) >>> 0;
        } else {
          core0SpinLockMask = (core0SpinLockMask | res) >>> 0;
          core1SpinLockMask = (core1SpinLockMask & ~res) >>> 0;
        }
      }
      return res;
    }
    return origSioRead(offset);
  };

  mcu.sio.writeUint32 = (offset, value) => {
    const isCore1 = mcu.activeCore === core1;
    if (offset === 0x50) {
      if (isCore1) fifoSt1 = 0;
      else fifoSt0 = 0;
      updateFifoIrqs();
      return;
    }
    if (offset === 0x54) {
      const tx = isCore1 ? fifo1to0 : fifo0to1;
      const otherCore = isCore1 ? core0 : core1;
      if (tx.length < 8) {
        tx.push(value >>> 0);
      } else if (isCore1) {
        fifoSt1 |= 4;
      } else {
        fifoSt0 |= 4;
      }
      updateFifoIrqs();
      otherCore.eventRegistered = true;
      otherCore.waiting = false;
      return;
    }
    if (offset >= 0x60 && offset <= 0x74) {
      const d = divState[isCore1 ? 1 : 0];
      const core = isCore1 ? core1 : core0;
      if (offset === 0x60) {
        d.dividend = value;
        updateCoreDivider(d, false, core);
      } else if (offset === 0x68) {
        d.dividend = value;
        updateCoreDivider(d, true, core);
      } else if (offset === 0x64) {
        d.divisor = value;
        updateCoreDivider(d, false, core);
      } else if (offset === 0x6c) {
        d.divisor = value;
        updateCoreDivider(d, true, core);
      } else if (offset === 0x70) {
        d.quotient = value;
        d.csr = 3;
      } else if (offset === 0x74) {
        d.remainder = value;
        d.csr = 3;
      }
      return;
    }
    if (offset >= 0x100 && offset <= 0x17c) {
      const lockMask = (1 << ((offset - 0x100) >>> 2)) >>> 0;
      core0SpinLockMask = (core0SpinLockMask & ~lockMask) >>> 0;
      core1SpinLockMask = (core1SpinLockMask & ~lockMask) >>> 0;
      origSioWrite(offset, value);
      return;
    }

    const isGpioReg = offset >= 0x10 && offset <= 0x2c;
    const prevOut = mcu.sio.gpioValue;
    const prevOe = mcu.sio.gpioOutputEnable;

    if (isGpioReg) {
      flushGpioTime();
    }

    origSioWrite(offset, value);

    if (isGpioReg) {
      const newOut = mcu.sio.gpioValue;
      const newOe = mcu.sio.gpioOutputEnable;
      if (((newOut ^ prevOut) & 0x7fff) !== 0 || ((newOe ^ prevOe) & 0x7fff) !== 0) {
        gpioSwitchCount++;
        activeHighMask = newOe & newOut & 0x7fff;
        activeLowMask = newOe & ~newOut & 0x7fff;
      }
      // Track GP24 / GP25 minibadge 1 Hz clock output toggles.
      const gp24High = (newOe & (1 << 24)) !== 0 && (newOut & (1 << 24)) !== 0;
      const gp25High = (newOe & (1 << 25)) !== 0 && (newOut & (1 << 25)) !== 0;
      if (gp24High !== lastGp24High) {
        gp24ToggleCount++;
        lastGp24High = gp24High;
      }
      if (gp25High !== lastGp25High) {
        gp25ToggleCount++;
        lastGp25High = gp25High;
      }
    }
  };

  // PSM Core 1 reset hook (PSM_FRCE_OFF bit 16).
  const disableCore1ResetGrace = Boolean(options.disableCore1ResetGrace);
  const psm = mcu.peripherals[0x40010];
  let prevCore1Reset = false;
  const origPsmWrite = psm.writeUint32.bind(psm);
  psm.writeUint32 = (offset, value) => {
    origPsmWrite(offset, value);
    const inReset = !!(psm.frceOff & (1 << 16));
    if (inReset) {
      // If Core 1 is inside a short spinlock critical section (PRIMASK=1 or holding a spinlock in
      // alarm_pool_add_alarm_at / sleep_until) when Core 0 resets it during flash save, let Core 1
      // finish releasing its spinlock so Core 0 does not deadlock on the shared alarm_pool spinlock
      // (0x10002652). Only spinlocks owned by Core 1 are cleared; any spinlock held by Core 0 stays held.
      if (!disableCore1ResetGrace) {
        if (!prevCore1Reset && (core1.PRIMASK || core1SpinLockMask !== 0)) {
          const prevActive = mcu.activeCore;
          const prevCore = mcu.core;
          mcu.activeCore = core1;
          mcu.core = core1;
          for (let guard = 0; guard < 512 && (core1.PRIMASK || core1SpinLockMask !== 0); guard++) {
            core1.executeInstruction();
          }
          mcu.activeCore = prevActive;
          mcu.core = prevCore;
        }
        mcu.sio.spinLock = (mcu.sio.spinLock & ~core1SpinLockMask) >>> 0;
        core1SpinLockMask = 0;
      }
      fifo0to1.length = 0;
      fifo1to0.length = 0;
      updateFifoIrqs();
      resetCore1ToBootrom();
    } else if (prevCore1Reset && !inReset) {
      resetCore1ToBootrom();
    }
    prevCore1Reset = inReset;
  };

  // SSI + IO_QSPI flash command emulation (0x4b unique ID, 0x06 WREN, 0x05 RDSR, 0x20/0xd8 erase, 0x02 program).
  const uniqueId = options.uniqueId ?? DEFAULT_UNIQUE_ID;
  const flashOps = [];
  let flashDirty = false;
  let qspiSsCtrl = 0;
  let spiCmdBuf = [];
  const spiRxQueue = [];
  const commitSpiTransaction = () => {
    if (spiCmdBuf.length === 0) return;
    const cmd = spiCmdBuf[0];
    if (cmd === 0x20 && spiCmdBuf.length >= 4) {
      const addr = ((spiCmdBuf[1] << 16) | (spiCmdBuf[2] << 8) | spiCmdBuf[3]) & ~0xfff;
      mcu.flash.fill(0xff, addr, addr + 4096);
      if (addr >= 0x1cc000 && addr < 0x1ce000) flashDirty = true;
      flashOps.push({ type: 'erase4k', addr });
    } else if (cmd === 0xd8 && spiCmdBuf.length >= 4) {
      const addr = ((spiCmdBuf[1] << 16) | (spiCmdBuf[2] << 8) | spiCmdBuf[3]) & ~0xffff;
      mcu.flash.fill(0xff, addr, addr + 65536);
      if (addr <= 0x1cd000 && addr + 65536 > 0x1cc000) flashDirty = true;
      flashOps.push({ type: 'erase64k', addr });
    } else if (cmd === 0x02 && spiCmdBuf.length > 4) {
      const addr = (spiCmdBuf[1] << 16) | (spiCmdBuf[2] << 8) | spiCmdBuf[3];
      const data = spiCmdBuf.slice(4);
      for (let i = 0; i < data.length; i++) {
        const off = (addr & ~0xff) | ((addr + i) & 0xff);
        mcu.flash[off] &= data[i];
      }
      if (addr >= 0x1cc000 && addr < 0x1ce000) flashDirty = true;
      flashOps.push({
        type: 'program',
        addr,
        length: data.length,
      });
    } else if (cmd === 0x4b && spiCmdBuf.length >= 13) {
      flashOps.push({ type: 'read_unique_id' });
    }
    spiCmdBuf = [];
  };

  const ioQspi = mcu.peripherals[0x40018];
  ioQspi.readUint32 = (offset) => (offset === 0x0c ? qspiSsCtrl : 0);
  ioQspi.writeUint32 = (offset, value) => {
    if (offset === 0x0c) {
      const prevCsLow = ((qspiSsCtrl >> 8) & 3) === 2;
      qspiSsCtrl = value >>> 0;
      const nowCsLow = ((qspiSsCtrl >> 8) & 3) === 2;
      if (!prevCsLow && nowCsLow) {
        spiCmdBuf = [];
      } else if (prevCsLow && !nowCsLow) {
        commitSpiTransaction();
      }
    }
  };

  const ssi = mcu.peripherals[0x18000];
  const origSsiRead = ssi.readUint32.bind(ssi);
  const origSsiWrite = ssi.writeUint32.bind(ssi);
  ssi.readUint32 = (offset) => {
    if (offset === 0x20) return 0; // TXFLR
    if (offset === 0x24) return spiRxQueue.length; // RXFLR
    if (offset === 0x28) return 0x06 | (spiRxQueue.length > 0 ? 0x08 : 0); // SR
    if (offset === 0x60) return spiRxQueue.length > 0 ? spiRxQueue.shift() : 0; // DR0
    return origSsiRead(offset);
  };
  ssi.writeUint32 = (offset, value) => {
    if (offset === 0x08 && (value & 1) === 0) {
      spiRxQueue.length = 0;
      return;
    }
    if (offset === 0x60) {
      const b = value & 0xff;
      spiCmdBuf.push(b);
      const cmd = spiCmdBuf[0];
      if (cmd === 0x4b && spiCmdBuf.length >= 6 && spiCmdBuf.length <= 13) {
        spiRxQueue.push(uniqueId[spiCmdBuf.length - 6]);
      } else {
        spiRxQueue.push(0x00);
      }
      return;
    }
    origSsiWrite(offset, value);
  };

  // I2C0 (0x29 ambient light sensor) and I2C1 (0x20..0x27 minibadge scan) emulation.
  const i2cTransactions = [];
  let alsCh0 = options.alsLux ?? 250; // >100 keeps LEDs enabled; 1..100 blanks LEDs.
  let alsReg = 0;
  const alsRegs = new Uint8Array(256);
  alsRegs[0x80] = 0x01;
  alsRegs[0x85] = 0x00;

  const hookI2C = (i2c, busIndex) => {
    let curTx = null;
    i2c.onStart = () => {
      if (curTx && (curTx.writes.length || curTx.reads.length)) {
        if (i2cTransactions.length < 200) i2cTransactions.push(curTx);
      }
      curTx = null;
      i2c.completeStart();
    };
    i2c.onConnect = (addr, mode) => {
      curTx = { bus: busIndex, addr, mode, writes: [], reads: [] };
      const ack = busIndex === 0 ? addr === 0x29 : (options.ackI2c1 ?? false);
      curTx.ack = ack;
      i2c.completeConnect(ack);
      if (!ack) {
        // On real DW_apb_i2c hardware, flushing the TX FIFO on a NACK abort
        // asserts IC_RAW_INTR_STAT.TX_EMPTY (bit 4 = 0x10), which Pico SDK's
        // i2c_write_blocking_internal (0x10008ebe) polls before checking IC_TX_ABRT_SOURCE.
        i2c.intRaw |= 0x10;
      }
    };
    i2c.onWriteByte = (byte) => {
      if (curTx) curTx.writes.push(byte);
      if (busIndex === 0 && curTx?.addr === 0x29) {
        if (curTx.writes.length === 1) {
          alsReg = byte;
        } else {
          alsRegs[alsReg] = byte;
        }
      }
      i2c.completeWrite(true);
    };
    i2c.onReadByte = () => {
      let val = 0xff;
      if (busIndex === 0 && curTx?.addr === 0x29) {
        if (alsReg === 0x88) val = alsCh0 & 0xff;
        else if (alsReg === 0x89) val = (alsCh0 >> 8) & 0xff;
        else if (alsReg === 0x8a) val = alsCh0 & 0xff;
        else if (alsReg === 0x8b) val = (alsCh0 >> 8) & 0xff;
        else val = alsRegs[alsReg];
      }
      if (curTx) curTx.reads.push(val);
      i2c.completeRead(val);
    };
    i2c.onStop = () => {
      if (curTx) {
        if (i2cTransactions.length < 200) i2cTransactions.push(curTx);
        curTx = null;
      }
      i2c.completeStop();
    };
  };
  hookI2C(mcu.i2c[0], 0);
  hookI2C(mcu.i2c[1], 1);

  // Serial I/O: either emulator-level hooks ('hook', default) or USBCDC ('usb', for 125 MHz reference test).
  const MAX_SERIAL_HISTORY_CHARS = 8192;
  const MAX_SERIAL_LINES = 200;
  const rxQueue = [];
  const evenBlinkSerialQueue = [];
  let serialOut = '';
  let serialHistory = '';
  const serialLines = [];
  const onSerialLine = options.onSerialLine ?? null;

  const pushSerialLine = (line) => {
    serialLines.push(line);
    if (serialLines.length > MAX_SERIAL_LINES) {
      serialLines.shift();
    }
    if (onSerialLine) onSerialLine(line);
  };

  const appendSerialChunk = (chunk) => {
    if (!chunk) return;
    for (let i = 0; i < chunk.length; i++) {
      const c = chunk.charCodeAt(i) & 0xff;
      if (c === 0x0a || c === 0x0d || (c >= 0x20 && c <= 0x7e)) {
        serialHistory += chunk[i];
      } else {
        serialHistory += '\\x' + c.toString(16).padStart(2, '0');
      }
    }
    if (serialHistory.length > MAX_SERIAL_HISTORY_CHARS) {
      serialHistory = serialHistory.slice(serialHistory.length - MAX_SERIAL_HISTORY_CHARS);
    }
    serialOut += chunk;
    // Extract any newline-delimited lines or printf kiosk replies (which firmware sends without \r\n).
    // Non-printing bytes (such as the stack bytes after printf("badge:%s")) are preserved and escaped as \xNN.
    while (true) {
      const nlIdx = serialOut.indexOf('\n');
      const badgeMatch = serialOut.match(
        /badge:(?:pong|[0-9A-Fa-f]{36}(?:(?!\r|\n|badge:|new clue!|invalid |incorrect |serial )[\s\S])*)|invalid clue length from kiosk|incorrect checksum|serial number doesn't match|new clue!/
      );
      if (badgeMatch && (nlIdx === -1 || badgeMatch.index <= nlIdx)) {
        const prefixRaw = serialOut.slice(0, badgeMatch.index).replace(/^\r+|\r+$/g, '');
        if (prefixRaw.length > 0) {
          pushSerialLine(escapeSerialBytes(prefixRaw));
        }
        pushSerialLine(escapeSerialBytes(badgeMatch[0]));
        serialOut = serialOut.slice(badgeMatch.index + badgeMatch[0].length);
        continue;
      }
      if (nlIdx !== -1) {
        const rawLine = serialOut.slice(0, nlIdx).replace(/\r$/, '');
        serialOut = serialOut.slice(nlIdx + 1).replace(/^\r+/, '');
        if (rawLine.length > 0) {
          pushSerialLine(escapeSerialBytes(rawLine));
        }
        continue;
      }
      break;
    }
  };

  let cdc = null;
  if (stdioMode === 'usb') {
    cdc = new USBCDC(mcu.usbCtrl);
    cdc.onSerialData = (buf) => {
      let chunk = '';
      for (let i = 0; i < buf.length; i++) {
        chunk += String.fromCharCode(buf[i] & 0xff);
      }
      appendSerialChunk(chunk);
    };
  }

  const enqueueRawSerial = (str) => {
    if (stdioMode === 'usb' && cdc) {
      for (let i = 0; i < str.length; i++) {
        cdc.sendSerialByte(str.charCodeAt(i) & 0xff);
      }
    } else {
      for (let i = 0; i < str.length; i++) {
        rxQueue.push(str.charCodeAt(i) & 0xff);
      }
    }
  };

  const sendSerial = (str, opts) => {
    if (opts && opts.sendWhenBlinkEven) {
      evenBlinkSerialQueue.push(str);
      return;
    }
    enqueueRawSerial(str);
  };

  // Minimum simulated button pulse and release gap (25 ms each):
  // At 8 MHz, Core 0 polls GP15..GP17 at 0x10001906..0x10001928 ~94,500 times/s in idle Mode 0
  // (median 8.5 us, 99th percentile 191.4 us, max idle 599.3 us), so even a 0.25 ms pin pulse registers.
  // However, in the browser UI, pointerdown and pointerup events from a quick click or tap can arrive
  // at the Worker before the same 30 fps (~33.3 ms) step_frame runs, which would otherwise press and
  // release the GPIO pin at the exact same simulated nanosecond (0 us pulse width) before any CPU
  // instruction executes. Holding each press low for at least 25 ms of simulated time (and spacing
  // back-to-back queued taps by 25 ms high) guarantees the press spans a frame while long holds stay low
  // until the visitor releases.
  const MIN_BUTTON_HOLD_NANOS = 25_000_000; // 25 ms minimum simulated low pulse
  const MIN_BUTTON_GAP_NANOS = 25_000_000; // 25 ms minimum simulated high gap between queued taps
  const buttonState = {
    15: { userHeld: false, low: false, pendingTap: false, minReleaseNanos: 0, nextPressAllowedNanos: 0, queuedTaps: 0 },
    16: { userHeld: false, low: false, pendingTap: false, minReleaseNanos: 0, nextPressAllowedNanos: 0, queuedTaps: 0 },
    17: { userHeld: false, low: false, pendingTap: false, minReleaseNanos: 0, nextPressAllowedNanos: 0, queuedTaps: 0 },
  };
  let nextButtonEventNanos = Infinity;

  const updateNextButtonEventNanos = () => {
    let next = Infinity;
    for (const pin of [15, 16, 17]) {
      const st = buttonState[pin];
      if (st.low && (!st.userHeld || st.queuedTaps > 0 || st.pendingTap)) {
        if (st.minReleaseNanos < next) next = st.minReleaseNanos;
      } else if (!st.low && (st.queuedTaps > 0 || st.pendingTap)) {
        if (st.nextPressAllowedNanos < next) next = st.nextPressAllowedNanos;
      }
    }
    nextButtonEventNanos = next;
  };

  const serviceButtonTimers = () => {
    const now = mcu.clock.nanos;
    for (const pin of [15, 16, 17]) {
      const st = buttonState[pin];
      if (st.low && (!st.userHeld || st.queuedTaps > 0 || st.pendingTap) && now >= st.minReleaseNanos) {
        st.low = false;
        st.nextPressAllowedNanos = now + MIN_BUTTON_GAP_NANOS;
        mcu.gpio[pin].setInputValue(true);
      }
      if (!st.low && now >= st.nextPressAllowedNanos) {
        if (st.queuedTaps > 0) {
          st.queuedTaps--;
          st.low = true;
          st.minReleaseNanos = now + MIN_BUTTON_HOLD_NANOS;
          mcu.gpio[pin].setInputValue(false);
        } else if (st.userHeld && st.pendingTap) {
          st.pendingTap = false;
          st.low = true;
          st.minReleaseNanos = now + MIN_BUTTON_HOLD_NANOS;
          mcu.gpio[pin].setInputValue(false);
        }
      }
    }
    updateNextButtonEventNanos();
  };

  core0.PC = 0x10000000;

  let totalCore0Cycles = 0;
  let totalCore1Cycles = 0;
  let inBootloader = false;
  let inSelfTest = false;
  let stdioHookInCalls = 0;
  let stdioHookOutCalls = 0;

  const stepLockstep = (simMicros) => {
    const targetNanos = mcu.clock.nanos + simMicros * 1000;
    const useHook = stdioMode === 'hook';

    while (mcu.clock.nanos < targetNanos) {
      if (mcu.clock.nanos >= nextButtonEventNanos) {
        serviceButtonTimers();
      }
      if (inBootloader) {
        mcu.clock.tick(targetNanos - mcu.clock.nanos);
        break;
      }
      const core1Halted = !!(psm.frceOff & (1 << 16));
      let c0 = 1;
      if (!core0.waiting) {
        mcu.activeCore = core0;
        mcu.core = core0;

        if (core0.interruptsUpdated) {
          if (core0.checkForInterrupts()) {
            core0.waiting = false;
          }
        }

        const pc0 = (core0.PC & ~1) >>> 0;
        if (pc0 === 0x10000a3c) {
          inSelfTest = true;
        }
        if (pc0 === STDIO_HOOKS.BOOTLOADER_ENTRY) {
          inBootloader = true;
          continue;
        } else if (useHook && pc0 === STDIO_HOOKS.GETCHAR_TIMEOUT_US) {
          stdioHookInCalls++;
          // Raw sendSerial messages reach the firmware immediately via rxQueue.
          // Only messages explicitly sent with { sendWhenBlinkEven: true } (used by the page's
          // kiosk stand-in Visit) wait in evenBlinkSerialQueue until newClueBlinkCounter (0x20001c1c)
          // is even in Mode 0 (0x20001c04 == 0) so the previous clue's LEDs are in the ON (2500)
          // state before process_kiosk_message computes old_mask ^ new_mask.
          if (
            evenBlinkSerialQueue.length > 0 &&
            rxQueue.length === 0 &&
            mcu.readUint32(0x20001d50) === 0 &&
            (mcu.readUint32(0x20001c04) !== 0 || (mcu.readUint32(0x20001c1c) & 1) === 0)
          ) {
            enqueueRawSerial(evenBlinkSerialQueue.shift());
          }
          core0.registers[0] = rxQueue.length > 0 ? rxQueue.shift() & 0xff : 0xffffffff;
          core0.BXWritePC(core0.LR);
          c0 = 1;
        } else if (useHook && pc0 === STDIO_HOOKS.STDIO_USB_OUT_CHARS) {
          stdioHookOutCalls++;
          const bufAddr = core0.registers[0] >>> 0;
          const len = core0.registers[1] | 0;
          if (len > 0 && len <= 4096) {
            let chunk = '';
            for (let i = 0; i < len; i++) {
              chunk += String.fromCharCode(mcu.readUint8(bufAddr + i));
            }
            appendSerialChunk(chunk);
          }
          core0.BXWritePC(core0.LR);
          c0 = 1;
        } else {
          const op0 = mcu.readUint16(pc0);
          c0 = core0.executeInstruction();
          if (op0 === 0xbf40) {
            // ARMv6-M SEV (0xbf40): wake both cores from WFE (e.g. wakeup_on_alarm at 0x100022d4).
            core0.eventRegistered = true;
            core0.waiting = false;
            core1.eventRegistered = true;
            core1.waiting = false;
          }
        }
        totalCore0Cycles += c0;
      }

      let c1 = 1;
      if (!core1Halted && !core1.waiting) {
        mcu.activeCore = core1;
        mcu.core = core1;
        const pc1 = (core1.PC & ~1) >>> 0;
        const op1 = mcu.readUint16(pc1);
        c1 = core1.executeInstruction();
        if (op1 === 0xbf40) {
          core0.eventRegistered = true;
          core0.waiting = false;
          core1.eventRegistered = true;
          core1.waiting = false;
        }
        totalCore1Cycles += c1;
      }

      const stepCycles = c0 > c1 ? c0 : c1;
      const dtNanos = stepCycles * cycleNanos;
      mcu.activeCore = core0;
      mcu.core = core0;
      mcu.clock.tick(dtNanos);
    }

    if (mcu.clock.nanos >= nextButtonEventNanos) {
      serviceButtonTimers();
    }
    flushGpioTime();
  };

  const resetLedCounters = () => {
    flushGpioTime();
    cumulativeChannelNanos.fill(0);
    cumulativeWindowNanos = 0;
    gpioSwitchCount = 0;
  };

  const getCumulativeLedReport = () => {
    flushGpioTime();
    const denom = cumulativeWindowNanos > 0 ? cumulativeWindowNanos : 1;
    const shares = new Float32Array(156);
    for (let i = 0; i < 156; i++) {
      shares[i] = cumulativeChannelNanos[i] / denom;
    }
    const rgb = sharesToRgb(shares);
    return {
      windowNanos: cumulativeWindowNanos,
      gpioSwitchCount,
      shares,
      rgb,
    };
  };

  const getFrameLeds = () => {
    flushGpioTime();
    let totalWin = 0;
    for (let b = 0; b < NUM_SUB_BUCKETS; b++) {
      totalWin += bucketWindowNanos[b];
    }
    const denom = totalWin > 0 ? totalWin : 1;
    const shares = new Float32Array(156);
    const rgb = new Uint8Array(156);
    if (!inBootloader) {
      for (let ch = 0; ch < 156; ch++) {
        let onNanos = 0;
        for (let b = 0; b < NUM_SUB_BUCKETS; b++) {
          onNanos += bucketChannelNanos[b][ch];
        }
        shares[ch] = onNanos / denom;
      }
      sharesToRgb(shares, rgb);
    }
    return { shares, rgb, windowNanos: totalWin, gpioSwitchCount };
  };

  const setButton = (buttonNameOrPin, pressed) => {
    const pin = typeof buttonNameOrPin === 'number' ? buttonNameOrPin : BUTTON_PINS[buttonNameOrPin];
    if (pin === undefined || !mcu.gpio[pin]) return;
    const st = buttonState[pin];
    if (!st) {
      mcu.gpio[pin].setInputValue(!pressed);
      return;
    }
    const now = mcu.clock.nanos;
    if (pressed) {
      if (st.userHeld) return;
      st.userHeld = true;
      if (!st.low && st.queuedTaps === 0 && now >= st.nextPressAllowedNanos) {
        st.low = true;
        st.pendingTap = false;
        st.minReleaseNanos = now + MIN_BUTTON_HOLD_NANOS;
        mcu.gpio[pin].setInputValue(false);
      } else {
        st.pendingTap = true;
      }
    } else {
      if (!st.userHeld) return;
      st.userHeld = false;
      if (st.pendingTap) {
        st.pendingTap = false;
        st.queuedTaps++;
      } else if (st.low && st.queuedTaps === 0 && now >= st.minReleaseNanos) {
        st.low = false;
        st.nextPressAllowedNanos = now;
        mcu.gpio[pin].setInputValue(true);
      }
    }
    updateNextButtonEventNanos();
  };

  const consumeDirtyFlashSectors = () => {
    if (!flashDirty) return null;
    flashDirty = false;
    return {
      sector1cc: mcu.flash.slice(0x1cc000, 0x1cc000 + 4096),
      sector1cd: mcu.flash.slice(0x1cd000, 0x1cd000 + 4096),
    };
  };

  const getFirmwareState = () => {
    const pc0 = (core0.PC & ~1) >>> 0;
    return {
      simSeconds: mcu.clock.nanos / 1e9,
      millis: mcu.readUint32(0x20001ed4) >>> 0,
      activeCategory: mcu.readUint32(0x20001d44) >>> 0,
      displayMode: mcu.readUint32(0x20001c04) >>> 0,
      gameWon: mcu.readUint8(0x20001f00),
      clueNum: mcu.readUint8(0x20001754),
      clueWordRaw: mcu.readUint32(0x20001754) >>> 0,
      suspectsCursor: mcu.readUint32(0x20000884) >>> 0,
      attacksCursor: mcu.readUint32(0x20000880) >>> 0,
      roomsCursor: mcu.readUint32(0x20001d48) >>> 0,
      suspectsMask: mcu.readUint32(0x20001c14) >>> 0,
      attacksMask: mcu.readUint32(0x20001c0c) >>> 0,
      roomsMask: mcu.readUint32(0x20001c10) >>> 0,
      winSuspectsMask: mcu.readUint32(0x20001ec8) >>> 0,
      winAttacksMask: mcu.readUint32(0x20001ec0) >>> 0,
      winRoomsMask: mcu.readUint32(0x20001ec4) >>> 0,
      newClueBlinkCounter: mcu.readUint32(0x20001c1c) >>> 0,
      ledsEnabled: mcu.readUint8(0x20000788),
      inBootloader,
      inSelfTest: inSelfTest || (pc0 >= 0x10000a3c && pc0 <= 0x10000b2e),
      gp24ToggleCount,
      gp25ToggleCount,
      gp15Input: mcu.gpio[15].inputValue,
      gp16Input: mcu.gpio[16].inputValue,
      gp17Input: mcu.gpio[17].inputValue,
      core0PC: core0.PC >>> 0,
      core1PC: core1.PC >>> 0,
      totalCore0Cycles,
      totalCore1Cycles,
      stdioHookInCalls,
      stdioHookOutCalls,
    };
  };

  return {
    mcu,
    core0,
    core1,
    cdc,
    ledTable,
    logs,
    flashOps,
    i2cTransactions,
    serialLines,
    getSerialOutput: () => serialHistory,
    clearSerialOutput: () => {
      const out = serialHistory;
      serialHistory = '';
      return out;
    },
    sendSerial,
    setButton,
    setAlsLux: (lux) => {
      alsCh0 = lux;
    },
    stepLockstep,
    resetLedCounters,
    getCumulativeLedReport,
    getFrameLeds,
    consumeFrameReport: getFrameLeds,
    isFlashDirty: () => flashDirty,
    consumeDirtyFlashSectors,
    getWatchdogTriggers: () => watchdogTriggers,
    getSpinLockOwners: () => ({
      core0SpinLockMask: core0SpinLockMask >>> 0,
      core1SpinLockMask: core1SpinLockMask >>> 0,
      rawSpinLock: mcu.sio.spinLock >>> 0,
    }),
    getFirmwareState,
  };
}
