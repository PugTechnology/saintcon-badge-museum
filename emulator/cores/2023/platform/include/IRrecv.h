#ifndef SC23_IRRECV_H
#define SC23_IRRECV_H

#include "IRremoteESP8266.h"

class IRrecv {
public:
    explicit IRrecv(int pin) { (void)pin; }
    void enableIRIn();
    void disableIRIn();
    bool decode(decode_results *results);
    void resume() {}
};

#endif /* SC23_IRRECV_H */
