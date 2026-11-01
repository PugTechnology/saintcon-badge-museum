#pragma once

#include <sys/time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SNTP_SYNC_STATUS_RESET = 0,
    SNTP_SYNC_STATUS_COMPLETED,
    SNTP_SYNC_STATUS_IN_PROGRESS,
} sntp_sync_status_t;

typedef void (*sntp_sync_time_cb_t)(struct timeval *tv);

typedef struct {
    bool smooth_sync;
    bool server_from_dhcp;
    bool wait_for_sync;
    bool start;
    sntp_sync_time_cb_t sync_cb;
    bool renew_servers_after_new_IP;
    int ip_event_to_renew;
    size_t index_of_first_server;
    size_t num_of_servers;
    const char *servers[4];
} esp_sntp_config_t;

#define ESP_NETIF_SNTP_DEFAULT_CONFIG(server) { .start = true, .num_of_servers = 1, .servers = { (server) } }

static inline esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config) { (void)config; return ESP_OK; }
static inline esp_err_t esp_netif_sntp_start(void) { return ESP_OK; }
static inline void esp_netif_sntp_deinit(void) {}
static inline esp_err_t esp_netif_sntp_sync_wait(uint32_t tout) { (void)tout; return ESP_OK; }
static inline sntp_sync_status_t sntp_get_sync_status(void) { return SNTP_SYNC_STATUS_COMPLETED; }

#ifdef __cplusplus
}
#endif
