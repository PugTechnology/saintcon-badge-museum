#pragma once

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool stdio_usb_init(void);
bool stdio_usb_connected(void);

#ifdef __cplusplus
}
#endif
