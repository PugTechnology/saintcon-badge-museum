#ifndef SC23_WIRE_H
#define SC23_WIRE_H

#include "Arduino.h"

class TwoWire {
public:
    TwoWire();

    void setPins(int sda, int scl);
    void setTimeOut(uint16_t timeoutMillis);
    bool begin();

    void beginTransmission(uint8_t address);
    size_t write(uint8_t data);
    uint8_t endTransmission(bool sendStop = true);

    uint8_t requestFrom(int address, int quantity);
    uint8_t requestFrom(uint8_t address, uint8_t quantity) {
        return requestFrom((int)address, (int)quantity);
    }

    int available() const;
    int read();

private:
    uint8_t tx_addr_;
    uint8_t tx_buf_[32];
    int tx_len_;
    uint8_t rx_buf_[32];
    int rx_len_;
    int rx_pos_;
};

extern TwoWire Wire;

#endif /* SC23_WIRE_H */
