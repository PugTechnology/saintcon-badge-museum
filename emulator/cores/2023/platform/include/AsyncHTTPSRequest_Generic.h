#ifndef SC23_ASYNC_HTTPS_REQUEST_GENERIC_H
#define SC23_ASYNC_HTTPS_REQUEST_GENERIC_H

#include "Arduino.h"

enum readyStates {
    readyStateUnsent    = 0,
    readyStateOpened    = 1,
    readyStateHdrsRecvd = 2,
    readyStateLoading   = 3,
    readyStateDone      = 4
};

class AsyncHTTPSRequest {
public:
    AsyncHTTPSRequest()
        : state_(readyStateUnsent),
          http_code_(0),
          complete_at_ms_(0) {}

    int readyState();
    bool open(const char *method, const char *url);
    void setReqHeader(const char *name, const char *value);
    bool send();
    bool send(const String &body);
    void abort();
    String responseLongText() const { return resp_body_; }
    int responseHTTPcode() const { return http_code_; }

private:
    int state_;
    String method_;
    String url_;
    String headers_;
    String resp_body_;
    int http_code_;
    uint32_t complete_at_ms_;
};

#endif /* SC23_ASYNC_HTTPS_REQUEST_GENERIC_H */
