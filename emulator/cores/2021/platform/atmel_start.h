#ifndef SC21_ATMEL_START_H
#define SC21_ATMEL_START_H

#include "driver_init.h"

void atmel_start_init(void);
void eeprom_save_state(void);
void nfc_trade_write_callback(uint8_t *enc);

#endif /* SC21_ATMEL_START_H */
