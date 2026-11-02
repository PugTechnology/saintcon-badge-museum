#pragma once
/*
 * Minimal Arduino shim header for SAINTCON 2017 HITB Minibadge (`HITB_Test.ino`).
 * Provides standard integer types, `byte`, ATtiny45 analog pin constants (`A2`, `A3`),
 * and declarations for `setup()` and `loop()`.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t byte;

enum {
  A0 = 0,
  A1 = 1,
  A2 = 2,
  A3 = 3
};

#ifdef __cplusplus
extern "C" {
#endif

void setup(void);
void loop(void);

#ifdef __cplusplus
}
#endif
