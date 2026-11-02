#pragma once
/*
 * Emulator ADCTouch shim for SAINTCON 2017 HITB Minibadge (`HITB_Test.ino`).
 * Replaces the external ADCTouch library and returns believable capacitive
 * touch ADC readings for `ADCTouch.read(A3, ...)` and `ADCTouch.read(A2, ...)`
 * so the sketch's own calibration (`ref0`, `ref1`) and thresholds (`value0 > 40`,
 * `value1 > 10`) run unmodified.
 */

#include "Arduino.h"

extern "C" int emu_2017_adctouch_read(uint8_t analog_pin, int samples);

class ADCTouchClass {
 public:
  int read(byte analog_pin, int samples = 100) {
    return emu_2017_adctouch_read((uint8_t)analog_pin, samples);
  }
};

extern ADCTouchClass ADCTouch;
