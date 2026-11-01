#ifndef SC23_WIFICLIENTSECURE_H
#define SC23_WIFICLIENTSECURE_H

#include "WiFi.h"

class WiFiClientSecure : public WiFiClient {
public:
    void setCACert(const char *rootCA) { (void)rootCA; }
    void setInsecure() {}
};

#endif /* SC23_WIFICLIENTSECURE_H */
