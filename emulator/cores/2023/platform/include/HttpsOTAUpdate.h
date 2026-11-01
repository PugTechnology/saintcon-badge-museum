#ifndef SC23_HTTPS_OTA_UPDATE_H
#define SC23_HTTPS_OTA_UPDATE_H

typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADER_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED
} HttpEventId_t;

typedef struct {
    HttpEventId_t event_id;
    const char *header_key;
    const char *header_value;
} HttpEvent_t;

typedef enum {
    HTTPS_OTA_IDLE = 0,
    HTTPS_OTA_UPDATING,
    HTTPS_OTA_SUCCESS,
    HTTPS_OTA_FAIL,
    HTTPS_OTA_ERR
} HttpsOTAStatus_t;

class HttpsOTAClass {
public:
    void onHttpEvent(void (*cb)(HttpEvent_t *)) { (void)cb; }
    void begin(const char *url, const char *cert_pem, bool skip_cert_common_name_check = true) {
        (void)url;
        (void)cert_pem;
        (void)skip_cert_common_name_check;
    }
    HttpsOTAStatus_t status() const { return HTTPS_OTA_FAIL; }
};

extern HttpsOTAClass HttpsOTA;

#endif /* SC23_HTTPS_OTA_UPDATE_H */
