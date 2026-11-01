#define _DEFAULT_SOURCE

#include "platform_internal.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* Embedded TLS root cert symbols referenced by components/api/api_client.cpp */
const uint8_t emu_isrgrootx1_pem_start[] asm("_binary_isrgrootx1_pem_start") __attribute__((used)) =
    "-----BEGIN CERTIFICATE-----\n-----END CERTIFICATE-----\n";
const uint8_t emu_isrgrootx1_pem_end[] asm("_binary_isrgrootx1_pem_end") __attribute__((used)) = "";

/* ============================================================================
 * HTTP Client Handle State
 * ============================================================================ */

#define MAX_HTTP_HEADERS 32

typedef struct {
    char *key;
    char *value;
} http_kv_t;

struct esp_http_client {
    char *url;
    esp_http_client_method_t method;
    http_event_handle_cb event_handler;
    void *user_data;

    http_kv_t req_headers[MAX_HTTP_HEADERS];
    int req_header_count;

    const char *post_data;
    int post_len;

    int status_code;
    int64_t content_length;
};

static const char *method_to_str(esp_http_client_method_t m) {
    switch (m) {
        case HTTP_METHOD_GET:     return "GET";
        case HTTP_METHOD_POST:    return "POST";
        case HTTP_METHOD_PUT:     return "PUT";
        case HTTP_METHOD_PATCH:   return "PATCH";
        case HTTP_METHOD_DELETE:  return "DELETE";
        case HTTP_METHOD_HEAD:    return "HEAD";
        case HTTP_METHOD_OPTIONS: return "OPTIONS";
        default:                  return "GET";
    }
}

static char *extract_host_from_url(const char *url) {
    if (!url) return strdup("127.0.0.1");
    const char *p = strstr(url, "://");
    p = p ? (p + 3) : url;
    const char *slash = strchr(p, '/');
    size_t len = slash ? (size_t)(slash - p) : strlen(p);
    char *host = (char *)malloc(len + 1);
    if (!host) return NULL;
    memcpy(host, p, len);
    host[len] = '\0';
    return host;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    if (!config) return NULL;
    struct esp_http_client *c = (struct esp_http_client *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->url = strdup(config->url ? config->url : "");
    c->method = config->method;
    c->event_handler = config->event_handler;
    c->user_data = config->user_data;
    c->status_code = 0;
    c->content_length = 0;

    char *host = extract_host_from_url(c->url);
    if (host) {
        esp_http_client_set_header(c, "Host", host);
        free(host);
    }
    return c;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    if (!client) return ESP_OK;
    free(client->url);
    for (int i = 0; i < client->req_header_count; i++) {
        free(client->req_headers[i].key);
        free(client->req_headers[i].value);
    }
    free(client);
    return ESP_OK;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char *url) {
    if (!client || !url) return ESP_ERR_INVALID_ARG;
    free(client->url);
    client->url = strdup(url);
    return client->url ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method) {
    if (!client) return ESP_ERR_INVALID_ARG;
    client->method = method;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    if (!client || !key || !value) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < client->req_header_count; i++) {
        if (strcasecmp(client->req_headers[i].key, key) == 0) {
            free(client->req_headers[i].value);
            client->req_headers[i].value = strdup(value);
            return ESP_OK;
        }
    }
    if (client->req_header_count >= MAX_HTTP_HEADERS) return ESP_ERR_NO_MEM;
    client->req_headers[client->req_header_count].key = strdup(key);
    client->req_headers[client->req_header_count].value = strdup(value);
    client->req_header_count++;
    return ESP_OK;
}

esp_err_t esp_http_client_get_header(esp_http_client_handle_t client, const char *key, char **value) {
    if (!client || !key || !value) return ESP_ERR_INVALID_ARG;
    *value = NULL;
    for (int i = 0; i < client->req_header_count; i++) {
        if (strcasecmp(client->req_headers[i].key, key) == 0) {
            *value = client->req_headers[i].value;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t esp_http_client_delete_header(esp_http_client_handle_t client, const char *key) {
    if (!client || !key) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < client->req_header_count; i++) {
        if (strcasecmp(client->req_headers[i].key, key) == 0) {
            free(client->req_headers[i].key);
            free(client->req_headers[i].value);
            for (int j = i; j + 1 < client->req_header_count; j++) {
                client->req_headers[j] = client->req_headers[j + 1];
            }
            client->req_header_count--;
            return ESP_OK;
        }
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char *data, int len) {
    if (!client) return ESP_ERR_INVALID_ARG;
    client->post_data = data;
    client->post_len = len;
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    return client ? client->status_code : 0;
}

int64_t esp_http_client_get_content_length(esp_http_client_handle_t client) {
    return client ? client->content_length : 0;
}

bool esp_http_client_is_chunked_response(esp_http_client_handle_t client) {
    (void)client;
    return false;
}

esp_err_t esp_http_client_get_user_data(esp_http_client_handle_t client, void **data) {
    if (!client || !data) return ESP_ERR_INVALID_ARG;
    *data = client->user_data;
    return ESP_OK;
}

esp_err_t esp_http_client_set_user_data(esp_http_client_handle_t client, void *data) {
    if (!client) return ESP_ERR_INVALID_ARG;
    client->user_data = data;
    return ESP_OK;
}

/* ============================================================================
 * JS Bridge (WASM) vs In-Process Fake Server (Native CLI)
 * ============================================================================ */

#ifdef __EMSCRIPTEN__
/* Calls window.__sc24FakeServer.handleRequest(method, url, headersStr, bodyStr)
 * and writes status, response header lines ("Key: Val\n..."), and body string. */
EM_JS(int, js_fake_server_request, (const char *method_ptr,
                                    const char *url_ptr,
                                    const char *req_hdrs_ptr,
                                    const char *body_ptr,
                                    int body_len,
                                    char *out_hdrs_ptr,
                                    int max_hdrs_len,
                                    char *out_body_ptr,
                                    int max_body_len,
                                    int *out_actual_body_len), {
    try {
        const srv = (typeof window !== 'undefined' && window.__sc24FakeServer)
            ? window.__sc24FakeServer
            : (typeof globalThis !== 'undefined' ? globalThis.__sc24FakeServer : null);
        if (!srv || typeof srv.handleRequest !== 'function') {
            const fallback = '{"status":false,"detail":"Fake tower server unavailable","result":null}';
            stringToUTF8('Content-Type: application/json\nContent-Length: ' + fallback.length + '\n',
                         out_hdrs_ptr, max_hdrs_len);
            stringToUTF8(fallback, out_body_ptr, max_body_len);
            HEAP32[out_actual_body_len >> 2] = fallback.length;
            return 503;
        }
        const method = UTF8ToString(method_ptr);
        const url = UTF8ToString(url_ptr);
        const reqHdrs = UTF8ToString(req_hdrs_ptr);
        const body = (body_ptr && body_len > 0) ? UTF8ToString(body_ptr, body_len) : "";

        const res = srv.handleRequest(method, url, reqHdrs, body) || {};
        const status = Number.isInteger(res.status) ? res.status : 200;
        const bodyStr = (typeof res.body === 'string') ? res.body : "";
        const bodyBytes = lengthBytesUTF8(bodyStr);

        const hdrObj = Object.assign({
            'Content-Type': 'application/json',
            'Content-Length': String(bodyBytes)
        }, res.headers || {});
        let hdrLines = "";
        for (const [k, v] of Object.entries(hdrObj)) {
            hdrLines += k + ': ' + String(v) + '\n';
        }

        stringToUTF8(hdrLines, out_hdrs_ptr, max_hdrs_len);
        const writeLen = Math.min(bodyBytes, max_body_len - 1);
        stringToUTF8(bodyStr, out_body_ptr, max_body_len);
        HEAP32[out_actual_body_len >> 2] = writeLen;
        return status;
    } catch (err) {
        const msg = '{"status":false,"detail":"Fake tower server exception","result":null}';
        stringToUTF8('Content-Type: application/json\n', out_hdrs_ptr, max_hdrs_len);
        stringToUTF8(msg, out_body_ptr, max_body_len);
        HEAP32[out_actual_body_len >> 2] = msg.length;
        return 500;
    }
});
#endif

static void emit_event(esp_http_client_handle_t client,
                       esp_http_client_event_id_t id,
                       void *data,
                       int data_len,
                       char *hkey,
                       char *hval) {
    if (!client || !client->event_handler) return;
    esp_http_client_event_t evt = {
        .event_id = id,
        .client = client,
        .data = data,
        .data_len = data_len,
        .user_data = client->user_data,
        .header_key = hkey,
        .header_value = hval,
    };
    client->event_handler(&evt);
}

static void parse_and_emit_header_lines(esp_http_client_handle_t client, char *hdr_lines) {
    if (!hdr_lines) return;
    char *saveptr = NULL;
    char *line = strtok_r(hdr_lines, "\r\n", &saveptr);
    while (line) {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *key = line;
            char *val = colon + 1;
            while (*val == ' ' || *val == '\t') val++;
            if (strcasecmp(key, "Content-Length") == 0) {
                client->content_length = strtoll(val, NULL, 10);
            }
            emit_event(client, HTTP_EVENT_ON_HEADER, NULL, 0, key, val);
        }
        line = strtok_r(NULL, "\r\n", &saveptr);
    }
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    if (!client || !client->url) return ESP_ERR_INVALID_ARG;

    /* Small cooperative yield (15 ms) so spinner animations & UI ticks stay responsive */
    vTaskDelay(pdMS_TO_TICKS(15));

    emit_event(client, HTTP_EVENT_ON_CONNECTED, NULL, 0, NULL, NULL);
    emit_event(client, HTTP_EVENT_HEADERS_SENT, NULL, 0, NULL, NULL);

    const char *m_str = method_to_str(client->method);

#ifdef __EMSCRIPTEN__
    char req_hdrs[4096];
    size_t rpos = 0;
    req_hdrs[0] = '\0';
    for (int i = 0; i < client->req_header_count; i++) {
        int n = snprintf(req_hdrs + rpos, sizeof(req_hdrs) - rpos, "%s: %s\n",
                         client->req_headers[i].key, client->req_headers[i].value);
        if (n > 0 && rpos + (size_t)n < sizeof(req_hdrs)) rpos += (size_t)n;
    }

    size_t max_hdrs = 4096;
    size_t max_body = 131072;
    char *resp_hdrs = (char *)calloc(1, max_hdrs);
    char *resp_body = (char *)calloc(1, max_body);
    if (!resp_hdrs || !resp_body) {
        free(resp_hdrs);
        free(resp_body);
        return ESP_ERR_NO_MEM;
    }
    int actual_body_len = 0;
    int status = js_fake_server_request(
        m_str,
        client->url,
        req_hdrs,
        client->post_data ? client->post_data : "",
        client->post_data ? client->post_len : 0,
        resp_hdrs,
        (int)max_hdrs,
        resp_body,
        (int)max_body,
        &actual_body_len);

    client->status_code = status;
    client->content_length = actual_body_len;
    parse_and_emit_header_lines(client, resp_hdrs);

    if (client->method != HTTP_METHOD_HEAD && actual_body_len > 0) {
        emit_event(client, HTTP_EVENT_ON_DATA, resp_body, actual_body_len, NULL, NULL);
    }

    free(resp_hdrs);
    free(resp_body);
#else
    char *resp_hdrs = NULL;
    char *resp_body = NULL;
    size_t resp_body_len = 0;
    int status = cli_fake_server_handle(
        m_str,
        client->url,
        client->post_data ? client->post_data : "",
        client->post_data && client->post_len > 0 ? (size_t)client->post_len : 0,
        &resp_hdrs,
        &resp_body,
        &resp_body_len);

    client->status_code = status;
    client->content_length = (int64_t)resp_body_len;
    parse_and_emit_header_lines(client, resp_hdrs);

    if (client->method != HTTP_METHOD_HEAD && resp_body && resp_body_len > 0) {
        emit_event(client, HTTP_EVENT_ON_DATA, resp_body, (int)resp_body_len, NULL, NULL);
    }

    free(resp_hdrs);
    free(resp_body);
#endif

    emit_event(client, HTTP_EVENT_ON_FINISH, NULL, 0, NULL, NULL);
    emit_event(client, HTTP_EVENT_DISCONNECTED, NULL, 0, NULL, NULL);

    /* Schedule a badge sync (80 ms later, after caller finishes setting
     * badge_config.registered / updating UI) whenever a state-mutating endpoint
     * succeeds so badge_config (id, xp, level, coins), minibadges, and
     * tower_info[0..10] stay fresh. */
    if (client->status_code >= 200 && client->status_code < 300) {
        if (strstr(client->url, "/badge/register") ||
            strstr(client->url, "/battle/attack") ||
            strstr(client->url, "/vend/buy") ||
            strstr(client->url, "/badge/ir_code") ||
            strstr(client->url, "/badge/levelup") ||
            strstr(client->url, "/battle/savior") ||
            strstr(client->url, "/battle/self_save")) {
            platform_schedule_badge_sync(80000ULL);
        }
    }

    return ESP_OK;
}
