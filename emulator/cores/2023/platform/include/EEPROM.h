#ifndef SC23_EEPROM_H
#define SC23_EEPROM_H

#include "Arduino.h"

class EEPROMClass {
public:
    bool begin(size_t size);
    String readString(int address);
    size_t writeString(int address, const String &value);
    bool commit();
};

extern EEPROMClass EEPROM;

#endif /* SC23_EEPROM_H */
