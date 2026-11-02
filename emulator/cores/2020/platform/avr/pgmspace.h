#ifndef SAINTCON_2020_AVR_PGMSPACE_H
#define SAINTCON_2020_AVR_PGMSPACE_H

#include <stdint.h>

#define PROGMEM
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))

#endif /* SAINTCON_2020_AVR_PGMSPACE_H */
