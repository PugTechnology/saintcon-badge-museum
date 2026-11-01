#ifndef SC23_IRSEND_H
#define SC23_IRSEND_H

#include "IRremoteESP8266.h"

class IRsend {
public:
    explicit IRsend(int pin) { (void)pin; }
    void begin() {}
    void sendNEC(uint64_t data, uint16_t nbits = 32);
};

#endif /* SC23_IRSEND_H */
