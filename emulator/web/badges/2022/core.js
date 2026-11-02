/**
 * SAINTCON 2022 Badge RP2040 Web Worker Core Bundle
 *
 * 1. wokwi/rp2040js (commit 5fbd20508a58b9859758c5cd70b207ba1e0b0150) - MIT License:
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Uri Shaked
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * 2. RP2040 Bootrom B1 (raspberrypi/pico-bootrom commit 00a4a19114195e20fb817bdfbca1165e157eef37) - BSD-3-Clause License:
 *
 * NOTE: this license applies to the contents of this repository, EXCLUDING the contents of the files bootrom/mufplib.S
 * and bootrom/mufplib-double.S, which are licensed separately:
 *
 * Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
 * following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
 *    disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * 3. RP2040 Bootrom B1 Floating-Point Routines (bootrom/mufplib.S in
 *    raspberrypi/pico-bootrom commit 00a4a19114195e20fb817bdfbca1165e157eef37):
 *    Copyright (c) 2020 Mark Owen (https://www.quinapalus.com). Raspberry Pi
 *    (Trading) Ltd grants a non-exclusive license to use the software solely on
 *    a Raspberry Pi RP2040 device; also available from the copyright owner under
 *    the GNU General Public License version 2 (GPLv2).
 *    Source: https://github.com/raspberrypi/pico-bootrom/blob/00a4a19114195e20fb817bdfbca1165e157eef37/bootrom/mufplib.S
 *    (Email addresses omitted; none present in upstream header.)
 *    Full third-party license texts and notices: badges/2022/THIRD_PARTY_LICENSES.txt
 */
/**
 * Copyright (c) 2020 Mark Owen https://www.quinapalus.com .
 *
 * Raspberry Pi (Trading) Ltd (Licensor) hereby grants to you a non-exclusive license to use the software solely on a
 * Raspberry Pi RP2040 device. No other use is permitted under the terms of this license.
 *
 * This software is also available from the copyright owner under GPLv2 licence.
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
var ClockAlarm = class {
  clock;
  callback;
  next;
  nanos;
  scheduled;
  constructor(clock, callback) {
    this.clock = clock;
    this.callback = callback;
    this.next = null;
    this.nanos = 0;
    this.scheduled = false;
  }
  schedule(deltaNanos) {
    if (this.scheduled) {
      this.cancel();
    }
    this.clock.linkAlarm(deltaNanos, this);
  }
  cancel() {
    this.clock.unlinkAlarm(this);
    this.scheduled = false;
  }
};
var SimulationClock = class {
  frequency;
  nextAlarm;
  nanosCounter;
  constructor(frequency = 125e6) {
    this.frequency = frequency;
    this.nextAlarm = null;
    this.nanosCounter = 0;
  }
  get nanos() {
    return this.nanosCounter;
  }
  get micros() {
    return this.nanos / 1e3;
  }
  createAlarm(callback) {
    return new ClockAlarm(this, callback);
  }
  linkAlarm(nanos, alarm) {
    alarm.nanos = this.nanos + nanos;
    let alarmListItem = this.nextAlarm;
    let lastItem = null;
    while (alarmListItem && alarmListItem.nanos < alarm.nanos) {
      lastItem = alarmListItem;
      alarmListItem = alarmListItem.next;
    }
    if (lastItem) {
      lastItem.next = alarm;
      alarm.next = alarmListItem;
    } else {
      this.nextAlarm = alarm;
      alarm.next = alarmListItem;
    }
    alarm.scheduled = true;
    return alarm;
  }
  unlinkAlarm(alarm) {
    let alarmListItem = this.nextAlarm;
    if (!alarmListItem) {
      return false;
    }
    let lastItem = null;
    while (alarmListItem) {
      if (alarmListItem === alarm) {
        if (lastItem) {
          lastItem.next = alarmListItem.next;
        } else {
          this.nextAlarm = alarmListItem.next;
        }
        return true;
      }
      lastItem = alarmListItem;
      alarmListItem = alarmListItem.next;
    }
    return false;
  }
  tick(deltaNanos) {
    const targetNanos = this.nanosCounter + deltaNanos;
    let alarm = this.nextAlarm;
    while (alarm && alarm.nanos <= targetNanos) {
      this.nextAlarm = alarm.next;
      this.nanosCounter = alarm.nanos;
      alarm.callback();
      alarm = this.nextAlarm;
    }
    this.nanosCounter = targetNanos;
  }
  get nanosToNextAlarm() {
    if (this.nextAlarm) {
      return this.nextAlarm.nanos - this.nanos;
    }
    return 0;
  }
};

var IRQ = /* @__PURE__ */ function(IRQ22) {
  IRQ22[IRQ22["TIMER_0"] = 0] = "TIMER_0";
  IRQ22[IRQ22["TIMER_1"] = 1] = "TIMER_1";
  IRQ22[IRQ22["TIMER_2"] = 2] = "TIMER_2";
  IRQ22[IRQ22["TIMER_3"] = 3] = "TIMER_3";
  IRQ22[IRQ22["PWM_WRAP"] = 4] = "PWM_WRAP";
  IRQ22[IRQ22["USBCTRL"] = 5] = "USBCTRL";
  IRQ22[IRQ22["XIP"] = 6] = "XIP";
  IRQ22[IRQ22["PIO0_IRQ0"] = 7] = "PIO0_IRQ0";
  IRQ22[IRQ22["PIO0_IRQ1"] = 8] = "PIO0_IRQ1";
  IRQ22[IRQ22["PIO1_IRQ0"] = 9] = "PIO1_IRQ0";
  IRQ22[IRQ22["PIO1_IRQ1"] = 10] = "PIO1_IRQ1";
  IRQ22[IRQ22["DMA_IRQ0"] = 11] = "DMA_IRQ0";
  IRQ22[IRQ22["DMA_IRQ1"] = 12] = "DMA_IRQ1";
  IRQ22[IRQ22["IO_BANK0"] = 13] = "IO_BANK0";
  IRQ22[IRQ22["IO_QSPI"] = 14] = "IO_QSPI";
  IRQ22[IRQ22["SIO_PROC0"] = 15] = "SIO_PROC0";
  IRQ22[IRQ22["SIO_PROC1"] = 16] = "SIO_PROC1";
  IRQ22[IRQ22["CLOCKS"] = 17] = "CLOCKS";
  IRQ22[IRQ22["SPI0"] = 18] = "SPI0";
  IRQ22[IRQ22["SPI1"] = 19] = "SPI1";
  IRQ22[IRQ22["UART0"] = 20] = "UART0";
  IRQ22[IRQ22["UART1"] = 21] = "UART1";
  IRQ22[IRQ22["ADC_FIFO"] = 22] = "ADC_FIFO";
  IRQ22[IRQ22["I2C0"] = 23] = "I2C0";
  IRQ22[IRQ22["I2C1"] = 24] = "I2C1";
  IRQ22[IRQ22["RTC"] = 25] = "RTC";
  return IRQ22;
}({});
var MAX_HARDWARE_IRQ = IRQ.RTC;

var EXC_RESET = 1;
var EXC_NMI = 2;
var EXC_HARDFAULT = 3;
var EXC_SVCALL = 11;
var EXC_PENDSV = 14;
var EXC_SYSTICK = 15;
var SYSM_APSR = 0;
var SYSM_XPSR = 3;
var SYSM_IPSR = 5;
var SYSM_MSP = 8;
var SYSM_PSP = 9;
var SYSM_PRIMASK = 16;
var SYSM_CONTROL = 20;
var LOWEST_PRIORITY = 4;
var ExecutionMode = /* @__PURE__ */ function(ExecutionMode2) {
  ExecutionMode2[ExecutionMode2["Mode_Thread"] = 0] = "Mode_Thread";
  ExecutionMode2[ExecutionMode2["Mode_Handler"] = 1] = "Mode_Handler";
  return ExecutionMode2;
}(ExecutionMode || {});
function signExtend8(value) {
  return value << 24 >> 24;
}
function signExtend16(value) {
  return value << 16 >> 16;
}
var spRegister = 13;
var pcRegister = 15;
var StackPointerBank = /* @__PURE__ */ function(StackPointerBank2) {
  StackPointerBank2[StackPointerBank2["SPmain"] = 0] = "SPmain";
  StackPointerBank2[StackPointerBank2["SPprocess"] = 1] = "SPprocess";
  return StackPointerBank2;
}(StackPointerBank || {});
var LOG_NAME = "CortexM0Core";
var CortexM0Core = class {
  rp2040;
  registers;
  bankedSP;
  cycles;
  eventRegistered;
  waiting;
  // APSR fields
  N;
  C;
  Z;
  V;
  // How many bytes to rewind the last break instruction
  breakRewind;
  // PRIMASK fields
  PM;
  // CONTROL fields
  SPSEL;
  nPRIV;
  currentMode;
  IPSR;
  interruptNMIMask;
  pendingInterrupts;
  enabledInterrupts;
  interruptPriorities;
  pendingNMI;
  pendingPendSV;
  pendingSVCall;
  pendingSystick;
  interruptsUpdated;
  VTOR;
  SHPR2;
  SHPR3;
  /** Hook to listen for function calls - branch-link (BL/BLX) instructions */
  blTaken;
  constructor(rp2040) {
    this.rp2040 = rp2040;
    this.registers = new Uint32Array(16);
    this.bankedSP = 0;
    this.cycles = 0;
    this.eventRegistered = false;
    this.waiting = false;
    this.N = false;
    this.C = false;
    this.Z = false;
    this.V = false;
    this.breakRewind = 0;
    this.PM = false;
    this.SPSEL = StackPointerBank.SPmain;
    this.nPRIV = false;
    this.currentMode = ExecutionMode.Mode_Thread;
    this.IPSR = 0;
    this.interruptNMIMask = 0;
    this.pendingInterrupts = 0;
    this.enabledInterrupts = 0;
    this.interruptPriorities = [
      4294967295,
      0,
      0,
      0
    ];
    this.pendingNMI = false;
    this.pendingPendSV = false;
    this.pendingSVCall = false;
    this.pendingSystick = false;
    this.interruptsUpdated = false;
    this.VTOR = 0;
    this.SHPR2 = 0;
    this.SHPR3 = 0;
    this.blTaken = (core, blx) => {
      void core;
      void blx;
    };
    this.SP = 4294967292;
    this.bankedSP = 4294967292;
  }
  get logger() {
    return this.rp2040.logger;
  }
  reset() {
    this.SP = this.rp2040.readUint32(this.VTOR);
    this.PC = this.rp2040.readUint32(this.VTOR + 4) & 4294967294;
    this.cycles = 0;
  }
  get SP() {
    return this.registers[13];
  }
  set SP(value) {
    this.registers[13] = value & ~3;
  }
  get LR() {
    return this.registers[14];
  }
  set LR(value) {
    this.registers[14] = value;
  }
  get PC() {
    return this.registers[15];
  }
  set PC(value) {
    this.registers[15] = value;
  }
  get APSR() {
    return (this.N ? 2147483648 : 0) | (this.Z ? 1073741824 : 0) | (this.C ? 536870912 : 0) | (this.V ? 268435456 : 0);
  }
  set APSR(value) {
    this.N = !!(value & 2147483648);
    this.Z = !!(value & 1073741824);
    this.C = !!(value & 536870912);
    this.V = !!(value & 268435456);
  }
  get xPSR() {
    return this.APSR | this.IPSR | 1 << 24;
  }
  set xPSR(value) {
    this.APSR = value;
    this.IPSR = value & 63;
  }
  checkCondition(cond) {
    let result = false;
    switch (cond >> 1) {
      case 0:
        result = this.Z;
        break;
      case 1:
        result = this.C;
        break;
      case 2:
        result = this.N;
        break;
      case 3:
        result = this.V;
        break;
      case 4:
        result = this.C && !this.Z;
        break;
      case 5:
        result = this.N === this.V;
        break;
      case 6:
        result = this.N === this.V && !this.Z;
        break;
      case 7:
        result = true;
        break;
    }
    return cond & 1 && cond != 15 ? !result : result;
  }
  readUint32(address) {
    return this.rp2040.readUint32(address);
  }
  readUint16(address) {
    return this.rp2040.readUint16(address);
  }
  readUint8(address) {
    return this.rp2040.readUint8(address);
  }
  writeUint32(address, value) {
    this.rp2040.writeUint32(address, value);
  }
  writeUint16(address, value) {
    this.rp2040.writeUint16(address, value);
  }
  writeUint8(address, value) {
    this.rp2040.writeUint8(address, value);
  }
  switchStack(stack) {
    if (this.SPSEL !== stack) {
      const temp = this.SP;
      this.SP = this.bankedSP;
      this.bankedSP = temp;
      this.SPSEL = stack;
    }
  }
  get SPprocess() {
    return this.SPSEL === StackPointerBank.SPprocess ? this.SP : this.bankedSP;
  }
  set SPprocess(value) {
    if (this.SPSEL === StackPointerBank.SPprocess) {
      this.SP = value;
    } else {
      this.bankedSP = value >>> 0;
    }
  }
  get SPmain() {
    return this.SPSEL === StackPointerBank.SPmain ? this.SP : this.bankedSP;
  }
  set SPmain(value) {
    if (this.SPSEL === StackPointerBank.SPmain) {
      this.SP = value;
    } else {
      this.bankedSP = value >>> 0;
    }
  }
  exceptionEntry(exceptionNumber) {
    let framePtr = 0;
    let framePtrAlign = 0;
    if (this.SPSEL && this.currentMode === ExecutionMode.Mode_Thread) {
      framePtrAlign = this.SPprocess & 4 ? 1 : 0;
      this.SPprocess = this.SPprocess - 32 & ~4;
      framePtr = this.SPprocess;
    } else {
      framePtrAlign = this.SPmain & 4 ? 1 : 0;
      this.SPmain = this.SPmain - 32 & ~4;
      framePtr = this.SPmain;
    }
    this.writeUint32(framePtr, this.registers[0]);
    this.writeUint32(framePtr + 4, this.registers[1]);
    this.writeUint32(framePtr + 8, this.registers[2]);
    this.writeUint32(framePtr + 12, this.registers[3]);
    this.writeUint32(framePtr + 16, this.registers[12]);
    this.writeUint32(framePtr + 20, this.LR);
    this.writeUint32(framePtr + 24, this.PC & ~1);
    this.writeUint32(framePtr + 28, this.xPSR & ~(1 << 9) | framePtrAlign << 9);
    if (this.currentMode == ExecutionMode.Mode_Handler) {
      this.LR = 4294967281;
    } else {
      if (!this.SPSEL) {
        this.LR = 4294967289;
      } else {
        this.LR = 4294967293;
      }
    }
    this.currentMode = ExecutionMode.Mode_Handler;
    this.IPSR = exceptionNumber;
    this.switchStack(StackPointerBank.SPmain);
    this.eventRegistered = true;
    const vectorTable = this.VTOR;
    this.PC = this.readUint32(vectorTable + 4 * exceptionNumber);
  }
  exceptionReturn(excReturn) {
    let framePtr = this.SPmain;
    switch (excReturn & 15) {
      case 1:
        this.currentMode = ExecutionMode.Mode_Handler;
        this.switchStack(StackPointerBank.SPmain);
        break;
      case 9:
        this.currentMode = ExecutionMode.Mode_Thread;
        this.switchStack(StackPointerBank.SPmain);
        break;
      case 13:
        framePtr = this.SPprocess;
        this.currentMode = ExecutionMode.Mode_Thread;
        this.switchStack(StackPointerBank.SPprocess);
        break;
    }
    this.registers[0] = this.readUint32(framePtr);
    this.registers[1] = this.readUint32(framePtr + 4);
    this.registers[2] = this.readUint32(framePtr + 8);
    this.registers[3] = this.readUint32(framePtr + 12);
    this.registers[12] = this.readUint32(framePtr + 16);
    this.LR = this.readUint32(framePtr + 20);
    this.PC = this.readUint32(framePtr + 24);
    const psr = this.readUint32(framePtr + 28);
    const framePtrAlign = psr & 1 << 9 ? 4 : 0;
    switch (excReturn & 15) {
      case 1:
        this.SPmain = this.SPmain + 32 | framePtrAlign;
        break;
      case 9:
        this.SPmain = this.SPmain + 32 | framePtrAlign;
        break;
      case 13:
        this.SPprocess = this.SPprocess + 32 | framePtrAlign;
        break;
    }
    this.APSR = psr & 4026531840;
    const forceThread = this.currentMode == ExecutionMode.Mode_Thread && this.nPRIV;
    this.IPSR = forceThread ? 0 : psr & 63;
    this.interruptsUpdated = true;
    this.eventRegistered = true;
  }
  get pendSVPriority() {
    return this.SHPR3 >> 22 & 3;
  }
  get svCallPriority() {
    return this.SHPR2 >>> 30;
  }
  get systickPriority() {
    return this.SHPR3 >>> 30;
  }
  exceptionPriority(n) {
    switch (n) {
      case EXC_RESET:
        return -3;
      case EXC_NMI:
        return -2;
      case EXC_HARDFAULT:
        return -1;
      case EXC_SVCALL:
        return this.svCallPriority;
      case EXC_PENDSV:
        return this.pendSVPriority;
      case EXC_SYSTICK:
        return this.systickPriority;
      default: {
        if (n < 16) {
          return LOWEST_PRIORITY;
        }
        const intNum = n - 16;
        for (let priority = 0; priority < 4; priority++) {
          if (this.interruptPriorities[priority] & 1 << intNum) {
            return priority;
          }
        }
        return LOWEST_PRIORITY;
      }
    }
  }
  get vectPending() {
    if (this.pendingNMI) {
      return EXC_NMI;
    }
    const { svCallPriority, systickPriority, pendSVPriority, pendingInterrupts } = this;
    for (let priority = 0; priority < LOWEST_PRIORITY; priority++) {
      const levelInterrupts = pendingInterrupts & this.interruptPriorities[priority];
      if (this.pendingSVCall && priority === svCallPriority) {
        return EXC_SVCALL;
      }
      if (this.pendingPendSV && priority === pendSVPriority) {
        return EXC_PENDSV;
      }
      if (this.pendingSystick && priority === systickPriority) {
        return EXC_SYSTICK;
      }
      if (levelInterrupts) {
        for (let interruptNumber = 0; interruptNumber < 32; interruptNumber++) {
          if (levelInterrupts & 1 << interruptNumber) {
            return 16 + interruptNumber;
          }
        }
      }
    }
    return 0;
  }
  setInterrupt(irq, value) {
    const irqBit = 1 << irq;
    if (value && !(this.pendingInterrupts & irqBit)) {
      this.pendingInterrupts |= irqBit;
      this.interruptsUpdated = true;
      if (this.waiting && this.checkForInterrupts()) {
        this.waiting = false;
      }
    } else if (!value) {
      this.pendingInterrupts &= ~irqBit;
    }
  }
  checkForInterrupts() {
    const currentPriority = this.waiting ? this.PM ? this.exceptionPriority(this.IPSR) : LOWEST_PRIORITY : Math.min(this.exceptionPriority(this.IPSR), this.PM ? 0 : LOWEST_PRIORITY);
    const interruptSet = this.pendingInterrupts & this.enabledInterrupts;
    const { svCallPriority, systickPriority, pendSVPriority } = this;
    if (this.pendingNMI) {
      this.pendingNMI = false;
      this.exceptionEntry(EXC_NMI);
      return true;
    }
    for (let priority = 0; priority < currentPriority; priority++) {
      const levelInterrupts = interruptSet & this.interruptPriorities[priority];
      if (this.pendingSVCall && priority === svCallPriority) {
        this.pendingSVCall = false;
        this.exceptionEntry(EXC_SVCALL);
        return true;
      }
      if (this.pendingPendSV && priority === pendSVPriority) {
        this.pendingPendSV = false;
        this.exceptionEntry(EXC_PENDSV);
        return true;
      }
      if (this.pendingSystick && priority === systickPriority) {
        this.pendingSystick = false;
        this.exceptionEntry(EXC_SYSTICK);
        return true;
      }
      if (levelInterrupts) {
        for (let interruptNumber = 0; interruptNumber < 32; interruptNumber++) {
          if (levelInterrupts & 1 << interruptNumber) {
            if (interruptNumber > MAX_HARDWARE_IRQ) {
              this.pendingInterrupts &= ~(1 << interruptNumber);
            }
            this.exceptionEntry(16 + interruptNumber);
            return true;
          }
        }
      }
    }
    this.interruptsUpdated = false;
    return false;
  }
  readSpecialRegister(sysm) {
    switch (sysm) {
      case SYSM_APSR:
        return this.APSR;
      case SYSM_XPSR:
        return this.xPSR;
      case SYSM_IPSR:
        return this.IPSR;
      case SYSM_PRIMASK:
        return this.PM ? 1 : 0;
      case SYSM_MSP:
        return this.SPmain;
      case SYSM_PSP:
        return this.SPprocess;
      case SYSM_CONTROL:
        return (this.SPSEL === StackPointerBank.SPprocess ? 2 : 0) | (this.nPRIV ? 1 : 0);
      default:
        this.logger.warn(LOG_NAME, `MRS with unimplemented SYSm value: ${sysm}`);
        return 0;
    }
  }
  writeSpecialRegister(sysm, value) {
    switch (sysm) {
      case SYSM_APSR:
        this.APSR = value;
        break;
      case SYSM_XPSR:
        this.xPSR = value;
        break;
      case SYSM_IPSR:
        this.IPSR = value;
        break;
      case SYSM_PRIMASK:
        this.PM = !!(value & 1);
        this.interruptsUpdated = true;
        break;
      case SYSM_MSP:
        this.SPmain = value;
        break;
      case SYSM_PSP:
        this.SPprocess = value;
        break;
      case SYSM_CONTROL:
        this.nPRIV = !!(value & 1);
        if (this.currentMode === ExecutionMode.Mode_Thread) {
          this.switchStack(value & 2 ? StackPointerBank.SPprocess : StackPointerBank.SPmain);
        }
        break;
      default:
        this.logger.warn(LOG_NAME, `MRS with unimplemented SYSm value: ${sysm}`);
        return 0;
    }
  }
  BXWritePC(address) {
    if (this.currentMode == ExecutionMode.Mode_Handler && address >>> 28 == 15) {
      this.exceptionReturn(address & 268435455);
    } else {
      this.PC = address & ~1;
    }
  }
  substractUpdateFlags(minuend, subtrahend) {
    const result = minuend - subtrahend;
    this.N = !!(result & 2147483648);
    this.Z = (result & 4294967295) === 0;
    this.C = minuend >= subtrahend;
    this.V = !!(result & 2147483648) && !(minuend & 2147483648) && !!(subtrahend & 2147483648) || !(result & 2147483648) && !!(minuend & 2147483648) && !(subtrahend & 2147483648);
    return result;
  }
  addUpdateFlags(addend1, addend2) {
    const unsignedSum = addend1 + addend2 >>> 0;
    const signedSum = (addend1 | 0) + (addend2 | 0);
    const result = addend1 + addend2;
    this.N = !!(result & 2147483648);
    this.Z = (result & 4294967295) === 0;
    this.C = result === unsignedSum ? false : true;
    this.V = (result | 0) === signedSum ? false : true;
    return result & 4294967295;
  }
  cyclesIO(addr, write = false) {
    addr = addr >>> 0;
    if (addr >= SIO_START_ADDRESS && addr < SIO_START_ADDRESS + 268435456) {
      return 0;
    }
    if (addr >= APB_START_ADDRESS && addr < APB_START_ADDRESS + 268435456) {
      return write ? 4 : 3;
    }
    return 1;
  }
  executeInstruction() {
    if (this.interruptsUpdated) {
      if (this.checkForInterrupts()) {
        this.waiting = false;
      }
    }
    const opcodePC = this.PC & ~1;
    const opcode = this.readUint16(opcodePC);
    const wideInstruction = opcode >> 12 === 15 || opcode >> 11 === 29;
    const opcode2 = wideInstruction ? this.readUint16(opcodePC + 2) : 0;
    this.PC += 2;
    let deltaCycles = 1;
    if (opcode >> 6 === 261) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      this.registers[Rdn] = this.addUpdateFlags(this.registers[Rm], this.registers[Rdn] + (this.C ? 1 : 0));
    } else if (opcode >> 11 === 21) {
      const imm8 = opcode & 255;
      const Rd = opcode >> 8 & 7;
      this.registers[Rd] = this.SP + (imm8 << 2);
    } else if (opcode >> 7 === 352) {
      const imm32 = (opcode & 127) << 2;
      this.SP += imm32;
    } else if (opcode >> 9 === 14) {
      const imm3 = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.addUpdateFlags(this.registers[Rn], imm3);
    } else if (opcode >> 11 === 6) {
      const imm8 = opcode & 255;
      const Rdn = opcode >> 8 & 7;
      this.registers[Rdn] = this.addUpdateFlags(this.registers[Rdn], imm8);
    } else if (opcode >> 9 === 12) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.addUpdateFlags(this.registers[Rn], this.registers[Rm]);
    } else if (opcode >> 8 === 68) {
      const Rm = opcode >> 3 & 15;
      const Rdn = (opcode & 128) >> 4 | opcode & 7;
      const leftValue = Rdn === pcRegister ? this.PC + 2 : this.registers[Rdn];
      const rightValue = Rm === pcRegister ? this.PC + 2 : this.registers[Rm];
      const result = leftValue + rightValue;
      if (Rdn !== spRegister && Rdn !== pcRegister) {
        this.registers[Rdn] = result;
      } else if (Rdn === pcRegister) {
        this.registers[Rdn] = result & ~1;
        deltaCycles++;
      } else if (Rdn === spRegister) {
        this.registers[Rdn] = result & ~3;
      }
    } else if (opcode >> 11 === 20) {
      const imm8 = opcode & 255;
      const Rd = opcode >> 8 & 7;
      this.registers[Rd] = (opcodePC & 4294967292) + 4 + (imm8 << 2);
    } else if (opcode >> 6 === 256) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const result = this.registers[Rdn] & this.registers[Rm];
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = (result & 4294967295) === 0;
    } else if (opcode >> 11 === 2) {
      const imm5 = opcode >> 6 & 31;
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const input = this.registers[Rm];
      const shiftN = imm5 ? imm5 : 32;
      const result = shiftN < 32 ? input >> shiftN : (input & 2147483648) >> 31;
      this.registers[Rd] = result;
      this.N = !!(result & 2147483648);
      this.Z = (result & 4294967295) === 0;
      this.C = input & 1 << shiftN - 1 ? true : false;
    } else if (opcode >> 6 === 260) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const input = this.registers[Rdn];
      const shiftN = (this.registers[Rm] & 255) < 32 ? this.registers[Rm] & 255 : 32;
      const result = shiftN < 32 ? input >> shiftN : (input & 2147483648) >> 31;
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = (result & 4294967295) === 0;
      this.C = input & 1 << shiftN - 1 ? true : false;
    } else if (opcode >> 12 === 13 && (opcode >> 9 & 7) !== 7) {
      let imm8 = (opcode & 255) << 1;
      const cond = opcode >> 8 & 15;
      if (imm8 & 1 << 8) {
        imm8 = (imm8 & 511) - 512;
      }
      if (this.checkCondition(cond)) {
        this.PC += imm8 + 2;
        deltaCycles++;
      }
    } else if (opcode >> 11 === 28) {
      let imm11 = (opcode & 2047) << 1;
      if (imm11 & 1 << 11) {
        imm11 = (imm11 & 2047) - 2048;
      }
      this.PC += imm11 + 2;
      deltaCycles++;
    } else if (opcode >> 6 === 270) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const result = this.registers[Rdn] &= ~this.registers[Rm];
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
    } else if (opcode >> 8 === 190) {
      const imm8 = opcode & 255;
      this.breakRewind = 2;
      this.rp2040.onBreak(imm8);
    } else if (opcode >> 11 === 30 && opcode2 >> 14 === 3 && (opcode2 >> 12 & 1) == 1) {
      const imm11 = opcode2 & 2047;
      const J2 = opcode2 >> 11 & 1;
      const J1 = opcode2 >> 13 & 1;
      const imm10 = opcode & 1023;
      const S = opcode >> 10 & 1;
      const I1 = 1 - (S ^ J1);
      const I2 = 1 - (S ^ J2);
      const imm32 = (S ? 255 : 0) << 24 | (I1 << 23 | I2 << 22 | imm10 << 12 | imm11 << 1);
      this.LR = this.PC + 2 | 1;
      this.PC += 2 + imm32;
      deltaCycles += 2;
      this.blTaken(this, false);
    } else if (opcode >> 7 === 143 && (opcode & 7) === 0) {
      const Rm = opcode >> 3 & 15;
      this.LR = this.PC | 1;
      this.PC = this.registers[Rm] & ~1;
      deltaCycles++;
      this.blTaken(this, true);
    } else if (opcode >> 7 === 142 && (opcode & 7) === 0) {
      const Rm = opcode >> 3 & 15;
      this.BXWritePC(this.registers[Rm]);
      deltaCycles++;
    } else if (opcode >> 6 === 267) {
      const Rm = opcode >> 3 & 7;
      const Rn = opcode & 7;
      this.addUpdateFlags(this.registers[Rn], this.registers[Rm]);
    } else if (opcode >> 11 === 5) {
      const Rn = opcode >> 8 & 7;
      const imm8 = opcode & 255;
      this.substractUpdateFlags(this.registers[Rn], imm8);
    } else if (opcode >> 6 === 266) {
      const Rm = opcode >> 3 & 7;
      const Rn = opcode & 7;
      this.substractUpdateFlags(this.registers[Rn], this.registers[Rm]);
    } else if (opcode >> 8 === 69) {
      const Rm = opcode >> 3 & 15;
      const Rn = opcode >> 4 & 8 | opcode & 7;
      this.substractUpdateFlags(this.registers[Rn], this.registers[Rm]);
    } else if (opcode === 46706) {
      this.PM = true;
    } else if (opcode === 46690) {
      this.PM = false;
      this.interruptsUpdated = true;
    } else if (opcode === 62399 && (opcode2 & 65520) === 36688) {
      this.PC += 2;
      deltaCycles += 2;
    } else if (opcode === 62399 && (opcode2 & 65520) === 36672) {
      this.PC += 2;
      deltaCycles += 2;
    } else if (opcode >> 6 === 257) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const result = this.registers[Rm] ^ this.registers[Rdn];
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
    } else if (opcode === 62399 && (opcode2 & 65520) === 36704) {
      this.PC += 2;
      deltaCycles += 2;
    } else if (opcode >> 11 === 25) {
      const Rn = opcode >> 8 & 7;
      const registers = opcode & 255;
      let address = this.registers[Rn];
      for (let i = 0; i < 8; i++) {
        if (registers & 1 << i) {
          this.registers[i] = this.readUint32(address);
          address += 4;
          deltaCycles++;
        }
      }
      if (!(registers & 1 << Rn)) {
        this.registers[Rn] = address;
      }
    } else if (opcode >> 11 === 13) {
      const imm5 = (opcode >> 6 & 31) << 2;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rn] + imm5;
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint32(addr);
    } else if (opcode >> 11 === 19) {
      const Rt = opcode >> 8 & 7;
      const imm8 = opcode & 255;
      const addr = this.SP + (imm8 << 2);
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint32(addr);
    } else if (opcode >> 11 === 9) {
      const imm8 = (opcode & 255) << 2;
      const Rt = opcode >> 8 & 7;
      const nextPC = this.PC + 2;
      const addr = (nextPC & 4294967292) + imm8;
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint32(addr);
    } else if (opcode >> 9 === 44) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint32(addr);
    } else if (opcode >> 11 === 15) {
      const imm5 = opcode >> 6 & 31;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rn] + imm5;
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint8(addr);
    } else if (opcode >> 9 === 46) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint8(addr);
    } else if (opcode >> 11 === 17) {
      const imm5 = opcode >> 6 & 31;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rn] + (imm5 << 1);
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint16(addr);
    } else if (opcode >> 9 === 45) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = this.readUint16(addr);
    } else if (opcode >> 9 === 43) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = signExtend8(this.readUint8(addr));
    } else if (opcode >> 9 === 47) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const addr = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(addr);
      this.registers[Rt] = signExtend16(this.readUint16(addr));
    } else if (opcode >> 11 === 0) {
      const imm5 = opcode >> 6 & 31;
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const input = this.registers[Rm];
      const result = input << imm5;
      this.registers[Rd] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
      this.C = imm5 ? !!(input & 1 << 32 - imm5) : this.C;
    } else if (opcode >> 6 === 258) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const input = this.registers[Rdn];
      const shiftCount = this.registers[Rm] & 255;
      const result = shiftCount >= 32 ? 0 : input << shiftCount;
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
      this.C = shiftCount ? !!(input & 1 << 32 - shiftCount) : this.C;
    } else if (opcode >> 11 === 1) {
      const imm5 = opcode >> 6 & 31;
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const input = this.registers[Rm];
      const result = imm5 ? input >>> imm5 : 0;
      this.registers[Rd] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
      this.C = !!(input >>> (imm5 ? imm5 - 1 : 31) & 1);
    } else if (opcode >> 6 === 259) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const shiftAmount = this.registers[Rm] & 255;
      const input = this.registers[Rdn];
      const result = shiftAmount < 32 ? input >>> shiftAmount : 0;
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
      this.C = shiftAmount <= 32 ? !!(input >>> shiftAmount - 1 & 1) : false;
    } else if (opcode >> 8 === 70) {
      const Rm = opcode >> 3 & 15;
      const Rd = opcode >> 4 & 8 | opcode & 7;
      let value = Rm === pcRegister ? this.PC + 2 : this.registers[Rm];
      if (Rd === pcRegister) {
        deltaCycles++;
        value &= ~1;
      } else if (Rd === spRegister) {
        value &= ~3;
      }
      this.registers[Rd] = value;
    } else if (opcode >> 11 === 4) {
      const value = opcode & 255;
      const Rd = opcode >> 8 & 7;
      this.registers[Rd] = value;
      this.N = !!(value & 2147483648);
      this.Z = value === 0;
    } else if (opcode === 62447 && opcode2 >> 12 == 8) {
      const SYSm = opcode2 & 255;
      const Rd = opcode2 >> 8 & 15;
      this.registers[Rd] = this.readSpecialRegister(SYSm);
      this.PC += 2;
      deltaCycles += 2;
    } else if (opcode >> 4 === 3896 && opcode2 >> 8 == 136) {
      const SYSm = opcode2 & 255;
      const Rn = opcode & 15;
      this.writeSpecialRegister(SYSm, this.registers[Rn]);
      this.PC += 2;
      deltaCycles += 2;
    } else if (opcode >> 6 === 269) {
      const Rn = opcode >> 3 & 7;
      const Rdm = opcode & 7;
      const result = Math.imul(this.registers[Rn], this.registers[Rdm]);
      this.registers[Rdm] = result;
      this.N = !!(result & 2147483648);
      this.Z = (result & 4294967295) === 0;
    } else if (opcode >> 6 === 271) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const result = ~this.registers[Rm];
      this.registers[Rd] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
    } else if (opcode >> 6 === 268) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const result = this.registers[Rdn] | this.registers[Rm];
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = (result & 4294967295) === 0;
    } else if (opcode >> 9 === 94) {
      const P = opcode >> 8 & 1;
      let address = this.SP;
      for (let i = 0; i <= 7; i++) {
        if (opcode & 1 << i) {
          this.registers[i] = this.readUint32(address);
          address += 4;
          deltaCycles++;
        }
      }
      if (P) {
        this.SP = address + 4;
        this.BXWritePC(this.readUint32(address));
        deltaCycles += 2;
      } else {
        this.SP = address;
      }
    } else if (opcode >> 9 === 90) {
      let bitCount = 0;
      for (let i = 0; i <= 8; i++) {
        if (opcode & 1 << i) {
          bitCount++;
        }
      }
      let address = this.SP - 4 * bitCount;
      for (let i = 0; i <= 7; i++) {
        if (opcode & 1 << i) {
          this.writeUint32(address, this.registers[i]);
          deltaCycles++;
          address += 4;
        }
      }
      if (opcode & 1 << 8) {
        this.writeUint32(address, this.registers[14]);
      }
      this.SP -= 4 * bitCount;
    } else if (opcode >> 6 === 744) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const input = this.registers[Rm];
      this.registers[Rd] = (input & 255) << 24 | (input >> 8 & 255) << 16 | (input >> 16 & 255) << 8 | input >> 24 & 255;
    } else if (opcode >> 6 === 745) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const input = this.registers[Rm];
      this.registers[Rd] = (input >> 16 & 255) << 24 | (input >> 24 & 255) << 16 | (input & 255) << 8 | input >> 8 & 255;
    } else if (opcode >> 6 === 747) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      const input = this.registers[Rm];
      this.registers[Rd] = signExtend16((input & 255) << 8 | input >> 8 & 255);
    } else if (opcode >> 6 === 263) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      const input = this.registers[Rdn];
      const shift = (this.registers[Rm] & 255) % 32;
      const result = input >>> shift | input << 32 - shift;
      this.registers[Rdn] = result;
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
      this.C = !!(result & 2147483648);
    } else if (opcode >> 6 === 265) {
      const Rn = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.substractUpdateFlags(0, this.registers[Rn]);
    } else if (opcode === 48896) {
    } else if (opcode >> 6 === 262) {
      const Rm = opcode >> 3 & 7;
      const Rdn = opcode & 7;
      this.registers[Rdn] = this.substractUpdateFlags(this.registers[Rdn], this.registers[Rm] + (1 - (this.C ? 1 : 0)));
    } else if (opcode === 48960) {
      this.logger.info(LOG_NAME, "SEV");
    } else if (opcode >> 11 === 24) {
      const Rn = opcode >> 8 & 7;
      const registers = opcode & 255;
      let address = this.registers[Rn];
      for (let i = 0; i < 8; i++) {
        if (registers & 1 << i) {
          this.writeUint32(address, this.registers[i]);
          address += 4;
          deltaCycles++;
        }
      }
      if (!(registers & 1 << Rn)) {
        this.registers[Rn] = address;
      }
    } else if (opcode >> 11 === 12) {
      const imm5 = (opcode >> 6 & 31) << 2;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const address = this.registers[Rn] + imm5;
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint32(address, this.registers[Rt]);
    } else if (opcode >> 11 === 18) {
      const Rt = opcode >> 8 & 7;
      const imm8 = opcode & 255;
      const address = this.SP + (imm8 << 2);
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint32(address, this.registers[Rt]);
    } else if (opcode >> 9 === 40) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const address = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint32(address, this.registers[Rt]);
    } else if (opcode >> 11 === 14) {
      const imm5 = opcode >> 6 & 31;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const address = this.registers[Rn] + imm5;
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint8(address, this.registers[Rt]);
    } else if (opcode >> 9 === 42) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const address = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint8(address, this.registers[Rt]);
    } else if (opcode >> 11 === 16) {
      const imm5 = (opcode >> 6 & 31) << 1;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const address = this.registers[Rn] + imm5;
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint16(address, this.registers[Rt]);
    } else if (opcode >> 9 === 41) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rt = opcode & 7;
      const address = this.registers[Rm] + this.registers[Rn];
      deltaCycles += this.cyclesIO(address, true);
      this.writeUint16(address, this.registers[Rt]);
    } else if (opcode >> 7 === 353) {
      const imm32 = (opcode & 127) << 2;
      this.SP -= imm32;
    } else if (opcode >> 9 === 15) {
      const imm3 = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.substractUpdateFlags(this.registers[Rn], imm3);
    } else if (opcode >> 11 === 7) {
      const imm8 = opcode & 255;
      const Rdn = opcode >> 8 & 7;
      this.registers[Rdn] = this.substractUpdateFlags(this.registers[Rdn], imm8);
    } else if (opcode >> 9 === 13) {
      const Rm = opcode >> 6 & 7;
      const Rn = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.substractUpdateFlags(this.registers[Rn], this.registers[Rm]);
    } else if (opcode >> 8 === 223) {
      this.pendingSVCall = true;
      this.interruptsUpdated = true;
    } else if (opcode >> 6 === 713) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = signExtend8(this.registers[Rm]);
    } else if (opcode >> 6 === 712) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = signExtend16(this.registers[Rm]);
    } else if (opcode >> 6 == 264) {
      const Rm = opcode >> 3 & 7;
      const Rn = opcode & 7;
      const result = this.registers[Rn] & this.registers[Rm];
      this.N = !!(result & 2147483648);
      this.Z = result === 0;
    } else if (opcode >> 8 == 222) {
      const imm8 = opcode & 255;
      this.breakRewind = 2;
      this.rp2040.onBreak(imm8);
    } else if (opcode >> 4 === 3967 && opcode2 >> 12 === 10) {
      const imm4 = opcode & 15;
      const imm12 = opcode2 & 4095;
      this.breakRewind = 4;
      this.rp2040.onBreak(imm4 << 12 | imm12);
      this.PC += 2;
    } else if (opcode >> 6 == 715) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.registers[Rm] & 255;
    } else if (opcode >> 6 == 714) {
      const Rm = opcode >> 3 & 7;
      const Rd = opcode & 7;
      this.registers[Rd] = this.registers[Rm] & 65535;
    } else if (opcode === 48928) {
      deltaCycles++;
      if (this.eventRegistered) {
        this.eventRegistered = false;
      } else {
        this.waiting = true;
      }
    } else if (opcode === 48944) {
      deltaCycles++;
      this.waiting = true;
    } else if (opcode === 48912) {
      this.logger.info(LOG_NAME, "Yield");
    } else {
      this.logger.warn(LOG_NAME, `Warning: Instruction at ${opcodePC.toString(16)} is not implemented yet!`);
      this.logger.warn(LOG_NAME, `Opcode: 0x${opcode.toString(16)} (0x${opcode2.toString(16)})`);
    }
    this.cycles += deltaCycles;
    return deltaCycles;
  }
};

var FIFO = class {
  buffer;
  start = 0;
  used = 0;
  constructor(size) {
    this.buffer = new Uint32Array(size);
  }
  get size() {
    return this.buffer.length;
  }
  get itemCount() {
    return this.used;
  }
  push(value) {
    const { length } = this.buffer;
    const { start, used } = this;
    if (this.used < length) {
      this.buffer[(start + used) % length] = value;
      this.used++;
    }
  }
  pull() {
    const { start, used } = this;
    const { length } = this.buffer;
    if (used) {
      this.start = (start + 1) % length;
      this.used--;
      return this.buffer[start];
    }
    return 0;
  }
  peek() {
    return this.used ? this.buffer[this.start] : 0;
  }
  reset() {
    this.used = 0;
  }
  get empty() {
    return this.used == 0;
  }
  get full() {
    return this.used === this.buffer.length;
  }
  get items() {
    const { start, used, buffer } = this;
    const { length } = buffer;
    const result = [];
    for (let i = 0; i < used; i++) {
      result[i] = buffer[(start + i) % length];
    }
    return result;
  }
};

var ATOMIC_NORMAL = 0;
var ATOMIC_XOR = 1;
var ATOMIC_SET = 2;
var ATOMIC_CLEAR = 3;
function atomicUpdate(currentValue, atomicType, newValue) {
  switch (atomicType) {
    case ATOMIC_XOR:
      return currentValue ^ newValue;
    case ATOMIC_SET:
      return currentValue | newValue;
    case ATOMIC_CLEAR:
      return currentValue & ~newValue;
    default:
      console.warn("Atomic update called with invalid writeType", atomicType);
      return newValue;
  }
}
var BasePeripheral = class {
  rp2040;
  name;
  rawWriteValue;
  constructor(rp2040, name) {
    this.rp2040 = rp2040;
    this.name = name;
    this.rawWriteValue = 0;
  }
  readUint32(offset) {
    this.warn(`Unimplemented peripheral read from 0x${offset.toString(16)}`);
    if (offset > 4096) {
      this.warn("Unimplemented read from peripheral in the atomic operation region");
    }
    return 4294967295;
  }
  writeUint32(offset, value) {
    this.warn(`Unimplemented peripheral write to 0x${offset.toString(16)}: 0x${value.toString(16)}`);
  }
  writeUint32Atomic(offset, value, atomicType) {
    this.rawWriteValue = value;
    const newValue = atomicType != ATOMIC_NORMAL ? atomicUpdate(this.readUint32(offset), atomicType, value) : value;
    this.writeUint32(offset, newValue);
  }
  debug(msg) {
    this.rp2040.logger.debug(this.name, msg);
  }
  info(msg) {
    this.rp2040.logger.info(this.name, msg);
  }
  warn(msg) {
    this.rp2040.logger.warn(this.name, msg);
  }
  error(msg) {
    this.rp2040.logger.error(this.name, msg);
  }
};
var UnimplementedPeripheral = class extends BasePeripheral {
};

var DREQChannel = /* @__PURE__ */ function(DREQChannel2) {
  DREQChannel2[DREQChannel2["DREQ_PIO0_TX0"] = 0] = "DREQ_PIO0_TX0";
  DREQChannel2[DREQChannel2["DREQ_PIO0_TX1"] = 1] = "DREQ_PIO0_TX1";
  DREQChannel2[DREQChannel2["DREQ_PIO0_TX2"] = 2] = "DREQ_PIO0_TX2";
  DREQChannel2[DREQChannel2["DREQ_PIO0_TX3"] = 3] = "DREQ_PIO0_TX3";
  DREQChannel2[DREQChannel2["DREQ_PIO0_RX0"] = 4] = "DREQ_PIO0_RX0";
  DREQChannel2[DREQChannel2["DREQ_PIO0_RX1"] = 5] = "DREQ_PIO0_RX1";
  DREQChannel2[DREQChannel2["DREQ_PIO0_RX2"] = 6] = "DREQ_PIO0_RX2";
  DREQChannel2[DREQChannel2["DREQ_PIO0_RX3"] = 7] = "DREQ_PIO0_RX3";
  DREQChannel2[DREQChannel2["DREQ_PIO1_TX0"] = 8] = "DREQ_PIO1_TX0";
  DREQChannel2[DREQChannel2["DREQ_PIO1_TX1"] = 9] = "DREQ_PIO1_TX1";
  DREQChannel2[DREQChannel2["DREQ_PIO1_TX2"] = 10] = "DREQ_PIO1_TX2";
  DREQChannel2[DREQChannel2["DREQ_PIO1_TX3"] = 11] = "DREQ_PIO1_TX3";
  DREQChannel2[DREQChannel2["DREQ_PIO1_RX0"] = 12] = "DREQ_PIO1_RX0";
  DREQChannel2[DREQChannel2["DREQ_PIO1_RX1"] = 13] = "DREQ_PIO1_RX1";
  DREQChannel2[DREQChannel2["DREQ_PIO1_RX2"] = 14] = "DREQ_PIO1_RX2";
  DREQChannel2[DREQChannel2["DREQ_PIO1_RX3"] = 15] = "DREQ_PIO1_RX3";
  DREQChannel2[DREQChannel2["DREQ_SPI0_TX"] = 16] = "DREQ_SPI0_TX";
  DREQChannel2[DREQChannel2["DREQ_SPI0_RX"] = 17] = "DREQ_SPI0_RX";
  DREQChannel2[DREQChannel2["DREQ_SPI1_TX"] = 18] = "DREQ_SPI1_TX";
  DREQChannel2[DREQChannel2["DREQ_SPI1_RX"] = 19] = "DREQ_SPI1_RX";
  DREQChannel2[DREQChannel2["DREQ_UART0_TX"] = 20] = "DREQ_UART0_TX";
  DREQChannel2[DREQChannel2["DREQ_UART0_RX"] = 21] = "DREQ_UART0_RX";
  DREQChannel2[DREQChannel2["DREQ_UART1_TX"] = 22] = "DREQ_UART1_TX";
  DREQChannel2[DREQChannel2["DREQ_UART1_RX"] = 23] = "DREQ_UART1_RX";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP0"] = 24] = "DREQ_PWM_WRAP0";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP1"] = 25] = "DREQ_PWM_WRAP1";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP2"] = 26] = "DREQ_PWM_WRAP2";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP3"] = 27] = "DREQ_PWM_WRAP3";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP4"] = 28] = "DREQ_PWM_WRAP4";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP5"] = 29] = "DREQ_PWM_WRAP5";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP6"] = 30] = "DREQ_PWM_WRAP6";
  DREQChannel2[DREQChannel2["DREQ_PWM_WRAP7"] = 31] = "DREQ_PWM_WRAP7";
  DREQChannel2[DREQChannel2["DREQ_I2C0_TX"] = 32] = "DREQ_I2C0_TX";
  DREQChannel2[DREQChannel2["DREQ_I2C0_RX"] = 33] = "DREQ_I2C0_RX";
  DREQChannel2[DREQChannel2["DREQ_I2C1_TX"] = 34] = "DREQ_I2C1_TX";
  DREQChannel2[DREQChannel2["DREQ_I2C1_RX"] = 35] = "DREQ_I2C1_RX";
  DREQChannel2[DREQChannel2["DREQ_ADC"] = 36] = "DREQ_ADC";
  DREQChannel2[DREQChannel2["DREQ_XIP_STREAM"] = 37] = "DREQ_XIP_STREAM";
  DREQChannel2[DREQChannel2["DREQ_XIP_SSITX"] = 38] = "DREQ_XIP_SSITX";
  DREQChannel2[DREQChannel2["DREQ_XIP_SSIRX"] = 39] = "DREQ_XIP_SSIRX";
  DREQChannel2[DREQChannel2["DREQ_MAX"] = 40] = "DREQ_MAX";
  return DREQChannel2;
}({});
var TREQ = /* @__PURE__ */ function(TREQ2) {
  TREQ2[TREQ2["Timer0"] = 59] = "Timer0";
  TREQ2[TREQ2["Timer1"] = 60] = "Timer1";
  TREQ2[TREQ2["Timer2"] = 61] = "Timer2";
  TREQ2[TREQ2["Timer3"] = 62] = "Timer3";
  TREQ2[TREQ2["Permanent"] = 63] = "Permanent";
  return TREQ2;
}(TREQ || {});
var CHn_READ_ADDR = 0;
var CHn_WRITE_ADDR = 4;
var CHn_TRANS_COUNT = 8;
var CHn_CTRL_TRIG = 12;
var CHn_AL1_CTRL = 16;
var CHn_AL1_READ_ADDR = 20;
var CHn_AL1_WRITE_ADDR = 24;
var CHn_AL1_TRANS_COUNT_TRIG = 28;
var CHn_AL2_CTRL = 32;
var CHn_AL2_TRANS_COUNT = 36;
var CHn_AL2_READ_ADDR = 40;
var CHn_AL2_WRITE_ADDR_TRIG = 44;
var CHn_AL3_CTRL = 48;
var CHn_AL3_WRITE_ADDR = 52;
var CHn_AL3_TRANS_COUNT = 56;
var CHn_AL3_READ_ADDR_TRIG = 60;
var CHn_DBG_CTDREQ = 2048;
var CHn_DBG_TCR = 2052;
var CHANNEL_REGISTERS_SIZE = 12 * 64;
var CHANNEL_REGISTERS_MASK = 2111;
var INTR = 1024;
var INTE0 = 1028;
var INTF0 = 1032;
var INTS0 = 1036;
var INTE1 = 1044;
var INTF1 = 1048;
var INTS1 = 1052;
var TIMER0 = 1056;
var TIMER1 = 1060;
var TIMER2 = 1064;
var TIMER3 = 1068;
var MULTI_CHAN_TRIGGER = 1072;
var CHAN_ABORT = 1092;
var N_CHANNELS = 1096;
var AHB_ERROR = 1 << 31;
var READ_ERROR = 1 << 30;
var WRITE_ERROR = 1 << 29;
var BUSY = 1 << 24;
var SNIFF_EN = 1 << 23;
var BSWAP = 1 << 22;
var IRQ_QUIET = 1 << 21;
var TREQ_SEL_MASK = 63;
var TREQ_SEL_SHIFT = 15;
var CHAIN_TO_MASK = 15;
var CHAIN_TO_SHIFT = 11;
var RING_SEL = 1 << 10;
var RING_SIZE_MASK = 15;
var RING_SIZE_SHIFT = 6;
var INCR_WRITE = 1 << 5;
var INCR_READ = 1 << 4;
var DATA_SIZE_MASK = 3;
var DATA_SIZE_SHIFT = 2;
var HIGH_PRIORITY = 1 << 1;
var EN = 1 << 0;
var CHn_CTRL_TRIG_WRITE_MASK = 16777215;
var CHn_CTRL_TRIG_WC_MASK = READ_ERROR | WRITE_ERROR;
var RPDMAChannel = class {
  dma;
  rp2040;
  index;
  ctrl;
  readAddr;
  writeAddr;
  transCount;
  dreqCounter;
  transCountReload;
  treqValue;
  dataSize;
  chainTo;
  ringMask;
  transferFn;
  transferAlarm;
  constructor(dma, rp2040, index) {
    this.dma = dma;
    this.rp2040 = rp2040;
    this.index = index;
    this.ctrl = 0;
    this.readAddr = 0;
    this.writeAddr = 0;
    this.transCount = 0;
    this.dreqCounter = 0;
    this.transCountReload = 0;
    this.treqValue = 0;
    this.dataSize = 1;
    this.chainTo = 0;
    this.ringMask = 0;
    this.transferFn = () => 0;
    this.transfer8 = () => {
      const { rp2040: rp20402 } = this;
      rp20402.writeUint8(this.writeAddr, rp20402.readUint8(this.readAddr));
    };
    this.transfer16 = () => {
      const { rp2040: rp20402 } = this;
      rp20402.writeUint16(this.writeAddr, rp20402.readUint16(this.readAddr));
    };
    this.transferSwap16 = () => {
      const { rp2040: rp20402 } = this;
      const input = rp20402.readUint16(this.readAddr);
      rp20402.writeUint16(this.writeAddr, (input & 255) << 8 | input >> 8);
    };
    this.transfer32 = () => {
      const { rp2040: rp20402 } = this;
      rp20402.writeUint32(this.writeAddr, rp20402.readUint32(this.readAddr));
    };
    this.transferSwap32 = () => {
      const { rp2040: rp20402 } = this;
      const input = rp20402.readUint32(this.readAddr);
      rp20402.writeUint32(this.writeAddr, (input & 255) << 24 | (input & 65280) << 8 | (input & 16711680) >> 8 | input >> 24 & 255);
    };
    this.transfer = () => {
      const { ctrl, dataSize, ringMask } = this;
      this.transferFn();
      if (ctrl & INCR_READ) {
        if (ringMask && !(ctrl & RING_SEL)) {
          this.readAddr = this.readAddr & ~ringMask | this.readAddr + dataSize & ringMask;
        } else {
          this.readAddr += dataSize;
        }
      }
      if (ctrl & INCR_WRITE) {
        if (ringMask && ctrl & RING_SEL) {
          this.writeAddr = this.writeAddr & ~ringMask | this.writeAddr + dataSize & ringMask;
        } else {
          this.writeAddr += dataSize;
        }
      }
      this.transCount--;
      if (this.transCount > 0) {
        this.scheduleTransfer();
      } else {
        this.ctrl &= ~BUSY;
        if (!(this.ctrl & IRQ_QUIET)) {
          this.dma.intRaw |= 1 << this.index;
          this.dma.checkInterrupts();
        }
        if (this.chainTo !== this.index) {
          this.dma.channels[this.chainTo]?.start();
        }
      }
    };
    this.transferAlarm = rp2040.clock.createAlarm(this.transfer);
    this.reset();
  }
  start() {
    if (!(this.ctrl & EN) || this.ctrl & BUSY) {
      return;
    }
    this.ctrl |= BUSY;
    this.transCount = this.transCountReload;
    if (this.transCount) {
      this.scheduleTransfer();
    }
  }
  get treq() {
    return this.treqValue;
  }
  get active() {
    return this.ctrl & EN && this.ctrl & BUSY;
  }
  transfer8;
  transfer16;
  transferSwap16;
  transfer32;
  transferSwap32;
  transfer;
  scheduleTransfer() {
    if (this.dma.dreq[this.treqValue] || this.treqValue === TREQ.Permanent) {
      this.transferAlarm.schedule(0);
    } else {
      const delay = this.dma.getTimer(this.treqValue);
      if (delay) {
        this.transferAlarm.schedule(delay * 1e3);
      }
    }
  }
  abort() {
    this.ctrl &= ~BUSY;
    this.transferAlarm.cancel();
  }
  readUint32(offset) {
    switch (offset) {
      case CHn_READ_ADDR:
      case CHn_AL1_READ_ADDR:
      case CHn_AL2_READ_ADDR:
      case CHn_AL3_READ_ADDR_TRIG:
        return this.readAddr;
      case CHn_WRITE_ADDR:
      case CHn_AL1_WRITE_ADDR:
      case CHn_AL2_WRITE_ADDR_TRIG:
      case CHn_AL3_WRITE_ADDR:
        return this.writeAddr;
      case CHn_TRANS_COUNT:
      case CHn_AL1_TRANS_COUNT_TRIG:
      case CHn_AL2_TRANS_COUNT:
      case CHn_AL3_TRANS_COUNT:
        return this.transCount;
      case CHn_CTRL_TRIG:
      case CHn_AL1_CTRL:
      case CHn_AL2_CTRL:
      case CHn_AL3_CTRL:
        return this.ctrl;
      case CHn_DBG_CTDREQ:
        return this.dreqCounter;
      case CHn_DBG_TCR:
        return this.transCountReload;
    }
    return 0;
  }
  writeUint32(offset, value) {
    switch (offset) {
      case CHn_READ_ADDR:
      case CHn_AL1_READ_ADDR:
      case CHn_AL2_READ_ADDR:
      case CHn_AL3_READ_ADDR_TRIG:
        this.readAddr = value;
        break;
      case CHn_WRITE_ADDR:
      case CHn_AL1_WRITE_ADDR:
      case CHn_AL2_WRITE_ADDR_TRIG:
      case CHn_AL3_WRITE_ADDR:
        this.writeAddr = value;
        break;
      case CHn_TRANS_COUNT:
      case CHn_AL1_TRANS_COUNT_TRIG:
      case CHn_AL2_TRANS_COUNT:
      case CHn_AL3_TRANS_COUNT:
        this.transCountReload = value;
        break;
      case CHn_CTRL_TRIG:
      case CHn_AL1_CTRL:
      case CHn_AL2_CTRL:
      case CHn_AL3_CTRL: {
        this.ctrl = this.ctrl & ~CHn_CTRL_TRIG_WRITE_MASK | value & CHn_CTRL_TRIG_WRITE_MASK;
        this.ctrl &= ~(value & CHn_CTRL_TRIG_WC_MASK);
        this.treqValue = this.ctrl >> TREQ_SEL_SHIFT & TREQ_SEL_MASK;
        this.chainTo = this.ctrl >> CHAIN_TO_SHIFT & CHAIN_TO_MASK;
        const ringSize = this.ctrl >> RING_SIZE_SHIFT & RING_SIZE_MASK;
        this.ringMask = ringSize ? (1 << ringSize) - 1 : 0;
        switch (this.ctrl >> DATA_SIZE_SHIFT & DATA_SIZE_MASK) {
          case 1:
            this.dataSize = 2;
            this.transferFn = this.ctrl & BSWAP ? this.transferSwap16 : this.transfer16;
            break;
          case 2:
            this.dataSize = 4;
            this.transferFn = this.ctrl & BSWAP ? this.transferSwap32 : this.transfer32;
            break;
          case 0:
          default:
            this.transferFn = this.transfer8;
            this.dataSize = 1;
        }
        if (this.ctrl & EN && this.ctrl & BUSY) {
          this.scheduleTransfer();
        }
        if (!(this.ctrl & EN)) {
          this.transferAlarm.cancel();
        }
        break;
      }
      case CHn_DBG_CTDREQ:
        this.dreqCounter = 0;
        break;
    }
    if (offset === CHn_AL3_READ_ADDR_TRIG || offset === CHn_AL2_WRITE_ADDR_TRIG || offset === CHn_AL1_TRANS_COUNT_TRIG || offset === CHn_CTRL_TRIG) {
      if (value) {
        this.start();
      } else if (this.ctrl & IRQ_QUIET) {
        this.dma.intRaw |= 1 << this.index;
        this.dma.checkInterrupts();
      }
    }
  }
  reset() {
    this.writeUint32(CHn_CTRL_TRIG, this.index << CHAIN_TO_SHIFT);
  }
};
var RPDMA = class extends BasePeripheral {
  channels = [
    new RPDMAChannel(this, this.rp2040, 0),
    new RPDMAChannel(this, this.rp2040, 1),
    new RPDMAChannel(this, this.rp2040, 2),
    new RPDMAChannel(this, this.rp2040, 3),
    new RPDMAChannel(this, this.rp2040, 4),
    new RPDMAChannel(this, this.rp2040, 5),
    new RPDMAChannel(this, this.rp2040, 6),
    new RPDMAChannel(this, this.rp2040, 7),
    new RPDMAChannel(this, this.rp2040, 8),
    new RPDMAChannel(this, this.rp2040, 9),
    new RPDMAChannel(this, this.rp2040, 10),
    new RPDMAChannel(this, this.rp2040, 11)
  ];
  intRaw = 0;
  intEnable0 = 0;
  intForce0 = 0;
  intEnable1 = 0;
  intForce1 = 0;
  timer0 = 0;
  timer1 = 0;
  timer2 = 0;
  timer3 = 0;
  dreq = Array(DREQChannel.DREQ_MAX);
  get intStatus0() {
    return this.intRaw & this.intEnable0 | this.intForce0;
  }
  get intStatus1() {
    return this.intRaw & this.intEnable1 | this.intForce1;
  }
  readUint32(offset) {
    if ((offset & 2047) < CHANNEL_REGISTERS_SIZE) {
      const channelIndex = (offset & 2047) >> 6;
      return this.channels[channelIndex].readUint32(offset & CHANNEL_REGISTERS_MASK);
    }
    switch (offset) {
      case TIMER0:
        return this.timer0;
      case TIMER1:
        return this.timer1;
      case TIMER2:
        return this.timer2;
      case TIMER3:
        return this.timer3;
      case INTR:
        return this.intRaw;
      case INTE0:
        return this.intEnable0;
      case INTF0:
        return this.intForce0;
      case INTS0:
        return this.intStatus0;
      case INTE1:
        return this.intEnable1;
      case INTF1:
        return this.intForce1;
      case INTS1:
        return this.intStatus1;
      case N_CHANNELS:
        return this.channels.length;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    if ((offset & 2047) < CHANNEL_REGISTERS_SIZE) {
      const channelIndex = (offset & 2047) >> 6;
      this.channels[channelIndex].writeUint32(offset & CHANNEL_REGISTERS_MASK, value);
      return;
    }
    switch (offset) {
      case TIMER0:
        this.timer0 = value;
        return;
      case TIMER1:
        this.timer1 = value;
        return;
      case TIMER2:
        this.timer2 = value;
        return;
      case TIMER3:
        this.timer3 = value;
        return;
      case INTR:
      case INTS0:
      case INTS1:
        this.intRaw &= ~this.rawWriteValue;
        this.checkInterrupts();
        return;
      case INTE0:
        this.intEnable0 = value & 65535;
        this.checkInterrupts();
        return;
      case INTF0:
        this.intForce0 = value & 65535;
        this.checkInterrupts();
        return;
      case INTE1:
        this.intEnable1 = value & 65535;
        this.checkInterrupts();
        return;
      case INTF1:
        this.intForce1 = value & 65535;
        this.checkInterrupts();
        return;
      case MULTI_CHAN_TRIGGER:
        for (const chan of this.channels) {
          if (value & 1 << chan.index) {
            chan.start();
          }
        }
        return;
      case CHAN_ABORT:
        for (const chan of this.channels) {
          if (value & 1 << chan.index) {
            chan.abort();
          }
        }
        return;
      default:
        super.writeUint32(offset, value);
    }
  }
  setDREQ(dreqChannel) {
    const { dreq } = this;
    if (!dreq[dreqChannel]) {
      dreq[dreqChannel] = true;
      for (const channel of this.channels) {
        if (channel.treq === dreqChannel && channel.active) {
          channel.scheduleTransfer();
        }
      }
    }
  }
  clearDREQ(dreqChannel) {
    this.dreq[dreqChannel] = false;
  }
  /**
   * Returns the number of microseconds for a cycle of the given DMA timer, or 0 if the timer is disabled.
   */
  getTimer(treq) {
    let dividend = 0, divisor = 1;
    switch (treq) {
      case TREQ.Permanent:
        dividend = 1;
        divisor = 1;
        break;
      case TREQ.Timer0:
        dividend = this.timer0 >>> 16;
        divisor = this.timer0 & 65535;
        break;
      case TREQ.Timer1:
        dividend = this.timer1 >>> 16;
        divisor = this.timer1 & 65535;
        break;
      case TREQ.Timer2:
        dividend = this.timer2 >>> 16;
        divisor = this.timer2 & 65535;
        break;
      case TREQ.Timer3:
        dividend = this.timer3 >>> 36;
        divisor = this.timer3 & 65535;
        break;
    }
    if (divisor === 0) {
      return 0;
    }
    return dividend / divisor * 1e6 / this.rp2040.clkSys;
  }
  checkInterrupts() {
    this.rp2040.setInterrupt(IRQ.DMA_IRQ0, !!this.intStatus0);
    this.rp2040.setInterrupt(IRQ.DMA_IRQ1, !!this.intStatus1);
  }
};

var CTRL = 0;
var FSTAT = 4;
var FDEBUG = 8;
var FLEVEL = 12;
var IRQ2 = 48;
var IRQ_FORCE = 52;
var INPUT_SYNC_BYPASS = 56;
var DBG_PADOUT = 60;
var DBG_PADOE = 64;
var DBG_CFGINFO = 68;
var INSTR_MEM0 = 72;
var INSTR_MEM31 = 196;
var INTR2 = 296;
var IRQ0_INTE = 300;
var IRQ0_INTF = 304;
var IRQ0_INTS = 308;
var IRQ1_INTE = 312;
var IRQ1_INTF = 316;
var IRQ1_INTS = 320;
var TXF0 = 16;
var TXF1 = 20;
var TXF2 = 24;
var TXF3 = 28;
var RXF0 = 32;
var RXF1 = 36;
var RXF2 = 40;
var RXF3 = 44;
var SM0_CLKDIV = 200;
var SM0_EXECCTRL = 204;
var SM0_SHIFTCTRL = 208;
var SM0_ADDR = 212;
var SM0_INSTR = 216;
var SM0_PINCTRL = 220;
var SM1_CLKDIV = 224;
var SM1_PINCTRL = 244;
var SM2_CLKDIV = 248;
var SM2_PINCTRL = 268;
var SM3_CLKDIV = 272;
var SM3_PINCTRL = 292;
var FSTAT_TXEMPTY = 1 << 24;
var FSTAT_TXFULL = 1 << 16;
var FSTAT_RXEMPTY = 1 << 8;
var FSTAT_RXFULL = 1 << 0;
var FDEBUG_TXSTALL = 1 << 24;
var FDEBUG_TXOVER = 1 << 16;
var FDEBUG_RXUNDER = 1 << 8;
var FDEBUG_RXSTALL = 1 << 0;
var SHIFTCTRL_AUTOPUSH = 1 << 16;
var SHIFTCTRL_AUTOPULL = 1 << 17;
var SHIFTCTRL_IN_SHIFTDIR = 1 << 18;
var SHIFTCTRL_OUT_SHIFTDIR = 1 << 19;
var EXECCTRL_STATUS_SEL = 1 << 4;
var EXECCTRL_SIDE_PINDIR = 1 << 29;
var EXECCTRL_SIDE_EN = 1 << 30;
var EXECCTRL_EXEC_STALLED = 1 << 31;
var WaitType = /* @__PURE__ */ function(WaitType2) {
  WaitType2[WaitType2["None"] = 0] = "None";
  WaitType2[WaitType2["Pin"] = 1] = "Pin";
  WaitType2[WaitType2["rxFIFO"] = 2] = "rxFIFO";
  WaitType2[WaitType2["txFIFO"] = 3] = "txFIFO";
  WaitType2[WaitType2["IRQ"] = 4] = "IRQ";
  WaitType2[WaitType2["Out"] = 5] = "Out";
  return WaitType2;
}({});
function bitReverse(x) {
  x = (x & 1431655765) << 1 | (x & 2863311530) >>> 1;
  x = (x & 858993459) << 2 | (x & 3435973836) >>> 2;
  x = (x & 252645135) << 4 | (x & 4042322160) >>> 4;
  x = (x & 16711935) << 8 | (x & 4278255360) >>> 8;
  x = (x & 65535) << 16 | (x & 4294901760) >>> 16;
  return x >>> 0;
}
function irqIndex(irq, machineIndex) {
  const rel = !!(irq & 16);
  return rel ? irq & 4 | (irq & 3) + machineIndex & 3 : irq & 7;
}
var dreqRx0 = [
  DREQChannel.DREQ_PIO0_RX0,
  DREQChannel.DREQ_PIO0_RX1,
  DREQChannel.DREQ_PIO0_RX2,
  DREQChannel.DREQ_PIO0_RX3
];
var dreqTx0 = [
  DREQChannel.DREQ_PIO0_TX0,
  DREQChannel.DREQ_PIO0_TX1,
  DREQChannel.DREQ_PIO0_TX2,
  DREQChannel.DREQ_PIO0_TX3
];
var dreqRx1 = [
  DREQChannel.DREQ_PIO1_RX0,
  DREQChannel.DREQ_PIO1_RX1,
  DREQChannel.DREQ_PIO1_RX2,
  DREQChannel.DREQ_PIO1_RX3
];
var dreqTx1 = [
  DREQChannel.DREQ_PIO1_TX0,
  DREQChannel.DREQ_PIO1_TX1,
  DREQChannel.DREQ_PIO1_TX2,
  DREQChannel.DREQ_PIO1_TX3
];
var StateMachine = class {
  rp2040;
  pio;
  index;
  enabled;
  // State machine registers
  x;
  y;
  pc;
  inputShiftReg;
  inputShiftCount;
  outputShiftReg;
  outputShiftCount;
  cycles;
  execOpcode;
  execValid;
  updatePC;
  clockDivInt;
  clockDivFrac;
  execCtrl;
  shiftCtrl;
  pinCtrl;
  rxFIFO;
  txFIFO;
  outPinValues;
  outPinDirection;
  waiting;
  waitType;
  waitIndex;
  waitPolarity;
  waitDelay;
  dreqRx;
  dreqTx;
  constructor(rp2040, pio, index) {
    this.rp2040 = rp2040;
    this.pio = pio;
    this.index = index;
    this.enabled = false;
    this.x = 0;
    this.y = 0;
    this.pc = 0;
    this.inputShiftReg = 0;
    this.inputShiftCount = 0;
    this.outputShiftReg = 0;
    this.outputShiftCount = 0;
    this.cycles = 0;
    this.execOpcode = 0;
    this.execValid = false;
    this.updatePC = true;
    this.clockDivInt = 1;
    this.clockDivFrac = 0;
    this.execCtrl = 31 << 12;
    this.shiftCtrl = 3 << 18;
    this.pinCtrl = 5 << 26;
    this.rxFIFO = new FIFO(4);
    this.txFIFO = new FIFO(4);
    this.outPinValues = 0;
    this.outPinDirection = 0;
    this.waiting = false;
    this.waitType = WaitType.None;
    this.waitIndex = 0;
    this.waitPolarity = false;
    this.waitDelay = -1;
    this.dreqRx = this.pio.dreqRx[this.index];
    this.dreqTx = this.pio.dreqTx[this.index];
    this.updateDMARx();
    this.updateDMATx();
  }
  updateDMATx() {
    if (this.txFIFO.full) {
      this.rp2040.dma.clearDREQ(this.dreqTx);
    } else {
      this.rp2040.dma.setDREQ(this.dreqTx);
    }
  }
  updateDMARx() {
    if (this.rxFIFO.empty) {
      this.rp2040.dma.clearDREQ(this.dreqRx);
    } else {
      this.rp2040.dma.setDREQ(this.dreqRx);
    }
  }
  writeFIFO(value) {
    if (this.txFIFO.full) {
      this.pio.fdebug |= FDEBUG_TXOVER << this.index;
      return;
    }
    this.txFIFO.push(value);
    this.pio.txStall &= ~(FDEBUG_TXSTALL << this.index);
    this.updateDMATx();
    this.checkWait();
    if (this.txFIFO.full) {
      this.pio.checkInterrupts();
    }
  }
  readFIFO() {
    if (this.rxFIFO.empty) {
      this.pio.fdebug |= FDEBUG_RXUNDER << this.index;
      return 0;
    }
    const result = this.rxFIFO.pull();
    this.pio.rxStall &= ~(FDEBUG_RXSTALL << this.index);
    this.updateDMARx();
    this.checkWait();
    if (this.rxFIFO.empty) {
      this.pio.checkInterrupts();
    }
    return result;
  }
  get status() {
    const statusN = this.execCtrl & 15;
    if (this.execCtrl & EXECCTRL_STATUS_SEL) {
      return this.rxFIFO.itemCount < statusN ? 4294967295 : 0;
    } else {
      return this.txFIFO.itemCount < statusN ? 4294967295 : 0;
    }
  }
  jmpCondition(condition) {
    switch (condition) {
      // (no condition): Always
      case 0:
        return true;
      // !X: scratch X zero
      case 1:
        return this.x === 0;
      // X--: scratch X non-zero, post-decrement
      case 2: {
        const oldX = this.x;
        this.x = this.x - 1 >>> 0;
        return oldX !== 0;
      }
      // !Y: scratch Y zero
      case 3:
        return this.y === 0;
      // Y--: scratch Y non-zero, post-decrement
      case 4: {
        const oldY = this.y;
        this.y = this.y - 1 >>> 0;
        return oldY !== 0;
      }
      // X!=Y: scratch X not equal scratch Y
      case 5:
        return this.x >>> 0 !== this.y >>> 0;
      // PIN: branch on input pin
      case 6: {
        const { gpio } = this.rp2040;
        const { jmpPin } = this;
        return jmpPin < gpio.length ? gpio[jmpPin].inputValue : false;
      }
      // !OSRE: output shift register not empty
      case 7:
        return this.outputShiftCount < this.pullThreshold;
    }
    this.pio.error(`jmpCondition with unsupported condition: ${condition}`);
    return false;
  }
  get inPins() {
    const { gpioValues } = this.rp2040;
    const { inBase } = this;
    return inBase ? gpioValues << 32 - inBase | gpioValues >>> inBase : gpioValues;
  }
  inSourceValue(source) {
    switch (source) {
      // PINS
      case 0:
        return this.inPins;
      // X (scratch register X)
      case 1:
        return this.x;
      // Y (scratch register Y)
      case 2:
        return this.y;
      // NULL (all zeroes)
      case 3:
        return 0;
      // Reserved
      case 4:
        return 0;
      // Reserved for IN, STATUS for MOV
      case 5:
        return this.status;
      // ISR
      case 6:
        return this.inputShiftReg;
      // OSR
      case 7:
        return this.outputShiftReg;
    }
    this.pio.error(`inSourceValue with unsupported source: ${source}`);
    return 0;
  }
  writeOutValue(destination, value, bitCount) {
    switch (destination) {
      // PINS
      case 0:
        this.setOutPins(value);
        break;
      // X (scratch register X)
      case 1:
        this.x = value;
        break;
      // Y (scratch register Y)
      case 2:
        this.y = value;
        break;
      // NULL (discard data)
      case 3:
        break;
      // PINDIRS
      case 4:
        this.setOutPinDirs(value);
        break;
      // PC
      case 5:
        this.pc = value & 31;
        this.updatePC = false;
        break;
      // ISR (also sets ISR shift counter to Bit count)
      case 6:
        this.inputShiftReg = value;
        this.inputShiftCount = bitCount;
        break;
      // EXEC (Execute OSR shift data as instruction)
      case 7:
        this.execOpcode = value;
        this.execValid = true;
        break;
    }
  }
  get pushThreshold() {
    const value = this.shiftCtrl >> 20 & 31;
    return value ? value : 32;
  }
  get pullThreshold() {
    const value = this.shiftCtrl >> 25 & 31;
    return value ? value : 32;
  }
  get sidesetCount() {
    return this.pinCtrl >> 29 & 7;
  }
  get setCount() {
    return this.pinCtrl >> 26 & 7;
  }
  get outCount() {
    return this.pinCtrl >> 20 & 63;
  }
  get inBase() {
    return this.pinCtrl >> 15 & 31;
  }
  get sidesetBase() {
    return this.pinCtrl >> 10 & 31;
  }
  get setBase() {
    return this.pinCtrl >> 5 & 31;
  }
  get outBase() {
    return this.pinCtrl >> 0 & 31;
  }
  get jmpPin() {
    return this.execCtrl >> 24 & 31;
  }
  get wrapTop() {
    return this.execCtrl >> 12 & 31;
  }
  get wrapBottom() {
    return this.execCtrl >> 7 & 31;
  }
  setOutPinDirs(value) {
    this.outPinDirection = value;
    this.pio.pinDirectionsChanged(value, this.outBase, this.outCount);
  }
  setOutPins(value) {
    this.outPinValues = value;
    this.pio.pinValuesChanged(value, this.outBase, this.outCount);
  }
  outInstruction(arg) {
    const bitCount = arg & 31;
    const destination = arg >> 5;
    if (bitCount === 0) {
      this.writeOutValue(destination, this.outputShiftReg, 32);
      this.outputShiftCount = 32;
    } else {
      if (this.shiftCtrl & SHIFTCTRL_OUT_SHIFTDIR) {
        const value = this.outputShiftReg & (1 << bitCount) - 1;
        this.outputShiftReg >>>= bitCount;
        this.writeOutValue(destination, value, bitCount);
      } else {
        const value = this.outputShiftReg >>> 32 - bitCount;
        this.outputShiftReg <<= bitCount;
        this.writeOutValue(destination, value, bitCount);
      }
      this.outputShiftCount += bitCount;
      if (this.outputShiftCount > 32) {
        this.outputShiftCount = 32;
      }
    }
  }
  executeInstruction(opcode) {
    const arg = opcode & 255;
    switch (opcode >>> 13) {
      /* JMP */
      case 0:
        if (this.jmpCondition(arg >> 5)) {
          this.pc = arg & 31;
          this.updatePC = false;
        }
        break;
      /* WAIT */
      case 1: {
        const polarity = !!(arg & 128);
        const source = arg >> 5 & 3;
        const index = arg & 31;
        switch (source) {
          // GPIO:
          case 0:
            this.wait(WaitType.Pin, polarity, index);
            break;
          // PIN:
          case 1:
            this.wait(WaitType.Pin, polarity, (index + this.inBase) % 32);
            break;
          // IRQ:
          case 2:
            this.wait(WaitType.IRQ, polarity, irqIndex(index, this.index));
            break;
        }
        break;
      }
      /* IN */
      case 2: {
        const bitCount = arg & 31;
        let sourceValue = this.inSourceValue(arg >> 5);
        if (bitCount == 0) {
          this.inputShiftReg = sourceValue;
          this.inputShiftCount = 32;
        } else {
          sourceValue &= (1 << bitCount) - 1;
          if (this.shiftCtrl & SHIFTCTRL_IN_SHIFTDIR) {
            this.inputShiftReg >>>= bitCount;
            this.inputShiftReg |= sourceValue << 32 - bitCount;
          } else {
            this.inputShiftReg <<= bitCount;
            this.inputShiftReg |= sourceValue;
          }
          this.inputShiftCount += bitCount;
          if (this.inputShiftCount > 32) {
            this.inputShiftCount = 32;
          }
        }
        if (this.shiftCtrl & SHIFTCTRL_AUTOPUSH && this.inputShiftCount >= this.pushThreshold) {
          if (!this.rxFIFO.full) {
            this.rxFIFO.push(this.inputShiftReg);
            this.updateDMARx();
            this.pio.checkInterrupts();
          } else {
            this.pio.rxStall |= FDEBUG_RXSTALL << this.index;
            this.pio.fdebug |= this.pio.rxStall;
            this.wait(WaitType.rxFIFO, false, this.inputShiftReg);
          }
          this.inputShiftCount = 0;
          this.inputShiftReg = 0;
        }
        break;
      }
      /* OUT */
      case 3: {
        if (this.shiftCtrl & SHIFTCTRL_AUTOPULL && this.outputShiftCount >= this.pullThreshold) {
          this.outputShiftCount = 0;
          if (!this.txFIFO.empty) {
            this.outputShiftReg = this.txFIFO.pull();
            this.updateDMATx();
            this.pio.checkInterrupts();
          } else {
            this.pio.txStall |= FDEBUG_TXSTALL << this.index;
            this.pio.fdebug |= this.pio.txStall;
            this.wait(WaitType.Out, false, arg);
          }
        }
        if (!this.waiting) {
          this.outInstruction(arg);
        }
        break;
      }
      /* PUSH/PULL */
      case 4: {
        const block = !!(arg & 1 << 5);
        const ifFullOrEmpty = !!(arg & 1 << 6);
        if (arg & 31) {
          break;
        }
        if (arg & 128) {
          if (ifFullOrEmpty && this.shiftCtrl & SHIFTCTRL_AUTOPULL && this.outputShiftCount < this.pullThreshold) {
            break;
          }
          if (!this.txFIFO.empty) {
            this.outputShiftReg = this.txFIFO.pull();
            this.updateDMATx();
            this.pio.checkInterrupts();
          } else {
            this.pio.txStall |= FDEBUG_TXSTALL << this.index;
            this.pio.fdebug |= this.pio.txStall;
            if (block) {
              this.wait(WaitType.txFIFO, false, 0);
            } else {
              this.outputShiftReg = this.x;
            }
          }
          this.outputShiftCount = 0;
        } else {
          if (ifFullOrEmpty && this.shiftCtrl & SHIFTCTRL_AUTOPUSH && this.inputShiftCount < this.pushThreshold) {
            break;
          }
          if (!this.rxFIFO.full) {
            this.rxFIFO.push(this.inputShiftReg);
            this.updateDMARx();
            this.pio.checkInterrupts();
          } else {
            this.pio.rxStall |= FDEBUG_RXSTALL << this.index;
            this.pio.fdebug |= this.pio.rxStall;
            if (block) {
              this.wait(WaitType.rxFIFO, false, this.inputShiftReg);
            }
          }
          this.inputShiftReg = 0;
          this.inputShiftCount = 0;
        }
        break;
      }
      /* MOV */
      case 5: {
        const source = arg & 7;
        const op = arg >> 3 & 3;
        const destination = arg >> 5 & 7;
        const value = this.inSourceValue(source);
        const transformedValue = this.transformMovValue(value, op) >>> 0;
        this.setMovDestination(destination, transformedValue);
        break;
      }
      /* IRQ */
      case 6: {
        if (arg & 128) {
          break;
        }
        const clear = !!(arg & 64);
        const wait = !!(arg & 32);
        const irq = irqIndex(arg & 31, this.index);
        if (clear) {
          this.pio.irq &= ~(1 << irq);
          this.pio.irqUpdated();
        } else {
          this.pio.irq |= 1 << irq;
          this.pio.irqUpdated();
          if (wait) {
            this.wait(WaitType.IRQ, false, irq);
          }
        }
        break;
      }
      /* SET */
      case 7: {
        const data = arg & 31;
        const destination = arg >> 5;
        switch (destination) {
          case 0:
            this.setSetPins(data);
            break;
          case 1:
            this.x = data;
            break;
          case 2:
            this.y = data;
            break;
          case 4:
            this.setSetPinDirs(data);
            break;
        }
        break;
      }
    }
    this.cycles++;
    const { sidesetCount, execCtrl } = this;
    const delaySideset = opcode >> 8 & 31;
    const sideEn = !!(execCtrl & EXECCTRL_SIDE_EN);
    const delay = delaySideset & (1 << 5 - sidesetCount) - 1;
    if (sidesetCount && (!sideEn || delaySideset & 16)) {
      const sideset = delaySideset >> 5 - sidesetCount;
      this.setSideset(sideset, sideEn ? sidesetCount - 1 : sidesetCount);
    }
    if (this.execValid) {
      this.execValid = false;
      this.executeInstruction(this.execOpcode);
    } else if (this.waiting) {
      if (this.waitDelay < 0) {
        this.waitDelay = delay;
      }
      this.checkWait();
    } else {
      this.cycles += delay;
    }
  }
  wait(type, polarity, index) {
    this.waiting = true;
    this.waitType = type;
    this.waitPolarity = polarity;
    this.waitIndex = index;
    this.waitDelay = -1;
    this.updatePC = false;
  }
  nextPC() {
    if (this.pc === this.wrapTop) {
      this.pc = this.wrapBottom;
    } else {
      this.pc = this.pc + 1 & 31;
    }
  }
  step() {
    if (this.waiting) {
      this.checkWait();
      if (this.waiting) {
        return;
      }
    }
    this.updatePC = true;
    this.executeInstruction(this.pio.instructions[this.pc]);
    if (this.updatePC) {
      this.nextPC();
    }
  }
  setSetPinDirs(value) {
    this.pio.pinDirectionsChanged(value, this.setBase, this.setCount);
  }
  setSetPins(value) {
    this.pio.pinValuesChanged(value, this.setBase, this.setCount);
  }
  setSideset(value, count) {
    if (this.execCtrl & EXECCTRL_SIDE_PINDIR) {
      this.pio.pinDirectionsChanged(value, this.sidesetBase, count);
    } else {
      this.pio.pinValuesChanged(value, this.sidesetBase, count);
    }
  }
  transformMovValue(value, op) {
    switch (op) {
      case 0:
        return value;
      case 1:
        return ~value;
      case 2:
        return bitReverse(value);
      case 3:
      default:
        return value;
    }
  }
  setMovDestination(destination, value) {
    switch (destination) {
      // PINS
      case 0:
        this.setOutPins(value);
        break;
      // X (scratch register X)
      case 1:
        this.x = value;
        break;
      // Y (scratch register Y)
      case 2:
        this.y = value;
        break;
      // reserved (discard data)
      case 3:
        break;
      // EXEC
      case 4:
        this.execOpcode = value;
        this.execValid = true;
        break;
      // PC
      case 5:
        this.pc = value & 31;
        this.updatePC = false;
        break;
      // ISR (Input shift counter is reset to 0 by this operation, i.e. empty)
      case 6:
        this.inputShiftReg = value;
        this.inputShiftCount = 0;
        break;
      // OSR (Output shift counter is reset to 0 by this operation, i.e. full)
      case 7:
        this.outputShiftReg = value;
        this.outputShiftCount = 0;
        break;
    }
  }
  readUint32(offset) {
    switch (offset + SM0_CLKDIV) {
      case SM0_CLKDIV:
        return this.clockDivInt << 16 | this.clockDivFrac << 8;
      case SM0_EXECCTRL:
        return this.execCtrl;
      case SM0_SHIFTCTRL:
        return this.shiftCtrl;
      case SM0_ADDR:
        return this.pc;
      case SM0_INSTR:
        return this.pio.instructions[this.pc];
      case SM0_PINCTRL:
        return this.pinCtrl;
    }
    this.pio.error(`Read from invalid state machine register: ${offset}`);
    return 0;
  }
  writeUint32(offset, value) {
    switch (offset + SM0_CLKDIV) {
      case SM0_CLKDIV:
        this.clockDivFrac = value >>> 8 & 255;
        this.clockDivInt = value >>> 16;
        break;
      case SM0_EXECCTRL:
        this.execCtrl = (value & 2147483647 | this.execCtrl & 2147483648) >>> 0;
        break;
      case SM0_SHIFTCTRL:
        this.shiftCtrl = value;
        break;
      case SM0_ADDR:
        break;
      case SM0_INSTR:
        this.executeInstruction(value & 65535);
        if (this.waiting) {
          this.execCtrl |= EXECCTRL_EXEC_STALLED;
        }
        break;
      case SM0_PINCTRL:
        this.pinCtrl = value;
        break;
      default:
        this.pio.error(`Write to invalid state machine register: ${offset}`);
    }
  }
  get fifoStat() {
    const result = (this.txFIFO.empty ? FSTAT_TXEMPTY : 0) | (this.txFIFO.full ? FSTAT_TXFULL : 0) | (this.rxFIFO.empty ? FSTAT_RXEMPTY : 0) | (this.rxFIFO.full ? FSTAT_RXFULL : 0);
    return result << this.index;
  }
  restart() {
    this.cycles = 0;
    this.inputShiftCount = 0;
    this.outputShiftCount = 32;
    this.inputShiftReg = 0;
    this.waiting = false;
  }
  clkDivRestart() {
    this.pio.warn("clkDivRestart not implemented");
  }
  checkWait() {
    if (!this.waiting) {
      return;
    }
    switch (this.waitType) {
      case WaitType.IRQ: {
        const irqValue = !!(this.pio.irq & 1 << this.waitIndex);
        if (irqValue === this.waitPolarity) {
          this.waiting = false;
          if (irqValue) {
            this.pio.irq &= ~(1 << this.waitIndex);
          }
        }
        break;
      }
      case WaitType.Pin: {
        if (this.waitIndex < this.rp2040.gpio.length && this.rp2040.gpio[this.waitIndex].inputValue === this.waitPolarity) {
          this.waiting = false;
        }
        break;
      }
      case WaitType.rxFIFO: {
        if (!this.rxFIFO.full) {
          this.rxFIFO.push(this.waitIndex);
          this.waiting = false;
          this.updateDMARx();
          this.pio.checkInterrupts();
        }
        break;
      }
      case WaitType.txFIFO: {
        if (!this.txFIFO.empty) {
          this.outputShiftReg = this.txFIFO.pull();
          this.waiting = false;
          this.updateDMATx();
          this.pio.checkInterrupts();
        }
        break;
      }
      case WaitType.Out: {
        if (!this.txFIFO.empty) {
          this.outputShiftReg = this.txFIFO.pull();
          this.outInstruction(this.waitIndex);
          this.waiting = false;
          this.updateDMATx();
          this.pio.checkInterrupts();
        }
        break;
      }
    }
    if (!this.waiting) {
      this.nextPC();
      this.cycles += this.waitDelay;
      this.execCtrl &= ~EXECCTRL_EXEC_STALLED;
    }
  }
};
var RPPIO = class extends BasePeripheral {
  firstIrq;
  index;
  instructions;
  dreqRx;
  dreqTx;
  machines;
  stopped;
  fdebug;
  txStall;
  rxStall;
  inputSyncBypass;
  irq;
  pinValues;
  pinDirections;
  oldPinValues;
  oldPinDirections;
  runTimer;
  irq0IntEnable;
  irq0IntForce;
  irq1IntEnable;
  irq1IntForce;
  constructor(rp2040, name, firstIrq, index) {
    super(rp2040, name), this.firstIrq = firstIrq, this.index = index, this.instructions = new Uint32Array(32), this.dreqRx = this.index ? dreqRx1 : dreqRx0, this.dreqTx = this.index ? dreqTx1 : dreqTx0, this.machines = [
      new StateMachine(this.rp2040, this, 0),
      new StateMachine(this.rp2040, this, 1),
      new StateMachine(this.rp2040, this, 2),
      new StateMachine(this.rp2040, this, 3)
    ], this.stopped = true, this.fdebug = 0, this.txStall = 0, this.rxStall = 0, this.inputSyncBypass = 0, this.irq = 0, this.pinValues = 0, this.pinDirections = 0, this.oldPinValues = 0, this.oldPinDirections = 0, this.runTimer = null, this.irq0IntEnable = 0, this.irq0IntForce = 0, this.irq1IntEnable = 0, this.irq1IntForce = 0;
  }
  get intRaw() {
    return (this.irq & 15) << 8 | (!this.machines[3].txFIFO.full ? 128 : 0) | (!this.machines[2].txFIFO.full ? 64 : 0) | (!this.machines[1].txFIFO.full ? 32 : 0) | (!this.machines[0].txFIFO.full ? 16 : 0) | (!this.machines[3].rxFIFO.empty ? 8 : 0) | (!this.machines[2].rxFIFO.empty ? 4 : 0) | (!this.machines[1].rxFIFO.empty ? 2 : 0) | (!this.machines[0].rxFIFO.empty ? 1 : 0);
  }
  get irq0IntStatus() {
    return this.intRaw & this.irq0IntEnable | this.irq0IntForce;
  }
  get irq1IntStatus() {
    return this.intRaw & this.irq1IntEnable | this.irq1IntForce;
  }
  readUint32(offset) {
    if (offset >= SM0_CLKDIV && offset <= SM0_PINCTRL) {
      return this.machines[0].readUint32(offset - SM0_CLKDIV);
    }
    if (offset >= SM1_CLKDIV && offset <= SM1_PINCTRL) {
      return this.machines[1].readUint32(offset - SM1_CLKDIV);
    }
    if (offset >= SM2_CLKDIV && offset <= SM2_PINCTRL) {
      return this.machines[2].readUint32(offset - SM2_CLKDIV);
    }
    if (offset >= SM3_CLKDIV && offset <= SM3_PINCTRL) {
      return this.machines[3].readUint32(offset - SM3_CLKDIV);
    }
    switch (offset) {
      case CTRL:
        return (this.machines[0].enabled ? 1 << 0 : 0) | (this.machines[1].enabled ? 1 << 1 : 0) | (this.machines[2].enabled ? 1 << 2 : 0) | (this.machines[3].enabled ? 1 << 3 : 0);
      case FSTAT:
        return this.machines[0].fifoStat | this.machines[1].fifoStat | this.machines[2].fifoStat | this.machines[3].fifoStat;
      case FDEBUG:
        return this.fdebug;
      case FLEVEL:
        return this.machines[0].txFIFO.itemCount & 15 | (this.machines[0].rxFIFO.itemCount & 15) << 4 | (this.machines[1].txFIFO.itemCount & 15) << 8 | (this.machines[1].rxFIFO.itemCount & 15) << 12 | (this.machines[2].txFIFO.itemCount & 15) << 16 | (this.machines[2].rxFIFO.itemCount & 15) << 20 | (this.machines[3].txFIFO.itemCount & 15) << 24 | (this.machines[3].rxFIFO.itemCount & 15) << 28;
      case RXF0:
        return this.machines[0].readFIFO();
      case RXF1:
        return this.machines[1].readFIFO();
      case RXF2:
        return this.machines[2].readFIFO();
      case RXF3:
        return this.machines[3].readFIFO();
      case IRQ2:
        return this.irq;
      case IRQ_FORCE:
        return 0;
      case INPUT_SYNC_BYPASS:
        return this.inputSyncBypass;
      case DBG_PADOUT:
        return this.pinValues;
      case DBG_PADOE:
        return this.pinDirections;
      case DBG_CFGINFO:
        return 2098180;
      case INTR2:
        return this.intRaw;
      case IRQ0_INTE:
        return this.irq0IntEnable;
      case IRQ0_INTF:
        return this.irq0IntForce;
      case IRQ0_INTS:
        return this.irq0IntStatus;
      case IRQ1_INTE:
        return this.irq1IntEnable;
      case IRQ1_INTF:
        return this.irq1IntForce;
      case IRQ1_INTS:
        return this.irq1IntStatus;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    if (offset >= INSTR_MEM0 && offset <= INSTR_MEM31) {
      const index = offset - INSTR_MEM0 >> 2;
      this.instructions[index] = value & 65535;
      return;
    }
    if (offset >= SM0_CLKDIV && offset <= SM0_PINCTRL) {
      this.machines[0].writeUint32(offset - SM0_CLKDIV, value);
      return;
    }
    if (offset >= SM1_CLKDIV && offset <= SM1_PINCTRL) {
      this.machines[1].writeUint32(offset - SM1_CLKDIV, value);
      return;
    }
    if (offset >= SM2_CLKDIV && offset <= SM2_PINCTRL) {
      this.machines[2].writeUint32(offset - SM2_CLKDIV, value);
      return;
    }
    if (offset >= SM3_CLKDIV && offset <= SM3_PINCTRL) {
      this.machines[3].writeUint32(offset - SM3_CLKDIV, value);
      return;
    }
    switch (offset) {
      case CTRL: {
        for (let index = 0; index < 4; index++) {
          this.machines[index].enabled = value & 1 << index ? true : false;
          if (value & 1 << 4 + index) {
            this.machines[index].restart();
          }
          if (value & 1 << 8 + index) {
            this.machines[index].clkDivRestart();
          }
        }
        const shouldRun = value & 15;
        if (this.stopped && shouldRun) {
          this.stopped = false;
          this.run();
        }
        if (!shouldRun) {
          this.stopped = true;
        }
        break;
      }
      case FDEBUG:
        this.fdebug &= ~this.rawWriteValue;
        this.fdebug |= this.txStall | this.rxStall;
        break;
      case TXF0:
        this.machines[0].writeFIFO(value);
        break;
      case TXF1:
        this.machines[1].writeFIFO(value);
        break;
      case TXF2:
        this.machines[2].writeFIFO(value);
        break;
      case TXF3:
        this.machines[3].writeFIFO(value);
        break;
      case IRQ2:
        this.irq &= ~this.rawWriteValue;
        this.irqUpdated();
        break;
      case INPUT_SYNC_BYPASS:
        this.inputSyncBypass = value;
        break;
      case IRQ_FORCE:
        this.irq |= value;
        this.irqUpdated();
        break;
      case IRQ0_INTE:
        this.irq0IntEnable = value & 4095;
        this.checkInterrupts();
        break;
      case IRQ0_INTF:
        this.irq0IntForce = value & 4095;
        this.checkInterrupts();
        break;
      case IRQ1_INTE:
        this.irq1IntEnable = value & 4095;
        this.checkInterrupts();
        break;
      case IRQ1_INTF:
        this.irq1IntForce = value & 4095;
        this.checkInterrupts();
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
  pinValuesChanged(value, firstPin, count) {
    const mask = count > 31 ? 4294967295 : (1 << count) - 1 << firstPin;
    const newValue = (this.pinValues & ~mask | value << firstPin & mask) & 1073741823;
    this.pinValues = newValue;
  }
  pinDirectionsChanged(value, firstPin, count) {
    const mask = count > 31 ? 4294967295 : (1 << count) - 1 << firstPin;
    const newValue = (this.pinDirections & ~mask | value << firstPin & mask) & 1073741823;
    this.pinDirections = newValue;
  }
  checkInterrupts() {
    const { firstIrq } = this;
    this.rp2040.setInterrupt(firstIrq, !!this.irq0IntStatus);
    this.rp2040.setInterrupt(firstIrq + 1, !!this.irq1IntStatus);
  }
  irqUpdated() {
    for (const machine of this.machines) {
      machine.checkWait();
    }
    this.checkInterrupts();
  }
  checkChangedPins() {
    const changedPins = this.oldPinDirections ^ this.pinDirections | this.oldPinValues ^ this.pinValues;
    if (changedPins) {
      this.oldPinDirections = this.pinDirections;
      this.oldPinValues = this.pinValues;
      const { gpio } = this.rp2040;
      for (let gpioIndex = 0; gpioIndex < gpio.length; gpioIndex++) {
        if (changedPins & 1 << gpioIndex) {
          gpio[gpioIndex].checkForUpdates();
        }
      }
    }
  }
  step() {
    for (const machine of this.machines) {
      machine.step();
    }
    this.checkChangedPins();
  }
  run() {
    for (let i = 0; i < 1e3 && !this.stopped; i++) {
      this.step();
    }
    if (!this.stopped) {
      this.runTimer = setTimeout(() => this.run(), 0);
    }
  }
  stop() {
    for (const machine of this.machines) {
      machine.enabled = false;
    }
    this.stopped = true;
    if (this.runTimer) {
      clearTimeout(this.runTimer);
      this.runTimer = null;
    }
  }
};

var GPIOPinState = /* @__PURE__ */ function(GPIOPinState2) {
  GPIOPinState2[GPIOPinState2["Low"] = 0] = "Low";
  GPIOPinState2[GPIOPinState2["High"] = 1] = "High";
  GPIOPinState2[GPIOPinState2["Input"] = 2] = "Input";
  GPIOPinState2[GPIOPinState2["InputPullUp"] = 3] = "InputPullUp";
  GPIOPinState2[GPIOPinState2["InputPullDown"] = 4] = "InputPullDown";
  GPIOPinState2[GPIOPinState2["InputBusKeeper"] = 5] = "InputBusKeeper";
  return GPIOPinState2;
}({});
var FUNCTION_PWM = 4;
var FUNCTION_SIO = 5;
var FUNCTION_PIO0 = 6;
var FUNCTION_PIO1 = 7;
function applyOverride(value, overrideType) {
  switch (overrideType) {
    case 0:
      return value;
    case 1:
      return !value;
    case 2:
      return false;
    case 3:
      return true;
  }
  console.error("applyOverride received invalid override type", overrideType);
  return value;
}
var IRQ_EDGE_HIGH = 1 << 3;
var IRQ_EDGE_LOW = 1 << 2;
var IRQ_LEVEL_HIGH = 1 << 1;
var IRQ_LEVEL_LOW = 1 << 0;
var GPIOPin = class {
  rp2040;
  index;
  name;
  rawInputValue;
  lastValue;
  ctrl;
  padValue;
  irqEnableMask;
  irqForceMask;
  irqStatus;
  listeners;
  constructor(rp2040, index, name = index.toString()) {
    this.rp2040 = rp2040;
    this.index = index;
    this.name = name;
    this.rawInputValue = false;
    this.lastValue = this.value;
    this.ctrl = 31;
    this.padValue = 54;
    this.irqEnableMask = 0;
    this.irqForceMask = 0;
    this.irqStatus = 0;
    this.listeners = /* @__PURE__ */ new Set();
  }
  get rawInterrupt() {
    return !!(this.irqStatus & this.irqEnableMask | this.irqForceMask);
  }
  get isSlewFast() {
    return !!(this.padValue & 1);
  }
  get schmittEnabled() {
    return !!(this.padValue & 2);
  }
  get pulldownEnabled() {
    return !!(this.padValue & 4);
  }
  get pullupEnabled() {
    return !!(this.padValue & 8);
  }
  get driveStrength() {
    return this.padValue >> 4 & 3;
  }
  get inputEnable() {
    return !!(this.padValue & 64);
  }
  get outputDisable() {
    return !!(this.padValue & 128);
  }
  get functionSelect() {
    return this.ctrl & 31;
  }
  get outputOverride() {
    return this.ctrl >> 8 & 3;
  }
  get outputEnableOverride() {
    return this.ctrl >> 12 & 3;
  }
  get inputOverride() {
    return this.ctrl >> 16 & 3;
  }
  get irqOverride() {
    return this.ctrl >> 28 & 3;
  }
  get rawOutputEnable() {
    const { index, rp2040, functionSelect } = this;
    const bitmask = 1 << index;
    switch (functionSelect) {
      case FUNCTION_PWM:
        return !!(rp2040.pwm.gpioDirection & bitmask);
      case FUNCTION_SIO:
        return !!(rp2040.sio.gpioOutputEnable & bitmask);
      case FUNCTION_PIO0:
        return !!(rp2040.pio[0].pinDirections & bitmask);
      case FUNCTION_PIO1:
        return !!(rp2040.pio[1].pinDirections & bitmask);
      default:
        return false;
    }
  }
  get rawOutputValue() {
    const { index, rp2040, functionSelect } = this;
    const bitmask = 1 << index;
    switch (functionSelect) {
      case FUNCTION_PWM:
        return !!(rp2040.pwm.gpioValue & bitmask);
      case FUNCTION_SIO:
        return !!(rp2040.sio.gpioValue & bitmask);
      case FUNCTION_PIO0:
        return !!(rp2040.pio[0].pinValues & bitmask);
      case FUNCTION_PIO1:
        return !!(rp2040.pio[1].pinValues & bitmask);
      default:
        return false;
    }
  }
  get inputValue() {
    return applyOverride(this.rawInputValue && this.inputEnable, this.inputOverride);
  }
  get irqValue() {
    return applyOverride(this.rawInterrupt, this.irqOverride);
  }
  get outputEnable() {
    return applyOverride(this.rawOutputEnable, this.outputEnableOverride);
  }
  get outputValue() {
    return applyOverride(this.rawOutputValue, this.outputOverride);
  }
  /**
   * Returns the STATUS register value for the pin, as outlined in section 2.19.6 of the datasheet
   */
  get status() {
    const irqToProc = this.irqValue ? 1 << 26 : 0;
    const irqFromPad = this.rawInterrupt ? 1 << 24 : 0;
    const inToPeri = this.inputValue ? 1 << 19 : 0;
    const inFromPad = this.rawInputValue ? 1 << 17 : 0;
    const oeToPad = this.outputEnable ? 1 << 13 : 0;
    const oeFromPeri = this.rawOutputEnable ? 1 << 12 : 0;
    const outToPad = this.outputValue ? 1 << 9 : 0;
    const outFromPeri = this.rawOutputValue ? 1 << 8 : 0;
    return irqToProc | irqFromPad | inToPeri | inFromPad | oeToPad | oeFromPeri | outToPad | outFromPeri;
  }
  get value() {
    if (this.outputEnable) {
      return this.outputValue ? GPIOPinState.High : GPIOPinState.Low;
    } else {
      if (this.pulldownEnabled && this.pullupEnabled) {
        return GPIOPinState.InputBusKeeper;
      } else if (this.pulldownEnabled) {
        return GPIOPinState.InputPullDown;
      } else if (this.pullupEnabled) {
        return GPIOPinState.InputPullUp;
      }
      return GPIOPinState.Input;
    }
  }
  setInputValue(value) {
    this.rawInputValue = value;
    const prevIrqValue = this.irqValue;
    if (value && this.inputEnable) {
      this.irqStatus |= IRQ_EDGE_HIGH | IRQ_LEVEL_HIGH;
      this.irqStatus &= ~IRQ_LEVEL_LOW;
    } else {
      this.irqStatus |= IRQ_EDGE_LOW | IRQ_LEVEL_LOW;
      this.irqStatus &= ~IRQ_LEVEL_HIGH;
    }
    if (this.irqValue !== prevIrqValue) {
      this.rp2040.updateIOInterrupt();
    }
    if (this.functionSelect === FUNCTION_PWM) {
      this.rp2040.pwm.gpioOnInput(this.index);
    }
    for (const pio of this.rp2040.pio) {
      for (const machine of pio.machines) {
        if (machine.enabled && machine.waiting && machine.waitType === WaitType.Pin && machine.waitIndex === this.index) {
          machine.checkWait();
        }
      }
    }
  }
  checkForUpdates() {
    const { lastValue, value } = this;
    if (value !== lastValue) {
      this.lastValue = value;
      for (const listener of this.listeners) {
        listener(value, lastValue);
      }
    }
  }
  refreshInput() {
    this.setInputValue(this.rawInputValue);
  }
  updateIRQValue(value) {
    if (value & IRQ_EDGE_LOW && this.irqStatus & IRQ_EDGE_LOW) {
      this.irqStatus &= ~IRQ_EDGE_LOW;
      this.rp2040.updateIOInterrupt();
    }
    if (value & IRQ_EDGE_HIGH && this.irqStatus & IRQ_EDGE_HIGH) {
      this.irqStatus &= ~IRQ_EDGE_HIGH;
      this.rp2040.updateIOInterrupt();
    }
  }
  addListener(callback) {
    this.listeners.add(callback);
    return () => this.listeners.delete(callback);
  }
};

var CS = 0;
var RESULT = 4;
var FCS = 8;
var FIFO_REG = 12;
var DIV = 16;
var INTR3 = 20;
var INTE = 24;
var INTF = 28;
var INTS = 32;
var CS_RROBIN_MASK = 31;
var CS_RROBIN_SHIFT = 16;
var CS_AINSEL_MASK = 7;
var CS_AINSEL_SHIFT = 12;
var CS_ERR_STICKY = 1 << 10;
var CS_ERR = 1 << 9;
var CS_READY = 1 << 8;
var CS_START_MANY = 1 << 3;
var CS_START_ONE = 1 << 2;
var CS_TS_EN = 1 << 1;
var CS_EN = 1 << 0;
var CS_WRITE_MASK = CS_RROBIN_MASK << CS_RROBIN_SHIFT | CS_AINSEL_MASK << CS_AINSEL_SHIFT | CS_START_MANY | CS_START_ONE | CS_TS_EN | CS_EN;
var FCS_THRES_MASK = 15;
var FCS_THRESH_SHIFT = 24;
var FCS_LEVEL_MASK = 15;
var FCS_LEVEL_SHIFT = 16;
var FCS_OVER = 1 << 11;
var FCS_UNDER = 1 << 10;
var FCS_FULL = 1 << 9;
var FCS_EMPTY = 1 << 8;
var FCS_DREQ_EN = 1 << 3;
var FCS_ERR = 1 << 2;
var FCS_SHIFT = 1 << 1;
var FCS_EN = 1 << 0;
var FCS_WRITE_MASK = FCS_THRES_MASK << FCS_THRESH_SHIFT | FCS_DREQ_EN | FCS_ERR | FCS_SHIFT | FCS_EN;
var FIFO_ERR = 1 << 15;
var DIV_INT_MASK = 65535;
var DIV_INT_SHIFT = 8;
var DIV_FRAC_MASK = 255;
var DIV_FRAC_SHIFT = 0;
var FIFO_INT = 1 << 0;
var RPADC = class extends BasePeripheral {
  /* Number of ADC channels */
  numChannels = 5;
  /** ADC resolution (in bits) */
  resolution = 12;
  /** Time to read a single sample, in microseconds */
  sampleTime = 2;
  /**
   * ADC Channel values. Channels 0...3 are connected to GPIO 26...29, and channel 4 is connected to the built-in
   * temperature sensor: T=27-(ADC_voltage-0.706)/0.001721.
   *
   * Changing the values will change the ADC reading, unless you override onADCRead() with a custom implementation.
   */
  channelValues = [
    0,
    0,
    0,
    0,
    0
  ];
  /**
   * Invoked whenever the emulated code performs an ADC read.
   *
   * The default implementation reads the result from the `channelValues` array, and then calls
   * completeADCRead() after `sampleTime` microseconds.
   *
   * If you override the default implementation, make sure to call `completeADCRead()` after
   * `sampleTime` microseconds (or else the ADC read will never complete).
   */
  onADCRead = (channel) => {
    this.currentChannel = channel;
    this.sampleAlarm.schedule(this.sampleTime * 1e3);
  };
  fifo = new FIFO(4);
  dreq = DREQChannel.DREQ_ADC;
  // Registers
  cs = 0;
  fcs = 0;
  clockDiv = 0;
  intEnable = 0;
  intForce = 0;
  result = 0;
  // Status
  busy = false;
  err = false;
  currentChannel = 0;
  /** Used to simulate ADC sample time */
  sampleAlarm;
  /** For scheduling multi-shot ADC capture */
  multiShotAlarm;
  get temperatueEnable() {
    return this.cs & CS_TS_EN;
  }
  get enabled() {
    return this.cs & CS_EN;
  }
  get divider() {
    return 1 + (this.clockDiv >> DIV_INT_SHIFT & DIV_INT_MASK) + (this.clockDiv >> DIV_FRAC_SHIFT & DIV_FRAC_MASK) / 256;
  }
  get intRaw() {
    const thres = this.fcs >> FCS_THRESH_SHIFT & FCS_THRES_MASK;
    return this.fifo.itemCount >= thres ? FIFO_INT : 0;
  }
  get intStatus() {
    return this.intRaw & this.intEnable | this.intForce;
  }
  get activeChannel() {
    return this.cs >> CS_AINSEL_SHIFT & CS_AINSEL_MASK;
  }
  set activeChannel(channel) {
    this.cs &= ~(CS_AINSEL_MASK << CS_AINSEL_SHIFT);
    this.cs |= (channel & CS_AINSEL_SHIFT) << CS_AINSEL_SHIFT;
  }
  constructor(rp2040, name) {
    super(rp2040, name);
    this.sampleAlarm = this.rp2040.clock.createAlarm(() => this.completeADCRead(this.channelValues[this.currentChannel], false));
    this.multiShotAlarm = this.rp2040.clock.createAlarm(() => {
      if (this.cs & CS_START_MANY) {
        this.startADCRead();
      }
    });
  }
  checkInterrupts() {
    this.rp2040.setInterrupt(IRQ.ADC_FIFO, !!this.intStatus);
  }
  startADCRead() {
    this.busy = true;
    this.onADCRead(this.activeChannel);
  }
  updateDMA() {
    if (this.fcs & FCS_DREQ_EN) {
      const thres = this.fcs >> FCS_THRESH_SHIFT & FCS_THRES_MASK;
      if (this.fifo.itemCount >= thres) {
        this.rp2040.dma.setDREQ(this.dreq);
      } else {
        this.rp2040.dma.clearDREQ(this.dreq);
      }
    }
  }
  completeADCRead(value, error) {
    this.busy = false;
    this.result = value;
    if (error) {
      this.cs |= CS_ERR_STICKY | CS_ERR;
    } else {
      this.cs &= ~CS_ERR;
    }
    if (this.fcs & FCS_EN) {
      if (this.fifo.full) {
        this.fcs |= FCS_OVER;
      } else {
        value &= 4095;
        if (this.fcs & FCS_SHIFT) {
          value >>= 4;
        }
        if (error && this.fcs & FCS_ERR) {
          value |= FIFO_ERR;
        }
        this.fifo.push(value);
        this.updateDMA();
        this.checkInterrupts();
      }
    }
    const round = this.cs >> CS_RROBIN_SHIFT & CS_RROBIN_MASK;
    if (round) {
      let channel = this.activeChannel + 1;
      while (!(round & 1 << channel)) {
        channel = (channel + 1) % this.numChannels;
      }
      this.activeChannel = channel;
    }
    if (this.cs & CS_START_MANY) {
      const clockMHZ = 48;
      const sampleTicks = clockMHZ * this.sampleTime;
      if (this.divider > sampleTicks) {
        const micros = (this.divider - sampleTicks) / clockMHZ;
        this.multiShotAlarm.schedule(micros * 1e3);
      } else {
        this.startADCRead();
      }
    }
  }
  readUint32(offset) {
    switch (offset) {
      case CS:
        return this.cs | (this.err ? CS_ERR : 0) | (this.busy ? 0 : CS_READY);
      case RESULT:
        return this.result;
      case FCS:
        return this.fcs | (this.fifo.itemCount & FCS_LEVEL_MASK) << FCS_LEVEL_SHIFT | (this.fifo.full ? FCS_FULL : 0) | (this.fifo.empty ? FCS_EMPTY : 0);
      case FIFO_REG:
        if (this.fifo.empty) {
          this.fcs |= FCS_UNDER;
          return 0;
        } else {
          const value = this.fifo.pull();
          this.updateDMA();
          return value;
        }
      case DIV:
        return this.clockDiv;
      case INTR3:
        return this.intRaw;
      case INTE:
        return this.intEnable;
      case INTF:
        return this.intForce;
      case INTS:
        return this.intStatus;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case CS:
        this.fcs &= ~(value & CS_ERR_STICKY);
        this.cs = this.cs & ~CS_WRITE_MASK | value & CS_WRITE_MASK;
        if (value & CS_EN && !this.busy && (value & CS_START_ONE || value & CS_START_MANY)) {
          this.startADCRead();
        }
        break;
      case FCS:
        this.fcs &= ~(value & (FCS_OVER | FCS_UNDER));
        this.fcs = this.fcs & ~FCS_WRITE_MASK | value & FCS_WRITE_MASK;
        this.checkInterrupts();
        break;
      case DIV:
        this.clockDiv = value;
        break;
      case INTE:
        this.intEnable = value & FIFO_INT;
        this.checkInterrupts();
        break;
      case INTF:
        this.intForce = value & FIFO_INT;
        this.checkInterrupts();
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var BUS_PRIORITY_ACK = 4;
var PERFCTR0 = 8;
var PERFSEL0 = 12;
var PERFCTR1 = 16;
var PERFSEL1 = 20;
var PERFCTR2 = 24;
var PERFSEL2 = 28;
var PERFCTR3 = 32;
var PERFSEL3 = 36;
var RPBUSCTRL = class extends BasePeripheral {
  voltageSelect = 0;
  perfCtr = [
    0,
    0,
    0,
    0
  ];
  perfSel = [
    31,
    31,
    31,
    31
  ];
  constructor(rp2040, name) {
    super(rp2040, name);
  }
  readUint32(offset) {
    switch (offset) {
      case BUS_PRIORITY_ACK:
        return 1;
      case PERFCTR0:
        return this.perfCtr[0];
      case PERFSEL0:
        return this.perfSel[0];
      case PERFCTR1:
        return this.perfCtr[1];
      case PERFSEL1:
        return this.perfSel[1];
      case PERFCTR2:
        return this.perfCtr[2];
      case PERFSEL2:
        return this.perfSel[2];
      case PERFCTR3:
        return this.perfCtr[3];
      case PERFSEL3:
        return this.perfSel[3];
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case PERFCTR0:
        this.perfCtr[0] = 0;
        break;
      case PERFSEL0:
        this.perfSel[0] = value & 31;
        break;
      case PERFCTR1:
        this.perfCtr[1] = 0;
        break;
      case PERFSEL1:
        this.perfSel[1] = value & 31;
        break;
      case PERFCTR2:
        this.perfCtr[2] = 0;
        break;
      case PERFSEL2:
        this.perfSel[2] = value & 31;
        break;
      case PERFCTR3:
        this.perfCtr[3] = 0;
        break;
      case PERFSEL3:
        this.perfSel[3] = value & 31;
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var CLK_GPOUT0_CTRL = 0;
var CLK_GPOUT0_DIV = 4;
var CLK_GPOUT0_SELECTED = 8;
var CLK_GPOUT1_CTRL = 12;
var CLK_GPOUT1_DIV = 16;
var CLK_GPOUT1_SELECTED = 20;
var CLK_GPOUT2_CTRL = 24;
var CLK_GPOUT2_DIV = 28;
var CLK_GPOUT2_SELECTED = 32;
var CLK_GPOUT3_CTRL = 36;
var CLK_GPOUT3_DIV = 40;
var CLK_GPOUT3_SELECTED = 44;
var CLK_REF_CTRL = 48;
var CLK_REF_DIV = 52;
var CLK_REF_SELECTED = 56;
var CLK_SYS_CTRL = 60;
var CLK_SYS_DIV = 64;
var CLK_SYS_SELECTED = 68;
var CLK_PERI_CTRL = 72;
var CLK_PERI_DIV = 76;
var CLK_PERI_SELECTED = 80;
var CLK_USB_CTRL = 84;
var CLK_USB_DIV = 88;
var CLK_USB_SELECTED = 92;
var CLK_ADC_CTRL = 96;
var CLK_ADC_DIV = 100;
var CLK_ADC_SELECTED = 104;
var CLK_RTC_CTRL = 108;
var CLK_RTC_DIV = 112;
var CLK_RTC_SELECTED = 116;
var CLK_SYS_RESUS_CTRL = 120;
var CLK_SYS_RESUS_STATUS = 124;
var CLK_REF_CTRL_SRC_MASK = 3;
var CLK_REF_CTRL_SRC_ROSC = 0;
var CLK_REF_CTRL_SRC_AUX = 1;
var CLK_REF_CTRL_SRC_XOSC = 2;
var CLK_REF_CTRL_AUXSRC_SHIFT = 5;
var CLK_REF_CTRL_AUXSRC_MASK = 3;
var CLK_REF_CTRL_AUXSRC_PLL_USB = 0;
var CLK_REF_DIV_INT_BITS = 768;
var CLK_SYS_CTRL_SRC_MASK = 1;
var CLK_SYS_CTRL_SRC_REF = 0;
var CLK_SYS_CTRL_AUXSRC_SHIFT = 5;
var CLK_SYS_CTRL_AUXSRC_MASK = 7;
var CLK_SYS_CTRL_AUXSRC_PLL_SYS = 0;
var CLK_SYS_CTRL_AUXSRC_PLL_USB = 1;
var CLK_SYS_CTRL_AUXSRC_ROSC = 2;
var CLK_SYS_CTRL_AUXSRC_XOSC = 3;
var CLK_PERI_CTRL_ENABLE = 1 << 11;
var CLK_PERI_CTRL_KILL = 1 << 10;
var CLK_PERI_CTRL_AUXSRC_SHIFT = 5;
var CLK_PERI_CTRL_AUXSRC_MASK = 7;
var CLK_PERI_CTRL_AUXSRC_CLK_SYS = 0;
var CLK_PERI_CTRL_AUXSRC_PLL_SYS = 1;
var CLK_PERI_CTRL_AUXSRC_PLL_USB = 2;
var CLK_PERI_CTRL_AUXSRC_ROSC = 3;
var CLK_PERI_CTRL_AUXSRC_XOSC = 4;
var CLK_DIV_INT_SHIFT = 8;
var CLK_DIV_FRAC_MASK = 255;
function clockDivisor(div) {
  const int = div >>> CLK_DIV_INT_SHIFT;
  return int ? int + (div & CLK_DIV_FRAC_MASK) / 256 : 65536;
}
var RPClocks = class extends BasePeripheral {
  gpout0Ctrl = 0;
  gpout0Div = 256;
  gpout1Ctrl = 0;
  gpout1Div = 256;
  gpout2Ctrl = 0;
  gpout2Div = 256;
  gpout3Ctrl = 0;
  gpout3Div = 256;
  refCtrl = 0;
  refDiv = 256;
  periCtrl = 0;
  periDiv = 256;
  usbCtrl = 0;
  usbDiv = 256;
  sysCtrl = 0;
  sysDiv = 256;
  adcCtrl = 0;
  adcDiv = 256;
  rtcCtrl = 0;
  rtcDiv = 256;
  constructor(rp2040, name) {
    super(rp2040, name);
  }
  /** clk_ref frequency, in Hz. GPIN0/GPIN1 are not modelled and yield 0. */
  get refFreq() {
    return this.refSourceFreq / clockDivisor(this.refDiv & CLK_REF_DIV_INT_BITS);
  }
  /** clk_sys frequency, in Hz. GPIN0/GPIN1 are not modelled and yield 0. */
  get sysFreq() {
    return this.sysSourceFreq / clockDivisor(this.sysDiv);
  }
  /**
   * clk_peri frequency, in Hz. Feeds the UART and SPI baud rate generators. Returns 0 while
   * the clock generator is stopped, and for the GPIN0/GPIN1 sources we do not model.
   */
  get periFreq() {
    const { rp2040 } = this;
    if (!(this.periCtrl & CLK_PERI_CTRL_ENABLE) || this.periCtrl & CLK_PERI_CTRL_KILL) {
      return 0;
    }
    switch (this.periCtrl >>> CLK_PERI_CTRL_AUXSRC_SHIFT & CLK_PERI_CTRL_AUXSRC_MASK) {
      case CLK_PERI_CTRL_AUXSRC_CLK_SYS:
        return this.sysFreq;
      case CLK_PERI_CTRL_AUXSRC_PLL_SYS:
        return rp2040.pllSys.frequency;
      case CLK_PERI_CTRL_AUXSRC_PLL_USB:
        return rp2040.pllUsb.frequency;
      case CLK_PERI_CTRL_AUXSRC_ROSC:
        return rp2040.roscFreq;
      case CLK_PERI_CTRL_AUXSRC_XOSC:
        return rp2040.xoscFreq;
      default:
        return 0;
    }
  }
  get refSourceFreq() {
    const { rp2040 } = this;
    switch (this.refCtrl & CLK_REF_CTRL_SRC_MASK) {
      case CLK_REF_CTRL_SRC_ROSC:
        return rp2040.roscFreq;
      case CLK_REF_CTRL_SRC_XOSC:
        return rp2040.xoscFreq;
      case CLK_REF_CTRL_SRC_AUX: {
        const auxsrc = this.refCtrl >>> CLK_REF_CTRL_AUXSRC_SHIFT & CLK_REF_CTRL_AUXSRC_MASK;
        return auxsrc === CLK_REF_CTRL_AUXSRC_PLL_USB ? rp2040.pllUsb.frequency : 0;
      }
      default:
        return 0;
    }
  }
  get sysSourceFreq() {
    const { rp2040 } = this;
    if ((this.sysCtrl & CLK_SYS_CTRL_SRC_MASK) === CLK_SYS_CTRL_SRC_REF) {
      return this.refFreq;
    }
    switch (this.sysCtrl >>> CLK_SYS_CTRL_AUXSRC_SHIFT & CLK_SYS_CTRL_AUXSRC_MASK) {
      case CLK_SYS_CTRL_AUXSRC_PLL_SYS:
        return rp2040.pllSys.frequency;
      case CLK_SYS_CTRL_AUXSRC_PLL_USB:
        return rp2040.pllUsb.frequency;
      case CLK_SYS_CTRL_AUXSRC_ROSC:
        return rp2040.roscFreq;
      case CLK_SYS_CTRL_AUXSRC_XOSC:
        return rp2040.xoscFreq;
      default:
        return 0;
    }
  }
  readUint32(offset) {
    switch (offset) {
      case CLK_GPOUT0_CTRL:
        return this.gpout0Ctrl & 1252832;
      case CLK_GPOUT0_DIV:
        return this.gpout0Div;
      case CLK_GPOUT0_SELECTED:
        return 1;
      case CLK_GPOUT1_CTRL:
        return this.gpout1Ctrl & 1252832;
      case CLK_GPOUT1_DIV:
        return this.gpout1Div;
      case CLK_GPOUT1_SELECTED:
        return 1;
      case CLK_GPOUT2_CTRL:
        return this.gpout2Ctrl & 1252832;
      case CLK_GPOUT2_DIV:
        return this.gpout2Div;
      case CLK_GPOUT2_SELECTED:
        return 1;
      case CLK_GPOUT3_CTRL:
        return this.gpout3Ctrl & 1252832;
      case CLK_GPOUT3_DIV:
        return this.gpout3Div;
      case CLK_GPOUT3_SELECTED:
        return 1;
      case CLK_REF_CTRL:
        return this.refCtrl & 99;
      case CLK_REF_DIV:
        return this.refDiv & 48;
      // b8..9 = int divisor. no frac divisor present
      case CLK_REF_SELECTED:
        return 1 << (this.refCtrl & 3);
      case CLK_SYS_CTRL:
        return this.sysCtrl & 225;
      case CLK_SYS_DIV:
        return this.sysDiv;
      case CLK_SYS_SELECTED:
        return 1 << (this.sysCtrl & 1);
      case CLK_PERI_CTRL:
        return this.periCtrl & 3296;
      case CLK_PERI_DIV:
        return this.periDiv;
      case CLK_PERI_SELECTED:
        return 1;
      case CLK_USB_CTRL:
        return this.usbCtrl & 1248480;
      case CLK_USB_DIV:
        return this.usbDiv;
      case CLK_USB_SELECTED:
        return 1;
      case CLK_ADC_CTRL:
        return this.adcCtrl & 1248480;
      case CLK_ADC_DIV:
        return this.adcDiv & 48;
      case CLK_ADC_SELECTED:
        return 1;
      case CLK_RTC_CTRL:
        return this.rtcCtrl & 1248480;
      case CLK_RTC_DIV:
        return this.rtcDiv & 48;
      case CLK_RTC_SELECTED:
        return 1;
      case CLK_SYS_RESUS_CTRL:
        return 255;
      case CLK_SYS_RESUS_STATUS:
        return 0;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case CLK_GPOUT0_CTRL:
        this.gpout0Ctrl = value;
        break;
      case CLK_GPOUT0_DIV:
        this.gpout0Div = value;
        break;
      case CLK_GPOUT1_CTRL:
        this.gpout1Ctrl = value;
        break;
      case CLK_GPOUT1_DIV:
        this.gpout1Div = value;
        break;
      case CLK_GPOUT2_CTRL:
        this.gpout2Ctrl = value;
        break;
      case CLK_GPOUT2_DIV:
        this.gpout2Div = value;
        break;
      case CLK_GPOUT3_CTRL:
        this.gpout3Ctrl = value;
        break;
      case CLK_GPOUT3_DIV:
        this.gpout3Div = value;
        break;
      case CLK_REF_CTRL:
        this.refCtrl = value;
        this.rp2040.updateClocks();
        break;
      case CLK_REF_DIV:
        this.refDiv = value;
        this.rp2040.updateClocks();
        break;
      case CLK_SYS_CTRL:
        this.sysCtrl = value;
        this.rp2040.updateClocks();
        break;
      case CLK_SYS_DIV:
        this.sysDiv = value;
        this.rp2040.updateClocks();
        break;
      case CLK_PERI_CTRL:
        this.periCtrl = value;
        this.rp2040.updateClocks();
        break;
      case CLK_PERI_DIV:
        this.periDiv = value;
        break;
      case CLK_USB_CTRL:
        this.usbCtrl = value;
        break;
      case CLK_USB_DIV:
        this.usbDiv = value;
        break;
      case CLK_ADC_CTRL:
        this.adcCtrl = value;
        break;
      case CLK_ADC_DIV:
        this.adcDiv = value;
        break;
      case CLK_RTC_CTRL:
        this.rtcCtrl = value;
        break;
      case CLK_RTC_DIV:
        this.rtcDiv = value;
        break;
      case CLK_SYS_RESUS_CTRL:
        return;
      /* clock resus not implemented */
      default:
        super.writeUint32(offset, value);
        break;
    }
  }
};

var IC_CON = 0;
var IC_TAR = 4;
var IC_SAR = 8;
var IC_DATA_CMD = 16;
var IC_SS_SCL_HCNT = 20;
var IC_SS_SCL_LCNT = 24;
var IC_FS_SCL_HCNT = 28;
var IC_FS_SCL_LCNT = 32;
var IC_INTR_STAT = 44;
var IC_INTR_MASK = 48;
var IC_RAW_INTR_STAT = 52;
var IC_RX_TL = 56;
var IC_TX_TL = 60;
var IC_CLR_INTR = 64;
var IC_CLR_RX_UNDER = 68;
var IC_CLR_RX_OVER = 72;
var IC_CLR_TX_OVER = 76;
var IC_CLR_RD_REQ = 80;
var IC_CLR_TX_ABRT = 84;
var IC_CLR_RX_DONE = 88;
var IC_CLR_ACTIVITY = 92;
var IC_CLR_STOP_DET = 96;
var IC_CLR_START_DET = 100;
var IC_CLR_GEN_CALL = 104;
var IC_ENABLE = 108;
var IC_STATUS = 112;
var IC_TXFLR = 116;
var IC_RXFLR = 120;
var IC_SDA_HOLD = 124;
var IC_TX_ABRT_SOURCE = 128;
var IC_ENABLE_STATUS = 156;
var IC_FS_SPKLEN = 160;
var IC_COMP_PARAM_1 = 244;
var IC_COMP_VERSION = 248;
var IC_COMP_TYPE = 252;
var STOP_DET_IF_MASTER_ACTIVE = 1 << 10;
var RX_FIFO_FULL_HLD_CTRL = 1 << 9;
var TX_EMPTY_CTRL = 1 << 8;
var STOP_DET_IFADDRESSED = 1 << 7;
var IC_SLAVE_DISABLE = 1 << 6;
var IC_RESTART_EN = 1 << 5;
var IC_10BITADDR_MASTER = 1 << 4;
var IC_10BITADDR_SLAVE = 1 << 3;
var SPEED_SHIFT = 1;
var SPEED_MASK = 3;
var MASTER_MODE = 1 << 0;
var SPECIAL = 1 << 11;
var GC_OR_START = 1 << 10;
var SLV_ACTIVITY = 1 << 6;
var MST_ACTIVITY = 1 << 5;
var RFF = 1 << 4;
var RFNE = 1 << 3;
var TFE = 1 << 2;
var TFNF = 1 << 1;
var ACTIVITY = 1 << 0;
var TX_CMD_BLOCK = 1 << 2;
var ABORT = 1 << 1;
var ENABLE = 1 << 0;
var TX_FLUSH_CNT_MASK = 511;
var TX_FLUSH_CNT_SHIFT = 23;
var ABRT_USER_ABRT = 1 << 16;
var ABRT_SLVRD_INT = 1 << 15;
var ABRT_SLV_ARBLOST = 1 << 14;
var ABRT_SLVFLUSH_TXFIFO = 1 << 13;
var ARB_LOST = 1 << 12;
var ABRT_MASTER_DIS = 1 << 11;
var ABRT_10B_RD_NORSTRT = 1 << 10;
var ABRT_SBYTE_NORSTRT = 1 << 9;
var ABRT_HS_NORSTRT = 1 << 8;
var ABRT_SBYTE_ACKDET = 1 << 7;
var ABRT_HS_ACKDET = 1 << 6;
var ABRT_GCALL_READ = 1 << 5;
var ABRT_GCALL_NOACK = 1 << 4;
var ABRT_TXDATA_NOACK = 1 << 3;
var ABRT_10ADDR2_NOACK = 1 << 2;
var ABRT_10ADDR1_NOACK = 1 << 1;
var ABRT_7B_ADDR_NOACK = 1 << 0;
var I2CMode = /* @__PURE__ */ function(I2CMode2) {
  I2CMode2[I2CMode2["Write"] = 0] = "Write";
  I2CMode2[I2CMode2["Read"] = 1] = "Read";
  return I2CMode2;
}({});
var I2CSpeed = /* @__PURE__ */ function(I2CSpeed2) {
  I2CSpeed2[I2CSpeed2["Invalid"] = 0] = "Invalid";
  I2CSpeed2[I2CSpeed2["Standard"] = 1] = "Standard";
  I2CSpeed2[I2CSpeed2["FastMode"] = 2] = "FastMode";
  I2CSpeed2[I2CSpeed2["HighSpeedMode"] = 3] = "HighSpeedMode";
  return I2CSpeed2;
}({});
var I2CState = /* @__PURE__ */ function(I2CState2) {
  I2CState2[I2CState2["Idle"] = 0] = "Idle";
  I2CState2[I2CState2["Start"] = 1] = "Start";
  I2CState2[I2CState2["Connect"] = 2] = "Connect";
  I2CState2[I2CState2["Connected"] = 3] = "Connected";
  I2CState2[I2CState2["Stop"] = 4] = "Stop";
  return I2CState2;
}(I2CState || {});
var R_RESTART_DET = 1 << 12;
var R_GEN_CALL = 1 << 11;
var R_START_DET = 1 << 10;
var R_STOP_DET = 1 << 9;
var R_ACTIVITY = 1 << 8;
var R_RX_DONE = 1 << 7;
var R_TX_ABRT = 1 << 6;
var R_RD_REQ = 1 << 5;
var R_TX_EMPTY = 1 << 4;
var R_TX_OVER = 1 << 3;
var R_RX_FULL = 1 << 2;
var R_RX_OVER = 1 << 1;
var R_RX_UNDER = 1 << 0;
var FIRST_DATA_BYTE = 1 << 10;
var RESTART = 1 << 10;
var STOP = 1 << 9;
var CMD = 1 << 8;
var RPI2C = class extends BasePeripheral {
  irq;
  state;
  busy;
  stop;
  pendingRestart;
  firstByte;
  rxFIFO;
  txFIFO;
  // user provided callbacks
  onStart;
  onConnect;
  onWriteByte;
  onReadByte;
  onStop;
  enable;
  rxThreshold;
  txThreshold;
  control;
  ssClockHighPeriod;
  ssClockLowPeriod;
  fsClockHighPeriod;
  fsClockLowPeriod;
  targetAddress;
  slaveAddress;
  abortSource;
  intRaw;
  intEnable;
  spikelen;
  get intStatus() {
    return this.intRaw & this.intEnable;
  }
  get speed() {
    return this.control >> SPEED_SHIFT & SPEED_MASK;
  }
  get sclLowPeriod() {
    return this.speed === I2CSpeed.Standard ? this.ssClockLowPeriod : this.fsClockLowPeriod;
  }
  get sclHighPeriod() {
    return this.speed === I2CSpeed.Standard ? this.ssClockHighPeriod : this.fsClockHighPeriod;
  }
  get masterBits() {
    return this.control & IC_10BITADDR_MASTER ? 10 : 7;
  }
  constructor(rp2040, name, irq) {
    super(rp2040, name), this.irq = irq, this.state = I2CState.Idle, this.busy = false, this.stop = false, this.pendingRestart = false, this.firstByte = false, this.rxFIFO = new FIFO(16), this.txFIFO = new FIFO(16), this.onStart = () => this.completeStart(), this.onConnect = () => this.completeConnect(false), this.onWriteByte = () => this.completeWrite(false), this.onReadByte = () => this.completeRead(255), this.onStop = () => this.completeStop(), this.enable = 0, this.rxThreshold = 0, this.txThreshold = 0, this.control = IC_SLAVE_DISABLE | IC_RESTART_EN | I2CSpeed.FastMode << SPEED_SHIFT | MASTER_MODE, this.ssClockHighPeriod = 40, this.ssClockLowPeriod = 47, this.fsClockHighPeriod = 6, this.fsClockLowPeriod = 13, this.targetAddress = 85, this.slaveAddress = 85, this.abortSource = 0, this.intRaw = 0, this.intEnable = 0, this.spikelen = 7;
  }
  checkInterrupts() {
    this.rp2040.setInterrupt(this.irq, !!this.intStatus);
  }
  clearInterrupts(mask) {
    if (this.intRaw & mask) {
      this.intRaw &= ~mask;
      this.checkInterrupts();
      return 1;
    } else {
      return 0;
    }
  }
  setInterrupts(mask) {
    if (!(this.intRaw & mask)) {
      this.intRaw |= mask;
      this.checkInterrupts();
    }
  }
  abort(reason) {
    this.abortSource &= ~TX_FLUSH_CNT_MASK;
    this.abortSource |= reason | this.txFIFO.itemCount << TX_FLUSH_CNT_SHIFT;
    this.txFIFO.reset();
    this.setInterrupts(R_TX_ABRT);
  }
  nextCommand() {
    const enabled = this.enable & ENABLE;
    const blocked = this.enable & TX_CMD_BLOCK;
    if (this.txFIFO.empty || this.busy || blocked || !enabled) {
      return;
    }
    this.busy = true;
    const restart = !!(this.txFIFO.peek() & RESTART) && !this.pendingRestart && !this.stop;
    if (this.state === I2CState.Idle || restart) {
      this.pendingRestart = restart;
      this.stop = false;
      this.state = I2CState.Start;
      this.onStart(restart);
      return;
    }
    this.pendingRestart = false;
    const cmd = this.txFIFO.pull();
    const readMode = !!(cmd & CMD);
    this.stop = !!(cmd & STOP);
    if (readMode) {
      this.onReadByte(!this.stop);
    } else {
      this.onWriteByte(cmd & 255);
    }
    if (this.txFIFO.itemCount <= this.txThreshold) {
      this.setInterrupts(R_TX_EMPTY);
    }
  }
  pushRX(value) {
    if (this.rxFIFO.full) {
      this.setInterrupts(R_RX_OVER);
      return;
    }
    this.rxFIFO.push(value);
    if (this.rxFIFO.itemCount > this.rxThreshold) {
      this.setInterrupts(R_RX_FULL);
    }
  }
  completeStart() {
    if (this.txFIFO.empty || this.state !== I2CState.Start || this.stop) {
      this.onStop();
      return;
    }
    const mode = this.txFIFO.peek() & CMD ? I2CMode.Read : I2CMode.Write;
    this.state = I2CState.Connect;
    this.setInterrupts(R_START_DET);
    const addressMask = this.masterBits === 10 ? 1023 : 255;
    this.onConnect(this.targetAddress & addressMask, mode);
  }
  completeConnect(ack, nackByte = 0) {
    if (!ack || this.stop) {
      if (!ack) {
        if (!this.targetAddress) {
          this.abort(ABRT_GCALL_NOACK);
        } else if (this.control & IC_10BITADDR_MASTER) {
          this.abort(nackByte === 0 ? ABRT_10ADDR1_NOACK : ABRT_10ADDR2_NOACK);
        } else {
          this.abort(ABRT_7B_ADDR_NOACK);
        }
      }
      this.state = I2CState.Stop;
      this.onStop();
      return;
    }
    this.state = I2CState.Connected;
    this.busy = false;
    this.firstByte = true;
    this.nextCommand();
  }
  completeWrite(ack) {
    if (!ack || this.stop) {
      if (!ack) {
        this.abort(ABRT_TXDATA_NOACK);
      }
      this.state = I2CState.Stop;
      this.onStop();
      return;
    }
    this.busy = false;
    this.nextCommand();
  }
  completeRead(value) {
    this.pushRX(value | (this.firstByte ? FIRST_DATA_BYTE : 0));
    if (this.stop) {
      this.state = I2CState.Stop;
      this.onStop();
      return;
    }
    this.firstByte = false;
    this.busy = false;
    this.nextCommand();
  }
  completeStop() {
    this.state = I2CState.Idle;
    this.setInterrupts(R_STOP_DET);
    this.busy = false;
    this.pendingRestart = false;
    if (this.enable & ABORT) {
      this.enable &= ~ABORT;
    } else {
      this.nextCommand();
    }
  }
  arbitrationLost() {
    this.state = I2CState.Idle;
    this.busy = false;
    this.abort(ARB_LOST);
  }
  readUint32(offset) {
    switch (offset) {
      case IC_CON:
        return this.control;
      case IC_TAR:
        return this.targetAddress;
      case IC_SAR:
        return this.slaveAddress;
      case IC_DATA_CMD:
        if (this.rxFIFO.empty) {
          this.setInterrupts(R_RX_UNDER);
          return 0;
        }
        this.clearInterrupts(R_RX_FULL);
        return this.rxFIFO.pull();
      case IC_SS_SCL_HCNT:
        return this.ssClockHighPeriod;
      case IC_SS_SCL_LCNT:
        return this.ssClockLowPeriod;
      case IC_FS_SCL_HCNT:
        return this.fsClockHighPeriod;
      case IC_FS_SCL_LCNT:
        return this.fsClockLowPeriod;
      case IC_INTR_STAT:
        return this.intStatus;
      case IC_INTR_MASK:
        return this.intEnable;
      case IC_RAW_INTR_STAT:
        return this.intRaw;
      case IC_RX_TL:
        return this.rxThreshold;
      case IC_TX_TL:
        return this.txThreshold;
      case IC_CLR_INTR:
        this.abortSource &= ABRT_SBYTE_NORSTRT;
        return this.clearInterrupts(R_RX_UNDER | R_RX_OVER | R_TX_OVER | R_RD_REQ | R_TX_ABRT | R_RX_DONE | R_ACTIVITY | R_STOP_DET | R_START_DET | R_GEN_CALL);
      case IC_CLR_RX_UNDER:
        return this.clearInterrupts(R_RX_UNDER);
      case IC_CLR_RX_OVER:
        return this.clearInterrupts(R_RX_OVER);
      case IC_CLR_TX_OVER:
        return this.clearInterrupts(R_TX_OVER);
      case IC_CLR_RD_REQ:
        return this.clearInterrupts(R_RD_REQ);
      case IC_CLR_TX_ABRT:
        this.abortSource &= ABRT_SBYTE_NORSTRT;
        return this.clearInterrupts(R_TX_ABRT);
      case IC_CLR_RX_DONE:
        return this.clearInterrupts(R_RX_DONE);
      case IC_CLR_ACTIVITY:
        return this.clearInterrupts(R_ACTIVITY);
      case IC_CLR_STOP_DET:
        return this.clearInterrupts(R_STOP_DET);
      case IC_CLR_START_DET:
        return this.clearInterrupts(R_START_DET);
      case IC_CLR_GEN_CALL:
        return this.clearInterrupts(R_GEN_CALL);
      case IC_ENABLE:
        return this.enable;
      case IC_STATUS:
        return (this.state !== I2CState.Idle ? MST_ACTIVITY | ACTIVITY : 0) | (this.rxFIFO.full ? RFF : 0) | (!this.rxFIFO.empty ? RFNE : 0) | (this.txFIFO.empty ? TFE : 0) | (!this.txFIFO.full ? TFNF : 0);
      case IC_TXFLR:
        return this.txFIFO.itemCount;
      case IC_RXFLR:
        return this.rxFIFO.itemCount;
      case IC_SDA_HOLD:
        return 1;
      case IC_TX_ABRT_SOURCE: {
        const value = this.abortSource;
        this.abortSource &= ABRT_SBYTE_NORSTRT;
        return value;
      }
      case IC_ENABLE_STATUS:
        return this.enable & 1;
      case IC_FS_SPKLEN:
        return this.spikelen & 255;
      case IC_COMP_PARAM_1:
        return 0;
      case IC_COMP_VERSION:
        return 842019114;
      case IC_COMP_TYPE:
        return 1146552640;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case IC_CON:
        if ((value >> SPEED_SHIFT & SPEED_MASK) === I2CSpeed.Invalid) {
          value = value & ~(SPEED_MASK << SPEED_SHIFT) | I2CSpeed.HighSpeedMode << SPEED_SHIFT;
        }
        this.control = value;
        return;
      case IC_TAR:
        this.targetAddress = value & 1023;
        return;
      case IC_SAR:
        this.slaveAddress = value & 1023;
        return;
      case IC_DATA_CMD:
        if (this.txFIFO.full) {
          this.setInterrupts(R_TX_OVER);
        } else {
          this.txFIFO.push(value);
          this.clearInterrupts(R_TX_EMPTY);
          this.nextCommand();
        }
        return;
      case IC_SS_SCL_HCNT:
        this.ssClockHighPeriod = value & 65535;
        return;
      case IC_SS_SCL_LCNT:
        this.ssClockLowPeriod = value & 65535;
        return;
      case IC_FS_SCL_HCNT:
        this.fsClockHighPeriod = value & 65535;
        return;
      case IC_FS_SCL_LCNT:
        this.fsClockLowPeriod = value & 65535;
        return;
      case IC_SDA_HOLD:
        if (!(value & ENABLE)) {
          if (value != 1) {
            this.warn("Unimplemented write to IC_SDA_HOLD");
          }
        }
        return;
      case IC_RX_TL:
        this.rxThreshold = value & 255;
        if (this.rxThreshold > this.rxFIFO.size) {
          this.rxThreshold = this.rxFIFO.size;
        }
        return;
      case IC_TX_TL:
        this.txThreshold = value & 255;
        if (this.txThreshold > this.txFIFO.size) {
          this.txThreshold = this.txFIFO.size;
        }
        return;
      case IC_ENABLE:
        value |= this.enable & ABORT;
        if (value & ABORT) {
          if (this.state === I2CState.Idle) {
            value &= ~ABORT;
          } else {
            this.abort(ABRT_USER_ABRT);
            this.stop = true;
          }
        }
        if (!(value & ENABLE)) {
          this.txFIFO.reset();
          this.rxFIFO.reset();
        }
        this.enable = value;
        this.nextCommand();
        return;
      case IC_FS_SPKLEN:
        if (!(value & ENABLE) && value > 0) {
          this.spikelen = value;
        }
        return;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var GPIO_CTRL_LAST = 236;
var INTR0 = 240;
var PROC0_INTE0 = 256;
var PROC0_INTF0 = 272;
var PROC0_INTS0 = 288;
var PROC0_INTS3 = 300;
var RPIO = class extends BasePeripheral {
  constructor(rp2040, name) {
    super(rp2040, name);
  }
  getPinFromOffset(offset) {
    const gpioIndex = offset >>> 3;
    return {
      gpio: this.rp2040.gpio[gpioIndex],
      isCtrl: !!(offset & 4)
    };
  }
  readUint32(offset) {
    if (offset <= GPIO_CTRL_LAST) {
      const { gpio, isCtrl } = this.getPinFromOffset(offset);
      return isCtrl ? gpio.ctrl : gpio.status;
    }
    if (offset >= INTR0 && offset <= PROC0_INTS3) {
      const startIndex = (offset & 15) * 2;
      const register = offset & ~15;
      const { gpio } = this.rp2040;
      let result = 0;
      for (let index = 7; index >= 0; index--) {
        const pin = gpio[index + startIndex];
        if (!pin) {
          continue;
        }
        result <<= 4;
        switch (register) {
          case INTR0:
            result |= pin.irqStatus;
            break;
          case PROC0_INTE0:
            result |= pin.irqEnableMask;
            break;
          case PROC0_INTF0:
            result |= pin.irqForceMask;
            break;
          case PROC0_INTS0:
            result |= pin.irqStatus & pin.irqEnableMask | pin.irqForceMask;
            break;
        }
      }
      return result;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    if (offset <= GPIO_CTRL_LAST) {
      const { gpio, isCtrl } = this.getPinFromOffset(offset);
      if (isCtrl) {
        gpio.ctrl = value;
        gpio.checkForUpdates();
      }
      return;
    }
    if (offset >= INTR0 && offset <= PROC0_INTS3) {
      const startIndex = (offset & 15) * 2;
      const register = offset & ~15;
      const { gpio } = this.rp2040;
      for (let index = 0; index < 8; index++) {
        const pin = gpio[index + startIndex];
        if (!pin) {
          continue;
        }
        const pinValue = value >> index * 4 & 15;
        const pinRawWriteValue = this.rawWriteValue >> index * 4 & 15;
        switch (register) {
          case INTR0:
            pin.updateIRQValue(pinRawWriteValue);
            break;
          case PROC0_INTE0:
            if (pin.irqEnableMask !== pinValue) {
              pin.irqEnableMask = pinValue;
              this.rp2040.updateIOInterrupt();
            }
            break;
          case PROC0_INTF0:
            if (pin.irqForceMask !== pinValue) {
              pin.irqForceMask = pinValue;
              this.rp2040.updateIOInterrupt();
            }
            break;
        }
      }
      return;
    }
    super.writeUint32(offset, value);
  }
};

var VOLTAGE_SELECT = 0;
var GPIO_FIRST = 4;
var GPIO_LAST = 120;
var QSPI_FIRST = 4;
var QSPI_LAST = 24;
var RPPADS = class extends BasePeripheral {
  bank;
  voltageSelect;
  firstPadRegister;
  lastPadRegister;
  constructor(rp2040, name, bank) {
    super(rp2040, name), this.bank = bank, this.voltageSelect = 0, this.firstPadRegister = this.bank === "qspi" ? QSPI_FIRST : GPIO_FIRST, this.lastPadRegister = this.bank === "qspi" ? QSPI_LAST : GPIO_LAST;
  }
  getPinFromOffset(offset) {
    const gpioIndex = offset - this.firstPadRegister >>> 2;
    if (this.bank === "qspi") {
      return this.rp2040.qspi[gpioIndex];
    } else {
      return this.rp2040.gpio[gpioIndex];
    }
  }
  readUint32(offset) {
    if (offset >= this.firstPadRegister && offset <= this.lastPadRegister) {
      const gpio = this.getPinFromOffset(offset);
      return gpio.padValue;
    }
    switch (offset) {
      case VOLTAGE_SELECT:
        return this.voltageSelect;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    if (offset >= this.firstPadRegister && offset <= this.lastPadRegister) {
      const gpio = this.getPinFromOffset(offset);
      const oldInputEnable = gpio.inputEnable;
      gpio.padValue = value;
      gpio.checkForUpdates();
      if (oldInputEnable !== gpio.inputEnable) {
        gpio.refreshInput();
      }
      return;
    }
    switch (offset) {
      case VOLTAGE_SELECT:
        this.voltageSelect = value & 1;
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var PLL_CS = 0;
var PLL_PWR = 4;
var PLL_FBDIV_INT = 8;
var PLL_PRIM = 12;
var PLL_CS_LOCK = 1 << 31;
var PLL_CS_BYPASS = 1 << 8;
var PLL_CS_REFDIV_MASK = 63;
var PLL_FBDIV_INT_MASK = 4095;
var PLL_PRIM_POSTDIV1_SHIFT = 16;
var PLL_PRIM_POSTDIV2_SHIFT = 12;
var PLL_PRIM_POSTDIV_MASK = 7;
var RPPLL = class extends BasePeripheral {
  cs = 1;
  pwr = 45;
  fbdivInt = 0;
  prim = 487424;
  get refdiv() {
    return this.cs & PLL_CS_REFDIV_MASK;
  }
  get fbdiv() {
    return this.fbdivInt & PLL_FBDIV_INT_MASK;
  }
  get postdiv1() {
    return this.prim >>> PLL_PRIM_POSTDIV1_SHIFT & PLL_PRIM_POSTDIV_MASK;
  }
  get postdiv2() {
    return this.prim >>> PLL_PRIM_POSTDIV2_SHIFT & PLL_PRIM_POSTDIV_MASK;
  }
  /** PLL output frequency, in Hz, derived from the current register values. */
  get frequency() {
    const refFreq = this.rp2040.xoscFreq / (this.refdiv || 1);
    if (this.cs & PLL_CS_BYPASS) {
      return refFreq;
    }
    const postdiv = (this.postdiv1 || 1) * (this.postdiv2 || 1);
    return refFreq * this.fbdiv / postdiv;
  }
  readUint32(offset) {
    switch (offset) {
      case PLL_CS:
        return this.cs | PLL_CS_LOCK;
      case PLL_PWR:
        return this.pwr;
      case PLL_FBDIV_INT:
        return this.fbdivInt;
      case PLL_PRIM:
        return this.prim;
      default:
        return super.readUint32(offset);
    }
  }
  writeUint32(offset, value) {
    switch (offset) {
      case PLL_CS:
        this.cs = value;
        break;
      case PLL_PWR:
        this.pwr = value;
        break;
      case PLL_FBDIV_INT:
        this.fbdivInt = value;
        break;
      case PLL_PRIM:
        this.prim = value;
        break;
      default:
        super.writeUint32(offset, value);
        return;
    }
    this.rp2040.updateClocks();
  }
};

var TimerMode = /* @__PURE__ */ function(TimerMode2) {
  TimerMode2[TimerMode2["Increment"] = 0] = "Increment";
  TimerMode2[TimerMode2["Decrement"] = 1] = "Decrement";
  TimerMode2[TimerMode2["ZigZag"] = 2] = "ZigZag";
  return TimerMode2;
}({});
var Timer32 = class {
  clock;
  baseFreq;
  baseValue;
  baseNanos;
  topValue;
  prescalerValue;
  timerMode;
  enabled;
  listeners;
  constructor(clock, baseFreq) {
    this.clock = clock;
    this.baseFreq = baseFreq;
    this.baseValue = 0;
    this.baseNanos = 0;
    this.topValue = 4294967295;
    this.prescalerValue = 1;
    this.timerMode = TimerMode.Increment;
    this.enabled = true;
    this.listeners = [];
  }
  reset() {
    this.baseNanos = this.clock.nanos;
    this.baseValue = 0;
    this.updated();
  }
  set(value, zigZagDown = false) {
    this.baseValue = zigZagDown ? this.topValue * 2 - value : value;
    this.baseNanos = this.clock.nanos;
    this.updated();
  }
  /**
   * Advances the counter by the given amount. Note that this will
   * decrease the counter if the timer is running in Decrement mode.
   *
   * @param delta The value to add to the counter. Can be negative.
   */
  advance(delta) {
    this.baseValue += delta;
    if (this.topValue !== 4294967295) {
      const topModulo = this.timerMode === TimerMode.ZigZag ? this.topValue * 2 : this.topValue + 1;
      this.baseValue = (this.baseValue % topModulo + topModulo) % topModulo;
    }
    this.updated();
  }
  get rawCounter() {
    const { baseFreq, prescalerValue, baseNanos, baseValue, enabled, timerMode } = this;
    if (!baseFreq || !prescalerValue || !enabled) {
      return this.baseValue;
    }
    const zigzag = timerMode == TimerMode.ZigZag;
    const ticks = (this.clock.nanos - baseNanos) / 1e9 * (baseFreq / prescalerValue);
    const topModulo = zigzag ? this.topValue * 2 : this.topValue + 1;
    const delta = timerMode == TimerMode.Decrement ? topModulo - ticks % topModulo : ticks;
    let currentValue = Math.round(baseValue + delta);
    if (this.topValue != 4294967295) {
      currentValue %= topModulo;
    }
    return currentValue;
  }
  get counter() {
    let currentValue = this.rawCounter;
    if (this.timerMode == TimerMode.ZigZag && currentValue > this.topValue) {
      currentValue = this.topValue * 2 - currentValue;
    }
    return currentValue >>> 0;
  }
  get top() {
    return this.topValue;
  }
  set top(value) {
    const { counter } = this;
    this.topValue = value;
    this.set(counter <= this.topValue ? counter : 0);
  }
  get frequency() {
    return this.baseFreq;
  }
  set frequency(value) {
    this.baseValue = this.counter;
    this.baseNanos = this.clock.nanos;
    this.baseFreq = value;
    this.updated();
  }
  get prescaler() {
    return this.prescalerValue;
  }
  set prescaler(value) {
    this.baseValue = this.counter;
    this.baseNanos = this.clock.nanos;
    this.enabled = this.prescalerValue !== 0;
    this.prescalerValue = value;
    this.updated();
  }
  toNanos(cycles) {
    const { baseFreq, prescalerValue } = this;
    return cycles * 1e9 / (baseFreq / prescalerValue);
  }
  get enable() {
    return this.enabled;
  }
  set enable(value) {
    if (value !== this.enabled) {
      if (value) {
        this.baseNanos = this.clock.nanos;
      } else {
        this.baseValue = this.counter;
      }
      this.enabled = value;
      this.updated();
    }
  }
  get mode() {
    return this.timerMode;
  }
  set mode(value) {
    if (this.timerMode !== value) {
      const { counter } = this;
      this.timerMode = value;
      this.set(counter);
    }
  }
  updated() {
    for (const listener of this.listeners) {
      listener();
    }
  }
};
var Timer32PeriodicAlarm = class {
  timer;
  callback;
  targetValue;
  enabled;
  clockAlarm;
  constructor(timer, callback) {
    this.timer = timer;
    this.callback = callback;
    this.targetValue = 0;
    this.enabled = false;
    this.handleAlarm = () => {
      this.callback();
      if (this.enabled && this.timer.enable) {
        this.schedule();
      }
    };
    this.update = () => {
      this.cancel();
      if (this.enabled && this.timer.enable) {
        this.schedule();
      }
    };
    this.clockAlarm = this.timer.clock.createAlarm(this.handleAlarm);
    timer.listeners.push(this.update);
  }
  get enable() {
    return this.enabled;
  }
  set enable(value) {
    if (value !== this.enabled) {
      this.enabled = value;
      if (value && this.timer.enable) {
        this.schedule();
      } else {
        this.cancel();
      }
    }
  }
  get target() {
    return this.targetValue;
  }
  set target(value) {
    if (value === this.targetValue) {
      return;
    }
    this.targetValue = value;
    if (this.enabled && this.timer.enable) {
      this.cancel();
      this.schedule();
    }
  }
  handleAlarm;
  update;
  schedule() {
    const { timer, targetValue } = this;
    const { top, mode, rawCounter } = timer;
    let cycleDelta = targetValue - rawCounter;
    if (mode === TimerMode.ZigZag && cycleDelta < 0) {
      if (cycleDelta < -top) {
        cycleDelta += 2 * top;
      } else {
        cycleDelta = top * 2 - targetValue - rawCounter;
      }
    }
    if (top != 4294967295) {
      if (cycleDelta <= 0) {
        cycleDelta += top + 1;
      }
      if (targetValue > top) {
        return;
      }
    }
    if (mode === TimerMode.Decrement) {
      cycleDelta = top + 1 - cycleDelta;
    }
    const cyclesToAlarm = cycleDelta >>> 0;
    const nanosToAlarm = timer.toNanos(cyclesToAlarm);
    this.clockAlarm.schedule(nanosToAlarm);
  }
  cancel() {
    this.clockAlarm.cancel();
  }
};

var CPUID = 3328;
var ICSR = 3332;
var VTOR = 3336;
var SHPR2 = 3356;
var SHPR3 = 3360;
var SYST_CSR = 16;
var SYST_RVR = 20;
var SYST_CVR = 24;
var SYST_CALIB = 28;
var NVIC_ISER = 256;
var NVIC_ICER = 384;
var NVIC_ISPR = 512;
var NVIC_ICPR = 640;
var NVIC_IPR0 = 1024;
var NVIC_IPR1 = 1028;
var NVIC_IPR2 = 1032;
var NVIC_IPR3 = 1036;
var NVIC_IPR4 = 1040;
var NVIC_IPR5 = 1044;
var NVIC_IPR6 = 1048;
var NVIC_IPR7 = 1052;
var NMIPENDSET = 1 << 31;
var PENDSVSET = 1 << 28;
var PENDSVCLR = 1 << 27;
var PENDSTSET = 1 << 26;
var PENDSTCLR = 1 << 25;
var ISRPREEMPT = 1 << 23;
var ISRPENDING = 1 << 22;
var VECTPENDING_SHIFT = 12;
var VECTACTIVE_MASK = 511;
var VECTACTIVE_SHIFT = 0;
var RPPPB = class extends BasePeripheral {
  // Systick
  systickCountFlag = false;
  systickClkSource = false;
  systickIntEnable = false;
  systickReload = 0;
  systickTimer = new Timer32(this.rp2040.clock, this.rp2040.clkSys);
  systickAlarm = new Timer32PeriodicAlarm(this.systickTimer, () => {
    this.systickCountFlag = true;
    if (this.systickIntEnable) {
      this.rp2040.core.pendingSystick = true;
      this.rp2040.core.interruptsUpdated = true;
    }
    this.systickTimer.set(this.systickReload);
  });
  constructor(rp2040, name) {
    super(rp2040, name);
    this.systickTimer.top = 16777215;
    this.systickTimer.mode = TimerMode.Decrement;
    this.systickAlarm.target = 0;
    this.systickAlarm.enable = true;
    this.reset();
  }
  reset() {
    this.writeUint32(SYST_CSR, 0);
    this.writeUint32(SYST_RVR, 16777215);
    this.systickTimer.set(16777215);
  }
  readUint32(offset) {
    const { rp2040 } = this;
    const { core } = rp2040;
    switch (offset) {
      case CPUID:
        return 1091356161;
      /* Verified against actual hardware */
      case ICSR: {
        const pendingInterrupts = core.pendingInterrupts || core.pendingPendSV || core.pendingSystick || core.pendingSVCall;
        const vectPending = core.vectPending;
        return (core.pendingNMI ? NMIPENDSET : 0) | (core.pendingPendSV ? PENDSVSET : 0) | (core.pendingSystick ? PENDSTSET : 0) | (pendingInterrupts ? ISRPENDING : 0) | vectPending << VECTPENDING_SHIFT | (core.IPSR & VECTACTIVE_MASK) << VECTACTIVE_SHIFT;
      }
      case VTOR:
        return core.VTOR;
      /* NVIC */
      case NVIC_ISPR:
        return core.pendingInterrupts >>> 0;
      case NVIC_ICPR:
        return core.pendingInterrupts >>> 0;
      case NVIC_ISER:
        return core.enabledInterrupts >>> 0;
      case NVIC_ICER:
        return core.enabledInterrupts >>> 0;
      case NVIC_IPR0:
      case NVIC_IPR1:
      case NVIC_IPR2:
      case NVIC_IPR3:
      case NVIC_IPR4:
      case NVIC_IPR5:
      case NVIC_IPR6:
      case NVIC_IPR7: {
        const regIndex = offset - NVIC_IPR0 >> 2;
        let result = 0;
        for (let byteIndex = 0; byteIndex < 4; byteIndex++) {
          const interruptNumber = regIndex * 4 + byteIndex;
          for (let priority = 0; priority < core.interruptPriorities.length; priority++) {
            if (core.interruptPriorities[priority] & 1 << interruptNumber) {
              result |= priority << 8 * byteIndex + 6;
            }
          }
        }
        return result;
      }
      case SHPR2:
        return core.SHPR2;
      case SHPR3:
        return core.SHPR3;
      /* SysTick */
      case SYST_CSR: {
        const countFlagValue = this.systickCountFlag ? 1 << 16 : 0;
        const clkSourceValue = this.systickClkSource ? 1 << 2 : 0;
        const tickIntValue = this.systickIntEnable ? 1 << 1 : 0;
        const enableFlagValue = this.systickTimer.enable ? 1 << 0 : 0;
        this.systickCountFlag = false;
        return countFlagValue | clkSourceValue | tickIntValue | enableFlagValue;
      }
      case SYST_CVR:
        return this.systickTimer.counter;
      case SYST_RVR:
        return this.systickReload;
      case SYST_CALIB:
        return 9999;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    const { rp2040 } = this;
    const { core } = rp2040;
    const hardwareInterruptMask = (1 << MAX_HARDWARE_IRQ) - 1;
    switch (offset) {
      case ICSR:
        if (value & NMIPENDSET) {
          core.pendingNMI = true;
          core.interruptsUpdated = true;
        }
        if (value & PENDSVSET) {
          core.pendingPendSV = true;
          core.interruptsUpdated = true;
        }
        if (value & PENDSVCLR) {
          core.pendingPendSV = false;
        }
        if (value & PENDSTSET) {
          core.pendingSystick = true;
          core.interruptsUpdated = true;
        }
        if (value & PENDSTCLR) {
          core.pendingSystick = false;
        }
        return;
      case VTOR:
        core.VTOR = value;
        return;
      /* NVIC */
      case NVIC_ISPR:
        core.pendingInterrupts |= value;
        core.interruptsUpdated = true;
        return;
      case NVIC_ICPR:
        core.pendingInterrupts &= ~value | hardwareInterruptMask;
        return;
      case NVIC_ISER:
        core.enabledInterrupts |= value;
        core.interruptsUpdated = true;
        return;
      case NVIC_ICER:
        core.enabledInterrupts &= ~value;
        return;
      case NVIC_IPR0:
      case NVIC_IPR1:
      case NVIC_IPR2:
      case NVIC_IPR3:
      case NVIC_IPR4:
      case NVIC_IPR5:
      case NVIC_IPR6:
      case NVIC_IPR7: {
        const regIndex = offset - NVIC_IPR0 >> 2;
        for (let byteIndex = 0; byteIndex < 4; byteIndex++) {
          const interruptNumber = regIndex * 4 + byteIndex;
          const newPriority = value >> 8 * byteIndex + 6 & 3;
          for (let priority = 0; priority < core.interruptPriorities.length; priority++) {
            core.interruptPriorities[priority] &= ~(1 << interruptNumber);
          }
          core.interruptPriorities[newPriority] |= 1 << interruptNumber;
        }
        core.interruptsUpdated = true;
        return;
      }
      case SHPR2:
        core.SHPR2 = value;
        return;
      case SHPR3:
        core.SHPR3 = value;
        return;
      // SysTick
      case SYST_CSR:
        this.systickClkSource = value & 1 << 2 ? true : false;
        this.systickIntEnable = value & 1 << 1 ? true : false;
        this.systickTimer.enable = value & 1 << 0 ? true : false;
        return;
      case SYST_CVR:
        this.systickTimer.set(0);
        return;
      case SYST_RVR:
        this.systickReload = value;
        return;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var FRCE_ON = 0;
var FRCE_OFF = 4;
var WDSEL = 8;
var DONE = 12;
var PSM_BITS_MASK = 131071;
var RPPSM = class extends BasePeripheral {
  frceOn = 0;
  frceOff = 0;
  wdsel = 0;
  readUint32(offset) {
    switch (offset) {
      case FRCE_ON:
        return this.frceOn;
      case FRCE_OFF:
        return this.frceOff;
      case WDSEL:
        return this.wdsel;
      case DONE:
        return PSM_BITS_MASK & ~this.frceOff | this.frceOn & this.frceOff;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case FRCE_ON:
        this.frceOn = value & PSM_BITS_MASK;
        break;
      case FRCE_OFF:
        this.frceOff = value & PSM_BITS_MASK;
        break;
      case WDSEL:
        this.wdsel = value & PSM_BITS_MASK;
        break;
      default:
        super.writeUint32(offset, value);
        break;
    }
  }
};

var CHn_CSR = 0;
var CHn_DIV = 4;
var CHn_CTR = 8;
var CHn_CC = 12;
var CHn_TOP = 16;
var EN2 = 160;
var INTR4 = 164;
var INTE2 = 168;
var INTF2 = 172;
var INTS2 = 176;
var INT_MASK = 255;
var CSR_PH_ADV = 1 << 7;
var CSR_PH_RET = 1 << 6;
var CSR_DIVMODE_SHIFT = 4;
var CSR_DIVMODE_MASK = 3;
var CSR_B_INV = 1 << 3;
var CSR_A_INV = 1 << 2;
var CSR_PH_CORRECT = 1 << 1;
var CSR_EN = 1 << 0;
var PWMDivMode = /* @__PURE__ */ function(PWMDivMode2) {
  PWMDivMode2[PWMDivMode2["FreeRunning"] = 0] = "FreeRunning";
  PWMDivMode2[PWMDivMode2["BGated"] = 1] = "BGated";
  PWMDivMode2[PWMDivMode2["BRisingEdge"] = 2] = "BRisingEdge";
  PWMDivMode2[PWMDivMode2["BFallingEdge"] = 3] = "BFallingEdge";
  return PWMDivMode2;
}(PWMDivMode || {});
var PWMChannel = class {
  pwm;
  clock;
  index;
  timer;
  alarmA;
  alarmB;
  alarmBottom;
  csr;
  div;
  cc;
  top;
  lastBValue;
  countingUp;
  ccUpdated;
  topUpdated;
  tickCounter;
  divMode;
  // GPIO pin indices: Table 525. Mapping of PWM channels to GPIO pins on RP2040
  pinA1;
  pinB1;
  pinA2;
  pinB2;
  constructor(pwm, clock, index) {
    this.pwm = pwm;
    this.clock = clock;
    this.index = index;
    this.timer = new Timer32(this.clock, this.pwm.clockFreq);
    this.alarmA = new Timer32PeriodicAlarm(this.timer, () => {
      this.setA(false);
    });
    this.alarmB = new Timer32PeriodicAlarm(this.timer, () => {
      this.setB(false);
    });
    this.alarmBottom = new Timer32PeriodicAlarm(this.timer, () => this.wrap());
    this.csr = 0;
    this.div = 0;
    this.cc = 0;
    this.top = 0;
    this.lastBValue = false;
    this.countingUp = true;
    this.ccUpdated = false;
    this.topUpdated = false;
    this.tickCounter = 0;
    this.divMode = PWMDivMode.FreeRunning;
    this.pinA1 = this.index * 2;
    this.pinB1 = this.index * 2 + 1;
    this.pinA2 = this.index < 7 ? 16 + this.index * 2 : -1;
    this.pinB2 = this.index < 7 ? 16 + this.index * 2 + 1 : -1;
    this.alarmA.enable = true;
    this.alarmB.enable = true;
    this.alarmBottom.enable = true;
  }
  readRegister(offset) {
    switch (offset) {
      case CHn_CSR:
        return this.csr;
      case CHn_DIV:
        return this.div;
      case CHn_CTR:
        return this.timer.counter;
      case CHn_CC:
        return this.cc;
      case CHn_TOP:
        return this.top;
    }
    return 0;
  }
  writeRegister(offset, value) {
    switch (offset) {
      case CHn_CSR:
        if (value & CSR_EN && !(this.csr & CSR_EN)) {
          this.updateDoubleBuffered();
        }
        this.csr = value & ~(CSR_PH_ADV | CSR_PH_RET);
        if (value & CSR_EN) {
          if (value & CSR_PH_ADV) {
            this.timer.advance(1);
          }
          if (value & CSR_PH_RET) {
            this.timer.advance(-1);
          }
        }
        this.divMode = this.csr >> CSR_DIVMODE_SHIFT & CSR_DIVMODE_MASK;
        this.setBDirection(this.divMode === PWMDivMode.FreeRunning);
        this.updateEnable();
        this.lastBValue = this.gpioBValue;
        this.timer.mode = value & CSR_PH_CORRECT ? TimerMode.ZigZag : TimerMode.Increment;
        break;
      case CHn_DIV: {
        this.div = value & 1048575;
        const intValue = value >> 4 & 255;
        const fracValue = value & 15;
        this.timer.prescaler = (intValue ? intValue : 256) + fracValue / 16;
        break;
      }
      case CHn_CTR:
        this.timer.set(value & 65535);
        break;
      case CHn_CC:
        this.cc = value;
        this.ccUpdated = true;
        break;
      case CHn_TOP:
        this.top = value & 65535;
        this.topUpdated = true;
        break;
    }
  }
  reset() {
    this.writeRegister(CHn_CSR, 0);
    this.writeRegister(CHn_DIV, 1 << 4);
    this.writeRegister(CHn_CTR, 0);
    this.writeRegister(CHn_CC, 0);
    this.writeRegister(CHn_TOP, 65535);
    this.countingUp = true;
    this.timer.enable = false;
    this.timer.reset();
  }
  updateDoubleBuffered() {
    if (this.ccUpdated) {
      this.alarmB.target = this.cc >>> 16;
      this.alarmA.target = this.cc & 65535;
      this.ccUpdated = false;
    }
    if (this.topUpdated) {
      this.timer.top = this.top;
      this.topUpdated = false;
    }
  }
  wrap() {
    this.pwm.channelInterrupt(this.index);
    this.updateDoubleBuffered();
    if (!(this.csr & CSR_PH_CORRECT)) {
      this.setA(this.alarmA.target > 0);
      this.setB(this.alarmB.target > 0);
    }
  }
  setA(value) {
    if (this.csr & CSR_A_INV) {
      value = !value;
    }
    this.pwm.gpioSet(this.pinA1, value);
    if (this.pinA2 >= 0) {
      this.pwm.gpioSet(this.pinA2, value);
    }
  }
  setB(value) {
    if (this.csr & CSR_B_INV) {
      value = !value;
    }
    this.pwm.gpioSet(this.pinB1, value);
    if (this.pinB2 >= 0) {
      this.pwm.gpioSet(this.pinB2, value);
    }
  }
  get gpioBValue() {
    return this.pwm.gpioRead(this.pinB1) || (this.pinB2 > 0 ? this.pwm.gpioRead(this.pinB2) : false);
  }
  setBDirection(value) {
    this.pwm.gpioSetDir(this.pinB1, value);
    if (this.pinB2 >= 0) {
      this.pwm.gpioSetDir(this.pinB2, value);
    }
  }
  gpioBChanged() {
    const value = this.gpioBValue;
    if (value === this.lastBValue) {
      return;
    }
    this.lastBValue = value;
    switch (this.divMode) {
      case PWMDivMode.BGated:
        this.updateEnable();
        break;
      case PWMDivMode.BRisingEdge:
        if (value) {
          this.tickCounter++;
        }
        break;
      case PWMDivMode.BFallingEdge:
        if (!value) {
          this.tickCounter++;
        }
        break;
    }
    if (this.tickCounter >= this.timer.prescaler) {
      this.timer.advance(1);
      this.tickCounter -= this.timer.prescaler;
    }
  }
  updateEnable() {
    const { csr, divMode } = this;
    const enable = !!(csr & CSR_EN);
    this.timer.enable = enable && (divMode === PWMDivMode.FreeRunning || divMode === PWMDivMode.BGated && this.gpioBValue);
  }
  set en(value) {
    if (value && !(this.csr & CSR_EN)) {
      this.updateDoubleBuffered();
    }
    if (value) {
      this.csr |= CSR_EN;
    } else {
      this.csr &= ~CSR_EN;
    }
    this.updateEnable();
  }
};
var RPPWM = class extends BasePeripheral {
  channels = [
    new PWMChannel(this, this.rp2040.clock, 0),
    new PWMChannel(this, this.rp2040.clock, 1),
    new PWMChannel(this, this.rp2040.clock, 2),
    new PWMChannel(this, this.rp2040.clock, 3),
    new PWMChannel(this, this.rp2040.clock, 4),
    new PWMChannel(this, this.rp2040.clock, 5),
    new PWMChannel(this, this.rp2040.clock, 6),
    new PWMChannel(this, this.rp2040.clock, 7)
  ];
  intRaw = 0;
  intEnable = 0;
  intForce = 0;
  gpioValue = 0;
  gpioDirection = 0;
  get intStatus() {
    return this.intRaw & this.intEnable | this.intForce;
  }
  readUint32(offset) {
    if (offset < EN2) {
      const channel = Math.floor(offset / 20);
      return this.channels[channel].readRegister(offset % 20);
    }
    switch (offset) {
      case EN2:
        return this.channels[7].en << 7 | this.channels[6].en << 6 | this.channels[5].en << 5 | this.channels[4].en << 4 | this.channels[3].en << 3 | this.channels[2].en << 2 | this.channels[1].en << 1 | this.channels[0].en << 0;
      case INTR4:
        return this.intRaw;
      case INTE2:
        return this.intEnable;
      case INTF2:
        return this.intForce;
      case INTS2:
        return this.intStatus;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    if (offset < EN2) {
      const channel = Math.floor(offset / 20);
      return this.channels[channel].writeRegister(offset % 20, value);
    }
    switch (offset) {
      case EN2:
        this.channels[7].en = value & 1 << 7;
        this.channels[6].en = value & 1 << 6;
        this.channels[5].en = value & 1 << 5;
        this.channels[4].en = value & 1 << 4;
        this.channels[3].en = value & 1 << 3;
        this.channels[2].en = value & 1 << 2;
        this.channels[1].en = value & 1 << 1;
        this.channels[0].en = value & 1 << 0;
        break;
      case INTR4:
        this.intRaw &= ~(value & INT_MASK);
        this.checkInterrupts();
        break;
      case INTE2:
        this.intEnable = value & INT_MASK;
        this.checkInterrupts();
        break;
      case INTF2:
        this.intForce = value & INT_MASK;
        this.checkInterrupts();
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
  get clockFreq() {
    return this.rp2040.clkSys;
  }
  channelInterrupt(index) {
    this.intRaw |= 1 << index;
    this.checkInterrupts();
    this.rp2040.dma.setDREQ(DREQChannel.DREQ_PWM_WRAP0 + index);
  }
  checkInterrupts() {
    this.rp2040.setInterrupt(IRQ.PWM_WRAP, !!this.intStatus);
  }
  gpioSet(index, value) {
    const bit = 1 << index;
    const newGpioValue = value ? this.gpioValue | bit : this.gpioValue & ~bit;
    if (this.gpioValue != newGpioValue) {
      this.gpioValue = newGpioValue;
      this.rp2040.gpio[index].checkForUpdates();
    }
  }
  gpioSetDir(index, output) {
    const bit = 1 << index;
    const newGpioDirection = output ? this.gpioDirection | bit : this.gpioDirection & ~bit;
    if (this.gpioDirection != newGpioDirection) {
      this.gpioDirection = newGpioDirection;
      this.rp2040.gpio[index].checkForUpdates();
    }
  }
  gpioRead(index) {
    return this.rp2040.gpio[index].inputValue;
  }
  gpioOnInput(index) {
    if (this.gpioDirection && 1 << index) {
      return;
    }
    for (const channel of this.channels) {
      if (channel.pinB1 === index || channel.pinB2 === index) {
        channel.gpioBChanged();
      }
    }
  }
  reset() {
    this.gpioDirection = 4294967295;
    for (const channel of this.channels) {
      channel.reset();
    }
  }
};

var RESET = 0;
var WDSEL2 = 4;
var RESET_DONE = 8;
var RPReset = class extends BasePeripheral {
  reset = 0;
  wdsel = 0;
  reset_done = 33554431;
  readUint32(offset) {
    switch (offset) {
      case RESET:
        return this.reset;
      case WDSEL2:
        return this.wdsel;
      case RESET_DONE:
        return this.reset_done;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case RESET:
        this.reset = value & 33554431;
        break;
      case WDSEL2:
        this.wdsel = value & 33554431;
        break;
      default:
        super.writeUint32(offset, value);
        break;
    }
  }
};

var RTC_SETUP0 = 4;
var RTC_SETUP1 = 8;
var RTC_CTRL = 12;
var IRQ_SETUP_0 = 16;
var RTC_RTC1 = 24;
var RTC_RTC0 = 28;
var RTC_ENABLE_BITS = 1;
var RTC_ACTIVE_BITS = 2;
var RTC_LOAD_BITS = 16;
var SETUP_0_YEAR_SHIFT = 12;
var SETUP_0_YEAR_MASK = 4095;
var SETUP_0_MONTH_SHIFT = 8;
var SETUP_0_MONTH_MASK = 15;
var SETUP_0_DAY_SHIFT = 0;
var SETUP_0_DAY_MASK = 31;
var SETUP_1_HOUR_SHIFT = 16;
var SETUP_1_HOUR_MASK = 31;
var SETUP_1_MIN_SHIFT = 8;
var SETUP_1_MIN_MASK = 63;
var SETUP_1_SEC_SHIFT = 0;
var SETUP_1_SEC_MASK = 63;
var RTC_0_YEAR_SHIFT = 12;
var RTC_0_YEAR_MASK = 4095;
var RTC_0_MONTH_SHIFT = 8;
var RTC_0_MONTH_MASK = 15;
var RTC_0_DAY_SHIFT = 0;
var RTC_0_DAY_MASK = 31;
var RTC_1_DOTW_SHIFT = 24;
var RTC_1_DOTW_MASK = 7;
var RTC_1_HOUR_SHIFT = 16;
var RTC_1_HOUR_MASK = 31;
var RTC_1_MIN_SHIFT = 8;
var RTC_1_MIN_MASK = 63;
var RTC_1_SEC_SHIFT = 0;
var RTC_1_SEC_MASK = 63;
var RP2040RTC = class extends BasePeripheral {
  setup0 = 0;
  setup1 = 0;
  ctrl = 0;
  baseline = new Date(2021, 0, 1);
  baselineNanos = 0;
  readUint32(offset) {
    const date = new Date(this.baseline.getTime() + (this.rp2040.clock.nanos - this.baselineNanos) / 1e6);
    switch (offset) {
      case RTC_SETUP0:
        return this.setup0;
      case RTC_SETUP1:
        return this.setup1;
      case RTC_CTRL:
        return this.ctrl;
      case IRQ_SETUP_0:
        return 0;
      case RTC_RTC1:
        return (date.getFullYear() & RTC_0_YEAR_MASK) << RTC_0_YEAR_SHIFT | (date.getMonth() + 1 & RTC_0_MONTH_MASK) << RTC_0_MONTH_SHIFT | (date.getDate() & RTC_0_DAY_MASK) << RTC_0_DAY_SHIFT;
      case RTC_RTC0:
        return (date.getDay() & RTC_1_DOTW_MASK) << RTC_1_DOTW_SHIFT | (date.getHours() & RTC_1_HOUR_MASK) << RTC_1_HOUR_SHIFT | (date.getMinutes() & RTC_1_MIN_MASK) << RTC_1_MIN_SHIFT | (date.getSeconds() & RTC_1_SEC_MASK) << RTC_1_SEC_SHIFT;
      default:
        break;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case RTC_SETUP0:
        this.setup0 = value;
        break;
      case RTC_SETUP1:
        this.setup1 = value;
        break;
      case RTC_CTRL:
        if (value & RTC_LOAD_BITS) {
          this.ctrl |= RTC_LOAD_BITS;
        }
        if (value & RTC_ENABLE_BITS) {
          this.ctrl |= RTC_ENABLE_BITS;
          this.ctrl |= RTC_ACTIVE_BITS;
          if (this.ctrl & RTC_LOAD_BITS) {
            const year = this.setup0 >> SETUP_0_YEAR_SHIFT & SETUP_0_YEAR_MASK;
            const month = this.setup0 >> SETUP_0_MONTH_SHIFT & SETUP_0_MONTH_MASK;
            const day = this.setup0 >> SETUP_0_DAY_SHIFT & SETUP_0_DAY_MASK;
            const hour = this.setup1 >> SETUP_1_HOUR_SHIFT & SETUP_1_HOUR_MASK;
            const min = this.setup1 >> SETUP_1_MIN_SHIFT & SETUP_1_MIN_MASK;
            const sec = this.setup1 >> SETUP_1_SEC_SHIFT & SETUP_1_SEC_MASK;
            this.baseline = new Date(year, month - 1, day, hour, min, sec);
            this.baselineNanos = this.rp2040.clock.nanos;
            this.ctrl &= ~RTC_LOAD_BITS;
          }
        } else {
          this.ctrl &= ~RTC_ENABLE_BITS;
          this.ctrl &= ~RTC_ACTIVE_BITS;
        }
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var SSPCR0 = 0;
var SSPCR1 = 4;
var SSPDR = 8;
var SSPSR = 12;
var SSPCPSR = 16;
var SSPIMSC = 20;
var SSPRIS = 24;
var SSPMIS = 28;
var SSPICR = 32;
var SSPDMACR = 36;
var SSPPERIPHID0 = 4064;
var SSPPERIPHID1 = 4068;
var SSPPERIPHID2 = 4072;
var SSPPERIPHID3 = 4076;
var SSPPCELLID0 = 4080;
var SSPPCELLID1 = 4084;
var SSPPCELLID2 = 4088;
var SSPPCELLID3 = 4092;
var SCR_MASK = 255;
var SCR_SHIFT = 8;
var SPH = 1 << 7;
var SPO = 1 << 6;
var DSS_MASK = 15;
var DSS_SHIFT = 0;
var SOD = 1 << 3;
var MS = 1 << 2;
var SSE = 1 << 1;
var LBM = 1 << 0;
var BSY = 1 << 4;
var RFF2 = 1 << 3;
var RNE = 1 << 2;
var TNF = 1 << 1;
var TFE2 = 1 << 0;
var CPSDVSR_MASK = 254;
var TXDMAE = 1 << 1;
var RXDMAE = 1 << 0;
var SSPTXINTR = 1 << 3;
var SSPRXINTR = 1 << 2;
var SSPRTINTR = 1 << 1;
var SSPRORINTR = 1 << 0;
var RPSPI = class extends BasePeripheral {
  irq;
  dreq;
  rxFIFO;
  txFIFO;
  // User provided callbacks
  onTransmit;
  busy;
  control0;
  control1;
  dmaControl;
  clockDivisor;
  intRaw;
  intEnable;
  get intStatus() {
    return this.intRaw & this.intEnable;
  }
  get enabled() {
    return !!(this.control1 & SSE);
  }
  /** Data size in bits: 4 to 16 bits */
  get dataBits() {
    return (this.control0 >> DSS_SHIFT & DSS_MASK) + 1;
  }
  get masterMode() {
    return !(this.control0 & MS);
  }
  get spiMode() {
    const cpol = this.control0 & SPO;
    const cpha = this.control0 & SPH;
    return cpol ? cpha ? 2 : 3 : cpha ? 1 : 0;
  }
  get clockFrequency() {
    if (!this.clockDivisor) {
      return 0;
    }
    const scr = this.control0 >> SCR_SHIFT & SCR_MASK;
    return this.rp2040.clkPeri / (this.clockDivisor * (1 + scr));
  }
  updateDMATx() {
    if (this.txFIFO.full) {
      this.rp2040.dma.clearDREQ(this.dreq.tx);
    } else {
      this.rp2040.dma.setDREQ(this.dreq.tx);
    }
  }
  updateDMARx() {
    if (this.rxFIFO.empty) {
      this.rp2040.dma.clearDREQ(this.dreq.rx);
    } else {
      this.rp2040.dma.setDREQ(this.dreq.rx);
    }
  }
  constructor(rp2040, name, irq, dreq) {
    super(rp2040, name), this.irq = irq, this.dreq = dreq, this.rxFIFO = new FIFO(8), this.txFIFO = new FIFO(8), this.onTransmit = () => this.completeTransmit(0), this.busy = false, this.control0 = 0, this.control1 = 0, this.dmaControl = 0, this.clockDivisor = 0, this.intRaw = 0, this.intEnable = 0;
    this.updateDMATx();
    this.updateDMARx();
  }
  doTX() {
    if (!this.busy && !this.txFIFO.empty) {
      const value = this.txFIFO.pull();
      this.busy = true;
      this.onTransmit(value);
      this.fifosUpdated();
    }
  }
  completeTransmit(rxValue) {
    this.busy = false;
    if (!this.rxFIFO.full) {
      this.rxFIFO.push(rxValue);
    } else {
      this.intRaw |= SSPRORINTR;
    }
    this.fifosUpdated();
    this.doTX();
  }
  checkInterrupts() {
    this.rp2040.setInterrupt(this.irq, !!this.intStatus);
  }
  fifosUpdated() {
    const prevStatus = this.intStatus;
    if (this.txFIFO.itemCount <= this.txFIFO.size / 2) {
      this.intRaw |= SSPTXINTR;
    } else {
      this.intRaw &= ~SSPTXINTR;
    }
    if (this.rxFIFO.itemCount >= this.rxFIFO.size / 2) {
      this.intRaw |= SSPRXINTR;
    } else {
      this.intRaw &= ~SSPRXINTR;
    }
    if (this.intStatus !== prevStatus) {
      this.checkInterrupts();
    }
    this.updateDMATx();
    this.updateDMARx();
  }
  readUint32(offset) {
    switch (offset) {
      case SSPCR0:
        return this.control0;
      case SSPCR1:
        return this.control1;
      case SSPDR:
        if (!this.rxFIFO.empty) {
          const value = this.rxFIFO.pull();
          this.fifosUpdated();
          return value;
        }
        return 0;
      case SSPSR:
        return (this.busy || !this.txFIFO.empty ? BSY : 0) | (this.rxFIFO.full ? RFF2 : 0) | (!this.rxFIFO.empty ? RNE : 0) | (!this.txFIFO.full ? TNF : 0) | (this.txFIFO.empty ? TFE2 : 0);
      case SSPCPSR:
        return this.clockDivisor;
      case SSPIMSC:
        return this.intEnable;
      case SSPRIS:
        return this.intRaw;
      case SSPMIS:
        return this.intStatus;
      case SSPDMACR:
        return this.dmaControl;
      case SSPPERIPHID0:
        return 34;
      case SSPPERIPHID1:
        return 16;
      case SSPPERIPHID2:
        return 52;
      case SSPPERIPHID3:
        return 0;
      case SSPPCELLID0:
        return 13;
      case SSPPCELLID1:
        return 240;
      case SSPPCELLID2:
        return 5;
      case SSPPCELLID3:
        return 177;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case SSPCR0:
        this.control0 = value;
        return;
      case SSPCR1:
        this.control1 = value;
        return;
      case SSPDR:
        if (!this.txFIFO.full) {
          this.txFIFO.push(value & (1 << this.dataBits) - 1);
          this.doTX();
          this.fifosUpdated();
        }
        return;
      case SSPCPSR:
        this.clockDivisor = value & CPSDVSR_MASK;
        return;
      case SSPIMSC:
        this.intEnable = value;
        this.checkInterrupts();
        return;
      case SSPDMACR:
        this.dmaControl = value;
        return;
      case SSPICR:
        this.intRaw &= ~(value & (SSPRTINTR | SSPRORINTR));
        this.checkInterrupts();
        return;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var SSI_CTRLR0 = 0;
var SSI_CTRLR1 = 4;
var SSI_SSIENR = 8;
var SSI_BAUDR = 20;
var SSI_TXFLR = 32;
var SSI_RXFLR = 36;
var SSI_SR = 40;
var SSI_SR_TFNF_BITS = 2;
var SSI_SR_TFE_BITS = 4;
var SSI_SR_RFNE_BITS = 8;
var SSI_IDR = 88;
var SSI_VERSION_ID = 92;
var SSI_DR0 = 96;
var SSI_RX_SAMPLE_DLY = 240;
var SSI_SPI_CTRL_R0 = 244;
var SSI_TXD_DRIVE_EDGE = 248;
var CMD_READ_STATUS = 5;
var RPSSI = class extends BasePeripheral {
  dr0 = 0;
  txflr = 0;
  rxflr = 0;
  baudr = 0;
  crtlr0 = 0;
  crtlr1 = 0;
  ssienr = 0;
  spictlr0 = 0;
  rxsampldly = 0;
  txddriveedge = 0;
  readUint32(offset) {
    switch (offset) {
      case SSI_TXFLR:
        return this.txflr;
      case SSI_RXFLR:
        return this.rxflr;
      case SSI_CTRLR0:
        return this.crtlr0;
      /*  & 0x017FFFFF = b23,b25..31 reserved */
      case SSI_CTRLR1:
        return this.crtlr1;
      case SSI_SSIENR:
        return this.ssienr;
      case SSI_BAUDR:
        return this.baudr;
      case SSI_SR:
        return SSI_SR_TFE_BITS | SSI_SR_RFNE_BITS | SSI_SR_TFNF_BITS;
      case SSI_IDR:
        return 1364414537;
      case SSI_VERSION_ID:
        return 875573546;
      case SSI_RX_SAMPLE_DLY:
        return this.rxsampldly;
      case SSI_TXD_DRIVE_EDGE:
        return this.txddriveedge;
      case SSI_SPI_CTRL_R0:
        return this.spictlr0;
      /* b6,7,10,19..23 reserved */
      case SSI_DR0:
        return this.dr0;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case SSI_TXFLR:
        this.txflr = value;
        return;
      case SSI_RXFLR:
        this.rxflr = value;
        return;
      case SSI_CTRLR0:
        this.crtlr0 = value;
        return;
      case SSI_CTRLR1:
        this.crtlr1 = value;
        return;
      case SSI_SSIENR:
        this.ssienr = value;
        return;
      case SSI_BAUDR:
        this.baudr = value;
        return;
      case SSI_RX_SAMPLE_DLY:
        this.rxsampldly = value & 255;
        return;
      case SSI_TXD_DRIVE_EDGE:
        this.txddriveedge = value & 255;
        return;
      case SSI_SPI_CTRL_R0:
        this.spictlr0 = value;
        return;
      case SSI_DR0:
        if (value === CMD_READ_STATUS) {
          this.dr0 = 0;
        }
        return;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var PROC0_NMI_MASK = 0;
var RP2040SysCfg = class extends BasePeripheral {
  readUint32(offset) {
    switch (offset) {
      case PROC0_NMI_MASK:
        return this.rp2040.core.interruptNMIMask;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case PROC0_NMI_MASK:
        this.rp2040.core.interruptNMIMask = value;
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var CHIP_ID = 0;
var PLATFORM = 4;
var GITREF_RP2040 = 64;
var RP2040SysInfo = class extends BasePeripheral {
  readUint32(offset) {
    switch (offset) {
      case CHIP_ID:
        return 268445991;
      case PLATFORM:
        return 2;
      case GITREF_RP2040:
        return 3771273960;
    }
    return super.readUint32(offset);
  }
};

var PLATFORM2 = 0;
var ASIC = 1;
var RPTBMAN = class extends BasePeripheral {
  readUint32(offset) {
    switch (offset) {
      case PLATFORM2:
        return ASIC;
      default:
        return super.readUint32(offset);
    }
  }
};

var TIMEHR = 8;
var TIMELR = 12;
var TIMERAWH = 36;
var TIMERAWL = 40;
var ALARM0 = 16;
var ALARM1 = 20;
var ALARM2 = 24;
var ALARM3 = 28;
var ARMED = 32;
var PAUSE = 48;
var INTR5 = 52;
var INTE3 = 56;
var INTF3 = 60;
var INTS3 = 64;
var ALARM_0 = 1 << 0;
var ALARM_1 = 1 << 1;
var ALARM_2 = 1 << 2;
var ALARM_3 = 1 << 3;
var timerInterrupts = [
  IRQ.TIMER_0,
  IRQ.TIMER_1,
  IRQ.TIMER_2,
  IRQ.TIMER_3
];
var RPTimerAlarm = class {
  bitValue;
  clockAlarm;
  armed;
  targetMicros;
  constructor(bitValue, clockAlarm) {
    this.bitValue = bitValue;
    this.clockAlarm = clockAlarm;
    this.armed = false;
    this.targetMicros = 0;
  }
};
var RPTimer = class extends BasePeripheral {
  clock;
  latchedTimeHigh = 0;
  alarms;
  intRaw = 0;
  intEnable = 0;
  intForce = 0;
  paused = false;
  constructor(rp2040, name) {
    super(rp2040, name);
    this.clock = rp2040.clock;
    this.alarms = [
      new RPTimerAlarm(ALARM_0, this.clock.createAlarm(() => this.fireAlarm(0))),
      new RPTimerAlarm(ALARM_1, this.clock.createAlarm(() => this.fireAlarm(1))),
      new RPTimerAlarm(ALARM_2, this.clock.createAlarm(() => this.fireAlarm(2))),
      new RPTimerAlarm(ALARM_3, this.clock.createAlarm(() => this.fireAlarm(3)))
    ];
  }
  get intStatus() {
    return this.intRaw & this.intEnable | this.intForce;
  }
  readUint32(offset) {
    const time = this.clock.nanos / 1e3;
    switch (offset) {
      case TIMEHR:
        return this.latchedTimeHigh;
      case TIMELR:
        this.latchedTimeHigh = Math.floor(time / 2 ** 32);
        return time >>> 0;
      case TIMERAWH:
        return Math.floor(time / 2 ** 32);
      case TIMERAWL:
        return time >>> 0;
      case ALARM0:
        return this.alarms[0].targetMicros;
      case ALARM1:
        return this.alarms[1].targetMicros;
      case ALARM2:
        return this.alarms[2].targetMicros;
      case ALARM3:
        return this.alarms[3].targetMicros;
      case PAUSE:
        return this.paused ? 1 : 0;
      case INTR5:
        return this.intRaw;
      case INTE3:
        return this.intEnable;
      case INTF3:
        return this.intForce;
      case INTS3:
        return this.intStatus;
      case ARMED:
        return (this.alarms[0].armed ? this.alarms[0].bitValue : 0) | (this.alarms[1].armed ? this.alarms[1].bitValue : 0) | (this.alarms[2].armed ? this.alarms[2].bitValue : 0) | (this.alarms[3].armed ? this.alarms[3].bitValue : 0);
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case ALARM0:
      case ALARM1:
      case ALARM2:
      case ALARM3: {
        const alarmIndex = (offset - ALARM0) / 4;
        const alarm = this.alarms[alarmIndex];
        const deltaMicros = value - this.clock.nanos / 1e3 >>> 0;
        alarm.armed = true;
        alarm.targetMicros = value;
        alarm.clockAlarm.schedule(deltaMicros * 1e3);
        break;
      }
      case ARMED:
        for (const alarm of this.alarms) {
          if (this.rawWriteValue & alarm.bitValue) {
            this.disarmAlarm(alarm);
          }
        }
        break;
      case PAUSE:
        this.paused = !!(value & 1);
        if (this.paused) {
          this.warn("Unimplemented Timer Pause");
        }
        break;
      case INTR5:
        this.intRaw &= ~this.rawWriteValue;
        this.checkInterrupts();
        break;
      case INTE3:
        this.intEnable = value & 15;
        this.checkInterrupts();
        break;
      case INTF3:
        this.intForce = value & 15;
        this.checkInterrupts();
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
  fireAlarm(index) {
    const alarm = this.alarms[index];
    this.disarmAlarm(alarm);
    this.intRaw |= alarm.bitValue;
    this.checkInterrupts();
  }
  checkInterrupts() {
    const { intStatus } = this;
    for (let i = 0; i < this.alarms.length; i++) {
      this.rp2040.setInterrupt(timerInterrupts[i], !!(intStatus & 1 << i));
    }
  }
  disarmAlarm(alarm) {
    alarm.clockAlarm.cancel();
    alarm.armed = false;
  }
};

var UARTDR = 0;
var UARTFR = 24;
var UARTIBRD = 36;
var UARTFBRD = 40;
var UARTLCR_H = 44;
var UARTCR = 48;
var UARTIMSC = 56;
var UARTIRIS = 60;
var UARTIMIS = 64;
var UARTICR = 68;
var UARTPERIPHID0 = 4064;
var UARTPERIPHID1 = 4068;
var UARTPERIPHID2 = 4072;
var UARTPERIPHID3 = 4076;
var UARTPCELLID0 = 4080;
var UARTPCELLID1 = 4084;
var UARTPCELLID2 = 4088;
var UARTPCELLID3 = 4092;
var TXFE = 1 << 7;
var RXFF = 1 << 6;
var RXFE = 1 << 4;
var FEN = 1 << 4;
var RXE = 1 << 9;
var TXE = 1 << 8;
var UARTEN = 1 << 0;
var UARTTXINTR = 1 << 5;
var UARTRXINTR = 1 << 4;
var RPUART = class extends BasePeripheral {
  irq;
  dreq;
  ctrlRegister;
  lineCtrlRegister;
  rxFIFO;
  interruptMask;
  interruptStatus;
  intDivisor;
  fracDivisor;
  onByte;
  onBaudRateChange;
  constructor(rp2040, name, irq, dreq) {
    super(rp2040, name), this.irq = irq, this.dreq = dreq, this.ctrlRegister = RXE | TXE, this.lineCtrlRegister = 0, this.rxFIFO = new FIFO(32), this.interruptMask = 0, this.interruptStatus = 0, this.intDivisor = 0, this.fracDivisor = 0;
  }
  get enabled() {
    return !!(this.ctrlRegister & UARTEN);
  }
  get txEnabled() {
    return !!(this.ctrlRegister & TXE);
  }
  get rxEnabled() {
    return !!(this.ctrlRegister & RXE);
  }
  get fifosEnabled() {
    return !!(this.lineCtrlRegister & FEN);
  }
  /**
   * Number of bits per UART character
   */
  get wordLength() {
    switch (this.lineCtrlRegister >>> 5 & 3) {
      case 0:
        return 5;
      case 1:
        return 6;
      case 2:
        return 7;
      case 3:
        return 8;
    }
  }
  get baudDivider() {
    return this.intDivisor + this.fracDivisor / 64;
  }
  get baudRate() {
    return Math.round(this.rp2040.clkPeri / (this.baudDivider * 16));
  }
  /** Re-reports the baud rate, unless the firmware has not set the divider yet. */
  clkPeriChanged() {
    if (this.baudDivider) {
      this.onBaudRateChange?.(this.baudRate);
    }
  }
  get flags() {
    return (this.rxFIFO.full ? RXFF : 0) | (this.rxFIFO.empty ? RXFE : 0) | TXFE;
  }
  checkInterrupts() {
    this.interruptStatus |= UARTTXINTR;
    this.rp2040.setInterrupt(this.irq, !!(this.interruptStatus & this.interruptMask));
  }
  feedByte(value) {
    this.rxFIFO.push(value);
    this.interruptStatus |= UARTRXINTR;
    this.checkInterrupts();
  }
  readUint32(offset) {
    switch (offset) {
      case UARTDR: {
        const value = this.rxFIFO.pull();
        if (!this.rxFIFO.empty) {
          this.interruptStatus |= UARTRXINTR;
        } else {
          this.interruptStatus &= ~UARTRXINTR;
        }
        this.checkInterrupts();
        return value;
      }
      case UARTFR:
        return this.flags;
      case UARTIBRD:
        return this.intDivisor;
      case UARTFBRD:
        return this.fracDivisor;
      case UARTLCR_H:
        return this.lineCtrlRegister;
      case UARTCR:
        return this.ctrlRegister;
      case UARTIMSC:
        return this.interruptMask;
      case UARTIRIS:
        return this.interruptStatus;
      case UARTIMIS:
        return this.interruptStatus & this.interruptMask;
      case UARTPERIPHID0:
        return 17;
      case UARTPERIPHID1:
        return 16;
      case UARTPERIPHID2:
        return 52;
      case UARTPERIPHID3:
        return 0;
      case UARTPCELLID0:
        return 13;
      case UARTPCELLID1:
        return 240;
      case UARTPCELLID2:
        return 5;
      case UARTPCELLID3:
        return 177;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case UARTDR:
        this.onByte?.(value & 255);
        break;
      case UARTIBRD:
        this.intDivisor = value & 65535;
        this.onBaudRateChange?.(this.baudRate);
        break;
      case UARTFBRD:
        this.fracDivisor = value & 63;
        this.onBaudRateChange?.(this.baudRate);
        break;
      case UARTLCR_H:
        this.lineCtrlRegister = value;
        break;
      case UARTCR:
        this.ctrlRegister = value;
        if (this.enabled) {
          this.rp2040.dma.setDREQ(this.dreq.tx);
        } else {
          this.rp2040.dma.clearDREQ(this.dreq.tx);
        }
        break;
      case UARTIMSC:
        this.interruptMask = value & 2047;
        this.checkInterrupts();
        break;
      case UARTICR:
        this.interruptStatus &= ~this.rawWriteValue;
        this.checkInterrupts();
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var StandardRequest = /* @__PURE__ */ function(StandardRequest2) {
  StandardRequest2[StandardRequest2["GetStatus"] = 0] = "GetStatus";
  StandardRequest2[StandardRequest2["ClearFeature"] = 1] = "ClearFeature";
  StandardRequest2[StandardRequest2["SetFeature"] = 3] = "SetFeature";
  StandardRequest2[StandardRequest2["SetAddress"] = 5] = "SetAddress";
  StandardRequest2[StandardRequest2["GetDescriptor"] = 6] = "GetDescriptor";
  StandardRequest2[StandardRequest2["SetDescriptor"] = 7] = "SetDescriptor";
  StandardRequest2[StandardRequest2["GetConfiguration"] = 8] = "GetConfiguration";
  StandardRequest2[StandardRequest2["SetConfiguration"] = 9] = "SetConfiguration";
  StandardRequest2[StandardRequest2["GetInterface"] = 10] = "GetInterface";
  StandardRequest2[StandardRequest2["SetInterface"] = 11] = "SetInterface";
  StandardRequest2[StandardRequest2["SynchFrame"] = 12] = "SynchFrame";
  return StandardRequest2;
}({});
function parseSetupPacket(setup) {
  return {
    bmRequestType: setup[0],
    bRequest: setup[1],
    wValue: setup[2] | setup[3] << 8,
    wIndex: setup[4] | setup[5] << 8,
    wLength: setup[6] | setup[7] << 8,
    // Helpers
    direction: setup[0] & 128 ? "in" : "out",
    type: setup[0] >> 5 & 3,
    recipient: setup[0] & 31
  };
}

var ENDPOINT_COUNT = 16;
var USB_HOST_INTERRUPT_ENDPOINTS = 15;
var EP1_IN_CONTROL = 8;
var EP0_IN_BUFFER_CONTROL = 128;
var EP0_OUT_BUFFER_CONTROL = 132;
var EP15_OUT_BUFFER_CONTROL = 252;
var HOST_SETUP_PACKET = 0;
var HOST_INT_EP_CTRL_BASE = 8;
var HOST_INT_EP_BUF_CTRL_BASE = 136;
var HOST_EPX_BUF_CTRL = 128;
var HOST_EPX_DATA = 384;
var USB_CTRL_DOUBLE_BUF = 1 << 30;
var USB_CTRL_INTERRUPT_PER_TRANSFER = 1 << 29;
var EP_CTRL_ENABLE_BITS = 1 << 31;
var EP_CTRL_HOST_INTERRUPT_INTERVAL_LSB = 16;
var USB_BUF_CTRL_AVAILABLE = 1 << 10;
var USB_BUF_CTRL_FULL = 1 << 15;
var USB_BUF_CTRL_LEN_MASK = 1023;
var USB_BUF_CTRL_DATA1_PID = 1 << 13;
var USB_BUF_CTRL_LAST = 1 << 14;
var USB_BUF1_SHIFT = 16;
var USB_BUF1_OFFSET = 64;
var ADDR_ENDP = 0;
var ADDR_ENDP1 = 4;
var ADDR_ENDP15 = 60;
var MAIN_CTRL = 64;
var SOF_WR = 68;
var SOF_RD = 72;
var SIE_CTRL = 76;
var SIE_STATUS = 80;
var INT_EP_CTRL = 84;
var BUFF_STATUS = 88;
var BUFF_CPU_SHOULD_HANDLE = 92;
var EP_ABORT = 96;
var EP_ABORT_DONE = 100;
var EP_STALL_ARM = 104;
var NAK_POLL = 108;
var EP_STATUS_STALL_NAK = 112;
var USB_MUXING = 116;
var USB_PWR = 120;
var INTR6 = 140;
var INTE4 = 144;
var INTF4 = 148;
var INTS4 = 152;
var SIM_TIMING = 1 << 31;
var HOST_NDEVICE = 1 << 1;
var CONTROLLER_EN = 1 << 0;
var SIE_CTRL_EP0_INT_STALL = 1 << 31;
var SIE_CTRL_EP0_DOUBLE_BUF = 1 << 30;
var SIE_CTRL_EP0_INT_1BUF = 1 << 29;
var SIE_CTRL_EP0_INT_2BUF = 1 << 28;
var SIE_CTRL_EP0_INT_NAK = 1 << 27;
var SIE_CTRL_DIRECT_EN = 1 << 26;
var SIE_CTRL_DIRECT_DP = 1 << 25;
var SIE_CTRL_DIRECT_DM = 1 << 24;
var SIE_CTRL_TRANSCEIVER_PD = 1 << 18;
var SIE_CTRL_RPU_OPT = 1 << 17;
var SIE_CTRL_PULLUP_EN = 1 << 16;
var SIE_CTRL_PULLDOWN_EN = 1 << 15;
var SIE_CTRL_RESET_BUS = 1 << 13;
var SIE_CTRL_RESUME = 1 << 12;
var SIE_CTRL_VBUS_EN = 1 << 11;
var SIE_CTRL_KEEP_ALIVE_EN = 1 << 10;
var SIE_CTRL_SOF_EN = 1 << 9;
var SIE_CTRL_SOF_SYNC = 1 << 8;
var SIE_CTRL_PREAMBLE_EN = 1 << 6;
var SIE_CTRL_STOP_TRANS = 1 << 4;
var SIE_CTRL_RECEIVE_DATA = 1 << 3;
var SIE_CTRL_SEND_DATA = 1 << 2;
var SIE_CTRL_SEND_SETUP = 1 << 1;
var SIE_CTRL_START_TRANS = 1 << 0;
var SIE_DATA_SEQ_ERROR = 1 << 31;
var SIE_ACK_REC = 1 << 30;
var SIE_STALL_REC = 1 << 29;
var SIE_NAK_REC = 1 << 28;
var SIE_RX_TIMEOUT = 1 << 27;
var SIE_RX_OVERFLOW = 1 << 26;
var SIE_BIT_STUFF_ERROR = 1 << 25;
var SIE_CRC_ERROR = 1 << 24;
var SIE_BUS_RESET = 1 << 19;
var SIE_TRANS_COMPLETE = 1 << 18;
var SIE_SETUP_REC = 1 << 17;
var SIE_CONNECTED = 1 << 16;
var SIE_RESUME = 1 << 11;
var SIE_VBUS_OVER_CURR = 1 << 10;
var SIE_SPEED = 1 << 9;
var SIE_SPEED_FS_VALUE = 2;
var SIE_SUSPENDED = 1 << 4;
var SIE_LINE_STATE_MASK = 3;
var SIE_LINE_STATE_SHIFT = 2;
var SIE_VBUS_DETECTED = 1 << 0;
var SOFTCON = 1 << 3;
var TO_DIGITAL_PAD = 1 << 2;
var TO_EXTPHY = 1 << 1;
var TO_PHY = 1 << 0;
var PWR_VBUS_DETECT = 1 << 3;
var PWR_VBUS_DETECT_OVERRIDE_EN = 1 << 2;
var PWR_OVERCURR_DETECT = 1 << 1;
var PWR_OVERCURR_DETECT_EN = 1 << 0;
var INTR_EP_STALL_NAK = 1 << 19;
var INTR_ABORT_DONE = 1 << 18;
var INTR_DEV_SOF = 1 << 17;
var INTR_SETUP_REQ = 1 << 16;
var INTR_DEV_RESUME_FROM_HOST = 1 << 15;
var INTR_DEV_SUSPEND = 1 << 14;
var INTR_DEV_CONN_DIS = 1 << 13;
var INTR_BUS_RESET = 1 << 12;
var INTR_VBUS_DETECT = 1 << 11;
var INTR_STALL = 1 << 10;
var INTR_ERROR_CRC = 1 << 9;
var INTR_ERROR_BIT_STUFF = 1 << 8;
var INTR_ERROR_RX_OVERFLOW = 1 << 7;
var INTR_ERROR_RX_TIMEOUT = 1 << 6;
var INTR_ERROR_DATA_SEQ = 1 << 5;
var INTR_BUFF_STATUS = 1 << 4;
var INTR_TRANS_COMPLETE = 1 << 3;
var INTR_HOST_SOF = 1 << 2;
var INTR_HOST_RESUME = 1 << 1;
var INTR_HOST_CONN_DIS = 1 << 0;
var SIELineState = /* @__PURE__ */ function(SIELineState2) {
  SIELineState2[SIELineState2["SE0"] = 0] = "SE0";
  SIELineState2[SIELineState2["J"] = 1] = "J";
  SIELineState2[SIELineState2["K"] = 2] = "K";
  SIELineState2[SIELineState2["SE1"] = 3] = "SE1";
  return SIELineState2;
}(SIELineState || {});
var SIE_WRITECLEAR_MASK = SIE_DATA_SEQ_ERROR | SIE_ACK_REC | SIE_STALL_REC | SIE_NAK_REC | SIE_RX_TIMEOUT | SIE_RX_OVERFLOW | SIE_BIT_STUFF_ERROR | SIE_CONNECTED | SIE_CRC_ERROR | SIE_BUS_RESET | SIE_TRANS_COMPLETE | SIE_SETUP_REC | SIE_RESUME;
var USBEndpointAlarm = class {
  alarm;
  buffers;
  constructor(alarm) {
    this.alarm = alarm;
    this.buffers = [];
  }
  schedule(buffer, delayNanos) {
    this.buffers.push(buffer);
    this.alarm.schedule(delayNanos);
  }
};
var RPUSBController = class extends BasePeripheral {
  // Common registers
  addrEndp = 0;
  mainCtrl = 0;
  intRaw = 0;
  intEnable = 0;
  intForce = 0;
  sieStatus = 0;
  buffStatus = 0;
  // Host mode registers
  sieCtrl = 0;
  sofFrameNumber = 0;
  devAddrCtrl = 0;
  intEpAddrCtrl = new Array(USB_HOST_INTERRUPT_ENDPOINTS).fill(0);
  intEpCtrl = 0;
  usbPwr = 0;
  nakPoll = 0;
  epAbort = 0;
  epAbortDone = 0;
  epStallArm = 0;
  epStatusStallNak = 0;
  // Host mode state
  hostMode = false;
  sofEnabled = false;
  connectedDevice = null;
  pendingSetupResponse = null;
  controlDataPid = 1;
  expectingStatusPhase = false;
  // Alarms
  endpointReadAlarms;
  endpointWriteAlarms;
  resetAlarm;
  sofAlarm;
  hostTransactionAlarm;
  // Device mode callbacks
  onUSBEnabled;
  onResetReceived;
  onEndpointWrite;
  onEndpointRead;
  readDelayMicroseconds = 10;
  writeDelayMicroseconds = 10;
  hostTransactionDelayMicroseconds = 5;
  get intStatus() {
    return this.intRaw & this.intEnable | this.intForce;
  }
  constructor(rp2040, name) {
    super(rp2040, name);
    const clock = rp2040.clock;
    this.endpointReadAlarms = [];
    this.endpointWriteAlarms = [];
    for (let i = 0; i < ENDPOINT_COUNT; ++i) {
      this.endpointReadAlarms.push(new USBEndpointAlarm(clock.createAlarm(() => {
        const buffer = this.endpointReadAlarms[i].buffers.shift();
        if (buffer) {
          this.finishRead(i, buffer);
        }
      })));
      this.endpointWriteAlarms.push(new USBEndpointAlarm(clock.createAlarm(() => {
        for (const buffer of this.endpointWriteAlarms[i].buffers) {
          this.onEndpointWrite?.(i, buffer);
        }
        this.endpointWriteAlarms[i].buffers = [];
      })));
    }
    this.resetAlarm = clock.createAlarm(() => {
      this.sieStatus |= SIE_BUS_RESET;
      this.sieStatusUpdated();
    });
    this.sofAlarm = clock.createAlarm(() => {
      this.generateSOF();
    });
    this.hostTransactionAlarm = clock.createAlarm(() => {
      this.completeHostTransaction();
    });
  }
  readUint32(offset) {
    if (offset >= ADDR_ENDP1 && offset <= ADDR_ENDP15 && (offset & 3) === 0) {
      const epIndex = offset - ADDR_ENDP1 >> 2;
      return this.intEpAddrCtrl[epIndex];
    }
    switch (offset) {
      case ADDR_ENDP:
        return this.hostMode ? this.devAddrCtrl : this.addrEndp & 491647;
      case MAIN_CTRL:
        return this.mainCtrl;
      case SOF_WR:
        return 0;
      // Write-only
      case SOF_RD:
        return this.sofFrameNumber & 2047;
      case SIE_CTRL:
        return this.sieCtrl;
      case SIE_STATUS:
        if (this.hostMode && this.intRaw & INTR_HOST_CONN_DIS) {
          this.intRaw &= ~INTR_HOST_CONN_DIS;
          this.checkInterrupts();
        }
        return this.sieStatus;
      case INT_EP_CTRL:
        return this.intEpCtrl;
      case BUFF_STATUS:
        return this.buffStatus;
      case BUFF_CPU_SHOULD_HANDLE:
        return 0;
      case EP_ABORT:
        return this.epAbort;
      case EP_ABORT_DONE:
        return this.epAbortDone;
      case EP_STALL_ARM:
        return this.epStallArm;
      case NAK_POLL:
        return this.nakPoll;
      case EP_STATUS_STALL_NAK:
        return this.epStatusStallNak;
      case USB_PWR:
        return this.usbPwr;
      case INTR6:
        return this.intRaw;
      case INTE4:
        return this.intEnable;
      case INTF4:
        return this.intForce;
      case INTS4:
        return this.intStatus;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    if (offset >= ADDR_ENDP1 && offset <= ADDR_ENDP15 && (offset & 3) === 0) {
      const epIndex = offset - ADDR_ENDP1 >> 2;
      this.intEpAddrCtrl[epIndex] = value;
      return;
    }
    switch (offset) {
      case ADDR_ENDP:
        if (this.hostMode) {
          this.devAddrCtrl = value;
        } else {
          this.addrEndp = value;
        }
        break;
      case MAIN_CTRL:
        this.mainCtrl = value & (SIM_TIMING | CONTROLLER_EN | HOST_NDEVICE);
        this.hostMode = !!(value & HOST_NDEVICE);
        if (value & CONTROLLER_EN) {
          if (this.hostMode) {
            this.debug("USB Host mode enabled");
            if (this.connectedDevice) {
              this.onDeviceConnected();
            }
          } else {
            this.onUSBEnabled?.();
          }
        }
        break;
      case SOF_WR:
        this.sofFrameNumber = value & 2047;
        break;
      case SIE_CTRL:
        this.handleSieCtrlWrite(value);
        break;
      case INT_EP_CTRL:
        this.intEpCtrl = value;
        break;
      case BUFF_STATUS:
        this.buffStatus &= ~this.rawWriteValue;
        this.buffStatusUpdated();
        break;
      case EP_ABORT:
        this.epAbort = value;
        this.epAbortDone |= value;
        break;
      case EP_ABORT_DONE:
        this.epAbortDone &= ~this.rawWriteValue;
        break;
      case EP_STALL_ARM:
        this.epStallArm = value;
        break;
      case NAK_POLL:
        this.nakPoll = value;
        break;
      case EP_STATUS_STALL_NAK:
        this.epStatusStallNak &= ~this.rawWriteValue;
        break;
      case USB_MUXING:
        if (value & TO_DIGITAL_PAD && !(value & TO_PHY)) {
          this.sieStatus |= SIE_CONNECTED;
        }
        break;
      case USB_PWR:
        this.usbPwr = value;
        if (value & PWR_VBUS_DETECT_OVERRIDE_EN) {
          if (value & PWR_VBUS_DETECT) {
            this.sieStatus |= SIE_VBUS_DETECTED;
          } else {
            this.sieStatus &= ~SIE_VBUS_DETECTED;
          }
        }
        break;
      case SIE_STATUS:
        this.sieStatus &= ~(this.rawWriteValue & SIE_WRITECLEAR_MASK);
        if (this.rawWriteValue & SIE_BUS_RESET) {
          if (!this.hostMode) {
            this.onResetReceived?.();
          }
          this.sieStatus &= ~(SIE_LINE_STATE_MASK << SIE_LINE_STATE_SHIFT);
          this.sieStatus |= SIELineState.J << SIE_LINE_STATE_SHIFT | SIE_CONNECTED;
        }
        this.sieStatusUpdated();
        break;
      case INTE4:
        this.intEnable = value & 1048575;
        this.checkInterrupts();
        break;
      case INTF4:
        this.intForce = value & 1048575;
        this.checkInterrupts();
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
  readEndpointControlReg(endpoint, out) {
    const controlRegOffset = EP1_IN_CONTROL + 8 * (endpoint - 1) + (out ? 4 : 0);
    return this.rp2040.usbDPRAMView.getUint32(controlRegOffset, true);
  }
  getEndpointBufferOffset(endpoint, out) {
    if (endpoint === 0) {
      return 256;
    }
    return this.readEndpointControlReg(endpoint, out) & 65472;
  }
  DPRAMUpdated(offset, value) {
    if (this.hostMode) {
      return;
    }
    if (value & USB_BUF_CTRL_AVAILABLE && offset >= EP0_IN_BUFFER_CONTROL && offset <= EP15_OUT_BUFFER_CONTROL) {
      const endpoint = offset - EP0_IN_BUFFER_CONTROL >> 3;
      const bufferOut = offset & 4 ? true : false;
      let doubleBuffer = false;
      let interrupt = true;
      if (endpoint != 0) {
        const control = this.readEndpointControlReg(endpoint, bufferOut);
        doubleBuffer = !!(control & USB_CTRL_DOUBLE_BUF);
        interrupt = !!(control & USB_CTRL_INTERRUPT_PER_TRANSFER);
      }
      if (doubleBuffer && value >> USB_BUF1_SHIFT & USB_BUF_CTRL_AVAILABLE) {
        const bufferLength2 = value >> USB_BUF1_SHIFT & USB_BUF_CTRL_LEN_MASK;
        const bufferOffset2 = this.getEndpointBufferOffset(endpoint, bufferOut) + USB_BUF1_OFFSET;
        this.debug(`Start USB transfer, endPoint=${endpoint}, direction=${bufferOut ? "out" : "in"} buffer=${bufferOffset2.toString(16)} length=${bufferLength2}`);
        value &= ~(USB_BUF_CTRL_AVAILABLE << USB_BUF1_SHIFT);
        this.rp2040.usbDPRAMView.setUint32(offset, value, true);
        if (bufferOut) {
          this.onEndpointRead?.(endpoint, bufferLength2);
        } else {
          value &= ~(USB_BUF_CTRL_FULL << USB_BUF1_SHIFT);
          this.rp2040.usbDPRAMView.setUint32(offset, value, true);
          const buffer = this.rp2040.usbDPRAM.slice(bufferOffset2, bufferOffset2 + bufferLength2);
          this.indicateBufferReady(endpoint, false);
          this.endpointWriteAlarms[endpoint].schedule(buffer, this.writeDelayMicroseconds * 1e3);
        }
      }
      const bufferLength = value & USB_BUF_CTRL_LEN_MASK;
      const bufferOffset = this.getEndpointBufferOffset(endpoint, bufferOut);
      this.debug(`Start USB transfer, endPoint=${endpoint}, direction=${bufferOut ? "out" : "in"} buffer=${bufferOffset.toString(16)} length=${bufferLength}`);
      value &= ~USB_BUF_CTRL_AVAILABLE;
      this.rp2040.usbDPRAMView.setUint32(offset, value, true);
      if (bufferOut) {
        this.onEndpointRead?.(endpoint, bufferLength);
      } else {
        value &= ~USB_BUF_CTRL_FULL;
        this.rp2040.usbDPRAMView.setUint32(offset, value, true);
        const buffer = this.rp2040.usbDPRAM.slice(bufferOffset, bufferOffset + bufferLength);
        if (interrupt || !doubleBuffer) {
          this.indicateBufferReady(endpoint, false);
        }
        this.endpointWriteAlarms[endpoint].schedule(buffer, this.writeDelayMicroseconds * 1e3);
      }
    }
  }
  endpointReadDone(endpoint, buffer, delay = this.readDelayMicroseconds) {
    this.endpointReadAlarms[endpoint].schedule(buffer, delay * 1e3);
  }
  finishRead(endpoint, buffer) {
    const bufferOffset = this.getEndpointBufferOffset(endpoint, true);
    const bufControlReg = EP0_OUT_BUFFER_CONTROL + endpoint * 8;
    let bufControl = this.rp2040.usbDPRAMView.getUint32(bufControlReg, true);
    const requestedLength = bufControl & USB_BUF_CTRL_LEN_MASK;
    const newLength = Math.min(buffer.length, requestedLength);
    bufControl |= USB_BUF_CTRL_FULL;
    bufControl = bufControl & ~USB_BUF_CTRL_LEN_MASK | newLength & USB_BUF_CTRL_LEN_MASK;
    this.rp2040.usbDPRAMView.setUint32(bufControlReg, bufControl, true);
    this.rp2040.usbDPRAM.set(buffer.subarray(0, newLength), bufferOffset);
    this.indicateBufferReady(endpoint, true);
  }
  checkInterrupts() {
    const { intStatus } = this;
    this.rp2040.setInterrupt(IRQ.USBCTRL, !!intStatus);
  }
  resetDevice() {
    this.resetAlarm.schedule(1e7);
  }
  sendSetupPacket(setupPacket) {
    this.rp2040.usbDPRAM.set(setupPacket);
    this.sieStatus |= SIE_SETUP_REC;
    this.sieStatusUpdated();
  }
  indicateBufferReady(endpoint, out) {
    this.buffStatus |= 1 << endpoint * 2 + (out ? 1 : 0);
    this.buffStatusUpdated();
  }
  buffStatusUpdated() {
    if (this.buffStatus) {
      this.intRaw |= INTR_BUFF_STATUS;
    } else {
      this.intRaw &= ~INTR_BUFF_STATUS;
    }
    this.checkInterrupts();
  }
  sieStatusUpdated() {
    if (this.hostMode) {
      const intRegisterMap = [
        [
          SIE_TRANS_COMPLETE,
          INTR_TRANS_COMPLETE
        ],
        [
          SIE_STALL_REC,
          INTR_STALL
        ],
        [
          SIE_CRC_ERROR,
          INTR_ERROR_CRC
        ],
        [
          SIE_BIT_STUFF_ERROR,
          INTR_ERROR_BIT_STUFF
        ],
        [
          SIE_RX_OVERFLOW,
          INTR_ERROR_RX_OVERFLOW
        ],
        [
          SIE_RX_TIMEOUT,
          INTR_ERROR_RX_TIMEOUT
        ],
        [
          SIE_DATA_SEQ_ERROR,
          INTR_ERROR_DATA_SEQ
        ]
      ];
      for (const [sieBit, intRawBit] of intRegisterMap) {
        if (this.sieStatus & sieBit) {
          this.intRaw |= intRawBit;
        } else {
          this.intRaw &= ~intRawBit;
        }
      }
    } else {
      const intRegisterMap = [
        [
          SIE_SETUP_REC,
          INTR_SETUP_REQ
        ],
        [
          SIE_RESUME,
          INTR_DEV_RESUME_FROM_HOST
        ],
        [
          SIE_SUSPENDED,
          INTR_DEV_SUSPEND
        ],
        [
          SIE_CONNECTED,
          INTR_DEV_CONN_DIS
        ],
        [
          SIE_BUS_RESET,
          INTR_BUS_RESET
        ],
        [
          SIE_VBUS_DETECTED,
          INTR_VBUS_DETECT
        ],
        [
          SIE_STALL_REC,
          INTR_STALL
        ],
        [
          SIE_CRC_ERROR,
          INTR_ERROR_CRC
        ],
        [
          SIE_BIT_STUFF_ERROR,
          INTR_ERROR_BIT_STUFF
        ],
        [
          SIE_RX_OVERFLOW,
          INTR_ERROR_RX_OVERFLOW
        ],
        [
          SIE_RX_TIMEOUT,
          INTR_ERROR_RX_TIMEOUT
        ],
        [
          SIE_DATA_SEQ_ERROR,
          INTR_ERROR_DATA_SEQ
        ]
      ];
      for (const [sieBit, intRawBit] of intRegisterMap) {
        if (this.sieStatus & sieBit) {
          this.intRaw |= intRawBit;
        } else {
          this.intRaw &= ~intRawBit;
        }
      }
    }
    this.checkInterrupts();
  }
  // ============ Host Mode Methods ============
  /**
   * Connect a simulated USB device to the host controller.
   */
  connectDevice(device) {
    this.connectedDevice = device;
    if (this.hostMode && this.mainCtrl & CONTROLLER_EN) {
      this.onDeviceConnected();
    }
  }
  /**
   * Disconnect the simulated USB device from the host controller.
   */
  disconnectDevice() {
    if (this.connectedDevice && this.hostMode) {
      this.connectedDevice = null;
      this.sieStatus &= ~(3 << 8);
      this.intRaw |= INTR_HOST_CONN_DIS;
      this.checkInterrupts();
    }
    this.connectedDevice = null;
  }
  onDeviceConnected() {
    this.sieStatus &= ~(3 << 8);
    this.sieStatus |= SIE_SPEED_FS_VALUE << 8;
    this.intRaw |= INTR_HOST_CONN_DIS;
    this.checkInterrupts();
    this.debug("USB device connected (full-speed)");
  }
  handleSieCtrlWrite(value) {
    this.sieCtrl = value;
    if (value & SIE_CTRL_SOF_EN && !this.sofEnabled) {
      this.sofEnabled = true;
      this.scheduleSofPacket();
      this.debug("SOF generation enabled");
    } else if (!(value & SIE_CTRL_SOF_EN) && this.sofEnabled) {
      this.sofEnabled = false;
      this.debug("SOF generation disabled");
    }
    if (value & SIE_CTRL_RESET_BUS) {
      this.debug("USB bus reset initiated");
      if (this.connectedDevice) {
        this.connectedDevice.onReset();
      }
      this.controlDataPid = 1;
    }
    if (value & SIE_CTRL_START_TRANS) {
      this.startHostTransaction();
    }
  }
  scheduleSofPacket() {
    if (this.sofEnabled) {
      this.sofAlarm.schedule(1e6);
    }
  }
  generateSOF() {
    this.sofFrameNumber = this.sofFrameNumber + 1 & 2047;
    this.intRaw |= INTR_HOST_SOF;
    this.checkInterrupts();
    this.pollInterruptEndpoints();
    this.scheduleSofPacket();
  }
  pollInterruptEndpoints() {
    if (this.intEpCtrl && this.sofFrameNumber % 100 === 0) {
      this.debug(`INT_EP poll: intEpCtrl=0x${this.intEpCtrl.toString(16)} sofFrame=${this.sofFrameNumber}`);
    }
    for (let i = 0; i < USB_HOST_INTERRUPT_ENDPOINTS; i++) {
      const epCtrlBit = 1 << i + 1;
      if (!(this.intEpCtrl & epCtrlBit)) continue;
      const addrEndp = this.intEpAddrCtrl[i];
      const devAddr = addrEndp & 127;
      const epNum = addrEndp >> 16 & 15;
      const isOut = !!(addrEndp & 1 << 25);
      if (this.sofFrameNumber % 500 === 0) {
        this.debug(`INT_EP[${i}]: addrEndp=0x${addrEndp.toString(16)} devAddr=${devAddr} epNum=${epNum} connectedAddr=${this.connectedDevice?.address}`);
      }
      if (!this.connectedDevice || this.connectedDevice.address !== devAddr) continue;
      const epCtrlOffset = HOST_INT_EP_CTRL_BASE + i * 8;
      const epCtrl = this.rp2040.usbDPRAMView.getUint32(epCtrlOffset, true);
      const interval = (epCtrl >> EP_CTRL_HOST_INTERRUPT_INTERVAL_LSB & 511) + 1;
      if (this.sofFrameNumber % interval !== 0) continue;
      const bufCtrlOffset = HOST_INT_EP_BUF_CTRL_BASE + i * 8;
      const bufCtrl = this.rp2040.usbDPRAMView.getUint32(bufCtrlOffset, true);
      if (this.sofFrameNumber % 500 === 0) {
        this.debug(`INT_EP[${i}]: bufCtrl=0x${bufCtrl.toString(16)} available=${!!(bufCtrl & USB_BUF_CTRL_AVAILABLE)}`);
      }
      if (!(bufCtrl & USB_BUF_CTRL_AVAILABLE)) continue;
      if (!isOut) {
        const epAddr = 128 | epNum;
        const result = this.connectedDevice.handleDataIn(epAddr);
        if (result.status === "ack" && result.data) {
          const bufferOffset = epCtrl & 65472;
          this.rp2040.usbDPRAM.set(result.data, bufferOffset);
          let newBufCtrl = bufCtrl & ~USB_BUF_CTRL_AVAILABLE;
          newBufCtrl |= USB_BUF_CTRL_FULL;
          newBufCtrl = newBufCtrl & ~USB_BUF_CTRL_LEN_MASK | result.data.length & USB_BUF_CTRL_LEN_MASK;
          this.rp2040.usbDPRAMView.setUint32(bufCtrlOffset, newBufCtrl, true);
          this.buffStatus |= 1 << (i + 1) * 2;
          this.buffStatusUpdated();
        }
      }
    }
  }
  startHostTransaction() {
    if (!this.hostMode) return;
    const devAddr = this.devAddrCtrl & 127;
    const epNum = this.devAddrCtrl >> 16 & 15;
    this.debug(`Host transaction: dev=${devAddr} ep=${epNum} sieCtrl=0x${this.sieCtrl.toString(16)}`);
    if (!this.connectedDevice) {
      this.sieStatus |= SIE_RX_TIMEOUT;
      this.sieStatusUpdated();
      return;
    }
    if (this.sieCtrl & SIE_CTRL_SEND_SETUP) {
      this.handleSetupTransaction(devAddr);
    } else if (this.sieCtrl & SIE_CTRL_RECEIVE_DATA) {
      this.handleInTransaction(devAddr, epNum);
    } else if (this.sieCtrl & SIE_CTRL_SEND_DATA) {
      this.handleOutTransaction(devAddr, epNum);
    }
  }
  handleSetupTransaction(_devAddr) {
    const setupPacket = this.rp2040.usbDPRAM.slice(HOST_SETUP_PACKET, HOST_SETUP_PACKET + 8);
    const setup = parseSetupPacket(setupPacket);
    this.debug(`SETUP: bmRequestType=0x${setup.bmRequestType.toString(16)} bRequest=${setup.bRequest} wValue=0x${setup.wValue.toString(16)} wIndex=${setup.wIndex} wLength=${setup.wLength}`);
    const result = this.connectedDevice.handleSetupPacket(setupPacket);
    if (setup.bRequest === StandardRequest.SetAddress && setup.type === 0) {
      const newAddr = setup.wValue & 127;
      this.connectedDevice.address = newAddr;
      this.connectedDevice.onAddressAssigned?.(newAddr);
      this.debug(`Device address set to ${newAddr}`);
    }
    if (setup.direction === "in" && result.data) {
      this.pendingSetupResponse = result.data;
      this.expectingStatusPhase = false;
    } else {
      this.pendingSetupResponse = null;
      this.expectingStatusPhase = true;
    }
    this.controlDataPid = 1;
    this.sieStatus |= SIE_ACK_REC;
    this.hostTransactionAlarm.schedule(this.hostTransactionDelayMicroseconds * 1e3);
  }
  handleInTransaction(devAddr, epNum) {
    const epAddr = 128 | epNum;
    let result;
    if (epNum === 0 && this.expectingStatusPhase) {
      result = {
        status: "ack",
        data: new Uint8Array(0)
      };
      this.expectingStatusPhase = false;
      this.debug("Control OUT status phase (ZLP)");
    } else if (epNum === 0 && this.pendingSetupResponse) {
      result = {
        status: "ack",
        data: this.pendingSetupResponse
      };
      const bufCtrl = this.rp2040.usbDPRAMView.getUint32(HOST_EPX_BUF_CTRL, true);
      const maxLen = bufCtrl & USB_BUF_CTRL_LEN_MASK;
      if (result.data && result.data.length > maxLen) {
        result.data = result.data.slice(0, maxLen);
      }
      if (!result.data || result.data.length <= maxLen) {
        this.pendingSetupResponse = null;
      }
    } else {
      result = this.connectedDevice.handleDataIn(epAddr);
    }
    if (result.status === "ack" && result.data) {
      this.rp2040.usbDPRAM.set(result.data, HOST_EPX_DATA);
      let bufCtrl = this.rp2040.usbDPRAMView.getUint32(HOST_EPX_BUF_CTRL, true);
      bufCtrl &= ~USB_BUF_CTRL_LEN_MASK;
      bufCtrl |= result.data.length & USB_BUF_CTRL_LEN_MASK;
      bufCtrl |= USB_BUF_CTRL_FULL;
      bufCtrl &= ~USB_BUF_CTRL_AVAILABLE;
      if (this.controlDataPid) {
        bufCtrl |= USB_BUF_CTRL_DATA1_PID;
      } else {
        bufCtrl &= ~USB_BUF_CTRL_DATA1_PID;
      }
      this.controlDataPid ^= 1;
      this.rp2040.usbDPRAMView.setUint32(HOST_EPX_BUF_CTRL, bufCtrl, true);
      this.buffStatus |= 1;
      this.buffStatusUpdated();
      this.sieStatus |= SIE_ACK_REC;
    } else if (result.status === "nak") {
      this.sieStatus |= SIE_NAK_REC;
    } else if (result.status === "stall") {
      this.sieStatus |= SIE_STALL_REC;
    }
    this.hostTransactionAlarm.schedule(this.hostTransactionDelayMicroseconds * 1e3);
  }
  handleOutTransaction(devAddr, epNum) {
    const epAddr = epNum;
    const bufCtrl = this.rp2040.usbDPRAMView.getUint32(HOST_EPX_BUF_CTRL, true);
    const dataLen = bufCtrl & USB_BUF_CTRL_LEN_MASK;
    const data = this.rp2040.usbDPRAM.slice(HOST_EPX_DATA, HOST_EPX_DATA + dataLen);
    let result;
    if (epNum === 0 && dataLen === 0) {
      result = {
        status: "ack"
      };
    } else {
      result = this.connectedDevice.handleDataOut(epAddr, data);
    }
    let newBufCtrl = bufCtrl;
    newBufCtrl &= ~USB_BUF_CTRL_AVAILABLE;
    if (this.controlDataPid) {
      newBufCtrl |= USB_BUF_CTRL_DATA1_PID;
    } else {
      newBufCtrl &= ~USB_BUF_CTRL_DATA1_PID;
    }
    this.controlDataPid ^= 1;
    this.rp2040.usbDPRAMView.setUint32(HOST_EPX_BUF_CTRL, newBufCtrl, true);
    if (result.status === "ack") {
      this.sieStatus |= SIE_ACK_REC;
      this.buffStatus |= 1;
      this.buffStatusUpdated();
    } else if (result.status === "nak") {
      this.sieStatus |= SIE_NAK_REC;
    } else if (result.status === "stall") {
      this.sieStatus |= SIE_STALL_REC;
    }
    this.hostTransactionAlarm.schedule(this.hostTransactionDelayMicroseconds * 1e3);
  }
  completeHostTransaction() {
    this.sieCtrl &= ~SIE_CTRL_START_TRANS;
    this.sieStatus |= SIE_TRANS_COMPLETE;
    this.sieStatusUpdated();
    this.debug("Host transaction complete");
  }
};

var CTRL2 = 0;
var LOAD = 4;
var REASON = 8;
var SCRATCH0 = 12;
var SCRATCH1 = 16;
var SCRATCH2 = 20;
var SCRATCH3 = 24;
var SCRATCH4 = 28;
var SCRATCH5 = 32;
var SCRATCH6 = 36;
var SCRATCH7 = 40;
var TICK = 44;
var TRIGGER = 1 << 31;
var ENABLE2 = 1 << 30;
var PAUSE_DBG1 = 1 << 26;
var PAUSE_DBG0 = 1 << 25;
var PAUSE_JTAG = 1 << 24;
var TIME_MASK = 16777215;
var TIME_SHIFT = 0;
var LOAD_MASK = 16777215;
var LOAD_SHIFT = 0;
var FORCE = 1 << 1;
var TIMER = 1 << 0;
var RUNNING = 1 << 10;
var TICK_ENABLE = 1 << 9;
var TICK_FREQUENCY = 2e6;
var RPWatchdog = class extends BasePeripheral {
  timer;
  alarm;
  scratchData = new Uint32Array(8);
  enable = false;
  tickEnable = true;
  reason = 0;
  pauseDbg0 = true;
  pauseDbg1 = true;
  pauseJtag = true;
  /** Called when the watchdog triggers - override with your own soft reset implementation */
  onWatchdogTrigger = () => {
    this.rp2040.logger.warn(this.name, "Watchdog triggered, but no reset handler provided");
  };
  // User provided
  constructor(rp2040, name) {
    super(rp2040, name);
    this.timer = new Timer32(rp2040.clock, TICK_FREQUENCY);
    this.timer.mode = TimerMode.Decrement;
    this.timer.enable = false;
    this.alarm = new Timer32PeriodicAlarm(this.timer, () => {
      this.reason = TIMER;
      this.onWatchdogTrigger?.();
    });
    this.alarm.target = 0;
    this.alarm.enable = false;
  }
  readUint32(offset) {
    switch (offset) {
      case CTRL2:
        return (this.timer.enable ? ENABLE2 : 0) | (this.pauseDbg0 ? PAUSE_DBG0 : 0) | (this.pauseDbg1 ? PAUSE_DBG1 : 0) | (this.pauseJtag ? PAUSE_JTAG : 0) | (this.timer.counter & TIME_MASK) << TIME_SHIFT;
      case REASON:
        return this.reason;
      case SCRATCH0:
      case SCRATCH1:
      case SCRATCH2:
      case SCRATCH3:
      case SCRATCH4:
      case SCRATCH5:
      case SCRATCH6:
      case SCRATCH7:
        return this.scratchData[offset - SCRATCH0 >> 2];
      case TICK:
        return this.tickEnable ? RUNNING | TICK_ENABLE : 0;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case CTRL2:
        if (value & TRIGGER) {
          this.reason = FORCE;
          this.onWatchdogTrigger?.();
        }
        this.enable = !!(value & ENABLE2);
        this.timer.enable = this.enable && this.tickEnable;
        this.alarm.enable = this.enable && this.tickEnable;
        this.pauseDbg0 = !!(value & PAUSE_DBG0);
        this.pauseDbg1 = !!(value & PAUSE_DBG1);
        this.pauseJtag = !!(value & PAUSE_JTAG);
        break;
      case LOAD:
        this.timer.set(value >>> LOAD_SHIFT & LOAD_MASK);
        break;
      case SCRATCH0:
      case SCRATCH1:
      case SCRATCH2:
      case SCRATCH3:
      case SCRATCH4:
      case SCRATCH5:
      case SCRATCH6:
      case SCRATCH7:
        this.scratchData[offset - SCRATCH0 >> 2] = value;
        break;
      case TICK:
        this.tickEnable = !!(value & TICK_ENABLE);
        this.timer.enable = this.enable && this.tickEnable;
        this.alarm.enable = this.enable && this.tickEnable;
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

var XOSC_CTRL = 0;
var XOSC_STATUS = 4;
var XOSC_DORMANT = 8;
var XOSC_STARTUP = 12;
var XOSC_COUNT = 28;
var CTRL_ENABLE_LSB = 12;
var CTRL_ENABLE_BITS = 16773120;
var CTRL_FREQ_RANGE_BITS = 4095;
var CTRL_ENABLE_DISABLE = 3358;
var CTRL_ENABLE_ENABLE = 4011;
var STATUS_STABLE = 2147483648;
var STATUS_BADWRITE = 16777216;
var STATUS_ENABLED = 4096;
var DORMANT_VALUE = 1668246881;
var WAKE_VALUE = 2002873189;
var STARTUP_X4 = 1048576;
var STARTUP_DELAY_BITS = 16383;
var RPXOSC = class extends BasePeripheral {
  ctrl = 0;
  status = 0;
  dormant = 0;
  startup = 0;
  count = 0;
  enabled = false;
  stable = false;
  isDormant = false;
  readUint32(offset) {
    switch (offset) {
      case XOSC_CTRL:
        return this.ctrl;
      case XOSC_STATUS: {
        let status = this.status;
        if (this.stable) {
          status |= STATUS_STABLE;
        }
        if (this.enabled) {
          status |= STATUS_ENABLED;
        }
        return status;
      }
      case XOSC_DORMANT:
        return this.dormant;
      case XOSC_STARTUP:
        return this.startup;
      case XOSC_COUNT:
        return this.count;
    }
    return super.readUint32(offset);
  }
  writeUint32(offset, value) {
    switch (offset) {
      case XOSC_CTRL: {
        this.ctrl = value;
        const enableValue = (value & CTRL_ENABLE_BITS) >>> CTRL_ENABLE_LSB;
        const freqRange = value & CTRL_FREQ_RANGE_BITS;
        void freqRange;
        if (enableValue === CTRL_ENABLE_ENABLE) {
          if (!this.isDormant) {
            this.enabled = true;
            this.stable = true;
          }
        } else if (enableValue === CTRL_ENABLE_DISABLE) {
          this.enabled = false;
          this.stable = false;
        } else if (enableValue !== 0) {
          this.status |= STATUS_BADWRITE;
          this.warn(`Invalid ENABLE value written: 0x${enableValue.toString(16)}`);
        }
        break;
      }
      case XOSC_STATUS:
        if (value & STATUS_BADWRITE) {
          this.status &= ~STATUS_BADWRITE;
        }
        break;
      case XOSC_DORMANT:
        if (value === DORMANT_VALUE) {
          this.isDormant = true;
          this.stable = false;
        } else if (value === WAKE_VALUE) {
          this.isDormant = false;
          if (this.enabled) {
            this.stable = true;
          }
        }
        this.dormant = value;
        break;
      case XOSC_STARTUP:
        this.startup = value & (STARTUP_X4 | STARTUP_DELAY_BITS);
        break;
      case XOSC_COUNT:
        this.count = value & 255;
        break;
      default:
        super.writeUint32(offset, value);
    }
  }
};

function s32(n) {
  return n | 0;
}
function u32(n) {
  return n >>> 0;
}

var InterpolatorConfig = class {
  shift = 0;
  maskLSB = 0;
  maskMSB = 0;
  signed = false;
  crossInput = false;
  crossResult = false;
  addRaw = false;
  forceMSB = 0;
  blend = false;
  clamp = false;
  overf0 = false;
  overf1 = false;
  overf = false;
  constructor(value) {
    this.shift = value >>> 0 & 31;
    this.maskLSB = value >>> 5 & 31;
    this.maskMSB = value >>> 10 & 31;
    this.signed = Boolean(value >>> 15 & 1);
    this.crossInput = Boolean(value >>> 16 & 1);
    this.crossResult = Boolean(value >>> 17 & 1);
    this.addRaw = Boolean(value >>> 18 & 1);
    this.forceMSB = value >>> 19 & 3;
    this.blend = Boolean(value >>> 21 & 1);
    this.clamp = Boolean(value >>> 22 & 1);
    this.overf0 = Boolean(value >>> 23 & 1);
    this.overf1 = Boolean(value >>> 24 & 1);
    this.overf = Boolean(value >>> 25 & 1);
  }
  toUint32() {
    return (this.shift & 31) << 0 | (this.maskLSB & 31) << 5 | (this.maskMSB & 31) << 10 | (Number(this.signed) & 1) << 15 | (Number(this.crossInput) & 1) << 16 | (Number(this.crossResult) & 1) << 17 | (Number(this.addRaw) & 1) << 18 | (this.forceMSB & 3) << 19 | (Number(this.blend) & 1) << 21 | (Number(this.clamp) & 1) << 22 | (Number(this.overf0) & 1) << 23 | (Number(this.overf1) & 1) << 24 | (Number(this.overf) & 1) << 25;
  }
};
var Interpolator = class {
  index;
  accum0;
  accum1;
  base0;
  base1;
  base2;
  ctrl0;
  ctrl1;
  result0;
  result1;
  result2;
  smresult0;
  smresult1;
  constructor(index) {
    this.index = index;
    this.accum0 = 0;
    this.accum1 = 0;
    this.base0 = 0;
    this.base1 = 0;
    this.base2 = 0;
    this.ctrl0 = 0;
    this.ctrl1 = 0;
    this.result0 = 0;
    this.result1 = 0;
    this.result2 = 0;
    this.smresult0 = 0;
    this.smresult1 = 0;
    this.update();
  }
  update() {
    const N = this.index;
    const ctrl0 = new InterpolatorConfig(this.ctrl0);
    const ctrl1 = new InterpolatorConfig(this.ctrl1);
    const do_clamp = ctrl0.clamp && N == 1;
    const do_blend = ctrl0.blend && N == 0;
    ctrl0.clamp = do_clamp;
    ctrl0.blend = do_blend;
    ctrl1.clamp = false;
    ctrl1.blend = false;
    ctrl1.overf0 = false;
    ctrl1.overf1 = false;
    ctrl1.overf = false;
    const input0 = s32(ctrl0.crossInput ? this.accum1 : this.accum0);
    const input1 = s32(ctrl1.crossInput ? this.accum0 : this.accum1);
    const msbmask0 = ctrl0.maskMSB == 31 ? 4294967295 : (1 << ctrl0.maskMSB + 1) - 1;
    const msbmask1 = ctrl1.maskMSB == 31 ? 4294967295 : (1 << ctrl1.maskMSB + 1) - 1;
    const mask0 = msbmask0 & ~((1 << ctrl0.maskLSB) - 1);
    const mask1 = msbmask1 & ~((1 << ctrl1.maskLSB) - 1);
    const uresult0 = input0 >>> ctrl0.shift & mask0;
    const uresult1 = input1 >>> ctrl1.shift & mask1;
    const overf0 = Boolean(input0 >>> ctrl0.shift & ~msbmask0);
    const overf1 = Boolean(input1 >>> ctrl1.shift & ~msbmask1);
    const overf = overf0 || overf1;
    const sextmask0 = uresult0 & 1 << ctrl0.maskMSB ? -1 << ctrl0.maskMSB : 0;
    const sextmask1 = uresult1 & 1 << ctrl1.maskMSB ? -1 << ctrl1.maskMSB : 0;
    const sresult0 = uresult0 | sextmask0;
    const sresult1 = uresult1 | sextmask1;
    const result0 = ctrl0.signed ? sresult0 : uresult0;
    const result1 = ctrl1.signed ? sresult1 : uresult1;
    const addresult0 = this.base0 + (ctrl0.addRaw ? input0 : result0);
    const addresult1 = this.base1 + (ctrl1.addRaw ? input1 : result1);
    const addresult2 = this.base2 + result0 + (do_blend ? 0 : result1);
    const uclamp0 = u32(result0) < u32(this.base0) ? this.base0 : u32(result0) > u32(this.base1) ? this.base1 : result0;
    const sclamp0 = s32(result0) < s32(this.base0) ? this.base0 : s32(result0) > s32(this.base1) ? this.base1 : result0;
    const clamp0 = ctrl0.signed ? sclamp0 : uclamp0;
    const alpha1 = result1 & 255;
    const ublend1 = u32(this.base0) + (Math.floor(alpha1 * (u32(this.base1) - u32(this.base0)) / 256) | 0);
    const sblend1 = s32(this.base0) + (Math.floor(alpha1 * (s32(this.base1) - s32(this.base0)) / 256) | 0);
    const blend1 = ctrl1.signed ? sblend1 : ublend1;
    this.smresult0 = u32(result0);
    this.smresult1 = u32(result1);
    this.result0 = u32(do_blend ? alpha1 : (do_clamp ? clamp0 : addresult0) | ctrl0.forceMSB << 28);
    this.result1 = u32((do_blend ? blend1 : addresult1) | ctrl0.forceMSB << 28);
    this.result2 = u32(addresult2);
    ctrl0.overf0 = overf0;
    ctrl0.overf1 = overf1;
    ctrl0.overf = overf;
    this.ctrl0 = ctrl0.toUint32();
    this.ctrl1 = ctrl1.toUint32();
  }
  writeback() {
    const ctrl0 = new InterpolatorConfig(this.ctrl0);
    const ctrl1 = new InterpolatorConfig(this.ctrl1);
    this.accum0 = u32(ctrl0.crossResult ? this.result1 : this.result0);
    this.accum1 = u32(ctrl1.crossResult ? this.result0 : this.result1);
    this.update();
  }
  setBase01(value) {
    const N = this.index;
    const ctrl0 = new InterpolatorConfig(this.ctrl0);
    const ctrl1 = new InterpolatorConfig(this.ctrl1);
    const do_blend = ctrl0.blend && N == 0;
    const input0 = value & 65535;
    const input1 = value >>> 16 & 65535;
    const sextmask0 = input0 & 1 << 15 ? -1 << 15 : 0;
    const sextmask1 = input1 & 1 << 15 ? -1 << 15 : 0;
    const base0 = (do_blend ? ctrl1.signed : ctrl0.signed) ? input0 | sextmask0 : input0;
    const base1 = ctrl1.signed ? input1 | sextmask1 : input1;
    this.base0 = u32(base0);
    this.base1 = u32(base1);
    this.update();
  }
};

var CPUID2 = 0;
var GPIO_IN = 4;
var GPIO_HI_IN = 8;
var GPIO_OUT = 16;
var GPIO_OUT_SET = 20;
var GPIO_OUT_CLR = 24;
var GPIO_OUT_XOR = 28;
var GPIO_OE = 32;
var GPIO_OE_SET = 36;
var GPIO_OE_CLR = 40;
var GPIO_OE_XOR = 44;
var GPIO_HI_OUT = 48;
var GPIO_HI_OUT_SET = 52;
var GPIO_HI_OUT_CLR = 56;
var GPIO_HI_OUT_XOR = 60;
var GPIO_HI_OE = 64;
var GPIO_HI_OE_SET = 68;
var GPIO_HI_OE_CLR = 72;
var GPIO_HI_OE_XOR = 76;
var GPIO_MASK = 1073741823;
var DIV_UDIVIDEND = 96;
var DIV_UDIVISOR = 100;
var DIV_SDIVIDEND = 104;
var DIV_SDIVISOR = 108;
var DIV_QUOTIENT = 112;
var DIV_REMAINDER = 116;
var DIV_CSR = 120;
var INTERP0_ACCUM0 = 128;
var INTERP0_ACCUM1 = 132;
var INTERP0_BASE0 = 136;
var INTERP0_BASE1 = 140;
var INTERP0_BASE2 = 144;
var INTERP0_POP_LANE0 = 148;
var INTERP0_POP_LANE1 = 152;
var INTERP0_POP_FULL = 156;
var INTERP0_PEEK_LANE0 = 160;
var INTERP0_PEEK_LANE1 = 164;
var INTERP0_PEEK_FULL = 168;
var INTERP0_CTRL_LANE0 = 172;
var INTERP0_CTRL_LANE1 = 176;
var INTERP0_ACCUM0_ADD = 180;
var INTERP0_ACCUM1_ADD = 184;
var INTERP0_BASE_1AND0 = 188;
var INTERP1_ACCUM0 = 192;
var INTERP1_ACCUM1 = 196;
var INTERP1_BASE0 = 200;
var INTERP1_BASE1 = 204;
var INTERP1_BASE2 = 208;
var INTERP1_POP_LANE0 = 212;
var INTERP1_POP_LANE1 = 216;
var INTERP1_POP_FULL = 220;
var INTERP1_PEEK_LANE0 = 224;
var INTERP1_PEEK_LANE1 = 228;
var INTERP1_PEEK_FULL = 232;
var INTERP1_CTRL_LANE0 = 236;
var INTERP1_CTRL_LANE1 = 240;
var INTERP1_ACCUM0_ADD = 244;
var INTERP1_ACCUM1_ADD = 248;
var INTERP1_BASE_1AND0 = 252;
var SPINLOCK_ST = 92;
var SPINLOCK0 = 256;
var SPINLOCK31 = 380;
var RPSIO = class {
  rp2040;
  gpioValue;
  gpioOutputEnable;
  qspiGpioValue;
  qspiGpioOutputEnable;
  divDividend;
  divDivisor;
  divQuotient;
  divRemainder;
  divCSR;
  spinLock;
  interp0;
  interp1;
  constructor(rp2040) {
    this.rp2040 = rp2040;
    this.gpioValue = 0;
    this.gpioOutputEnable = 0;
    this.qspiGpioValue = 0;
    this.qspiGpioOutputEnable = 0;
    this.divDividend = 0;
    this.divDivisor = 1;
    this.divQuotient = 0;
    this.divRemainder = 0;
    this.divCSR = 0;
    this.spinLock = 0;
    this.interp0 = new Interpolator(0);
    this.interp1 = new Interpolator(1);
  }
  updateHardwareDivider(signed) {
    if (this.divDivisor == 0) {
      this.divQuotient = this.divDividend > 0 ? -1 : 1;
      this.divRemainder = this.divDividend;
    } else {
      if (signed) {
        this.divQuotient = (this.divDividend | 0) / (this.divDivisor | 0);
        this.divRemainder = (this.divDividend | 0) % (this.divDivisor | 0);
      } else {
        this.divQuotient = (this.divDividend >>> 0) / (this.divDivisor >>> 0);
        this.divRemainder = (this.divDividend >>> 0) % (this.divDivisor >>> 0);
      }
    }
    this.divCSR = 3;
    this.rp2040.core.cycles += 8;
  }
  readUint32(offset) {
    if (offset >= SPINLOCK0 && offset <= SPINLOCK31) {
      const bitIndexMask = 1 << (offset - SPINLOCK0) / 4;
      if (this.spinLock & bitIndexMask) {
        return 0;
      } else {
        this.spinLock |= bitIndexMask;
        return bitIndexMask;
      }
    }
    switch (offset) {
      case GPIO_IN:
        return this.rp2040.gpioValues;
      case GPIO_HI_IN: {
        const { qspi } = this.rp2040;
        let result = 0;
        for (let qspiIndex = 0; qspiIndex < qspi.length; qspiIndex++) {
          if (qspi[qspiIndex].inputValue) {
            result |= 1 << qspiIndex;
          }
        }
        return result;
      }
      case GPIO_OUT:
        return this.gpioValue;
      case GPIO_OE:
        return this.gpioOutputEnable;
      case GPIO_HI_OUT:
        return this.qspiGpioValue;
      case GPIO_HI_OE:
        return this.qspiGpioOutputEnable;
      case GPIO_OUT_SET:
      case GPIO_OUT_CLR:
      case GPIO_OUT_XOR:
      case GPIO_OE_SET:
      case GPIO_OE_CLR:
      case GPIO_OE_XOR:
      case GPIO_HI_OUT_SET:
      case GPIO_HI_OUT_CLR:
      case GPIO_HI_OUT_XOR:
      case GPIO_HI_OE_SET:
      case GPIO_HI_OE_CLR:
      case GPIO_HI_OE_XOR:
        return 0;
      // TODO verify with silicone
      case CPUID2:
        return 0;
      case SPINLOCK_ST:
        return this.spinLock;
      case DIV_UDIVIDEND:
        return this.divDividend;
      case DIV_SDIVIDEND:
        return this.divDividend;
      case DIV_UDIVISOR:
        return this.divDivisor;
      case DIV_SDIVISOR:
        return this.divDivisor;
      case DIV_QUOTIENT:
        this.divCSR &= ~2;
        return this.divQuotient;
      case DIV_REMAINDER:
        return this.divRemainder;
      case DIV_CSR:
        return this.divCSR;
      case INTERP0_ACCUM0:
        return this.interp0.accum0;
      case INTERP0_ACCUM1:
        return this.interp0.accum1;
      case INTERP0_BASE0:
        return this.interp0.base0;
      case INTERP0_BASE1:
        return this.interp0.base1;
      case INTERP0_BASE2:
        return this.interp0.base2;
      case INTERP0_CTRL_LANE0:
        return this.interp0.ctrl0;
      case INTERP0_CTRL_LANE1:
        return this.interp0.ctrl1;
      case INTERP0_PEEK_LANE0:
        return this.interp0.result0;
      case INTERP0_PEEK_LANE1:
        return this.interp0.result1;
      case INTERP0_PEEK_FULL:
        return this.interp0.result2;
      case INTERP0_POP_LANE0: {
        const value = this.interp0.result0;
        this.interp0.writeback();
        return value;
      }
      case INTERP0_POP_LANE1: {
        const value = this.interp0.result1;
        this.interp0.writeback();
        return value;
      }
      case INTERP0_POP_FULL: {
        const value = this.interp0.result2;
        this.interp0.writeback();
        return value;
      }
      case INTERP0_ACCUM0_ADD:
        return this.interp0.smresult0;
      case INTERP0_ACCUM1_ADD:
        return this.interp0.smresult1;
      case INTERP1_ACCUM0:
        return this.interp1.accum0;
      case INTERP1_ACCUM1:
        return this.interp1.accum1;
      case INTERP1_BASE0:
        return this.interp1.base0;
      case INTERP1_BASE1:
        return this.interp1.base1;
      case INTERP1_BASE2:
        return this.interp1.base2;
      case INTERP1_CTRL_LANE0:
        return this.interp1.ctrl0;
      case INTERP1_CTRL_LANE1:
        return this.interp1.ctrl1;
      case INTERP1_PEEK_LANE0:
        return this.interp1.result0;
      case INTERP1_PEEK_LANE1:
        return this.interp1.result1;
      case INTERP1_PEEK_FULL:
        return this.interp1.result2;
      case INTERP1_POP_LANE0: {
        const value = this.interp1.result0;
        this.interp1.writeback();
        return value;
      }
      case INTERP1_POP_LANE1: {
        const value = this.interp1.result1;
        this.interp1.writeback();
        return value;
      }
      case INTERP1_POP_FULL: {
        const value = this.interp1.result2;
        this.interp1.writeback();
        return value;
      }
      case INTERP1_ACCUM0_ADD:
        return this.interp1.smresult0;
      case INTERP1_ACCUM1_ADD:
        return this.interp1.smresult1;
    }
    console.warn(`Read from invalid SIO address: ${offset.toString(16)}`);
    return 4294967295;
  }
  writeUint32(offset, value) {
    if (offset >= SPINLOCK0 && offset <= SPINLOCK31) {
      const bitIndexMask = ~(1 << (offset - SPINLOCK0) / 4);
      this.spinLock &= bitIndexMask;
      return;
    }
    const prevGpioValue = this.gpioValue;
    const prevGpioOutputEnable = this.gpioOutputEnable;
    switch (offset) {
      case GPIO_OUT:
        this.gpioValue = value & GPIO_MASK;
        break;
      case GPIO_OUT_SET:
        this.gpioValue |= value & GPIO_MASK;
        break;
      case GPIO_OUT_CLR:
        this.gpioValue &= ~value;
        break;
      case GPIO_OUT_XOR:
        this.gpioValue ^= value & GPIO_MASK;
        break;
      case GPIO_OE:
        this.gpioOutputEnable = value & GPIO_MASK;
        break;
      case GPIO_OE_SET:
        this.gpioOutputEnable |= value & GPIO_MASK;
        break;
      case GPIO_OE_CLR:
        this.gpioOutputEnable &= ~value;
        break;
      case GPIO_OE_XOR:
        this.gpioOutputEnable ^= value & GPIO_MASK;
        break;
      case GPIO_HI_OUT:
        this.qspiGpioValue = value & GPIO_MASK;
        break;
      case GPIO_HI_OUT_SET:
        this.qspiGpioValue |= value & GPIO_MASK;
        break;
      case GPIO_HI_OUT_CLR:
        this.qspiGpioValue &= ~value;
        break;
      case GPIO_HI_OUT_XOR:
        this.qspiGpioValue ^= value & GPIO_MASK;
        break;
      case GPIO_HI_OE:
        this.qspiGpioOutputEnable = value & GPIO_MASK;
        break;
      case GPIO_HI_OE_SET:
        this.qspiGpioOutputEnable |= value & GPIO_MASK;
        break;
      case GPIO_HI_OE_CLR:
        this.qspiGpioOutputEnable &= ~value;
        break;
      case GPIO_HI_OE_XOR:
        this.qspiGpioOutputEnable ^= value & GPIO_MASK;
        break;
      case DIV_UDIVIDEND:
        this.divDividend = value;
        this.updateHardwareDivider(false);
        break;
      case DIV_SDIVIDEND:
        this.divDividend = value;
        this.updateHardwareDivider(true);
        break;
      case DIV_UDIVISOR:
        this.divDivisor = value;
        this.updateHardwareDivider(false);
        break;
      case DIV_SDIVISOR:
        this.divDivisor = value;
        this.updateHardwareDivider(true);
        break;
      case DIV_QUOTIENT:
        this.divQuotient = value;
        this.divCSR = 3;
        break;
      case DIV_REMAINDER:
        this.divRemainder = value;
        this.divCSR = 3;
        break;
      case INTERP0_ACCUM0:
        this.interp0.accum0 = value;
        this.interp0.update();
        break;
      case INTERP0_ACCUM1:
        this.interp0.accum1 = value;
        this.interp0.update();
        break;
      case INTERP0_BASE0:
        this.interp0.base0 = value;
        this.interp0.update();
        break;
      case INTERP0_BASE1:
        this.interp0.base1 = value;
        this.interp0.update();
        break;
      case INTERP0_BASE2:
        this.interp0.base2 = value;
        this.interp0.update();
        break;
      case INTERP0_CTRL_LANE0:
        this.interp0.ctrl0 = value;
        this.interp0.update();
        break;
      case INTERP0_CTRL_LANE1:
        this.interp0.ctrl1 = value;
        this.interp0.update();
        break;
      case INTERP0_ACCUM0_ADD:
        this.interp0.accum0 += value;
        this.interp0.update();
        break;
      case INTERP0_ACCUM1_ADD:
        this.interp0.accum1 += value;
        this.interp0.update();
        break;
      case INTERP0_BASE_1AND0:
        this.interp0.setBase01(value);
        break;
      case INTERP1_ACCUM0:
        this.interp1.accum0 = value;
        this.interp1.update();
        break;
      case INTERP1_ACCUM1:
        this.interp1.accum1 = value;
        this.interp1.update();
        break;
      case INTERP1_BASE0:
        this.interp1.base0 = value;
        this.interp1.update();
        break;
      case INTERP1_BASE1:
        this.interp1.base1 = value;
        this.interp1.update();
        break;
      case INTERP1_BASE2:
        this.interp1.base2 = value;
        this.interp1.update();
        break;
      case INTERP1_CTRL_LANE0:
        this.interp1.ctrl0 = value;
        this.interp1.update();
        break;
      case INTERP1_CTRL_LANE1:
        this.interp1.ctrl1 = value;
        this.interp1.update();
        break;
      case INTERP1_ACCUM0_ADD:
        this.interp1.accum0 += value;
        this.interp1.update();
        break;
      case INTERP1_ACCUM1_ADD:
        this.interp1.accum1 += value;
        this.interp1.update();
        break;
      case INTERP1_BASE_1AND0:
        this.interp1.setBase01(value);
        break;
      default:
        console.warn(`Write to invalid SIO address: ${offset.toString(16)}, value=${value.toString(16)}`);
    }
    const pinsToUpdate = this.gpioValue ^ prevGpioValue | this.gpioOutputEnable ^ prevGpioOutputEnable;
    if (pinsToUpdate) {
      const { gpio } = this.rp2040;
      for (let gpioIndex = 0; gpioIndex < gpio.length; gpioIndex++) {
        if (pinsToUpdate & 1 << gpioIndex) {
          gpio[gpioIndex].checkForUpdates();
        }
      }
    }
  }
};

function getCurrentMicroseconds() {
  if (typeof performance != "undefined") {
    return Math.floor(performance.now() * 1e3);
  } else {
    return Math.floor(eval("require")("perf_hooks").performance.now() * 1e3);
  }
}
function leftPad(value, minLength, padChar = " ") {
  if (value.length < minLength) {
    value = padChar + value;
  }
  return value;
}
function rightPad(value, minLength, padChar = " ") {
  if (value.length < minLength) {
    value += padChar;
  }
  return value;
}
function formatTime(date) {
  const hours = leftPad(date.getHours().toString(), 2, "0");
  const minutes = leftPad(date.getMinutes().toString(), 2, "0");
  const seconds = leftPad(date.getSeconds().toString(), 2, "0");
  const milliseconds = rightPad(date.getMilliseconds().toString(), 3);
  return `${hours}:${minutes}:${seconds}.${milliseconds}`;
}

var LogLevel = /* @__PURE__ */ function(LogLevel2) {
  LogLevel2[LogLevel2["Debug"] = 0] = "Debug";
  LogLevel2[LogLevel2["Info"] = 1] = "Info";
  LogLevel2[LogLevel2["Warn"] = 2] = "Warn";
  LogLevel2[LogLevel2["Error"] = 3] = "Error";
  return LogLevel2;
}({});
var ConsoleLogger = class {
  currentLogLevel;
  throwOnError;
  constructor(currentLogLevel, throwOnError = true) {
    this.currentLogLevel = currentLogLevel;
    this.throwOnError = throwOnError;
  }
  aboveLogLevel(logLevel) {
    return logLevel >= this.currentLogLevel ? true : false;
  }
  formatMessage(componentName, message) {
    const currentTime = formatTime(/* @__PURE__ */ new Date());
    return `${currentTime} [${componentName}] ${message}`;
  }
  debug(componetName, message) {
    if (this.aboveLogLevel(LogLevel.Debug)) {
      console.debug(this.formatMessage(componetName, message));
    }
  }
  warn(componetName, message) {
    if (this.aboveLogLevel(LogLevel.Warn)) {
      console.warn(this.formatMessage(componetName, message));
    }
  }
  error(componentName, message) {
    if (this.aboveLogLevel(LogLevel.Error)) {
      console.error(this.formatMessage(componentName, message));
      if (this.throwOnError) {
        throw new Error(`[${componentName}] ${message}`);
      }
    }
  }
  info(componentName, message) {
    if (this.aboveLogLevel(LogLevel.Info)) {
      console.info(this.formatMessage(componentName, message));
    }
  }
};

var FLASH_START_ADDRESS = 268435456;
var FLASH_END_ADDRESS = 335544320;
var RAM_START_ADDRESS = 536870912;
var APB_START_ADDRESS = 1073741824;
var DPRAM_START_ADDRESS = 1343225856;
var SIO_START_ADDRESS = 3489660928;
var LOG_NAME2 = "RP2040";
var KB = 1024;
var MB = 1024 * KB;
var MHz = 1e6;
var RP2040 = class {
  clock;
  bootrom;
  sram;
  sramView;
  flash;
  flash16;
  flashView;
  usbDPRAM;
  usbDPRAMView;
  core;
  /* Clocks */
  clkSys;
  clkPeri;
  /** Crystal oscillator frequency. 12 MHz on the Raspberry Pi Pico and most other boards. */
  xoscFreq;
  /** Ring oscillator frequency. Varies with voltage/temperature on real silicon. */
  roscFreq;
  pllSys;
  pllUsb;
  clocks;
  ppb;
  sio;
  uart;
  i2c;
  pwm;
  adc;
  gpio;
  qspi;
  dma;
  pio;
  usbCtrl;
  spi;
  logger;
  clockListeners;
  peripherals;
  // Debugging
  // eslint-disable-next-line @typescript-eslint/no-unused-vars
  onBreak;
  constructor(clock = new SimulationClock()) {
    this.clock = clock;
    this.bootrom = new Uint32Array(4 * KB);
    this.sram = new Uint8Array(264 * KB);
    this.sramView = new DataView(this.sram.buffer);
    this.flash = new Uint8Array(16 * MB);
    this.flash16 = new Uint16Array(this.flash.buffer);
    this.flashView = new DataView(this.flash.buffer);
    this.usbDPRAM = new Uint8Array(4 * KB);
    this.usbDPRAMView = new DataView(this.usbDPRAM.buffer);
    this.core = new CortexM0Core(this);
    this.clkSys = 125 * MHz;
    this.clkPeri = 125 * MHz;
    this.xoscFreq = 12 * MHz;
    this.roscFreq = 6.5 * MHz;
    this.pllSys = new RPPLL(this, "PLL_SYS_BASE");
    this.pllUsb = new RPPLL(this, "PLL_USB_BASE");
    this.clocks = new RPClocks(this, "CLOCKS_BASE");
    this.ppb = new RPPPB(this, "PPB");
    this.sio = new RPSIO(this);
    this.uart = [
      new RPUART(this, "UART0", IRQ.UART0, {
        rx: DREQChannel.DREQ_UART0_RX,
        tx: DREQChannel.DREQ_UART0_TX
      }),
      new RPUART(this, "UART1", IRQ.UART1, {
        rx: DREQChannel.DREQ_UART1_RX,
        tx: DREQChannel.DREQ_UART1_TX
      })
    ];
    this.i2c = [
      new RPI2C(this, "I2C0", IRQ.I2C0),
      new RPI2C(this, "I2C1", IRQ.I2C1)
    ];
    this.pwm = new RPPWM(this, "PWM_BASE");
    this.adc = new RPADC(this, "ADC");
    this.gpio = [
      new GPIOPin(this, 0),
      new GPIOPin(this, 1),
      new GPIOPin(this, 2),
      new GPIOPin(this, 3),
      new GPIOPin(this, 4),
      new GPIOPin(this, 5),
      new GPIOPin(this, 6),
      new GPIOPin(this, 7),
      new GPIOPin(this, 8),
      new GPIOPin(this, 9),
      new GPIOPin(this, 10),
      new GPIOPin(this, 11),
      new GPIOPin(this, 12),
      new GPIOPin(this, 13),
      new GPIOPin(this, 14),
      new GPIOPin(this, 15),
      new GPIOPin(this, 16),
      new GPIOPin(this, 17),
      new GPIOPin(this, 18),
      new GPIOPin(this, 19),
      new GPIOPin(this, 20),
      new GPIOPin(this, 21),
      new GPIOPin(this, 22),
      new GPIOPin(this, 23),
      new GPIOPin(this, 24),
      new GPIOPin(this, 25),
      new GPIOPin(this, 26),
      new GPIOPin(this, 27),
      new GPIOPin(this, 28),
      new GPIOPin(this, 29)
    ];
    this.qspi = [
      new GPIOPin(this, 0, "SCLK"),
      new GPIOPin(this, 1, "SS"),
      new GPIOPin(this, 2, "SD0"),
      new GPIOPin(this, 3, "SD1"),
      new GPIOPin(this, 4, "SD2"),
      new GPIOPin(this, 5, "SD3")
    ];
    this.dma = new RPDMA(this, "DMA");
    this.pio = [
      new RPPIO(this, "PIO0", IRQ.PIO0_IRQ0, 0),
      new RPPIO(this, "PIO1", IRQ.PIO1_IRQ0, 1)
    ];
    this.usbCtrl = new RPUSBController(this, "USB");
    this.spi = [
      new RPSPI(this, "SPI0", IRQ.SPI0, {
        rx: DREQChannel.DREQ_SPI0_RX,
        tx: DREQChannel.DREQ_SPI0_TX
      }),
      new RPSPI(this, "SPI1", IRQ.SPI1, {
        rx: DREQChannel.DREQ_SPI1_RX,
        tx: DREQChannel.DREQ_SPI1_TX
      })
    ];
    this.logger = new ConsoleLogger(LogLevel.Debug, true);
    this.clockListeners = /* @__PURE__ */ new Set();
    this.peripherals = {
      98304: new RPSSI(this, "SSI"),
      262144: new RP2040SysInfo(this, "SYSINFO_BASE"),
      262148: new RP2040SysCfg(this, "SYSCFG"),
      262152: this.clocks,
      262156: new RPReset(this, "RESETS_BASE"),
      262160: new RPPSM(this, "PSM_BASE"),
      262164: new RPIO(this, "IO_BANK0_BASE"),
      262168: new UnimplementedPeripheral(this, "IO_QSPI_BASE"),
      262172: new RPPADS(this, "PADS_BANK0_BASE", "bank0"),
      262176: new RPPADS(this, "PADS_QSPI_BASE", "qspi"),
      262180: new RPXOSC(this, "XOSC_BASE"),
      262184: this.pllSys,
      262188: this.pllUsb,
      262192: new RPBUSCTRL(this, "BUSCTRL_BASE"),
      262196: this.uart[0],
      262200: this.uart[1],
      262204: this.spi[0],
      262208: this.spi[1],
      262212: this.i2c[0],
      262216: this.i2c[1],
      262220: this.adc,
      262224: this.pwm,
      262228: new RPTimer(this, "TIMER_BASE"),
      262232: new RPWatchdog(this, "WATCHDOG_BASE"),
      262236: new RP2040RTC(this, "RTC_BASE"),
      262240: new UnimplementedPeripheral(this, "ROSC_BASE"),
      262244: new UnimplementedPeripheral(this, "VREG_AND_CHIP_RESET_BASE"),
      262252: new RPTBMAN(this, "TBMAN_BASE"),
      327680: this.dma,
      327952: this.usbCtrl,
      328192: this.pio[0],
      328448: this.pio[1]
    };
    this.onBreak = (code) => {
    };
    this.reset();
  }
  loadBootrom(bootromData) {
    this.bootrom.set(bootromData);
    this.reset();
  }
  reset() {
    this.core.reset();
    this.pwm.reset();
    this.flash.fill(255);
  }
  readUint32(address) {
    address = address >>> 0;
    if (address & 3) {
      this.logger.error(LOG_NAME2, `read from address ${address.toString(16)}, which is not 32 bit aligned`);
    }
    const { bootrom } = this;
    if (address < bootrom.length * 4) {
      return bootrom[address / 4];
    } else if (address >= FLASH_START_ADDRESS && address < FLASH_END_ADDRESS) {
      const offset = address & 16777215;
      if (offset + 3 < this.flash.length) {
        return this.flashView.getUint32(offset, true);
      }
    } else if (address >= RAM_START_ADDRESS && address + 3 < RAM_START_ADDRESS + this.sram.length) {
      return this.sramView.getUint32(address - RAM_START_ADDRESS, true);
    } else if (address >= DPRAM_START_ADDRESS && address + 3 < DPRAM_START_ADDRESS + this.usbDPRAM.length) {
      return this.usbDPRAMView.getUint32(address - DPRAM_START_ADDRESS, true);
    } else if (address >>> 12 === 917518) {
      return this.ppb.readUint32(address & 4095);
    } else if (address >= SIO_START_ADDRESS && address < SIO_START_ADDRESS + 268435456) {
      return this.sio.readUint32(address - SIO_START_ADDRESS);
    }
    const peripheral = this.findPeripheral(address);
    if (peripheral) {
      return peripheral.readUint32(address & 16383);
    }
    this.logger.warn(LOG_NAME2, `Read from invalid memory address: ${address.toString(16)}`);
    return 4294967295;
  }
  /**
   * Recomputes `clkSys` and `clkPeri` from the PLL and CLOCKS registers and updates the
   * peripherals derived from them. Until firmware configures the clock tree, each keeps its default.
   */
  updateClocks() {
    this.updateClkSys();
    this.updateClkPeri();
  }
  updateClkSys() {
    const clkSys = this.clocks.sysFreq;
    if (!clkSys || clkSys === this.clkSys) {
      return;
    }
    const oldClkSys = this.clkSys;
    this.clkSys = clkSys;
    this.ppb.systickTimer.frequency = clkSys;
    for (const channel of this.pwm.channels) {
      channel.timer.frequency = this.pwm.clockFreq;
    }
    for (const listener of this.clockListeners) {
      listener(clkSys, oldClkSys);
    }
  }
  updateClkPeri() {
    const clkPeri = this.clocks.periFreq;
    if (!clkPeri || clkPeri === this.clkPeri) {
      return;
    }
    this.clkPeri = clkPeri;
    for (const uart of this.uart) {
      uart.clkPeriChanged();
    }
  }
  /** Registers a listener to be notified whenever clk_sys changes. Returns an unsubscribe function. */
  addClockListener(listener) {
    this.clockListeners.add(listener);
    return () => this.clockListeners.delete(listener);
  }
  findPeripheral(address) {
    return this.peripherals[address >>> 14 << 2];
  }
  /** We assume the address is 16-bit aligned */
  readUint16(address) {
    if (address >= FLASH_START_ADDRESS && address + 1 < FLASH_START_ADDRESS + this.flash.length) {
      return this.flashView.getUint16(address - FLASH_START_ADDRESS, true);
    } else if (address >= RAM_START_ADDRESS && address + 1 < RAM_START_ADDRESS + this.sram.length) {
      return this.sramView.getUint16(address - RAM_START_ADDRESS, true);
    }
    const value = this.readUint32(address & 4294967292);
    return address & 2 ? (value & 4294901760) >>> 16 : value & 65535;
  }
  readUint8(address) {
    if (address >= FLASH_START_ADDRESS && address < FLASH_START_ADDRESS + this.flash.length) {
      return this.flash[address - FLASH_START_ADDRESS];
    } else if (address >= RAM_START_ADDRESS && address < RAM_START_ADDRESS + this.sram.length) {
      return this.sram[address - RAM_START_ADDRESS];
    }
    const value = this.readUint16(address & 4294967294);
    return (address & 1 ? (value & 65280) >>> 8 : value & 255) >>> 0;
  }
  writeUint32(address, value) {
    address = address >>> 0;
    const { bootrom } = this;
    const peripheral = this.findPeripheral(address);
    if (peripheral) {
      const atomicType = (address & 12288) >> 12;
      const offset = address & 4095;
      peripheral.writeUint32Atomic(offset, value, atomicType);
    } else if (address < bootrom.length * 4) {
      bootrom[address / 4] = value;
    } else if (address >= FLASH_START_ADDRESS && address + 3 < FLASH_START_ADDRESS + this.flash.length) {
      this.flashView.setUint32(address - FLASH_START_ADDRESS, value, true);
    } else if (address >= RAM_START_ADDRESS && address + 3 < RAM_START_ADDRESS + this.sram.length) {
      this.sramView.setUint32(address - RAM_START_ADDRESS, value, true);
    } else if (address >= DPRAM_START_ADDRESS && address + 3 < DPRAM_START_ADDRESS + this.usbDPRAM.length) {
      const offset = address - DPRAM_START_ADDRESS;
      this.usbDPRAMView.setUint32(offset, value, true);
      this.usbCtrl.DPRAMUpdated(offset, value);
    } else if (address >= SIO_START_ADDRESS && address < SIO_START_ADDRESS + 268435456) {
      this.sio.writeUint32(address - SIO_START_ADDRESS, value);
    } else if (address >>> 12 === 917518) {
      this.ppb.writeUint32(address & 4095, value);
    } else {
      this.logger.warn(LOG_NAME2, `Write to undefined address: ${address.toString(16)}`);
    }
  }
  writeUint8(address, value) {
    if (address >= RAM_START_ADDRESS && address < RAM_START_ADDRESS + this.sram.length) {
      this.sram[address - RAM_START_ADDRESS] = value;
      return;
    }
    const alignedAddress = (address & 4294967292) >>> 0;
    const offset = address & 3;
    const peripheral = this.findPeripheral(address);
    if (peripheral) {
      const atomicType = (alignedAddress & 12288) >> 12;
      const offset2 = alignedAddress & 4095;
      peripheral.writeUint32Atomic(offset2, value & 255 | (value & 255) << 8 | (value & 255) << 16 | (value & 255) << 24, atomicType);
      return;
    }
    const originalValue = this.readUint32(alignedAddress);
    const newValue = new Uint32Array([
      originalValue
    ]);
    new DataView(newValue.buffer).setUint8(offset, value);
    this.writeUint32(alignedAddress, newValue[0]);
  }
  writeUint16(address, value) {
    if (address >= RAM_START_ADDRESS && address + 1 < RAM_START_ADDRESS + this.sram.length) {
      this.sramView.setUint16(address - RAM_START_ADDRESS, value, true);
      return;
    }
    const alignedAddress = (address & 4294967292) >>> 0;
    const offset = address & 3;
    const peripheral = this.findPeripheral(address);
    if (peripheral) {
      const atomicType = (alignedAddress & 12288) >> 12;
      const offset2 = alignedAddress & 4095;
      peripheral.writeUint32Atomic(offset2, value & 65535 | (value & 65535) << 16, atomicType);
      return;
    }
    if (offset === 3) {
      this.writeUint8(address, value & 255);
      this.writeUint8(address + 1, value >> 8 & 255);
      return;
    }
    const originalValue = this.readUint32(alignedAddress);
    const newValue = new Uint32Array([
      originalValue
    ]);
    new DataView(newValue.buffer).setUint16(offset, value, true);
    this.writeUint32(alignedAddress, newValue[0]);
  }
  get gpioValues() {
    const { gpio } = this;
    let result = 0;
    for (let gpioIndex = 0; gpioIndex < gpio.length; gpioIndex++) {
      if (gpio[gpioIndex].inputValue) {
        result |= 1 << gpioIndex;
      }
    }
    return result;
  }
  setInterrupt(irq, value) {
    this.core.setInterrupt(irq, value);
  }
  updateIOInterrupt() {
    let interruptValue = false;
    for (const pin of this.gpio) {
      if (pin.irqValue) {
        interruptValue = true;
      }
    }
    this.setInterrupt(IRQ.IO_BANK0, interruptValue);
  }
  step() {
    this.core.executeInstruction();
  }
};

var DataDirection = /* @__PURE__ */ function(DataDirection2) {
  DataDirection2[DataDirection2["HostToDevice"] = 0] = "HostToDevice";
  DataDirection2[DataDirection2["DeviceToHost"] = 1] = "DeviceToHost";
  return DataDirection2;
}({});
var SetupType = /* @__PURE__ */ function(SetupType2) {
  SetupType2[SetupType2["Standard"] = 0] = "Standard";
  SetupType2[SetupType2["Class"] = 1] = "Class";
  SetupType2[SetupType2["Vendor"] = 2] = "Vendor";
  SetupType2[SetupType2["Reserved"] = 3] = "Reserved";
  return SetupType2;
}({});
var SetupRecipient = /* @__PURE__ */ function(SetupRecipient2) {
  SetupRecipient2[SetupRecipient2["Device"] = 0] = "Device";
  SetupRecipient2[SetupRecipient2["Interface"] = 1] = "Interface";
  SetupRecipient2[SetupRecipient2["Endpoint"] = 2] = "Endpoint";
  SetupRecipient2[SetupRecipient2["Other"] = 3] = "Other";
  return SetupRecipient2;
}({});
var SetupRequest = /* @__PURE__ */ function(SetupRequest2) {
  SetupRequest2[SetupRequest2["GetStatus"] = 0] = "GetStatus";
  SetupRequest2[SetupRequest2["ClearFeature"] = 1] = "ClearFeature";
  SetupRequest2[SetupRequest2["Reserved1"] = 2] = "Reserved1";
  SetupRequest2[SetupRequest2["SetFeature"] = 3] = "SetFeature";
  SetupRequest2[SetupRequest2["Reserved2"] = 4] = "Reserved2";
  SetupRequest2[SetupRequest2["SetAddress"] = 5] = "SetAddress";
  SetupRequest2[SetupRequest2["GetDescriptor"] = 6] = "GetDescriptor";
  SetupRequest2[SetupRequest2["SetDescriptor"] = 7] = "SetDescriptor";
  SetupRequest2[SetupRequest2["GetConfiguration"] = 8] = "GetConfiguration";
  SetupRequest2[SetupRequest2["SetDeviceConfiguration"] = 9] = "SetDeviceConfiguration";
  SetupRequest2[SetupRequest2["GetInterface"] = 10] = "GetInterface";
  SetupRequest2[SetupRequest2["SetInterface"] = 11] = "SetInterface";
  SetupRequest2[SetupRequest2["SynchFrame"] = 12] = "SynchFrame";
  return SetupRequest2;
}({});
var DescriptorType = /* @__PURE__ */ function(DescriptorType2) {
  DescriptorType2[DescriptorType2["Device"] = 1] = "Device";
  DescriptorType2[DescriptorType2["Configration"] = 2] = "Configration";
  DescriptorType2[DescriptorType2["String"] = 3] = "String";
  DescriptorType2[DescriptorType2["Interface"] = 4] = "Interface";
  DescriptorType2[DescriptorType2["Endpoint"] = 5] = "Endpoint";
  return DescriptorType2;
}({});

function createSetupPacket(params) {
  const setupPacket = new Uint8Array(8);
  setupPacket[0] = params.dataDirection << 7 | params.type << 5 | params.recipient;
  setupPacket[1] = params.bRequest;
  setupPacket[2] = params.wValue & 255;
  setupPacket[3] = params.wValue >> 8 & 255;
  setupPacket[4] = params.wIndex & 255;
  setupPacket[5] = params.wIndex >> 8 & 255;
  setupPacket[6] = params.wLength & 255;
  setupPacket[7] = params.wLength >> 8 & 255;
  return setupPacket;
}
function setDeviceAddressPacket(address) {
  return createSetupPacket({
    dataDirection: DataDirection.HostToDevice,
    type: SetupType.Standard,
    recipient: SetupRecipient.Device,
    bRequest: SetupRequest.SetAddress,
    wValue: address,
    wIndex: 0,
    wLength: 0
  });
}
function getDescriptorPacket(type, length, index = 0) {
  return createSetupPacket({
    dataDirection: DataDirection.DeviceToHost,
    type: SetupType.Standard,
    recipient: SetupRecipient.Device,
    bRequest: SetupRequest.GetDescriptor,
    wValue: type << 8,
    wIndex: index,
    wLength: length
  });
}
function setDeviceConfigurationPacket(configurationNumber) {
  return createSetupPacket({
    dataDirection: DataDirection.HostToDevice,
    type: SetupType.Standard,
    recipient: SetupRecipient.Device,
    bRequest: SetupRequest.SetDeviceConfiguration,
    wValue: configurationNumber,
    wIndex: 0,
    wLength: 0
  });
}

var CDC_REQUEST_SET_CONTROL_LINE_STATE = 34;
var CDC_DTR = 1 << 0;
var CDC_RTS = 1 << 1;
var CDC_DATA_CLASS = 10;
var ENDPOINT_BULK = 2;
var TX_FIFO_SIZE = 512;
var ENDPOINT_ZERO = 0;
var CONFIGURATION_DESCRIPTOR_SIZE = 9;
function extractEndpointNumbers(descriptors) {
  let index = 0;
  let foundInterface = false;
  const result = {
    in: -1,
    out: -1
  };
  while (index < descriptors.length) {
    const len = descriptors[index];
    if (len < 2 || descriptors.length < index + len) {
      break;
    }
    const type = descriptors[index + 1];
    if (type === DescriptorType.Interface && len === 9) {
      const numEndpoints = descriptors[index + 4];
      const interfaceClass = descriptors[index + 5];
      foundInterface = numEndpoints === 2 && interfaceClass === CDC_DATA_CLASS;
    }
    if (foundInterface && type === DescriptorType.Endpoint && len === 7) {
      const address = descriptors[index + 2];
      const attributes = descriptors[index + 3];
      if ((attributes & 3) === ENDPOINT_BULK) {
        if (address & 128) {
          result.in = address & 15;
        } else {
          result.out = address & 15;
        }
      }
    }
    index += descriptors[index];
  }
  return result;
}
var USBCDC = class {
  usb;
  txFIFO;
  onSerialData;
  onDeviceConnected;
  initialized;
  descriptorsSize;
  descriptors;
  outEndpoint;
  inEndpoint;
  constructor(usb) {
    this.usb = usb;
    this.txFIFO = new FIFO(TX_FIFO_SIZE);
    this.initialized = false;
    this.descriptorsSize = null;
    this.descriptors = [];
    this.outEndpoint = -1;
    this.inEndpoint = -1;
    this.usb.onUSBEnabled = () => {
      this.usb.resetDevice();
    };
    this.usb.onResetReceived = () => {
      this.usb.sendSetupPacket(setDeviceAddressPacket(1));
    };
    this.usb.onEndpointWrite = (endpoint, buffer) => {
      if (endpoint === ENDPOINT_ZERO && buffer.length === 0) {
        if (this.descriptorsSize == null) {
          this.usb.sendSetupPacket(getDescriptorPacket(DescriptorType.Configration, CONFIGURATION_DESCRIPTOR_SIZE));
        } else if (!this.initialized) {
          this.cdcSetControlLineState();
          this.onDeviceConnected?.();
        }
      }
      if (endpoint === ENDPOINT_ZERO && buffer.length > 1) {
        if (buffer.length === CONFIGURATION_DESCRIPTOR_SIZE && buffer[1] === DescriptorType.Configration && this.descriptorsSize == null) {
          this.descriptorsSize = buffer[3] << 8 | buffer[2];
          this.usb.sendSetupPacket(getDescriptorPacket(DescriptorType.Configration, this.descriptorsSize));
        } else if (this.descriptorsSize != null && this.descriptors.length < this.descriptorsSize) {
          this.descriptors.push(...buffer);
        }
        if (this.descriptorsSize === this.descriptors.length) {
          const endpoints = extractEndpointNumbers(this.descriptors);
          this.inEndpoint = endpoints.in;
          this.outEndpoint = endpoints.out;
          this.usb.sendSetupPacket(setDeviceConfigurationPacket(1));
        }
      }
      if (endpoint === this.inEndpoint) {
        this.onSerialData?.(buffer);
      }
    };
    this.usb.onEndpointRead = (endpoint, size) => {
      if (endpoint === this.outEndpoint) {
        const buffer = new Uint8Array(Math.min(size, this.txFIFO.itemCount));
        for (let i = 0; i < buffer.length; i++) {
          buffer[i] = this.txFIFO.pull();
        }
        this.usb.endpointReadDone(this.outEndpoint, buffer);
      }
    };
  }
  cdcSetControlLineState(value = CDC_DTR | CDC_RTS, interfaceNumber = 0) {
    this.usb.sendSetupPacket(createSetupPacket({
      dataDirection: DataDirection.HostToDevice,
      type: SetupType.Class,
      recipient: SetupRecipient.Device,
      bRequest: CDC_REQUEST_SET_CONTROL_LINE_STATE,
      wValue: value,
      wIndex: interfaceNumber,
      wLength: 0
    }));
    this.initialized = true;
  }
  sendSerialByte(data) {
    this.txFIFO.push(data);
  }
};

var bootromB1 = new Uint32Array([
  537140992,
  239,
  53,
  49,
  33649997,
  13107322,
  29,
  2281841408,
  3489874586,
  805603395,
  3522642577,
  1198529560,
  3892166448,
  4026550004,
  1218377733,
  1610686720,
  1189568577,
  553666716,
  1610695625,
  1198547009,
  10789265,
  7689,
  539575080,
  808595506,
  1935757856,
  1919246960,
  1344305522,
  1918115945,
  1852400737,
  1951146087,
  860880996,
  861012697,
  860619517,
  861143847,
  1397556063,
  877864669,
  1129129681,
  876815937,
  1112876585,
  1413752245,
  1162084741,
  1448542603,
  1179189559,
  1480926369,
  1163011061,
  1347560317,
  1128670149,
  1480794977,
  1128604465,
  69,
  5263943,
  5788227,
  27805267,
  36193363,
  27679302,
  660099910,
  776750406,
  777278276,
  1034700100,
  1215496192,
  687892481,
  4160737567,
  1232207773,
  1745505137,
  3489743386,
  3885195275,
  1315983215,
  1118883599,
  1079628039,
  3506706496,
  1614298896,
  2282288002,
  4026681240,
  3206609377,
  143224097,
  1700844539,
  477019968,
  1281443632,
  553930597,
  1839292729,
  139029793,
  2769015547,
  4160692224,
  671219693,
  4160737782,
  1622736873,
  4293326847,
  2282288e3,
  4293064703,
  4160726273,
  1187053535,
  1631133952,
  3881650048,
  1830862624,
  3556444224,
  671116704,
  1198575838,
  1127687681,
  3187689400,
  940697594,
  3170925248,
  1078113536,
  4026542592,
  3539400706,
  1181763214,
  2115328,
  11113,
  11109,
  11313,
  11517,
  10279,
  10279,
  11697,
  10317,
  10319,
  10369,
  10371,
  10455,
  10457,
  10471,
  10473,
  10687,
  10613,
  10717,
  49,
  10725,
  10831,
  10251,
  10867,
  10415,
  10417,
  10397,
  10399,
  13697,
  13699,
  13707,
  13709,
  13885,
  11873,
  11861,
  12221,
  12569,
  13419,
  13419,
  13021,
  13669,
  13671,
  13683,
  13685,
  14019,
  14021,
  14011,
  14013,
  14385,
  14401,
  14353,
  49,
  15173,
  15329,
  13423,
  14641,
  14033,
  14035,
  14027,
  14029,
  13761,
  13763,
  13787,
  13789,
  13923,
  4085270538,
  4026632200,
  65307,
  1073758208,
  1073774752,
  3489660928,
  1074151432,
  16777216,
  1074102300,
  2953298131,
  3758157056,
  1343228080,
  138561843,
  142819338,
  1074282507,
  415242368,
  406849729,
  1074284847,
  406849921,
  1128810798,
  1198526080,
  139545133,
  4407377,
  1074282507,
  1125648448,
  1074987139,
  142622736,
  1244152579,
  1074790680,
  152780819,
  3120579352,
  2737129328,
  3507227649,
  3506702977,
  3506768129,
  807033880,
  1549289328,
  1198534672,
  806771800,
  176703344,
  151572740,
  1549324548,
  1198534666,
  1198545944,
  805723160,
  2737260400,
  3490644993,
  3489989e3,
  3490120065,
  823136009,
  1198546008,
  1549274761,
  1198534666,
  1545080448,
  1198534660,
  3490054529,
  3490185608,
  806358784,
  806378520,
  243287920,
  807033880,
  243877744,
  806640728,
  18288,
  1227133513,
  3340530119,
  67125252,
  3435973836,
  4042322160,
  67372294,
  50529027,
  33686018,
  33686018,
  16843009,
  16843009,
  16843009,
  16843009,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  65542,
  65538,
  65539,
  65538,
  65540,
  65538,
  65539,
  65538,
  65541,
  65538,
  65539,
  65538,
  65540,
  65538,
  65539,
  65538,
  551616561,
  474554729,
  499981225,
  3187679997,
  1754483202,
  1620261696,
  1187006320,
  1343228540,
  2106804738,
  1241675027,
  1198546963,
  1343228396,
  1343291392,
  1610687232,
  1614962947,
  1623423051,
  1198547075,
  1753434372,
  1947869697,
  2015062043,
  1125341899,
  18200,
  1084,
  3044042755,
  3489799705,
  588216473,
  603996185,
  1242703104,
  412745931,
  1616207892,
  1309671428,
  2335323176,
  2015724162,
  754984e3,
  421842944,
  2201229588,
  3490588928,
  754983252,
  824233984,
  807994953,
  2013337682,
  404441094,
  3489868032,
  570449946,
  3178258522,
  3892011033,
  1343225984,
  1343228396,
  1343225856,
  474330416,
  2011804361,
  1881814312,
  230083,
  2015669251,
  1880896296,
  4793089,
  411648090,
  2010717379,
  3187719472,
  3052404755,
  786438,
  2961506325,
  1610691336,
  2015597315,
  2019760356,
  3522833157,
  2032236899,
  1124794907,
  553723663,
  2432727202,
  3751959,
  164784168,
  4291885055,
  1241894152,
  1612447935,
  2600685755,
  3521397419,
  2953117744,
  1187036656,
  1343228424,
  2145066051,
  2013409321,
  14371075,
  704649304,
  1241698305,
  1198528664,
  1343225988,
  1343225984,
  439664,
  294787,
  721434136,
  2328088863,
  227139,
  2014917417,
  687873347,
  495177984,
  2325970909,
  508559402,
  1208631682,
  404423058,
  1638078619,
  1117332224,
  2150662,
  4291885055,
  423952493,
  3000731691,
  587298595,
  3176355,
  1187036528,
  1343225856,
  3044024832,
  2010783171,
  275201,
  1116766168,
  595644719,
  1125712091,
  858325027,
  721451035,
  595644418,
  1125712411,
  2144541858,
  1910273,
  1099243112,
  2010333299,
  57475104,
  4160701197,
  493092771,
  2997714906,
  411058354,
  2147820032,
  2007112993,
  973176778,
  1755412426,
  1753887784,
  1619671553,
  704673826,
  2145046530,
  2011054166,
  547405168,
  1124139328,
  3889002458,
  308592,
  721447075,
  1755041813,
  3490851584,
  2146770214,
  3490589952,
  2145066467,
  3507104512,
  858325027,
  721451035,
  2150406,
  4277204991,
  1118535667,
  3178287590,
  2330001440,
  4288870399,
  3044075488,
  262157,
  2621457,
  4160684058,
  587267799,
  855728355,
  2121893,
  4160713899,
  3178299343,
  242960,
  1224952322,
  4160702467,
  3172007912,
  16217,
  1343228452,
  1343229376,
  1753442369,
  1116452112,
  587255811,
  4160710666,
  3172007896,
  3037724675,
  2145071923,
  721432620,
  4160737537,
  3172007917,
  3044016131,
  2015048489,
  851972,
  721420310,
  4160737283,
  587267895,
  3371267,
  2097193,
  4160702977,
  3178299322,
  16217,
  570471696,
  1208174850,
  4293195775,
  1187036432,
  1343228356,
  1343229800,
  570471696,
  1208174850,
  4292409343,
  1187036432,
  1343228376,
  1343229844,
  1258534160,
  1225017859,
  4160702468,
  3172007832,
  1885,
  16217,
  1343228356,
  1343229800,
  499365136,
  1753317338,
  3506776576,
  721447115,
  2081149194,
  3506907904,
  587211777,
  1619227852,
  3489809050,
  1619747019,
  1749794064,
  3489737472,
  3891873688,
  721447107,
  1619251446,
  1956405442,
  4283234303,
  3187730416,
  499561840,
  294899,
  721420301,
  474206490,
  721453019,
  250119,
  2015048489,
  1095975514,
  855722506,
  2123411,
  4273666047,
  1744970624,
  1125318939,
  2012569603,
  721447203,
  2150401,
  3178252184,
  3539747467,
  3891951601,
  1343299584,
  671135088,
  1259917326,
  1489240192,
  2144017697,
  2009805569,
  755001509,
  553832713,
  4160684064,
  3178299335,
  687885329,
  1276236272,
  2083252206,
  3489803008,
  1953178369,
  497281011,
  857962458,
  721451035,
  1756090370,
  3489803008,
  4160684064,
  2095840769,
  3521391360,
  2124011,
  1626028801,
  4286773247,
  1187047389,
  1343228424,
  1343229800,
  1343229844,
  3044025088,
  288901,
  4160713771,
  2095841139,
  3508218624,
  908656678,
  721451059,
  2150403,
  4160716577,
  587267735,
  2087352227,
  3490595482,
  488797291,
  989954003,
  570521555,
  2145066403,
  2016490497,
  1079672800,
  4288739327,
  2145648,
  4273928191,
  3187730426,
  308728,
  4265539583,
  1744961573,
  891833113,
  3491578394,
  721451051,
  488886275,
  721518555,
  587321604,
  2144410786,
  2010333259,
  2145000675,
  1083384577,
  1242646800,
  1309820945,
  1712023123,
  1111191159,
  939643138,
  3507562496,
  1716741651,
  4160684064,
  2016149015,
  509157888,
  488718731,
  1610756865,
  587298763,
  872973474,
  3187177443,
  3890570763,
  67109888,
  1343299584,
  1343303680,
  1e5,
  1343291392,
  1292416368,
  4160684072,
  1275592631,
  4160684064,
  2686899,
  4160692481,
  553778977,
  4160684064,
  3178299165,
  1343229800,
  1343229844,
  620803440,
  942213,
  4160684036,
  1118764959,
  480432129,
  2007332821,
  2144411106,
  3489999104,
  1763932117,
  3489737472,
  1201143840,
  2103134983,
  3490261760,
  687892577,
  1755566086,
  3506645760,
  1745485856,
  4267702271,
  1187036528,
  1343228396,
  1260303632,
  1612467230,
  1260266624,
  1612449252,
  82060512,
  1260150812,
  3492554752,
  1126179841,
  1641827354,
  1113876956,
  1646018628,
  1654153817,
  8987130,
  1616528202,
  1745963541,
  1108419074,
  1226101003,
  1745438740,
  553861194,
  1225998346,
  570712074,
  1108437889,
  571265276,
  1259365082,
  855585525,
  578838554,
  97667854,
  570515482,
  1880771341,
  1641594128,
  1187047386,
  1073807368,
  131068,
  1074114560,
  1074102272,
  2953298131,
  1074184192,
  1073774640,
  1073774592,
  1073778736,
  1074110508,
  1074110464,
  1343229620,
  308592,
  536877125,
  3523297964,
  3489671168,
  1258601840,
  1746599968,
  1201170651,
  22750080,
  3891337444,
  1343229372,
  101851872,
  579082369,
  47316995,
  1116807169,
  585881863,
  412812818,
  536879744,
  1117389266,
  1198539072,
  495301936,
  3001159436,
  859242515,
  3623889929,
  1879259911,
  17379329,
  3522380456,
  3187719472,
  3037733376,
  1208371973,
  2048619098,
  3623889791,
  1241860100,
  4160702725,
  3172007413,
  1343228600,
  1343229528,
  1343229476,
  2909,
  1343228080,
  1778535172,
  3037752539,
  3506520730,
  4292933631,
  1187036432,
  1343228616,
  1208268048,
  2145066435,
  3506514688,
  4257413119,
  1187036432,
  1343229528,
  1208221699,
  3624086275,
  989940227,
  1486356635,
  1187006320,
  15799,
  16104,
  33573634,
  1746624536,
  1187006320,
  1073840140,
  308592,
  860162,
  4294113279,
  587473600,
  1126762017,
  235406674,
  1712339713,
  721420809,
  3178287609,
  1241795331,
  90136,
  1134127251,
  1198576123,
  1073803264,
  1073790976,
  808039927,
  1283094531,
  3490327296,
  1617109761,
  2232480751,
  2143858,
  1201170595,
  2282812293,
  1265942007,
  2015043591,
  3507038976,
  2145262947,
  2992340955,
  3659868928,
  132868325,
  537253122,
  3890569312,
  4160684072,
  671154005,
  537186561,
  602662902,
  102442880,
  33495275,
  3656794811,
  807141416,
  4282841087,
  3505399808,
  418073442,
  3656139451,
  2630172,
  4026616160,
  1264647411,
  1612343522,
  3574466291,
  2144935331,
  508755978,
  1264337306,
  858370770,
  688025626,
  570544387,
  856116057,
  133396442,
  108254232,
  587912221,
  3490464286,
  2145000931,
  856162339,
  1263763417,
  3489868032,
  537294873,
  3518579345,
  129200154,
  588043280,
  3508879902,
  3887144960,
  1746619208,
  1201170587,
  3504351232,
  587327402,
  1125345506,
  3889973144,
  84109600,
  1768018337,
  3516794123,
  578823664,
  423822893,
  1116931410,
  407099551,
  1116936539,
  1262147739,
  1763403803,
  671106968,
  3884830935,
  2648293,
  4276418559,
  3489998848,
  417884643,
  4276025343,
  3508411911,
  3557426995,
  26354304,
  3523625621,
  418081251,
  3540009619,
  654436221,
  569434129,
  101261952,
  89266283,
  3640672915,
  1776543603,
  408623339,
  3640672915,
  3002066797,
  3489672960,
  120842081,
  1260442904,
  1184655777,
  788555803,
  1776472099,
  1118541057,
  2466371840,
  413676316,
  3523232387,
  2600534040,
  3523363459,
  1259685472,
  2646019,
  4233818113,
  3574007667,
  1772120291,
  3490721536,
  1776418841,
  4233162753,
  3582396083,
  1746619149,
  1201170523,
  2680630,
  1201170779,
  3504941056,
  1242097456,
  1746010113,
  1771175960,
  671106960,
  3878146282,
  1343229324,
  1343229620,
  3942645788,
  16076,
  1343229372,
  1343229888,
  1343228540,
  1343228444,
  1783019e3,
  3342340,
  2015048489,
  721420336,
  4160737315,
  1768160205,
  2058722,
  1772093445,
  926948945,
  1075388992,
  3640672903,
  1898584770,
  1747479075,
  4026603609,
  2032925709,
  2124130,
  1633884371,
  1746627235,
  1258702744,
  1746534448,
  1612335877,
  4247713791,
  4160732664,
  392105,
  1187047403,
  1343229796,
  3489660952,
  1275901296,
  1826777603,
  1116930053,
  1749274897,
  1618169353,
  3489999616,
  1784684802,
  4239587327,
  1920148224,
  1805806059,
  416481312,
  4160709539,
  3178299313,
  1343228616,
  1343228600,
  308592,
  1292510721,
  1258976616,
  1490026674,
  3489802240,
  4160692481,
  906100095,
  3522440709,
  1099177568,
  2648171,
  1201189601,
  1187036528,
  1343228396,
  1343228424,
  536917264,
  4292933631,
  1241850624,
  1241871635,
  570515475,
  1112689412,
  1696228762,
  1187036432,
  1343228396,
  1343291392,
  1343303680,
  1259217440,
  1612363024,
  1746422541,
  102441936,
  1649959256,
  4196923391,
  1258955328,
  10164419,
  1124755481,
  1612325248,
  1133144090,
  1258709018,
  587536576,
  1615003840,
  1187036432,
  1073803264,
  1343229796,
  268464129,
  134227968,
  701712,
  4026605824,
  3172006855,
  3044035345,
  1276213274,
  3506907648,
  1225869840,
  1611752226,
  1629637136,
  1880760833,
  556289295,
  4160684072,
  594608103,
  996569963,
  620786987,
  2706187,
  1629290528,
  4245747711,
  2687008,
  4160696364,
  3178298635,
  1343229472,
  1343229376,
  1343228472,
  15834,
  5197,
  1343228540,
  1343229244,
  2013995616,
  1075492112,
  706749440,
  2992361748,
  1118009418,
  721345042,
  2286670094,
  3507176099,
  1118013643,
  1209257992,
  4208916479,
  1880909827,
  1896100865,
  4223989759,
  3171942432,
  3522898687,
  721455179,
  2294993400,
  704643100,
  1259000308,
  2144411098,
  3506514179,
  2010200321,
  2014982994,
  3506514435,
  1880766977,
  4288215039,
  4160693249,
  3890346895,
  1343229800,
  1343229376,
  898551,
  1057152,
  1310857,
  4286773247,
  3508546816,
  818085920,
  1229922827,
  4026609919,
  603978487,
  5972565,
  1263293666,
  1424103570,
  2031766347,
  3506711040,
  1787972170,
  570515482,
  2126106,
  817457179,
  822055681,
  2835423748,
  4208914433,
  755097635,
  1262604561,
  704674074,
  1245761795,
  1897753234,
  1746624538,
  2105920,
  2466335037,
  4207734785,
  807862304,
  578938854,
  5381803,
  3524018835,
  3640667008,
  721435521,
  990433541,
  856129571,
  2158198883,
  536903907,
  527154686,
  723467263,
  721475641,
  1227805175,
  824910347,
  4026597408,
  589888171,
  2323171,
  2105956,
  1294618153,
  1880765229,
  978929961,
  2250606054,
  2267383526,
  4026609696,
  2357913,
  858464545,
  587362329,
  871335779,
  2581475,
  576978979,
  926954318,
  1226797946,
  2153611294,
  2170388766,
  3684953,
  4202885121,
  555810851,
  861544963,
  2149217017,
  1709384510,
  1025894335,
  124468735,
  149803451,
  2281744,
  1209213343,
  4199542785,
  1293025312,
  2695692,
  4026609762,
  2161255,
  2695692,
  3884003522,
  3517328641,
  2105918,
  3883616523,
  15864,
  511,
  1343229364,
  1074085888,
  15980,
  4294935193,
  14629,
  15788,
  15800,
  16378,
  1343229621,
  16116,
  1267054064,
  1746010134,
  2416160901,
  3489809050,
  2953125888,
  1266793968,
  1117415538,
  587125240,
  1266614354,
  1117411506,
  1756615154,
  3589211290,
  1777420924,
  3521856145,
  1909249,
  1108557845,
  595644902,
  5990706,
  3521200794,
  3696887,
  4234344447,
  3489933312,
  822018104,
  4233951231,
  1773338629,
  721439856,
  586207241,
  414844434,
  755012097,
  1167622,
  1116818028,
  536926471,
  3888537888,
  1153537,
  1116818024,
  3003111426,
  3522440960,
  841566822,
  704673810,
  838980079,
  2449621034,
  654862626,
  3493151379,
  558891040,
  4268554239,
  2583822371,
  1214133068,
  755003418,
  547934465,
  1114178880,
  597705065,
  1112100186,
  1076429019,
  1616976073,
  1612712137,
  4267112447,
  1743617,
  1117408081,
  1213519878,
  1230326612,
  1625514144,
  4266326015,
  1751280051,
  3640672915,
  1629742982,
  654320385,
  2583773787,
  1262969315,
  1116955047,
  923287935,
  860618787,
  2583853083,
  3517727379,
  1773300083,
  3534242451,
  556269600,
  4160696356,
  2293291,
  843619075,
  1650682225,
  1760780643,
  1245736983,
  1184112641,
  578839266,
  1679949906,
  843710498,
  572485648,
  1083195402,
  155871269,
  1646330002,
  413819683,
  908093461,
  2416010214,
  3489677829,
  629204810,
  95250918,
  3523953331,
  1103053483,
  3687039,
  1103053486,
  2533507711,
  2550267911,
  3489874567,
  3523363502,
  3540009643,
  2449891,
  2016228684,
  3508153600,
  97059342,
  889261769,
  208748685,
  9005222,
  1745754225,
  3507372590,
  52103963,
  595616611,
  1671627099,
  1126000651,
  2187277,
  826811138,
  1126922253,
  1772318731,
  855742465,
  1746100643,
  1125646369,
  1292781155,
  891838480,
  573072099,
  1208693028,
  4026628139,
  537000255,
  3208671272,
  1881158731,
  1187047165,
  171066965,
  2656915799,
  179400496,
  3834380118,
  1343229244,
  268435201,
  1343229888,
  1343229636,
  122080,
  352336956,
  7710,
  963,
  5681,
  898416,
  4173920255,
  2686980,
  4160710656,
  1746992535,
  3178262821,
  3037733376,
  554519559,
  1208425891,
  4160714714,
  571342827,
  4026597409,
  1208285445,
  4182571007,
  1187036432,
  1343228540,
  1343229376,
  1797,
  499365136,
  721453019,
  1258672393,
  3506848408,
  856050436,
  721453019,
  4160737281,
  3172007897,
  1343229376,
  1343228540,
  1242346768,
  2144935251,
  3506907393,
  5972864,
  2224244745,
  4160696577,
  3172006311,
  3506776322,
  1208295936,
  808220634,
  4187944959,
  4290574335,
  1187047411,
  1343228540,
  1343229376,
  3044025088,
  493177873,
  1646491627,
  855735045,
  3623890303,
  1117401857,
  493015047,
  587364307,
  4160713507,
  536936399,
  1753473039,
  3490202251,
  2009210208,
  3523297931,
  1931616770,
  3640672907,
  536936459,
  721445411,
  3178287339,
  1343228540,
  2088941048,
  34765891,
  2093171482,
  68878351,
  2097365786,
  102464961,
  2105688851,
  1125188105,
  3125366304,
  3122331698,
  788640393,
  841797632,
  38358045,
  3826274,
  4160709347,
  671154105,
  1260048425,
  759130653,
  1243207976,
  1746075680,
  855722264,
  591355923,
  1259815837,
  1654874533,
  1243040534,
  595616291,
  1642266779,
  1633886976,
  4286511102,
  1755526463,
  425396653,
  1759731875,
  1625626973,
  3506908929,
  3154688,
  1626562740,
  4160713895,
  3187210349,
  3892077492,
  4285200383,
  1187047417,
  1343229376,
  1343228492,
  1343228540,
  1343228448,
  15895,
  15844,
  1343228700,
  5237,
  374128,
  4160636940,
  1755971559,
  293123,
  3640672921,
  570490905,
  4160684072,
  671154023,
  1241960457,
  1779648518,
  1754362147,
  449557211,
  4160708755,
  3178297453,
  1343229376,
  1343228540,
  5237,
  1258823936,
  492483856,
  1753380817,
  3490071178,
  570522369,
  3623889279,
  855971986,
  4160714714,
  3172007717,
  1343228540,
  1744980762,
  3037751641,
  1116340228,
  1749078289,
  3507365888,
  1763273114,
  3507110538,
  843841562,
  704673810,
  1775816704,
  1225859834,
  4160684178,
  3423271373,
  2215703535,
  1242478194,
  1116825618,
  721473809,
  570544140,
  1931102987,
  1943679494,
  1985622553,
  1989818910,
  2010788613,
  4277336063,
  4160702470,
  4085578667,
  3171977232,
  1343229244,
  537141248,
  1343228448,
  1343228540,
  1343228492,
  3037733120,
  4160702479,
  1209006457,
  4160692480,
  1208940917,
  721451587,
  578867203,
  43141900,
  554721306,
  4231460863,
  570444554,
  858325017,
  1879716136,
  1258844186,
  858325017,
  1879716136,
  3171971098,
  1343229528,
  1343229476,
  1343228600,
  1073848348,
  1343229888,
  1343229572,
  1258627712,
  3037725330,
  687890458,
  4160737283,
  4160748569,
  3172007879,
  1073848348,
  2013995616,
  3037732864,
  708853786,
  2992361747,
  1115912266,
  709024272,
  2295124237,
  3507104784,
  1208549409,
  4267374591,
  1225261090,
  4285853696,
  4161599487,
  3171950593,
  3522964033,
  4289001471,
  4293720062,
  1187047414,
  1343229800,
  1343228600,
  1310242296,
  1385664,
  90457780,
  1125515294,
  3506651950,
  4160692227,
  3187210751,
  1785096743,
  3491047936,
  789387711,
  503830546,
  2013777921,
  1713844225,
  771766785,
  1848037608,
  3489737472,
  3890428673,
  3489736960,
  822177806,
  3890035969,
  3522309632,
  666912358,
  46098486,
  3503637054,
  1187047384,
  1073840156,
  832784,
  537067521,
  4191877119,
  587473536,
  536870945,
  4160684114,
  3172007871,
  3037733872,
  415237659,
  4293720063,
  3171950592,
  537048336,
  4190042111,
  570827712,
  1712981339,
  587276800,
  1048593,
  4289263615,
  3187719440,
  650163571,
  91628545,
  4160692226,
  587594155,
  1181443635,
  2235869,
  2687011,
  4160692224,
  2016149397,
  3490005539,
  1746553603,
  43721664,
  3504947738,
  1187036531,
  1073840156,
  374128,
  4160684044,
  2752461,
  4160684064,
  570489235,
  1123076,
  4160684048,
  4160749433,
  3178299345,
  3037733872,
  415237659,
  4160692512,
  536936423,
  3044064528,
  786437,
  4289918975,
  537002025,
  4185454591,
  587473536,
  2105600,
  4160684114,
  4160749405,
  3178299317,
  3037733872,
  415237659,
  4293392383,
  3171950592,
  1266819712,
  72529392,
  1249992730,
  1746010118,
  2961506317,
  3560507282,
  1249779715,
  1283410302,
  1249861649,
  1249861648,
  1738688639,
  989805311,
  1818255362,
  3506192912,
  5382784,
  1705141282,
  1611745914,
  687892561,
  545315580,
  1612185920,
  4182702079,
  1266033153,
  1612325153,
  1620718179,
  47325866,
  1249075418,
  1746493457,
  3673958656,
  1611866888,
  1675830016,
  570499970,
  1700987163,
  1612335981,
  1818370818,
  3506192922,
  71311488,
  4180604927,
  1265181196,
  1658462592,
  854936424,
  1612329727,
  1265050113,
  1612335207,
  4160684361,
  1265040095,
  1139499038,
  3506440091,
  771761408,
  4160737281,
  1281555119,
  2116450,
  4160710681,
  1264711847,
  1746476448,
  4171429887,
  1281304065,
  3491704064,
  572542814,
  3670049,
  4265537536,
  1891312416,
  637608704,
  2846971,
  2466463795,
  3490136629,
  3670049,
  824189463,
  4026544137,
  637599277,
  1899700993,
  1929061120,
  3971843,
  3506973440,
  1330577441,
  3689039,
  4160631049,
  1263467921,
  128671867,
  555209743,
  1313620849,
  1246507273,
  409010224,
  4253349886,
  1246448458,
  1246453786,
  1263231059,
  1263231091,
  3506449665,
  1296708356,
  1626098250,
  1330258944,
  1263165739,
  1613365281,
  570515627,
  2483036992,
  4160618552,
  1313275221,
  2162722,
  2483036992,
  4160618544,
  595656013,
  5963809,
  2200633400,
  4245878782,
  2171776,
  3145819,
  4160652075,
  599850251,
  2200633435,
  1262105601,
  1490026658,
  3489802240,
  4160626944,
  872545535,
  3522440197,
  536890165,
  1261789291,
  1854005,
  855900185,
  1116954632,
  587846138,
  855861091,
  990603171,
  4160709667,
  1261435395,
  1692617263,
  1612335919,
  1244603168,
  1244618771,
  4160643091,
  1187052517,
  1073799168,
  1074184192,
  1074135040,
  16429056,
  1073774592,
  1073786928,
  511,
  1073786940,
  1073889280,
  1073905664,
  1073917956,
  1073782844,
  1074102272,
  1074110508,
  335556608,
  1343225856,
  1343229796,
  1343229621,
  1073741888,
  80,
  15897,
  1343229212,
  1343229464,
  15836,
  4021,
  1343229520,
  16184,
  1343228580,
  15852,
  1343229528,
  5929,
  15972,
  1343228396,
  15952,
  1343229800,
  2933,
  1343229844,
  1343228424,
  5901,
  1343291392,
  1343291524,
  536936448,
  70640,
  1343291536,
  3758154368,
  3758153984,
  3037743874,
  1763272920,
  4271437823,
  1074102272,
  3052882724,
  1260677146,
  1612343428,
  1776615426,
  1768108585,
  510556178,
  2449555486,
  3491047936,
  3489869312,
  4160684064,
  3187144973,
  425290146,
  3640672913,
  754981589,
  1789120756,
  2031831575,
  1523777576,
  671106968,
  3891187948,
  4241684478,
  1772251489,
  471360,
  1116826083,
  1430290,
  1074601561,
  3506455050,
  771751965,
  1776406787,
  4160711200,
  1780742547,
  1748597050,
  4026538392,
  3889757451,
  3342365,
  1118516032,
  2634142445,
  1187047403,
  1343229796,
  3489660948,
  1084,
  439800,
  4238800894,
  723482883,
  3768635392,
  1263888389,
  1117415466,
  3768242176,
  721451883,
  3767980032,
  863992617,
  1075445770,
  3489677849,
  2074861711,
  722418433,
  3767195904,
  1263291467,
  1751867427,
  1756061795,
  2079023267,
  3489803011,
  1986163682,
  654341794,
  723743527,
  3625504839,
  3493538586,
  721672205,
  722653265,
  1119604773,
  587321434,
  855929635,
  857437155,
  587232867,
  723247196,
  723439708,
  2675187,
  4237883391,
  724230154,
  3624652841,
  3492883237,
  724050177,
  2675175,
  4228446207,
  4160618544,
  3187211733,
  3505007407,
  3504876341,
  556066779,
  4160702509,
  555350909,
  1210843140,
  406847522,
  4235587584,
  1885545344,
  4160684072,
  3890609233,
  1210392836,
  4218222591,
  1610818307,
  553838580,
  554493913,
  4160702496,
  571276131,
  4026550560,
  3890871421,
  1209803016,
  4217042943,
  1226646024,
  554887157,
  4160702488,
  2227027,
  822944274,
  4234997760,
  1986491367,
  3889526439,
  2144476642,
  3500943872,
  1931616769,
  1944197266,
  1986146872,
  3886315171,
  2095784451,
  721567763,
  872927647,
  2011380481,
  1275651995,
  2105603,
  4246534142,
  553844768,
  4160630828,
  3886021911,
  1128420181,
  1343228540,
  1396855637,
  1343229376,
  16192,
  15883,
  15875,
  1258927376,
  1784302601,
  2014458409,
  3506448896,
  1242057735,
  474507281,
  1611688665,
  1658461258,
  1201687045,
  1187036432,
  1343228492,
  4145,
  4537,
  1343228448,
  1343228700,
  1269478903,
  2466342939,
  3556770779,
  4089438344,
  1336381279,
  3678464,
  4160627713,
  1319501251,
  3154176,
  4257150974,
  481501471,
  1252161500,
  2010913979,
  1406995,
  1117863961,
  3768766720,
  3506448642,
  687923402,
  828428631,
  3511960075,
  721465947,
  3725900,
  4221368318,
  489579,
  721840134,
  721997837,
  721473598,
  2150879557,
  2297108772,
  3707781795,
  1899692067,
  4235655166,
  2288574538,
  721553923,
  721670157,
  721539094,
  1267388723,
  1746478098,
  3505006848,
  3145762,
  4224380928,
  2998986725,
  3508938752,
  1759071107,
  2022406348,
  1125909028,
  3891253467,
  671134400,
  1266601999,
  1754997762,
  805390232,
  2015043139,
  3506645760,
  1882469123,
  3888803955,
  872567603,
  604301299,
  3889777014,
  2103135092,
  3888148531,
  721778771,
  722063364,
  4160671800,
  3758882109,
  1456677634,
  721455185,
  1265425911,
  1972961336,
  1231899244,
  4227659774,
  1265377920,
  1696203410,
  115055361,
  637588746,
  1332290561,
  755002813,
  772460548,
  3766669312,
  1704807268,
  81500929,
  4160673029,
  578879423,
  51530592,
  603481370,
  10197505,
  3490202138,
  1830505309,
  3674221312,
  578871422,
  101862233,
  3187107098,
  671119504,
  1263718404,
  2036033755,
  3518907011,
  4287035390,
  4225562622,
  2291394504,
  2102479436,
  3501467904,
  1108025854,
  1762841009,
  10203867,
  671111320,
  1749274795,
  3507759872,
  2016092768,
  3517202963,
  168986794,
  2992361889,
  3667798784,
  738228332,
  3723675,
  4209440766,
  1612474371,
  1896030978,
  2746196,
  671106968,
  3890598302,
  704678034,
  713084993,
  1228263520,
  687897929,
  3884241152,
  10504504,
  1745246272,
  1118992566,
  872534068,
  3522571269,
  2020206457,
  3490326785,
  3489671427,
  2288772979,
  3489671936,
  553838447,
  4225431550,
  2288773038,
  3489671936,
  499312487,
  704806866,
  4160673794,
  3886283959,
  2009280514,
  1110239136,
  553766923,
  1260482042,
  1704738850,
  508758128,
  1135690138,
  4226217982,
  6570917,
  3882235393,
  4281399294,
  3204993,
  1108550240,
  3880046592,
  721465947,
  2020334279,
  3489671936,
  2288772925,
  3489671936,
  2297227065,
  3489672194,
  620881717,
  2143498247,
  1117585464,
  4160635309,
  1745091195,
  1612530285,
  3874058500,
  3890020408,
  1343291544,
  1343229800,
  1343229844,
  1343225856,
  1343228396,
  16178,
  1117,
  1343228356,
  1343303680,
  1343291392,
  1343228424,
  4160664848,
  3172006877,
  1243264504,
  1612335898,
  2232480751,
  4089427570,
  1276677983,
  925368359,
  3000924219,
  3507694336,
  2282812293,
  2232480751,
  4089427570,
  1276284767,
  925368359,
  721451067,
  573100051,
  1208942625,
  4203409408,
  3758518334,
  2171432,
  4026550283,
  587266691,
  4085608507,
  2131984,
  4249548798,
  4085639122,
  3206580240,
  1187047374,
  16076,
  1343229372,
  1343229888,
  1343229572,
  1343229324,
  2416031223,
  4195809278,
  723548419,
  3768504320,
  1263757318,
  1117415474,
  3768111104,
  556289105,
  808189984,
  4275763198,
  1746094671,
  1611873025,
  1659070578,
  2050188514,
  1263309568,
  1612355840,
  1918509568,
  1616523777,
  1914267954,
  1671586786,
  2246292,
  844196209,
  2083546145,
  578777104,
  1073911808,
  671620688,
  3765491968,
  1128407043,
  2054637631,
  1118788741,
  620810593,
  1616713856,
  2017491011,
  2990538765,
  3674220544,
  1760755741,
  3512091307,
  412764982,
  2023201536,
  3507038978,
  1773291104,
  4227266558,
  4236310526,
  4160657409,
  3187145539,
  3494326016,
  860880931,
  2322463,
  637608450,
  1880765266,
  1918782248,
  3492293888,
  1244220200,
  1646486627,
  5972864,
  587227619,
  1633903266,
  1638222373,
  1227161632,
  4178376702,
  1755526463,
  425396653,
  1759731875,
  425525306,
  1625629448,
  1109344282,
  1260310531,
  1625055843,
  1209919436,
  1650488486,
  1623351428,
  4193581054,
  1209722820,
  327713,
  891833113,
  573072227,
  2016096556,
  4191219712,
  3208671278,
  570615734,
  2600525828,
  3500878594,
  1258824195,
  553803866,
  4160636941,
  553843343,
  4160636938,
  3886414475,
  1126158603,
  1343228616,
  1343229616,
  1343228600,
  16044,
  1343228100,
  15856,
  2837,
  15895,
  1343229528,
  1343229476,
  1343229572,
  2885,
  1275704688,
  1688226569,
  2181129,
  1667432453,
  573060392,
  2016096556,
  4187287552,
  1881677825,
  877903680,
  3178262560,
  1343228616,
  3673,
  1343229572,
  570434496,
  1620706651,
  1225083396,
  1728538,
  1611739892,
  1620713985,
  1187006320,
  2032384,
  50332184,
  4160730384,
  536936427,
  3187719440,
  587276960,
  89306384,
  1750229075,
  536889858,
  4160643091,
  3172006925,
  335552512,
  308728,
  2031637,
  1119098950,
  583062021,
  43141900,
  1108568091,
  31872e5,
  1109139051,
  456380680,
  3540337323,
  3735584,
  4200527871,
  3890878820,
  555745312,
  4200134655,
  22750080,
  3890354404,
  1073840156,
  374264,
  262159,
  460920966,
  1119099145,
  583062021,
  43141893,
  1108568091,
  31872e5,
  872480800,
  4200200191,
  3891148031,
  1073840156,
  3052413697,
  1113305221,
  2151395075,
  570434496,
  1620706651,
  1822059162,
  1633296902,
  47325920,
  570515482,
  1629113375,
  1747345562,
  3285900,
  1134175620,
  1125457923,
  4222679038,
  2466325250,
  1613046656,
  1617232155,
  1625645221,
  3523033857,
  570696448,
  1572889,
  4187289599,
  554181388,
  537019293,
  4160635661,
  2567043993,
  3507890433,
  1613111820,
  1133928550,
  638058547,
  1621508894,
  1625694210,
  4220188670,
  570565376,
  3670041,
  4185454591,
  1258562048,
  2953142298,
  587316720,
  1187047373,
  1073872904,
  1073840140,
  1258758288,
  3037724800,
  4160643096,
  570489739,
  1616530180,
  1633312986,
  1650090458,
  3171967706,
  1073799168,
  1073840128,
  4160730384,
  4160749545,
  1258618769,
  721446939,
  4160671745,
  536935693,
  1187036432,
  1343229796,
  1244378616,
  1611877163,
  1611876907,
  1261117954,
  1108568091,
  554029064,
  1612270377,
  102441936,
  1679450906,
  1679434240,
  5972936,
  3523033857,
  604578e3,
  621027840,
  100671745,
  989921323,
  1753469437,
  140196865,
  416432139,
  3522505728,
  3642960388,
  4160693952,
  4160749487,
  91684695,
  583018240,
  1748197559,
  1133726998,
  1125364450,
  587292723,
  1622343721,
  4160684088,
  570554685,
  2630140,
  4026548818,
  1259337779,
  1117284379,
  595644424,
  26948672,
  3521200796,
  532736,
  4188338175,
  4276680703,
  1186870529,
  1187006248,
  1073799168,
  2097728,
  1073803264,
  1074184192,
  1073840140,
  537140992,
  537141244,
  3037743877,
  1624777226,
  1208246553,
  4160637188,
  3207658011,
  1187047421,
  1074102272,
  7065,
  537141248,
  403289392,
  3758902621,
  3121838084,
  103039068,
  6562568,
  1080873728,
  3522837249,
  1080164882,
  1116221441,
  470866928,
  3039870256,
  1547911753,
  169619969,
  3506782637,
  822194781,
  1101683780,
  1566776084,
  889279827,
  822202875,
  3174093295,
  705185412,
  3027292986,
  973201436,
  1417895051,
  1180750331,
  1187006320,
  705185412,
  440652590,
  3522299803,
  436843632,
  138615813,
  1548014338,
  805400580,
  3540125827,
  2147768900,
  403255298,
  424811053,
  3540204048,
  3229141368,
  3539679760,
  3540059986,
  3222849816,
  3540058194,
  3221801224,
  5427209,
  2282476292,
  3489955843,
  805449986,
  2014040065,
  3161485315,
  1198540384,
  9609993,
  855710363,
  1187006232,
  1904441739,
  1900247371,
  1896053003,
  1891858635,
  1887664267,
  1883469899,
  1879275531,
  1198540384,
  2999535236,
  1125712395,
  1187045393,
  705185412,
  138662698,
  1879167745,
  2999529473,
  1125712395,
  3540060291,
  805470209,
  437995107,
  3121289426,
  470500121,
  3540466192,
  470594608,
  1186995213,
  974176314,
  3157316348,
  3539994450,
  5423114,
  3221410560,
  5427206,
  2147603202,
  805490690,
  1879166976,
  1198540384,
  446407429,
  855710363,
  1904297752,
  1895919937,
  1887531201,
  1879142465,
  1198540384,
  79764919,
  1174975749,
  3171567130,
  37752258,
  604047936,
  1126172132,
  3000118015,
  1111546112,
  721304065,
  981389825,
  671106928,
  3573564960,
  981353024,
  847249554,
  264521584,
  3573811172,
  3489672448,
  1111502849,
  973198339,
  3490846720,
  847369723,
  813748481,
  813748741,
  755028483,
  4247567,
  721304065,
  838982150,
  172023047,
  1125123538,
  1198539552,
  96477439,
  536930298,
  101009264,
  172020205,
  3890938496,
  336376451,
  335823715,
  1131197069,
  2995001627,
  69550956,
  421202980,
  336139264,
  46154568,
  1124927193,
  406851929,
  4343664,
  3489730066,
  3506514687,
  96472512,
  236060746,
  721408001,
  231330049,
  570492361,
  3557376065,
  3573563457,
  1116226130,
  3674266626,
  1112678912,
  1198530064,
  407454465,
  671142136,
  3891714808,
  3037733120,
  4287035391,
  864157715,
  1141560336,
  3674683927,
  3709413895,
  1137252289,
  130031617,
  3171958856,
  706757202,
  572578560,
  3171959056,
  3171950592,
  3037733120,
  4285331455,
  82954,
  974640366,
  1136778222,
  3706268168,
  3171958928,
  3039830528,
  3573950720,
  1124403149,
  973146184,
  570482704,
  374064,
  3491054349,
  369891277,
  3506782892,
  239337929,
  29377313,
  3891671559,
  524293,
  842875474,
  553705476,
  572372272,
  620763730,
  4283824127,
  553696560,
  671135024,
  572447477,
  130357842,
  3891464256,
  1185162496,
  687923213,
  3758480386,
  3657640448,
  1115626377,
  3758168338,
  454171017,
  1130841700,
  3406830528,
  3523217508,
  1174811905,
  1175404846,
  140788015,
  3049277296,
  4293064703,
  4293326847,
  327603196,
  282203023,
  1129792342,
  1130841700,
  322900790,
  428415936,
  3049307584,
  4291885055,
  4291950591,
  687920124,
  428465154,
  3758168850,
  420617097,
  274993270,
  3891122677,
  555267376,
  4285134847,
  153373789,
  3674020608,
  3573356800,
  6619266,
  553666643,
  3674358434,
  1111497554,
  13821946,
  604087125,
  4291164159,
  570437897,
  620765952,
  4161597440,
  4277008383,
  4275107839,
  4161204224,
  3036735375,
  4292409343,
  3170911752,
  124003329,
  3691201184,
  1117799012,
  1198578944,
  1198522400,
  4160730480,
  3784114121,
  555267376,
  4281464831,
  1245451265,
  348734289,
  273232129,
  21148674,
  1128351801,
  1211701842,
  2739413248,
  4160701388,
  1141440400,
  3882138628,
  4160730416,
  130723,
  1228133397,
  4276025343,
  3540062289,
  272642305,
  1174516738,
  10177326,
  449386688,
  2738561536,
  4160701396,
  1175584648,
  1293316,
  587196233,
  3039881211,
  4292933631,
  3691653958,
  726024795,
  1210375174,
  822625112,
  436736265,
  3879215621,
  587154368,
  3039881013,
  4269078527,
  4268685311,
  4268816383,
  21561664,
  308549844,
  3557438465,
  3657767636,
  1092633188,
  3540790300,
  3758757824,
  740049185,
  671142663,
  1209260547,
  1078466505,
  399040531,
  570482705,
  3657574400,
  1112097344,
  2735557133,
  4160693249,
  1175519046,
  411322890,
  444912130,
  446747648,
  570439681,
  1187047169,
  326016436,
  5909,
  372130559,
  748557770,
  93569721,
  23258160,
  1686629713,
  995675660,
  526087672,
  267050316,
  134043372,
  67087032,
  33551700,
  16776876,
  8388564,
  4194300,
  2097148,
  1048576,
  524290,
  1179625964,
  1179625965,
  548494836,
  269846812,
  134392900,
  134392901,
  67130724,
  33557164,
  16777556,
  8388652,
  4194308,
  2097156,
  1048576,
  524288,
  524291,
  1079069369,
  398767472,
  236060738,
  721408081,
  399364178,
  236650571,
  738185297,
  1320407122,
  1076969520,
  1127233025,
  1080050481,
  453001321,
  446503753,
  3557628628,
  3657968670,
  1258784,
  1084882954,
  3758833953,
  532992,
  1302538,
  3758563840,
  3673632030,
  144416,
  1093157026,
  3491305536,
  3489861569,
  1112687552,
  805425408,
  1118837174,
  412275204,
  989937984,
  3556393648,
  3540125760,
  704655361,
  738119689,
  130667018,
  98255880,
  3178251288,
  3506186752,
  138471394,
  3891396672,
  130603521,
  34127216,
  96481535,
  975224176,
  3886815506,
  1143079442,
  992012201,
  3886815579,
  1143669275,
  1187047337,
  1174582656,
  265437258,
  1184237522,
  4784192,
  3493662210,
  3493604095,
  3493596683,
  3493538815,
  1065359575,
  34144768,
  172558912,
  1184110658,
  164301250,
  1128809306,
  3540126866,
  3556780032,
  37958145,
  38931904,
  1147148416,
  3507359169,
  3526045694,
  3540058203,
  805425157,
  100611841,
  1148208184,
  805420416,
  4196416,
  922871798,
  3524472830,
  3540125760,
  721432577,
  436195333,
  96417537,
  1148208184,
  138460544,
  3891658816,
  34748944,
  990963648,
  3888185883,
  922868242,
  805490958,
  671288768,
  3758477578,
  922868234,
  805425414,
  3489861056,
  96477185,
  3179299952,
  3179300464,
  96477439,
  3179299952,
  604091760,
  98845286,
  173146698,
  164840226,
  103622096,
  1718314542,
  3002273222,
  171967040,
  231293728,
  171327566,
  1865222134,
  687911625,
  704630832,
  721473580,
  738185273,
  442224682,
  167850877,
  201933673,
  1311680,
  453264204,
  1131156132,
  55120932,
  252451081,
  822464776,
  3540977932,
  41945289,
  440419153,
  3758674954,
  822686465,
  3540322636,
  37751049,
  440419153,
  872535040,
  3523816446,
  408946137,
  3178240384,
  3507235839,
  96477439,
  3178251056,
  475650810,
  241291525,
  536990467,
  1127220672,
  3194224,
  1187036528,
  4305936,
  34198074,
  570493513,
  411633106,
  3493465538,
  3493210879,
  273822333,
  4838144,
  223061018,
  164125924,
  1130382176,
  1130369792,
  35918656,
  883563044,
  1128267808,
  168496064,
  318784344,
  356533088,
  1130568228,
  1575899,
  38355776,
  289413640,
  31146820,
  806360032,
  1141051776,
  1176294150,
  1130643812,
  453575689,
  855757824,
  416286162,
  1198570512,
  3489926665,
  96475072,
  230746104,
  264300539,
  3891464128,
  3150568177,
  2543756976,
  2189855633,
  2147483648,
  8388607,
  604091888,
  1080231908,
  1187045377,
  218936816,
  510005199,
  461964598,
  3540190564,
  1111507913,
  822203136,
  3489860964,
  183901286,
  467980295,
  8331264,
  126426233,
  1015026313,
  220005156,
  510529503,
  463144246,
  3540190573,
  1112687579,
  855757568,
  3489860973,
  183901294,
  468570119,
  8331776,
  127605883,
  1031803547,
  456065837,
  3562543974,
  773867172,
  924899910,
  1086062612,
  1086128157,
  1093878002,
  411058986,
  264978777,
  1137299461,
  570442688,
  1095778916,
  1180844369,
  3509062989,
  3506900237,
  3491702784,
  1094719780,
  973160777,
  3505982733,
  3540385892,
  3540004865,
  738210049,
  138465537,
  973144128,
  479515659,
  3506703076,
  1141966098,
  1142491099,
  131710448,
  1125731104,
  131719168,
  3186630656,
  3520932096,
  3520801792,
  838974960,
  138414022,
  1126696909,
  771754057,
  3889811681,
  3660131900,
  926957088,
  1086062612,
  604098560,
  1125400818,
  1085997082,
  399721244,
  1185736620,
  3657969440,
  276e3,
  868532,
  1090011317,
  1126711609,
  792520613,
  1059117580,
  276032,
  3489677492,
  1090003969,
  541444,
  1124876465,
  3890878401,
  1638416,
  3885245440,
  2146435072,
  218936816,
  87432806,
  182852489,
  224658788,
  476434434,
  3489925869,
  553721856,
  1015022857,
  1185153828,
  510070044,
  467338559,
  90442471,
  3489795428,
  183311461,
  570478596,
  85664513,
  52706432,
  1147420791,
  2995041431,
  1131721366,
  1132334087,
  1131351061,
  1130934912,
  3540129846,
  67117057,
  70260799,
  419433525,
  1183072637,
  2996482696,
  202130256,
  203375458,
  2995667836,
  429015934,
  637653762,
  430179382,
  202834966,
  1097275446,
  2994846721,
  1129427610,
  1130499076,
  1130105883,
  1129886336,
  3540129810,
  67117057,
  68163620,
  406850579,
  405619043,
  536887646,
  3154526535,
  2996023944,
  202130264,
  202523491,
  2995340116,
  408634193,
  553767682,
  409207817,
  203031577,
  1096947721,
  1096161389,
  1095180288,
  49921048,
  1125191026,
  225051376,
  49103632,
  3506638090,
  1094719853,
  989937993,
  463162898,
  1119027318,
  7197198,
  805425927,
  1097934336,
  1127564902,
  138465537,
  85655616,
  132389065,
  3186639905,
  855759371,
  805425414,
  822202628,
  3489729871,
  3891398729,
  536872929,
  906083824,
  536872241,
  59371,
  1023,
  219985392,
  88022631,
  182918107,
  224658788,
  476499970,
  3489925878,
  587276800,
  1015022875,
  634389284,
  637535789,
  1714308086,
  1718487326,
  435556302,
  4802228,
  3489795407,
  183901310,
  536924163,
  1061167360,
  457048895,
  1152647350,
  92225281,
  139008969,
  906063662,
  43780214,
  1126501781,
  334316405,
  330122101,
  275592449,
  460719094,
  46969868,
  1126960453,
  2998121122,
  203899738,
  204817275,
  2997109615,
  421217132,
  604099330,
  423560228,
  203228188,
  1098717348,
  1097668900,
  2995886592,
  1130476204,
  1132203031,
  1130302507,
  1130017426,
  3540129956,
  68297217,
  69343423,
  408030243,
  2617327995,
  421217132,
  31002194,
  21250826,
  2994805408,
  1129427634,
  1129976835,
  1131613238,
  1131917959,
  433199062,
  1098786560,
  429589558,
  202769431,
  1096685695,
  418190963,
  637534707,
  1098199936,
  3506835369,
  174653540,
  173737448,
  3523822360,
  570744879,
  864044180,
  178864501,
  177931688,
  3542500120,
  1095319872,
  2583692324,
  891649,
  459555669,
  451167043,
  2995171989,
  202851189,
  201540478,
  2995929951,
  414597978,
  570544898,
  415171602,
  204670002,
  1098586450,
  1100759634,
  3556912128,
  805380608,
  138428753,
  1125124042,
  2952923209,
  131548770,
  1264128146,
  1264130258,
  3523429018,
  411632914,
  3186629065,
  704651264,
  3791873,
  855752176,
  3891594521,
  553703943,
  264888327,
  223479753,
  315215875,
  1229905921,
  536872201,
  1187006320,
  3539075146,
  973147474,
  1117408072,
  3052458730,
  453575956,
  3540060242,
  1095309312,
  416417947,
  1184105746,
  206218305,
  151739554,
  1129530195,
  1129517851,
  34739035,
  1252050,
  190530395,
  1130563660,
  1129518043,
  855709147,
  449974363,
  449973267,
  1130037267,
  226755209,
  2995602209,
  1131786910,
  1132334095,
  1130302491,
  1130148492,
  3540130102,
  69477377,
  70523199,
  425987123,
  27083131,
  1126960804,
  2997171232,
  337920853,
  204292948,
  295967076,
  454165458,
  2995696277,
  202851189,
  202130302,
  2995995495,
  418792291,
  587322114,
  419365915,
  204735539,
  1098651995,
  1097079003,
  587208923,
  2996715875,
  2996650870,
  1132268575,
  74204031,
  430181357,
  33964413,
  456525967,
  125649327,
  1097730294,
  2996155060,
  339166060,
  202523517,
  2998289239,
  401228630,
  637540781,
  68305266,
  70129855,
  422972458,
  839401850,
  3524858194,
  94112413,
  420485073,
  1147224425,
  48624,
  1021,
  2046,
  2047,
  3604999160,
  3099510221,
  2762517938,
  2509872288,
  2324533138,
  2172880264,
  173883730,
  399574492,
  1095571732,
  1130561579,
  1131852454,
  203928226,
  1132413818,
  198313041,
  1098520969,
  416422098,
  1112081792,
  3556917648,
  872489728,
  140525917,
  132974697,
  1147224872,
  3050356208,
  3050364945,
  5001139,
  3489729892,
  3506586300,
  218701824,
  6030601,
  3489729892,
  3506586300,
  219881984,
  637601051,
  3557572683,
  3573563467,
  1117340278,
  1116786947,
  3540113411,
  3690997248,
  506479222,
  1124842960,
  1124276443,
  3505734419,
  3673565440,
  1178920948,
  1180059213,
  3035645535,
  3169863536,
  1185498784,
  1186678450,
  1180845936,
  1184156184,
  704661082,
  3758414624,
  3390588514,
  687883924,
  415291930,
  1180385633,
  1180451131,
  1179795636,
  1126318330,
  1179469380,
  1097548130,
  1184450192,
  1085490731,
  1090273597,
  1179796252,
  1101153883,
  1183990187,
  1198540443,
  1101077184,
  1094403659,
  1085556300,
  1090143810,
  1179927330,
  1096959581,
  1183990123,
  1177241243,
  1094533299,
  1125925116,
  1179338306,
  1101742498,
  1184450192,
  536889200,
  1198530816,
  3036684800,
  4026544672,
  587818,
  570473728,
  841004288,
  4163956736,
  3170893832,
  3036684544,
  4161073152,
  553705502,
  3571848643,
  1183626512,
  4200387,
  3490319874,
  3490458367,
  981409361,
  440403465,
  448807e3,
  117444865,
  536928287,
  196609,
  1138277648,
  3171959769,
  3036684800,
  4161597440,
  1117394890,
  3170947328,
  553731032,
  1078003657,
  570473728,
  3569227019,
  1184150800,
  4172869632,
  872485908,
  553703936,
  1147279307,
  3557571124,
  3657902604,
  1083244548,
  1112686736,
  1087648288,
  3171959585,
  1138312152,
  841006352,
  1175245831,
  1112686740,
  1091645984,
  1126187216,
  572688,
  840964041,
  1112724483,
  1091580448,
  1621264,
  3171942425,
  131796931,
  235536449,
  721408007,
  151638024,
  411650621,
  121651993,
  1656688,
  1198530560,
  415844666,
  4909050,
  1262030162,
  3709016786,
  3659410175,
  264963538,
  1125779419,
  255852739,
  172557065,
  1125139208,
  3540058203,
  805425153,
  138626928,
  1198576379,
  264818690,
  1198524352,
  319947530,
  3522703873,
  705105730,
  570544629,
  587194368,
  33558472,
  96475264,
  553666416,
  553648138,
  553705476,
  398524426,
  570482691,
  3758433024,
  399188480,
  1079591e3,
  1100552896,
  1276949808,
  687872674,
  119043,
  536924176,
  357317152,
  3523531026,
  402668033,
  223101257,
  1276498938,
  3523494562,
  411632914,
  415827931,
  1137884464,
  536874322,
  3891601664,
  973198339,
  1095309312,
  839636475,
  180356420,
  1126696269,
  6556361,
  604033027,
  1096892768,
  3554732001,
  3891791940,
  2047,
  939524096,
  2146435072,
  896,
  1074,
  2046,
  265030922,
  85663315,
  89266889,
  1137300227,
  3540009536,
  223490305,
  475254787,
  3490122459,
  536877842,
  476119140,
  310970249,
  51526272,
  449989474,
  839010160,
  705483813,
  621926940,
  727725,
  839401771,
  406791,
  1083195630,
  1127301265,
  1130614e3,
  587207962,
  1129660762,
  1129792342,
  317981375,
  427165046,
  401424743,
  461379967,
  1198539193,
  536879628,
  4790217,
  84488449,
  34203611,
  1125715459,
  1112736256,
  3557306912,
  1092157452,
  1087914132,
  570442528,
  1095844176,
  542576,
  991958985,
  3589354016,
  553656320,
  1198531072,
  4160730608,
  4026596950,
  1183119389,
  4164874240,
  1180742659,
  4166578176,
  4160732172,
  3832872528,
  4160730608,
  4026596934,
  4026595341,
  3758553135,
  4160730608,
  4026596926,
  4026595333,
  4160747589,
  3186687550,
  4160730368,
  2754740095,
  4288149503,
  1294803968,
  131223085,
  1140249346,
  1097220717,
  3523477586,
  1185105570,
  1186023080,
  1184948227,
  1185564321,
  2758756019,
  654395044,
  4160693791,
  922877483,
  790707713,
  3170947577,
  2995013209,
  1131197069,
  1131746310,
  1130238987,
  1130017410,
  401217709,
  1137891072,
  414581778,
  204145706,
  1098062098,
  1179207232,
  396034205,
  1126829970,
  1100562832,
  3875283518,
  2995013193,
  1131197069,
  1131746310,
  1130238987,
  1130017410,
  401217709,
  1137891072,
  414581778,
  204145706,
  1098062098,
  1180255824,
  396034205,
  1126829970,
  1096368464,
  3873317438,
  1023,
  2649771451,
  922113733,
  20861,
  1335578,
  690309,
  1647099,
  4160730608,
  1296104902,
  1076625420,
  1118621697,
  218747138,
  536872201,
  1076625436,
  1118621697,
  219926786,
  570426651,
  49096192,
  3573820160,
  1080763906,
  3556786281,
  424624758,
  1117377796,
  906091788,
  3758309481,
  3657908923,
  1080770049,
  1048583,
  983098,
  3866649,
  704689216,
  721473807,
  6082570,
  872486244,
  5034249,
  872486244,
  956420354,
  3758242561,
  553656320,
  4160741422,
  574553009,
  4261672959,
  1183532674,
  553656320,
  570508928,
  1183909778,
  1185195037,
  639575809,
  4253153279,
  1040267009,
  3522768673,
  1179862602,
  604060172,
  654313316,
  1771602,
  1100207109,
  1098455296,
  3522627684,
  1096015876,
  1102650112,
  3522234468,
  273221582,
  1127221312,
  771800128,
  1275711498,
  3573632265,
  1139622884,
  3506505718,
  1097406720,
  1097406720,
  4160692797,
  4160749136,
  3186687308,
  2146435072,
  2287612045,
  843314856,
  1639665513,
  497837829,
  2520805041,
  263043836,
  2869681010,
  133525158,
  3852451129,
  67021686,
  3130483275,
  33543515,
  3722155222,
  16775850,
  1458498141,
  8388437,
  2864150382,
  4194282,
  1431681980,
  2097149,
  2863312350,
  1048575,
  4116010351,
  524287,
  4272597675,
  262143,
  4292171093,
  131071,
  4294617771,
  65535,
  4294923605,
  32767,
  4294961835,
  16383,
  4294966613,
  8191,
  4294967211,
  4095,
  4294967285,
  2047,
  4294967295,
  1023,
  0,
  512,
  0,
  256,
  0,
  128,
  0,
  64,
  0,
  32,
  0,
  16,
  0,
  8,
  0,
  4,
  0,
  2,
  0,
  1,
  2147483648,
  0,
  1073741824,
  0,
  4160730608,
  2757033479,
  4263507967,
  3657705728,
  1294027808,
  1097406720,
  3020175873,
  2790401793,
  587276800,
  3459254171,
  453002932,
  3557507497,
  908083838,
  1094516765,
  1085538332,
  1090387990,
  1096958772,
  3758178667,
  1097406720,
  922830438,
  3521654561,
  2996744837,
  336020341,
  202982270,
  2995012431,
  401687372,
  603986230,
  67715425,
  70523007,
  425987121,
  262422905,
  1126170760,
  411047817,
  3154395481,
  842941010,
  4253022207,
  48624,
  4101234283,
  744261117,
  5223920,
  360698448,
  922865742,
  4160737359,
  3020225971,
  230818377,
  37765905,
  2787845889,
  570443444,
  1115562752,
  865824,
  803133,
  409780,
  1127497982,
  1095582020,
  3506769838,
  2687008,
  3459270246,
  1101732626,
  1151607816,
  790705921,
  9032167,
  403837065,
  3162521931,
  3423839255,
  1131951873,
  1132217209,
  315163983,
  432019405,
  44515689,
  399316388,
  1097603529,
  411047901,
  1097613657,
  399319614,
  3490202284,
  118360320,
  151601968,
  1127286566,
  973345060,
  4160743411,
  3186687270,
  536889602,
  1224916464,
  3186630656,
  4293918720,
  2146435072,
  47274,
  1302123,
  1044387,
  726817,
  3214429171,
  435364844,
  3444379862,
  239598563,
  2327624058,
  126468571,
  36459724,
  65095192,
  3884134405,
  33040816,
  2279612190,
  16647493,
  2889981476,
  8356009,
  1164056449,
  4186133,
  2846560486,
  2095106,
  1430608008,
  1048064,
  178891460,
  524160,
  22365526,
  262112,
  2795947,
  131064,
  349509,
  65534,
  2147527338,
  32767,
  3758101845,
  16383,
  4160750251,
  8191,
  4261412949,
  4095,
  4286578699,
  2047,
  4292870145,
  1023,
  4294443008,
  511,
  4294836224,
  255,
  4294934528,
  127,
  4294959104,
  63,
  4294965248,
  31,
  4294966784,
  15,
  4294967168,
  7,
  4294967264,
  3,
  4294967288,
  1,
  4294967294,
  0,
  2147483648,
  0,
  1073741824,
  0,
  1162104393,
  538976344,
  5067848,
  1330007625,
  843470175,
  5527636,
  1886609746,
  1920099682,
  1766858873,
  844124672,
  1869562400,
  17170548,
  1343229376,
  1343229420,
  1101,
  10,
  3187671300,
  79,
  12,
  14,
  1,
  262143,
  16776963,
  512,
  0,
  50331395,
  134218240,
  922880256,
  66048,
  67762816,
  134348800,
  117460998,
  1073905925,
  84344832,
  4194818,
  17041664,
  16712192,
  84344832,
  4194819,
  2214921984,
  16386,
  17826066,
  1073741824,
  208522,
  33620224,
  3187671299,
  1343229464,
  1343229520,
  1301298411,
  1313429331,
  3223092,
  67586,
  131074,
  8517632,
  65537,
  1,
  262143,
  2686976,
  1375731712,
  1378699600,
  538980944,
  1095114784,
  540422484,
  4276822048,
  16777216,
  790528,
  33556480,
  134512648,
  2176,
  536870913,
  71303172,
  3187703808,
  28,
  9045,
  9417,
  6285,
  2729,
  6341,
  6141,
  15812,
  15825,
  1343229621,
  540165717,
  1953460034,
  1684107116,
  1981837925,
  170929714,
  1701080909,
  1377843820,
  1651536737,
  2037543525,
  543772704,
  171069522,
  1918988098,
  1145646436,
  1347559482,
  1347562825,
  50596402,
  3187672073,
  1343229528,
  1343229476,
  33685504,
  32,
  541675602,
  1347551740,
  537459506,
  4247914233,
  1006699030,
  1819112552,
  1701329982,
  1010721889,
  1635018093,
  1953785888,
  1902456176,
  1031170421,
  1717924386,
  1752393074,
  1868767266,
  1852142702,
  807550324,
  1280464187,
  637282109,
  791624307,
  1886609778,
  1920099682,
  778662009,
  795701091,
  1769366884,
  1378837859,
  1983853136,
  1769173605,
  1631415919,
  1667392097,
  1701078115,
  661022309,
  1010708258,
  1651374639,
  1048142959,
  1768187218,
  1952671090,
  543649385,
  1008758644,
  1702002685,
  1046529638,
  792498673,
  1651637601,
  792488956,
  3187731451,
  3187719680
]);

var DEFAULT_UNIQUE_ID = new Uint8Array([
  230,
  97,
  65,
  3,
  231,
  21,
  34,
  42
]);
var STDIO_HOOKS = Object.freeze({
  GETCHAR_TIMEOUT_US: 268460584,
  STDIO_USB_OUT_CHARS: 268461096,
  BOOTLOADER_ENTRY: 268439250
});
var FLASH_LED_TABLES = Object.freeze([
  {
    category: "suspects",
    categoryIndex: 0,
    flashAddr: 268482060,
    ramAddr: 536873096,
    count: 21,
    cursorRamAddr: 536873092,
    clueMaskRamAddr: 536878100,
    winMaskRamAddr: 536878792,
    msbBit: 23
  },
  {
    category: "attacks",
    categoryIndex: 1,
    flashAddr: 268481300,
    ramAddr: 536872336,
    count: 21,
    cursorRamAddr: 536873088,
    clueMaskRamAddr: 536878092,
    winMaskRamAddr: 536878784,
    msbBit: 23
  },
  {
    category: "rooms",
    categoryIndex: 2,
    flashAddr: 268481812,
    ramAddr: 536872848,
    count: 10,
    cursorRamAddr: 536878408,
    clueMaskRamAddr: 536878096,
    winMaskRamAddr: 536878788,
    msbBit: 15
  }
]);
var BUTTON_PINS = Object.freeze({
  UP: 15,
  DOWN: 16,
  SELECT: 17
});
var textDecoder = new TextDecoder();
function shareToBrightness(share) {
  if (!(share > 0)) return 0;
  if (share >= 1) return 255;
  const v = Math.pow(share / (share + 2e-3), 0.45);
  return Math.min(255, Math.max(1, Math.round(v * 255)));
}
function sharesToRgb(shares, rgbOut = new Uint8Array(156)) {
  for (let i = 0; i < 52; i++) {
    const base = i * 3;
    const rShare = shares[base];
    const gShare = shares[base + 1];
    const bShare = shares[base + 2];
    const maxShare = Math.max(rShare, gShare, bShare);
    if (maxShare > 0) {
      const peak = shareToBrightness(maxShare);
      rgbOut[base] = rShare > 0 ? Math.min(255, Math.round(peak * rShare / maxShare)) : 0;
      rgbOut[base + 1] = gShare > 0 ? Math.min(255, Math.round(peak * gShare / maxShare)) : 0;
      rgbOut[base + 2] = bShare > 0 ? Math.min(255, Math.round(peak * bShare / maxShare)) : 0;
    } else {
      rgbOut[base] = 0;
      rgbOut[base + 1] = 0;
      rgbOut[base + 2] = 0;
    }
  }
  return rgbOut;
}
function escapeSerialBytes(rawStr) {
  let out = "";
  for (let i = 0; i < rawStr.length; i++) {
    const c = rawStr.charCodeAt(i) & 255;
    if (c >= 32 && c <= 126) {
      out += rawStr[i];
    } else {
      out += "\\x" + c.toString(16).padStart(2, "0");
    }
  }
  return out;
}
function loadUF2(mcu, uf2Bytes) {
  const view = new DataView(uf2Bytes.buffer, uf2Bytes.byteOffset, uf2Bytes.byteLength);
  for (let off = 0; off + 512 <= uf2Bytes.byteLength; off += 512) {
    if (view.getUint32(off, true) !== 171066965) continue;
    const addr = view.getUint32(off + 12, true);
    const len = view.getUint32(off + 16, true);
    if (addr >= 268435456 && addr < 285212672) {
      mcu.flash.set(uf2Bytes.subarray(off + 32, off + 32 + len), addr - 268435456);
    }
  }
}
function extractLedTableFromFlash(flashBytes) {
  const leds = [];
  for (const tbl of FLASH_LED_TABLES) {
    const baseOff = tbl.flashAddr - 268435456;
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
        r: {
          cathode: rCathode,
          anode: rAnode
        },
        g: {
          cathode: gCathode,
          anode: gAnode
        },
        b: {
          cathode: bCathode,
          anode: bAnode
        }
      });
    }
  }
  return leds;
}
function buildKioskMessage(serialHex, clueNum, suspectsMask24, attacksMask24, roomsMask16) {
  const payload34 = serialHex.toUpperCase() + (clueNum & 255).toString(16).toUpperCase().padStart(2, "0") + (suspectsMask24 & 16777215).toString(16).toUpperCase().padStart(6, "0") + (attacksMask24 & 16777215).toString(16).toUpperCase().padStart(6, "0") + (roomsMask16 & 65535).toString(16).toUpperCase().padStart(4, "0");
  let sum = 0;
  for (let i = 0; i < 17; i++) {
    sum = sum + parseInt(payload34.slice(i * 2, i * 2 + 2), 16) & 255;
  }
  const chk = -sum & 255;
  const shift = chk % 34;
  const rotated = shift === 0 ? payload34 : payload34.slice(34 - shift) + payload34.slice(0, 34 - shift);
  return `kiosk:${rotated}${chk.toString(16).toUpperCase().padStart(2, "0")}
\r`;
}
function parseBadgeMessage(line) {
  if (typeof line !== "string") return {
    valid: false,
    raw: ""
  };
  const m = line.match(/badge:([0-9A-Fa-f]{34})([0-9A-Fa-f]{2})/);
  if (!m) {
    return {
      valid: false,
      raw: line.trim()
    };
  }
  const rotated = m[1];
  const chk = parseInt(m[2], 16);
  const shift = chk % 34;
  const unrotated = shift === 0 ? rotated : rotated.slice(shift) + rotated.slice(0, shift);
  let sum = 0;
  for (let i = 0; i < 34; i++) {
    sum = sum + unrotated.charCodeAt(i) & 255;
  }
  if ((sum + chk & 255) !== 0) {
    return {
      valid: false,
      raw: m[0],
      checksumMismatch: true
    };
  }
  return {
    valid: true,
    raw: m[0],
    serial: unrotated.slice(0, 16).toLowerCase(),
    statusByte: parseInt(unrotated.slice(16, 18), 16),
    suspectsCursor: parseInt(unrotated.slice(18, 24), 16),
    attacksCursor: parseInt(unrotated.slice(24, 30), 16),
    roomsCursor: parseInt(unrotated.slice(30, 34), 16)
  };
}
function createBadge(options = {}) {
  const mcu = new RP2040();
  const cycleNanos = options.cycleNanos ?? 125;
  const stdioMode = options.stdioMode ?? "hook";
  const logs = [];
  const maxLogs = options.maxLogs ?? 200;
  mcu.logger = {
    debug() {
    },
    info(name, msg) {
      if (name === "CortexM0Core" && msg === "SEV") {
        mcu.core0.eventRegistered = true;
        mcu.core0.waiting = false;
        mcu.core1.eventRegistered = true;
        mcu.core1.waiting = false;
        return;
      }
      if (logs.length < maxLogs) logs.push({
        level: "info",
        name,
        msg
      });
    },
    warn(name, msg) {
      if (logs.length < maxLogs) logs.push({
        level: "warn",
        name,
        msg
      });
    },
    error(name, msg) {
      if (logs.length < maxLogs) logs.push({
        level: "error",
        name,
        msg
      });
    }
  };
  let watchdogTriggers = 0;
  mcu.onWatchdogTrigger = () => {
    watchdogTriggers++;
    mcu.watchdog.enable = false;
  };
  const disableTimerPastGuard = Boolean(options.disableTimerPastGuard);
  const timer = mcu.peripherals[262228];
  const origTimerWrite = timer.writeUint32.bind(timer);
  timer.writeUint32 = (offset, value) => {
    if (offset >= 16 && offset <= 28) {
      const alarmIndex = offset - 16 >>> 2;
      const alarm = timer.alarms[alarmIndex];
      const target = value >>> 0;
      const nowMicros = Math.floor(mcu.clock.nanos / 1e3) >>> 0;
      const signedDelta = target - nowMicros | 0;
      if (!disableTimerPastGuard && signedDelta <= 0) {
        alarm.targetMicros = target;
        timer.fireAlarm(alarmIndex);
        return;
      }
      const deltaMicros = target - nowMicros >>> 0;
      alarm.armed = true;
      alarm.targetMicros = target;
      const subMicrosNanos = mcu.clock.nanos % 1e3;
      const deltaNanos = Math.max(0, deltaMicros * 1e3 - subMicrosNanos);
      alarm.clockAlarm.schedule(deltaNanos);
      return;
    }
    origTimerWrite(offset, value);
  };
  mcu.loadBootrom(bootromB1);
  if (!options.uf2Bytes) {
    throw new Error("options.uf2Bytes is required");
  }
  mcu.flash.fill(255);
  loadUF2(mcu, options.uf2Bytes);
  if (options.savedSectors) {
    if (options.savedSectors.sector1cc && options.savedSectors.sector1cc.byteLength === 4096) {
      mcu.flash.set(options.savedSectors.sector1cc, 1884160);
    }
    if (options.savedSectors.sector1cd && options.savedSectors.sector1cd.byteLength === 4096) {
      mcu.flash.set(options.savedSectors.sector1cd, 1888256);
    }
  }
  const ledTable = extractLedTableFromFlash(mcu.flash);
  const pairToChannel = new Int16Array(15 * 15).fill(-1);
  for (let i = 0; i < ledTable.length; i++) {
    const entry = ledTable[i];
    pairToChannel[entry.r.cathode * 15 + entry.r.anode] = i * 3 + 0;
    pairToChannel[entry.g.cathode * 15 + entry.g.anode] = i * 3 + 1;
    pairToChannel[entry.b.cathode * 15 + entry.b.anode] = i * 3 + 2;
  }
  for (const pin of [
    15,
    16,
    17
  ]) {
    mcu.gpio[pin].setInputValue(true);
  }
  if (options.holdButtonsAtBoot) {
    for (const btn of options.holdButtonsAtBoot) {
      const pin = typeof btn === "number" ? btn : BUTTON_PINS[btn];
      if (pin !== void 0) mcu.gpio[pin].setInputValue(false);
    }
  }
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
  const cumulativeChannelNanos = new Float64Array(156);
  let cumulativeWindowNanos = 0;
  let gpioSwitchCount = 0;
  const NUM_SUB_BUCKETS = 5;
  const SUB_BUCKET_NANOS = 1e7;
  const bucketChannelNanos = Array.from({
    length: NUM_SUB_BUCKETS
  }, () => new Float64Array(156));
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
        if ((activeLowMask & activeLowMask - 1) === 0 && (activeHighMask & activeHighMask - 1) === 0) {
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
  const fifo0to1 = [];
  const fifo1to0 = [];
  let fifoSt0 = 0;
  let fifoSt1 = 0;
  const divState = [
    {
      dividend: 0,
      divisor: 1,
      quotient: 0,
      remainder: 0,
      csr: 0
    },
    {
      dividend: 0,
      divisor: 1,
      quotient: 0,
      remainder: 0,
      csr: 0
    }
  ];
  const updateCoreDivider = (d, signed, core) => {
    if (d.divisor === 0) {
      d.quotient = d.dividend > 0 ? -1 : 1;
      d.remainder = d.dividend;
    } else if (signed) {
      d.quotient = (d.dividend | 0) / (d.divisor | 0) | 0;
      d.remainder = (d.dividend | 0) % (d.divisor | 0) | 0;
    } else {
      d.quotient = (d.dividend >>> 0) / (d.divisor >>> 0) >>> 0;
      d.remainder = (d.dividend >>> 0) % (d.divisor >>> 0) >>> 0;
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
    if (offset === 0) return isCore1 ? 1 : 0;
    if (offset === 4) {
      const inVal = origSioRead(4);
      const oe = mcu.sio.gpioOutputEnable;
      return (inVal & ~oe | mcu.sio.gpioValue & oe) >>> 0;
    }
    if (offset === 80) {
      const rx = isCore1 ? fifo0to1 : fifo1to0;
      const tx = isCore1 ? fifo1to0 : fifo0to1;
      const sticky = isCore1 ? fifoSt1 : fifoSt0;
      return sticky | (tx.length < 8 ? 2 : 0) | (rx.length > 0 ? 1 : 0);
    }
    if (offset === 88) {
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
    if (offset >= 96 && offset <= 120) {
      const d = divState[isCore1 ? 1 : 0];
      if (offset === 96 || offset === 104) return d.dividend >>> 0;
      if (offset === 100 || offset === 108) return d.divisor >>> 0;
      if (offset === 112) {
        d.csr &= ~2;
        return d.quotient >>> 0;
      }
      if (offset === 116) return d.remainder >>> 0;
      if (offset === 120) return d.csr >>> 0;
    }
    if (offset >= 256 && offset <= 380) {
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
    if (offset === 80) {
      if (isCore1) fifoSt1 = 0;
      else fifoSt0 = 0;
      updateFifoIrqs();
      return;
    }
    if (offset === 84) {
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
    if (offset >= 96 && offset <= 116) {
      const d = divState[isCore1 ? 1 : 0];
      const core = isCore1 ? core1 : core0;
      if (offset === 96) {
        d.dividend = value;
        updateCoreDivider(d, false, core);
      } else if (offset === 104) {
        d.dividend = value;
        updateCoreDivider(d, true, core);
      } else if (offset === 100) {
        d.divisor = value;
        updateCoreDivider(d, false, core);
      } else if (offset === 108) {
        d.divisor = value;
        updateCoreDivider(d, true, core);
      } else if (offset === 112) {
        d.quotient = value;
        d.csr = 3;
      } else if (offset === 116) {
        d.remainder = value;
        d.csr = 3;
      }
      return;
    }
    if (offset >= 256 && offset <= 380) {
      const lockMask = 1 << (offset - 256 >>> 2) >>> 0;
      core0SpinLockMask = (core0SpinLockMask & ~lockMask) >>> 0;
      core1SpinLockMask = (core1SpinLockMask & ~lockMask) >>> 0;
      origSioWrite(offset, value);
      return;
    }
    const isGpioReg = offset >= 16 && offset <= 44;
    const prevOut = mcu.sio.gpioValue;
    const prevOe = mcu.sio.gpioOutputEnable;
    if (isGpioReg) {
      flushGpioTime();
    }
    origSioWrite(offset, value);
    if (isGpioReg) {
      const newOut = mcu.sio.gpioValue;
      const newOe = mcu.sio.gpioOutputEnable;
      if (((newOut ^ prevOut) & 32767) !== 0 || ((newOe ^ prevOe) & 32767) !== 0) {
        gpioSwitchCount++;
        activeHighMask = newOe & newOut & 32767;
        activeLowMask = newOe & ~newOut & 32767;
      }
      const gp24High = (newOe & 1 << 24) !== 0 && (newOut & 1 << 24) !== 0;
      const gp25High = (newOe & 1 << 25) !== 0 && (newOut & 1 << 25) !== 0;
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
  const disableCore1ResetGrace = Boolean(options.disableCore1ResetGrace);
  const psm = mcu.peripherals[262160];
  let prevCore1Reset = false;
  const origPsmWrite = psm.writeUint32.bind(psm);
  psm.writeUint32 = (offset, value) => {
    origPsmWrite(offset, value);
    const inReset = !!(psm.frceOff & 1 << 16);
    if (inReset) {
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
  const uniqueId = options.uniqueId ?? DEFAULT_UNIQUE_ID;
  const flashOps = [];
  let flashDirty = false;
  let qspiSsCtrl = 0;
  let spiCmdBuf = [];
  const spiRxQueue = [];
  const commitSpiTransaction = () => {
    if (spiCmdBuf.length === 0) return;
    const cmd = spiCmdBuf[0];
    if (cmd === 32 && spiCmdBuf.length >= 4) {
      const addr = (spiCmdBuf[1] << 16 | spiCmdBuf[2] << 8 | spiCmdBuf[3]) & ~4095;
      mcu.flash.fill(255, addr, addr + 4096);
      if (addr >= 1884160 && addr < 1892352) flashDirty = true;
      flashOps.push({
        type: "erase4k",
        addr
      });
    } else if (cmd === 216 && spiCmdBuf.length >= 4) {
      const addr = (spiCmdBuf[1] << 16 | spiCmdBuf[2] << 8 | spiCmdBuf[3]) & ~65535;
      mcu.flash.fill(255, addr, addr + 65536);
      if (addr <= 1888256 && addr + 65536 > 1884160) flashDirty = true;
      flashOps.push({
        type: "erase64k",
        addr
      });
    } else if (cmd === 2 && spiCmdBuf.length > 4) {
      const addr = spiCmdBuf[1] << 16 | spiCmdBuf[2] << 8 | spiCmdBuf[3];
      const data = spiCmdBuf.slice(4);
      for (let i = 0; i < data.length; i++) {
        const off = addr & ~255 | addr + i & 255;
        mcu.flash[off] &= data[i];
      }
      if (addr >= 1884160 && addr < 1892352) flashDirty = true;
      flashOps.push({
        type: "program",
        addr,
        length: data.length
      });
    } else if (cmd === 75 && spiCmdBuf.length >= 13) {
      flashOps.push({
        type: "read_unique_id"
      });
    }
    spiCmdBuf = [];
  };
  const ioQspi = mcu.peripherals[262168];
  ioQspi.readUint32 = (offset) => offset === 12 ? qspiSsCtrl : 0;
  ioQspi.writeUint32 = (offset, value) => {
    if (offset === 12) {
      const prevCsLow = (qspiSsCtrl >> 8 & 3) === 2;
      qspiSsCtrl = value >>> 0;
      const nowCsLow = (qspiSsCtrl >> 8 & 3) === 2;
      if (!prevCsLow && nowCsLow) {
        spiCmdBuf = [];
      } else if (prevCsLow && !nowCsLow) {
        commitSpiTransaction();
      }
    }
  };
  const ssi = mcu.peripherals[98304];
  const origSsiRead = ssi.readUint32.bind(ssi);
  const origSsiWrite = ssi.writeUint32.bind(ssi);
  ssi.readUint32 = (offset) => {
    if (offset === 32) return 0;
    if (offset === 36) return spiRxQueue.length;
    if (offset === 40) return 6 | (spiRxQueue.length > 0 ? 8 : 0);
    if (offset === 96) return spiRxQueue.length > 0 ? spiRxQueue.shift() : 0;
    return origSsiRead(offset);
  };
  ssi.writeUint32 = (offset, value) => {
    if (offset === 8 && (value & 1) === 0) {
      spiRxQueue.length = 0;
      return;
    }
    if (offset === 96) {
      const b = value & 255;
      spiCmdBuf.push(b);
      const cmd = spiCmdBuf[0];
      if (cmd === 75 && spiCmdBuf.length >= 6 && spiCmdBuf.length <= 13) {
        spiRxQueue.push(uniqueId[spiCmdBuf.length - 6]);
      } else {
        spiRxQueue.push(0);
      }
      return;
    }
    origSsiWrite(offset, value);
  };
  const i2cTransactions = [];
  let alsCh0 = options.alsLux ?? 250;
  let alsReg = 0;
  const alsRegs = new Uint8Array(256);
  alsRegs[128] = 1;
  alsRegs[133] = 0;
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
      curTx = {
        bus: busIndex,
        addr,
        mode,
        writes: [],
        reads: []
      };
      const ack = busIndex === 0 ? addr === 41 : options.ackI2c1 ?? false;
      curTx.ack = ack;
      i2c.completeConnect(ack);
      if (!ack) {
        i2c.intRaw |= 16;
      }
    };
    i2c.onWriteByte = (byte) => {
      if (curTx) curTx.writes.push(byte);
      if (busIndex === 0 && curTx?.addr === 41) {
        if (curTx.writes.length === 1) {
          alsReg = byte;
        } else {
          alsRegs[alsReg] = byte;
        }
      }
      i2c.completeWrite(true);
    };
    i2c.onReadByte = () => {
      let val = 255;
      if (busIndex === 0 && curTx?.addr === 41) {
        if (alsReg === 136) val = alsCh0 & 255;
        else if (alsReg === 137) val = alsCh0 >> 8 & 255;
        else if (alsReg === 138) val = alsCh0 & 255;
        else if (alsReg === 139) val = alsCh0 >> 8 & 255;
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
  const MAX_SERIAL_HISTORY_CHARS = 8192;
  const MAX_SERIAL_LINES = 200;
  const rxQueue = [];
  const evenBlinkSerialQueue = [];
  let serialOut = "";
  let serialHistory = "";
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
      const c = chunk.charCodeAt(i) & 255;
      if (c === 10 || c === 13 || c >= 32 && c <= 126) {
        serialHistory += chunk[i];
      } else {
        serialHistory += "\\x" + c.toString(16).padStart(2, "0");
      }
    }
    if (serialHistory.length > MAX_SERIAL_HISTORY_CHARS) {
      serialHistory = serialHistory.slice(serialHistory.length - MAX_SERIAL_HISTORY_CHARS);
    }
    serialOut += chunk;
    while (true) {
      const nlIdx = serialOut.indexOf("\n");
      const badgeMatch = serialOut.match(/badge:(?:pong|[0-9A-Fa-f]{36}(?:(?!\r|\n|badge:|new clue!|invalid |incorrect |serial )[\s\S])*)|invalid clue length from kiosk|incorrect checksum|serial number doesn't match|new clue!/);
      if (badgeMatch && (nlIdx === -1 || badgeMatch.index <= nlIdx)) {
        const prefixRaw = serialOut.slice(0, badgeMatch.index).replace(/^\r+|\r+$/g, "");
        if (prefixRaw.length > 0) {
          pushSerialLine(escapeSerialBytes(prefixRaw));
        }
        pushSerialLine(escapeSerialBytes(badgeMatch[0]));
        serialOut = serialOut.slice(badgeMatch.index + badgeMatch[0].length);
        continue;
      }
      if (nlIdx !== -1) {
        const rawLine = serialOut.slice(0, nlIdx).replace(/\r$/, "");
        serialOut = serialOut.slice(nlIdx + 1).replace(/^\r+/, "");
        if (rawLine.length > 0) {
          pushSerialLine(escapeSerialBytes(rawLine));
        }
        continue;
      }
      break;
    }
  };
  let cdc = null;
  if (stdioMode === "usb") {
    cdc = new USBCDC(mcu.usbCtrl);
    cdc.onSerialData = (buf) => {
      let chunk = "";
      for (let i = 0; i < buf.length; i++) {
        chunk += String.fromCharCode(buf[i] & 255);
      }
      appendSerialChunk(chunk);
    };
  }
  const enqueueRawSerial = (str) => {
    if (stdioMode === "usb" && cdc) {
      for (let i = 0; i < str.length; i++) {
        cdc.sendSerialByte(str.charCodeAt(i) & 255);
      }
    } else {
      for (let i = 0; i < str.length; i++) {
        rxQueue.push(str.charCodeAt(i) & 255);
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
  const MIN_BUTTON_HOLD_NANOS = 25e6;
  const MIN_BUTTON_GAP_NANOS = 25e6;
  const buttonState = {
    15: {
      userHeld: false,
      low: false,
      pendingTap: false,
      minReleaseNanos: 0,
      nextPressAllowedNanos: 0,
      queuedTaps: 0
    },
    16: {
      userHeld: false,
      low: false,
      pendingTap: false,
      minReleaseNanos: 0,
      nextPressAllowedNanos: 0,
      queuedTaps: 0
    },
    17: {
      userHeld: false,
      low: false,
      pendingTap: false,
      minReleaseNanos: 0,
      nextPressAllowedNanos: 0,
      queuedTaps: 0
    }
  };
  let nextButtonEventNanos = Infinity;
  const updateNextButtonEventNanos = () => {
    let next = Infinity;
    for (const pin of [
      15,
      16,
      17
    ]) {
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
    for (const pin of [
      15,
      16,
      17
    ]) {
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
  core0.PC = 268435456;
  let totalCore0Cycles = 0;
  let totalCore1Cycles = 0;
  let inBootloader = false;
  let inSelfTest = false;
  let stdioHookInCalls = 0;
  let stdioHookOutCalls = 0;
  const stepLockstep = (simMicros) => {
    const targetNanos = mcu.clock.nanos + simMicros * 1e3;
    const useHook = stdioMode === "hook";
    while (mcu.clock.nanos < targetNanos) {
      if (mcu.clock.nanos >= nextButtonEventNanos) {
        serviceButtonTimers();
      }
      if (inBootloader) {
        mcu.clock.tick(targetNanos - mcu.clock.nanos);
        break;
      }
      const core1Halted = !!(psm.frceOff & 1 << 16);
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
        if (pc0 === 268438076) {
          inSelfTest = true;
        }
        if (pc0 === STDIO_HOOKS.BOOTLOADER_ENTRY) {
          inBootloader = true;
          continue;
        } else if (useHook && pc0 === STDIO_HOOKS.GETCHAR_TIMEOUT_US) {
          stdioHookInCalls++;
          if (evenBlinkSerialQueue.length > 0 && rxQueue.length === 0 && mcu.readUint32(536878416) === 0 && (mcu.readUint32(536878084) !== 0 || (mcu.readUint32(536878108) & 1) === 0)) {
            enqueueRawSerial(evenBlinkSerialQueue.shift());
          }
          core0.registers[0] = rxQueue.length > 0 ? rxQueue.shift() & 255 : 4294967295;
          core0.BXWritePC(core0.LR);
          c0 = 1;
        } else if (useHook && pc0 === STDIO_HOOKS.STDIO_USB_OUT_CHARS) {
          stdioHookOutCalls++;
          const bufAddr = core0.registers[0] >>> 0;
          const len = core0.registers[1] | 0;
          if (len > 0 && len <= 4096) {
            let chunk = "";
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
          if (op0 === 48960) {
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
        if (op1 === 48960) {
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
      rgb
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
    return {
      shares,
      rgb,
      windowNanos: totalWin,
      gpioSwitchCount
    };
  };
  const setButton = (buttonNameOrPin, pressed) => {
    const pin = typeof buttonNameOrPin === "number" ? buttonNameOrPin : BUTTON_PINS[buttonNameOrPin];
    if (pin === void 0 || !mcu.gpio[pin]) return;
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
      sector1cc: mcu.flash.slice(1884160, 1884160 + 4096),
      sector1cd: mcu.flash.slice(1888256, 1888256 + 4096)
    };
  };
  const getFirmwareState = () => {
    const pc0 = (core0.PC & ~1) >>> 0;
    return {
      simSeconds: mcu.clock.nanos / 1e9,
      millis: mcu.readUint32(536878804) >>> 0,
      activeCategory: mcu.readUint32(536878404) >>> 0,
      displayMode: mcu.readUint32(536878084) >>> 0,
      gameWon: mcu.readUint8(536878848),
      clueNum: mcu.readUint8(536876884),
      clueWordRaw: mcu.readUint32(536876884) >>> 0,
      suspectsCursor: mcu.readUint32(536873092) >>> 0,
      attacksCursor: mcu.readUint32(536873088) >>> 0,
      roomsCursor: mcu.readUint32(536878408) >>> 0,
      suspectsMask: mcu.readUint32(536878100) >>> 0,
      attacksMask: mcu.readUint32(536878092) >>> 0,
      roomsMask: mcu.readUint32(536878096) >>> 0,
      winSuspectsMask: mcu.readUint32(536878792) >>> 0,
      winAttacksMask: mcu.readUint32(536878784) >>> 0,
      winRoomsMask: mcu.readUint32(536878788) >>> 0,
      newClueBlinkCounter: mcu.readUint32(536878108) >>> 0,
      ledsEnabled: mcu.readUint8(536872840),
      inBootloader,
      inSelfTest: inSelfTest || pc0 >= 268438076 && pc0 <= 268438318,
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
      stdioHookOutCalls
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
      serialHistory = "";
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
      rawSpinLock: mcu.sio.spinLock >>> 0
    }),
    getFirmwareState
  };
}

var isWorkerScope = typeof WorkerGlobalScope !== "undefined" && self instanceof WorkerGlobalScope;
if (typeof self !== "undefined") {
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
    BUTTON_PINS
  };
}
if (isWorkerScope) {
  let emitFrame = function(simDeltaUs, wallDeltaMs) {
    if (!badge) return;
    const report = badge.consumeFrameReport();
    const state = badge.getFirmwareState();
    let dirtySectors = null;
    if (badge.isFlashDirty()) {
      dirtySectors = badge.consumeDirtyFlashSectors();
      if (dirtySectors) {
        currentSavedSectors = {
          sector1cc: new Uint8Array(dirtySectors.sector1cc),
          sector1cd: new Uint8Array(dirtySectors.sector1cd)
        };
      }
    }
    const instSpeed = wallDeltaMs > 0 && simDeltaUs > 0 ? simDeltaUs / (wallDeltaMs * 1e3) : 1;
    smoothedSpeed = smoothedSpeed * 0.8 + Math.min(1.2, instSpeed) * 0.2;
    self.postMessage({
      type: "frame",
      rgb: report.rgb,
      shares: report.shares,
      gpioSwitchCount: report.gpioSwitchCount,
      windowNanos: report.windowNanos,
      simDeltaUs,
      wallDeltaMs,
      speedRatio: Math.min(1, Math.max(0.01, Number(smoothedSpeed.toFixed(2)))),
      rawSpeedRatio: Number(instSpeed.toFixed(2)),
      state,
      dirtySectors
    });
  }, stepFrame = function() {
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
      const targetSimUs = Math.round(dtWallMs * 1e3);
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
      const effectiveWallMs = Math.max(dtWallMs, elapsedWallMs);
      emitFrame(simDoneUs, effectiveWallMs);
      if (running) {
        const nextDelay = Math.max(
          2,
          FRAME_INTERVAL_MS - Math.round(performance.now() - startWallMs)
        );
        timerId = setTimeout(stepFrame, nextDelay);
      }
    } catch (err) {
      stopLoop();
      self.postMessage({
        type: "error",
        message: err && err.message ? err.message : String(err)
      });
    }
  }, startLoop = function() {
    if (running) return;
    running = true;
    lastTickWallMs = performance.now();
    timerId = setTimeout(stepFrame, 0);
  }, stopLoop = function() {
    running = false;
    if (timerId !== null) {
      clearTimeout(timerId);
      timerId = null;
    }
  }, rebuildBadge = function(holdButtonsAtBoot = []) {
    if (!cachedUf2Bytes) return;
    badge = createBadge({
      uf2Bytes: cachedUf2Bytes,
      cycleNanos: cachedCycleNanos,
      savedSectors: currentSavedSectors,
      holdButtonsAtBoot,
      onSerialLine(line) {
        self.postMessage({ type: "serial_line", line });
      }
    });
    badge.setAlsLux(cachedAlsLux);
    smoothedSpeed = 1;
    if (holdButtonsAtBoot.includes("SELECT")) {
      badge.stepLockstep(25e4);
    }
  };
  let badge = null;
  let cachedUf2Bytes = null;
  let cachedCycleNanos = 125;
  let cachedAlsLux = 250;
  let currentSavedSectors = { sector1cc: null, sector1cd: null };
  let running = false;
  let timerId = null;
  let lastTickWallMs = 0;
  let smoothedSpeed = 1;
  const FRAME_INTERVAL_MS = 33;
  const MAX_BUDGET_MS = 28;
  const CHUNK_US = 2e3;
  self.onmessage = (ev) => {
    const msg = ev.data;
    if (!msg || typeof msg.type !== "string") return;
    try {
      switch (msg.type) {
        case "init": {
          stopLoop();
          cachedUf2Bytes = msg.uf2Bytes instanceof Uint8Array ? msg.uf2Bytes : new Uint8Array(msg.uf2Bytes);
          cachedCycleNanos = msg.cycleNanos ?? 125;
          if (msg.savedSectors) {
            currentSavedSectors = {
              sector1cc: msg.savedSectors.sector1cc ? new Uint8Array(msg.savedSectors.sector1cc) : null,
              sector1cd: msg.savedSectors.sector1cd ? new Uint8Array(msg.savedSectors.sector1cd) : null
            };
          } else {
            currentSavedSectors = { sector1cc: null, sector1cd: null };
          }
          rebuildBadge(msg.holdButtonsAtBoot ?? []);
          self.postMessage({
            type: "ready",
            ledTable: badge.ledTable,
            state: badge.getFirmwareState()
          });
          if (msg.autoStart !== false) {
            startLoop();
          }
          break;
        }
        case "start": {
          startLoop();
          self.postMessage({
            type: "started",
            state: badge ? badge.getFirmwareState() : null
          });
          break;
        }
        case "pause": {
          stopLoop();
          self.postMessage({
            type: "paused",
            state: badge ? badge.getFirmwareState() : null
          });
          break;
        }
        case "reset": {
          const wasRunning = running;
          stopLoop();
          if (msg.savedSectors !== void 0) {
            currentSavedSectors = msg.savedSectors ? {
              sector1cc: msg.savedSectors.sector1cc ? new Uint8Array(msg.savedSectors.sector1cc) : null,
              sector1cd: msg.savedSectors.sector1cd ? new Uint8Array(msg.savedSectors.sector1cd) : null
            } : { sector1cc: null, sector1cd: null };
          }
          const hold = Array.isArray(msg.holdButtonsAtBoot) ? msg.holdButtonsAtBoot : [];
          rebuildBadge(hold);
          self.postMessage({
            type: "reset_done",
            holdButtonsAtBoot: hold,
            state: badge ? badge.getFirmwareState() : null
          });
          emitFrame(0, FRAME_INTERVAL_MS);
          if (wasRunning || msg.autoStart) {
            startLoop();
          }
          break;
        }
        case "wipe_save": {
          const wasRunning = running;
          stopLoop();
          currentSavedSectors = { sector1cc: null, sector1cd: null };
          rebuildBadge([]);
          self.postMessage({
            type: "wipe_done",
            state: badge ? badge.getFirmwareState() : null
          });
          emitFrame(0, FRAME_INTERVAL_MS);
          if (wasRunning || msg.autoStart !== false) {
            startLoop();
          }
          break;
        }
        case "button": {
          if (badge && msg.name) {
            badge.setButton(msg.name, Boolean(msg.pressed));
          }
          break;
        }
        case "serial_in": {
          if (badge && typeof msg.text === "string") {
            badge.sendSerial(msg.text, { sendWhenBlinkEven: Boolean(msg.sendWhenBlinkEven) });
          }
          break;
        }
        case "als_lux": {
          const val = typeof msg.lux === "number" ? msg.lux : msg.ch0;
          if (typeof val === "number") {
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
        type: "error",
        message: err && err.message ? err.message : String(err)
      });
    }
  };
}
export {
  BUTTON_PINS,
  FLASH_LED_TABLES,
  STDIO_HOOKS,
  buildKioskMessage,
  createBadge,
  escapeSerialBytes,
  extractLedTableFromFlash,
  parseBadgeMessage,
  shareToBrightness,
  sharesToRgb
};
