#ifndef SC23_HTTPCLIENT_H
#define SC23_HTTPCLIENT_H

#include "Arduino.h"
#include "WiFi.h"

class HTTPClient {
public:
    bool begin(WiFiClient &client, const String &url) {
        (void)client;
        (void)url;
        return true;
    }
    void end() {}
};

#endif /* SC23_HTTPCLIENT_H */
