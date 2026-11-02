#pragma once
/*
 * Emulator APA102 shim for SAINTCON 2017 HITB Minibadge (`HITB_Test.ino`).
 * Replaces the external APA102 library with a direct capture of the RGB array
 * and 5-bit global brightness value passed to `ledStrip.write(colors, ledCount, brightness)`.
 */

#include <stdint.h>

struct rgb_color {
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  constexpr rgb_color() : red(0), green(0), blue(0) {}
  constexpr rgb_color(uint8_t r, uint8_t g, uint8_t b) : red(r), green(g), blue(b) {}
};

extern "C" void emu_2017_on_apa102_write(uint8_t data_pin, uint8_t clock_pin,
                                         const rgb_color *colors,
                                         uint16_t count,
                                         uint8_t brightness);

template <uint8_t DataPin, uint8_t ClockPin>
class APA102 {
 public:
  void write(const rgb_color *colors, uint16_t count, uint8_t brightness = 31) {
    emu_2017_on_apa102_write(DataPin, ClockPin, colors, count, brightness);
  }
};
