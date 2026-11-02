#ifndef SAINTCON_2020_AVR_IO_H
#define SAINTCON_2020_AVR_IO_H

#include <stdint.h>
#include "../emu_2020.h"

/* ATtiny84A register/bit definitions referenced in main.cpp */
#define _SFR_IO8(addr) (*(volatile uint8_t *)(uintptr_t)(addr))
#define PCIE1   4
#define PCINT10 2
#define WGM12   3
#define CS11    1
#define CS10    0
#define OCIE1A  1

#ifdef __cplusplus

/*
 * 16-bit AVR unsigned integer type (`sizeof == 2`) for `uint16_t` in main.cpp:
 * - Stores a true 16-bit `uint16_t v` (`0..65535`) that wraps modulo 65536 on increment/addition/subtraction.
 * - On AVR (`avr-g++`), `int` is 16-bit and `uint16_t` is `unsigned int` (16-bit), whereas on WASM/x86_64
 *   `int` is 32-bit so built-in `unsigned short` would promote to signed 32-bit `int` under C++ [conv.prom].
 * - Overloaded `+`, `-`, and comparisons keep 16-bit unsigned arithmetic (`millis - lastDisplayUpdate > ringDelay`
 *   and `millis > (lastDisplayUpdate + binaryDelay)` wrap in 16-bit unsigned as on AVR), while combining with
 *   `uint32_t` (`millis - lastDebounceTime` at main.cpp:484) promotes to `uint32_t` as on AVR.
 * - Reading a `volatile avr_uint16_t` (`millis` at main.cpp:36) calls `emu_2020_read_millis(&v)` to advance
 *   the 1 kHz Timer1 ISR and yield the firmware fiber when the frame tick budget is spent.
 */
struct avr_uint16_t {
  uint16_t v;

  constexpr avr_uint16_t() : v(0) {}
  constexpr avr_uint16_t(uint16_t x) : v(x) {}
  constexpr avr_uint16_t(int x) : v(static_cast<uint16_t>(x)) {}
  constexpr avr_uint16_t(unsigned int x) : v(static_cast<uint16_t>(x)) {}
  avr_uint16_t(const volatile avr_uint16_t &o) : v(o.read()) {}
  constexpr avr_uint16_t(const avr_uint16_t &o) = default;

  uint16_t read() const { return v; }
  uint16_t read() const volatile { return emu_2020_read_millis(&v); }

  operator uint16_t() const { return v; }
  operator uint16_t() const volatile { return read(); }

  avr_uint16_t &operator=(uint16_t x) { v = x; return *this; }
  avr_uint16_t &operator=(int x) { v = static_cast<uint16_t>(x); return *this; }
  avr_uint16_t &operator=(const avr_uint16_t &o) { v = o.v; return *this; }
  avr_uint16_t &operator=(const volatile avr_uint16_t &o) { v = o.read(); return *this; }

  void operator=(uint16_t x) volatile { v = x; }
  void operator=(int x) volatile { v = static_cast<uint16_t>(x); }
  void operator=(const avr_uint16_t &o) volatile { v = o.v; }
  void operator=(const volatile avr_uint16_t &o) volatile { v = o.read(); }

  void operator+=(int x) volatile {
    v = static_cast<uint16_t>(v + static_cast<uint16_t>(x));
  }
  void operator+=(int x) {
    v = static_cast<uint16_t>(v + static_cast<uint16_t>(x));
  }
};

static_assert(sizeof(avr_uint16_t) == 2, "avr_uint16_t must be 16 bits");

/* 16-bit unsigned addition and subtraction matching AVR 16-bit unsigned int promotion */
inline avr_uint16_t operator+(const avr_uint16_t &a, const avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.v + b.v));
}
inline avr_uint16_t operator+(const volatile avr_uint16_t &a, const avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.read() + b.v));
}
inline avr_uint16_t operator+(const avr_uint16_t &a, const volatile avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.v + b.read()));
}
inline avr_uint16_t operator+(const avr_uint16_t &a, int b) {
  return avr_uint16_t(static_cast<uint16_t>(a.v + static_cast<uint16_t>(b)));
}
inline avr_uint16_t operator+(const volatile avr_uint16_t &a, int b) {
  return avr_uint16_t(static_cast<uint16_t>(a.read() + static_cast<uint16_t>(b)));
}

inline avr_uint16_t operator-(const avr_uint16_t &a, const avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.v - b.v));
}
inline avr_uint16_t operator-(const volatile avr_uint16_t &a, const avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.read() - b.v));
}
inline avr_uint16_t operator-(const avr_uint16_t &a, const volatile avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.v - b.read()));
}
inline avr_uint16_t operator-(const volatile avr_uint16_t &a, const volatile avr_uint16_t &b) {
  return avr_uint16_t(static_cast<uint16_t>(a.read() - b.read()));
}

/* Promotion to uint32_t when combined with uint32_t (e.g. `millis - lastDebounceTime` at main.cpp:484) */
inline uint32_t operator-(const volatile avr_uint16_t &a, uint32_t b) {
  return static_cast<uint32_t>(a.read()) - b;
}
inline uint32_t operator-(const avr_uint16_t &a, uint32_t b) {
  return static_cast<uint32_t>(a.v) - b;
}

/* 16-bit unsigned relational operators matching AVR */
inline bool operator>(const avr_uint16_t &a, const avr_uint16_t &b) { return a.v > b.v; }
inline bool operator>(const volatile avr_uint16_t &a, const avr_uint16_t &b) { return a.read() > b.v; }
inline bool operator>(const avr_uint16_t &a, const volatile avr_uint16_t &b) { return a.v > b.read(); }
inline bool operator>(const avr_uint16_t &a, uint8_t b) { return a.v > static_cast<uint16_t>(b); }
inline bool operator>(const volatile avr_uint16_t &a, uint8_t b) { return a.read() > static_cast<uint16_t>(b); }
inline bool operator>(const avr_uint16_t &a, int b) { return a.v > static_cast<uint16_t>(b); }
inline bool operator>(const volatile avr_uint16_t &a, int b) { return a.read() > static_cast<uint16_t>(b); }

inline bool operator<(const avr_uint16_t &a, const avr_uint16_t &b) { return a.v < b.v; }
inline bool operator<(const volatile avr_uint16_t &a, const avr_uint16_t &b) { return a.read() < b.v; }
inline bool operator==(const avr_uint16_t &a, const avr_uint16_t &b) { return a.v == b.v; }
inline bool operator!=(const avr_uint16_t &a, const avr_uint16_t &b) { return a.v != b.v; }

struct AvrPortAReg {
  uint8_t v;
  operator uint8_t() const volatile { return v; }
  void operator=(uint8_t x) volatile {
    v = x;
    emu_2020_on_porta_write(x);
  }
  void operator|=(uint8_t x) volatile {
    uint8_t nx = (uint8_t)(v | x);
    v = nx;
    emu_2020_on_porta_write(nx);
  }
  void operator&=(uint8_t x) volatile {
    uint8_t nx = (uint8_t)(v & x);
    v = nx;
    emu_2020_on_porta_write(nx);
  }
};

struct AvrPortBReg {
  uint8_t v;
  operator uint8_t() const volatile { return v; }
  void operator=(uint8_t x) volatile {
    v = x;
    emu_2020_on_portb_write(x);
  }
  void operator|=(uint8_t x) volatile {
    uint8_t nx = (uint8_t)(v | x);
    v = nx;
    emu_2020_on_portb_write(nx);
  }
  void operator&=(uint8_t x) volatile {
    uint8_t nx = (uint8_t)(v & x);
    v = nx;
    emu_2020_on_portb_write(nx);
  }
};

struct AvrPinBReg {
  operator uint8_t() const volatile { return emu_2020_read_pinb(); }
};

extern volatile uint8_t GIMSK;
extern volatile uint8_t PCMSK1;
extern volatile uint8_t DDRA;
extern volatile uint8_t DDRB;
extern volatile AvrPortAReg PORTA;
extern volatile AvrPortBReg PORTB;
extern volatile AvrPinBReg PINB;
extern volatile uint8_t TCCR1A;
extern volatile uint8_t TCCR1B;
extern volatile uint16_t TCNT1;
extern volatile uint16_t OCR1A;
extern volatile uint8_t TIMSK1;

/*
 * In `main.cpp:201`, Ring mode (`displayState == 0`) executes
 * `LEDS[displayCounter - NUMLEDS] = 0;` when `displayCounter == 16`, writing
 * one byte past `volatile uint8_t LEDS[8]` (`LEDS[8] = 0`). On the real badge
 * that byte is unused RAM (per the review's look at the upstream hex).
 * In both the native CLI (`emu-cli`) and WebAssembly (`core.wasm`) builds, we
 * place `LEDS` and `millis` (`main.cpp:33-36`) in `.bss.leds_isolated` with
 * 64-byte alignment. In `core.wasm`, `millis` starts 64 bytes after `LEDS`. In
 * `emu-cli`, `millis` comes first and `LEDS` is last, before the next variable.
 * In both, `LEDS[8]` is unused padding. `build.sh` verifies the native symbol
 * table and the WASM link map (`core_wasm.map`), and fails if `LEDS + 8`
 * belongs to another variable.
 */
extern volatile uint8_t LEDS[8] __attribute__((section(".bss.leds_isolated"), aligned(64)));
extern volatile avr_uint16_t millis __attribute__((section(".bss.leds_isolated"), aligned(64)));

#ifdef SAINTCON_2020_FW_TU
#define uint16_t avr_uint16_t
#define main badge_2020_fw_main
#endif

#endif /* __cplusplus */

#endif /* SAINTCON_2020_AVR_IO_H */
