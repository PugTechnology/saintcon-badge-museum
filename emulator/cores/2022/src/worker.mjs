/**
 * SAINTCON 2022 Badge Web Worker & Core Entrypoint
 *
 * Runs the dual-core RP2040 emulator (wokwi/rp2040js, MIT License) in a Web Worker,
 * integrating real SIO GPIO pin-time shares for the 52 charlieplexed RGB LEDs
 * (~30 frames/second) and reporting real simulation speed if the host CPU is slower
 * than real time.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2021, Uri Shaked (wokwi/rp2040js)
 */

import {
  createBadge,
  buildKioskMessage,
  parseBadgeMessage,
  shareToBrightness,
  sharesToRgb,
  escapeSerialBytes,
  extractLedTableFromFlash,
  FLASH_LED_TABLES,
  STDIO_HOOKS,
  BUTTON_PINS,
} from './badge2022.mjs';

export {
  createBadge,
  buildKioskMessage,
  parseBadgeMessage,
  shareToBrightness,
  sharesToRgb,
  escapeSerialBytes,
  extractLedTableFromFlash,
  FLASH_LED_TABLES,
  STDIO_HOOKS,
  BUTTON_PINS,
};

const isWorkerScope =
  typeof WorkerGlobalScope !== 'undefined' && self instanceof WorkerGlobalScope;

if (typeof self !== 'undefined') {
  self.Badge2022Core = {
    createBadge,
    buildKioskMessage,
    parseBadgeMessage,
    shareToBrightness,
    sharesToRgb,
    escapeSerialBytes,
    extractLedTableFromFlash,
    FLASH_LED_TABLES,
    STDIO_HOOKS,
    BUTTON_PINS,
  };
}

if (isWorkerScope) {
  let badge = null;
  let cachedUf2Bytes = null;
  let cachedCycleNanos = 125;
  let cachedAlsLux = 250;
  let currentSavedSectors = { sector1cc: null, sector1cd: null };
  let running = false;
  let timerId = null;
  let lastTickWallMs = 0;
  let smoothedSpeed = 1.0;

  const FRAME_INTERVAL_MS = 33; // ~30 fps
  const MAX_BUDGET_MS = 28; // yield before next message tick if host CPU is slow
  const CHUNK_US = 2000; // 2 ms simulated chunks

  function emitFrame(simDeltaUs, wallDeltaMs) {
    if (!badge) return;
    const report = badge.consumeFrameReport();
    const state = badge.getFirmwareState();
    let dirtySectors = null;
    if (badge.isFlashDirty()) {
      dirtySectors = badge.consumeDirtyFlashSectors();
      if (dirtySectors) {
        currentSavedSectors = {
          sector1cc: new Uint8Array(dirtySectors.sector1cc),
          sector1cd: new Uint8Array(dirtySectors.sector1cd),
        };
      }
    }
    const instSpeed =
      wallDeltaMs > 0 && simDeltaUs > 0 ? simDeltaUs / (wallDeltaMs * 1000) : 1.0;
    smoothedSpeed = smoothedSpeed * 0.8 + Math.min(1.2, instSpeed) * 0.2;

    self.postMessage({
      type: 'frame',
      rgb: report.rgb,
      shares: report.shares,
      gpioSwitchCount: report.gpioSwitchCount,
      windowNanos: report.windowNanos,
      simDeltaUs,
      wallDeltaMs,
      speedRatio: Math.min(1.0, Math.max(0.01, Number(smoothedSpeed.toFixed(2)))),
      rawSpeedRatio: Number(instSpeed.toFixed(2)),
      state,
      dirtySectors,
    });
  }

  function stepFrame() {
    if (!running || !badge) return;
    try {
      const nowMs = performance.now();
      const dtWallMs = Math.min(100, Math.max(5, nowMs - lastTickWallMs));
      lastTickWallMs = nowMs;

      const st = badge.getFirmwareState();
      if (st.inBootloader) {
        emitFrame(0, dtWallMs);
        if (running) {
          timerId = setTimeout(stepFrame, FRAME_INTERVAL_MS);
        }
        return;
      }

      const targetSimUs = Math.round(dtWallMs * 1000);
      let simDoneUs = 0;
      const startWallMs = performance.now();

      while (simDoneUs < targetSimUs) {
        const stepUs = Math.min(CHUNK_US, targetSimUs - simDoneUs);
        badge.stepLockstep(stepUs);
        simDoneUs += stepUs;
        if (badge.getFirmwareState().inBootloader) break;
        if (performance.now() - startWallMs >= MAX_BUDGET_MS) {
          break;
        }
      }

      const elapsedWallMs = Math.max(1, performance.now() - startWallMs);
      // Compute speed ratio against the wall-clock interval between frames (or CPU time if throttled)
      const effectiveWallMs = Math.max(dtWallMs, elapsedWallMs);
      emitFrame(simDoneUs, effectiveWallMs);

      if (running) {
        const nextDelay = Math.max(
          2,
          FRAME_INTERVAL_MS - Math.round(performance.now() - startWallMs),
        );
        timerId = setTimeout(stepFrame, nextDelay);
      }
    } catch (err) {
      stopLoop();
      self.postMessage({
        type: 'error',
        message: err && err.message ? err.message : String(err),
      });
    }
  }

  function startLoop() {
    if (running) return;
    running = true;
    lastTickWallMs = performance.now();
    timerId = setTimeout(stepFrame, 0);
  }

  function stopLoop() {
    running = false;
    if (timerId !== null) {
      clearTimeout(timerId);
      timerId = null;
    }
  }

  function rebuildBadge(holdButtonsAtBoot = []) {
    if (!cachedUf2Bytes) return;
    badge = createBadge({
      uf2Bytes: cachedUf2Bytes,
      cycleNanos: cachedCycleNanos,
      savedSectors: currentSavedSectors,
      holdButtonsAtBoot,
      onSerialLine(line) {
        self.postMessage({ type: 'serial_line', line });
      },
    });
    badge.setAlsLux(cachedAlsLux);
    smoothedSpeed = 1.0;
    if (holdButtonsAtBoot.includes('SELECT')) {
      badge.stepLockstep(250_000);
    }
  }

  self.onmessage = (ev) => {
    const msg = ev.data;
    if (!msg || typeof msg.type !== 'string') return;

    try {
      switch (msg.type) {
        case 'init': {
          stopLoop();
          cachedUf2Bytes =
            msg.uf2Bytes instanceof Uint8Array ? msg.uf2Bytes : new Uint8Array(msg.uf2Bytes);
          cachedCycleNanos = msg.cycleNanos ?? 125;
          if (msg.savedSectors) {
            currentSavedSectors = {
              sector1cc: msg.savedSectors.sector1cc
                ? new Uint8Array(msg.savedSectors.sector1cc)
                : null,
              sector1cd: msg.savedSectors.sector1cd
                ? new Uint8Array(msg.savedSectors.sector1cd)
                : null,
            };
          } else {
            currentSavedSectors = { sector1cc: null, sector1cd: null };
          }
          rebuildBadge(msg.holdButtonsAtBoot ?? []);
          self.postMessage({
            type: 'ready',
            ledTable: badge.ledTable,
            state: badge.getFirmwareState(),
          });
          if (msg.autoStart !== false) {
            startLoop();
          }
          break;
        }
        case 'start': {
          startLoop();
          self.postMessage({
            type: 'started',
            state: badge ? badge.getFirmwareState() : null,
          });
          break;
        }
        case 'pause': {
          stopLoop();
          self.postMessage({
            type: 'paused',
            state: badge ? badge.getFirmwareState() : null,
          });
          break;
        }
        case 'reset': {
          const wasRunning = running;
          stopLoop();
          if (msg.savedSectors !== undefined) {
            currentSavedSectors = msg.savedSectors
              ? {
                  sector1cc: msg.savedSectors.sector1cc
                    ? new Uint8Array(msg.savedSectors.sector1cc)
                    : null,
                  sector1cd: msg.savedSectors.sector1cd
                    ? new Uint8Array(msg.savedSectors.sector1cd)
                    : null,
                }
              : { sector1cc: null, sector1cd: null };
          }
          const hold = Array.isArray(msg.holdButtonsAtBoot) ? msg.holdButtonsAtBoot : [];
          rebuildBadge(hold);
          self.postMessage({
            type: 'reset_done',
            holdButtonsAtBoot: hold,
            state: badge ? badge.getFirmwareState() : null,
          });
          emitFrame(0, FRAME_INTERVAL_MS);
          if (wasRunning || msg.autoStart) {
            startLoop();
          }
          break;
        }
        case 'wipe_save': {
          const wasRunning = running;
          stopLoop();
          currentSavedSectors = { sector1cc: null, sector1cd: null };
          rebuildBadge([]);
          self.postMessage({
            type: 'wipe_done',
            state: badge ? badge.getFirmwareState() : null,
          });
          emitFrame(0, FRAME_INTERVAL_MS);
          if (wasRunning || msg.autoStart !== false) {
            startLoop();
          }
          break;
        }
        case 'button': {
          if (badge && msg.name) {
            badge.setButton(msg.name, Boolean(msg.pressed));
          }
          break;
        }
        case 'serial_in': {
          if (badge && typeof msg.text === 'string') {
            badge.sendSerial(msg.text, { sendWhenBlinkEven: Boolean(msg.sendWhenBlinkEven) });
          }
          break;
        }
        case 'als_lux': {
          const val = typeof msg.lux === 'number' ? msg.lux : msg.ch0;
          if (typeof val === 'number') {
            cachedAlsLux = val;
            if (badge) {
              badge.setAlsLux(val);
            }
          }
          break;
        }
      }
    } catch (err) {
      stopLoop();
      self.postMessage({
        type: 'error',
        message: err && err.message ? err.message : String(err),
      });
    }
  };
}
