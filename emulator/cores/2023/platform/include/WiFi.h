#ifndef SC23_WIFI_H
#define SC23_WIFI_H

#include "Arduino.h"

#define WL_IDLE_STATUS     0
#define WL_NO_SSID_AVAIL   1
#define WL_SCAN_COMPLETED  2
#define WL_CONNECTED       3
#define WL_CONNECT_FAILED  4
#define WL_CONNECTION_LOST 5
#define WL_DISCONNECTED    6

#define WIFI_OFF 0
#define WIFI_STA 1
#define WIFI_AP  2

class WiFiClient {
public:
    virtual ~WiFiClient() {}
};

class WiFiClass {
public:
    WiFiClass() : status_(WL_CONNECTED), auto_reconnect_(true) {}

    void mode(int m) { (void)m; }

    bool disconnect(bool wifioff = false) {
        status_ = wifioff ? WL_DISCONNECTED : WL_IDLE_STATUS;
        return true;
    }

    void setAutoReconnect(bool autoReconnect) {
        auto_reconnect_ = autoReconnect;
    }

    int begin(const char *ssid, const char *passphrase = nullptr) {
        (void)passphrase;
        if (ssid && ssid[0] != '\0') {
            ssid_ = ssid;
            status_ = WL_CONNECTED;
        } else {
            status_ = WL_CONNECT_FAILED;
        }
        return status_;
    }

    int status() const { return status_; }

    IPAddress localIP() const { return IPAddress(10, 23, 0, 42); }

    int scanNetworks() {
        return 2;
    }

    String SSID(int i) const {
        if (i == 0) return String("MUSEUM");
        if (i == 1) return String("SAINTCON-2023");
        return String();
    }

private:
    int status_;
    bool auto_reconnect_;
    String ssid_;
};

extern WiFiClass WiFi;

#endif /* SC23_WIFI_H */
