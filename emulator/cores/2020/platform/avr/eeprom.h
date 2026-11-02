#ifndef SAINTCON_2020_AVR_EEPROM_H
#define SAINTCON_2020_AVR_EEPROM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t eeprom_read_byte(const uint8_t *p);
void eeprom_write_byte(uint8_t *p, uint8_t v);

#ifdef __cplusplus
}
#endif

#endif /* SAINTCON_2020_AVR_EEPROM_H */
