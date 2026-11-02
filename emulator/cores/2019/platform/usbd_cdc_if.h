#ifndef __USBD_CDC_IF_H
#define __USBD_CDC_IF_H
#include <stdint.h>
static inline uint8_t CDC_Transmit_FS(uint8_t *Buf, uint16_t Len) {
  (void)Buf;
  (void)Len;
  return 0;
}
#endif
