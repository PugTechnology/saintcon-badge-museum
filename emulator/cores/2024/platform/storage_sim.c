#include "sdkconfig.h"
#include "platform_internal.h"

#include "nvs_flash.h"
#include "badge.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

/* ============================================================================
 * Browser localStorage Persistence Helpers (WASM; skipped in ?mock=1)
 * ============================================================================ */

#ifdef __EMSCRIPTEN__
EM_JS(int, js_storage_load, (const char *ls_key, char *out_buf, int max_len), {
    try {
        if (typeof window !== 'undefined' && window.location) {
            const params = new URLSearchParams(window.location.search);
            if (params.get('mock') === '1') return 0;
        }
        const key = UTF8ToString(ls_key);
        const val = window.localStorage.getItem(key);
        if (!val) return 0;
        const needed = lengthBytesUTF8(val) + 1;
        if (needed > max_len) return -1;
        stringToUTF8(val, out_buf, max_len);
        return needed - 1;
    } catch (e) {
        return 0;
    }
});

EM_JS(void, js_storage_save, (const char *ls_key, const char *val_str), {
    try {
        if (typeof window !== 'undefined' && window.location) {
            const params = new URLSearchParams(window.location.search);
            if (params.get('mock') === '1') return;
        }
        const key = UTF8ToString(ls_key);
        const val = UTF8ToString(val_str);
        window.localStorage.setItem(key, val);
    } catch (e) {
        if (typeof window.__onBadgeStorageError === 'function') {
            try { window.__onBadgeStorageError(); } catch (_) {}
        }
    }
});

static void bytes_to_hex(const uint8_t *src, size_t len, char *dst) {
    static const char k_hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        dst[i * 2 + 0] = k_hex[(src[i] >> 4) & 0x0F];
        dst[i * 2 + 1] = k_hex[src[i] & 0x0F];
    }
    dst[len * 2] = '\0';
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t hex_to_bytes(const char *hex, size_t hex_len, uint8_t *dst, size_t max_out) {
    size_t pairs = hex_len / 2;
    if (pairs > max_out) pairs = max_out;
    for (size_t i = 0; i < pairs; i++) {
        int hi = hex_nibble(hex[i * 2 + 0]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return i;
        dst[i] = (uint8_t)((hi << 4) | lo);
    }
    return pairs;
}
#endif

/* ============================================================================
 * In-Memory NVS Implementation (persisted under saintcon_2024_stock_nvs)
 * ============================================================================ */

#define NVS_MAX_ENTRIES 128
#define NVS_MAX_HANDLES 24
#define NVS_LS_KEY      "saintcon_2024_stock_nvs"

typedef enum {
    NVS_VAL_NONE = 0,
    NVS_VAL_NUM  = 1,
    NVS_VAL_STR  = 2,
    NVS_VAL_BLOB = 3,
} nvs_val_type_t;

typedef struct {
    bool used;
    char ns[16];
    char key[16];
    nvs_val_type_t type;
    uint64_t num_val;
    uint8_t *blob_data;
    size_t blob_len;
} nvs_entry_t;

typedef struct {
    bool open;
    char ns[16];
    nvs_open_mode_t mode;
} nvs_handle_slot_t;

static nvs_entry_t s_nvs_entries[NVS_MAX_ENTRIES];
static nvs_handle_slot_t s_nvs_handles[NVS_MAX_HANDLES];
static bool s_nvs_ready = false;

static void nvs_clear_memory(void) {
    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        if (s_nvs_entries[i].blob_data) {
            free(s_nvs_entries[i].blob_data);
        }
        memset(&s_nvs_entries[i], 0, sizeof(s_nvs_entries[i]));
    }
    memset(s_nvs_handles, 0, sizeof(s_nvs_handles));
}

static void nvs_persist_to_host(void) {
#ifdef __EMSCRIPTEN__
    /* Line format: ns|key|type|num_or_len|hexdata\n */
    size_t cap = 65536;
    char *buf = (char *)malloc(cap);
    if (!buf) return;
    size_t pos = 0;
    buf[0] = '\0';

    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        const nvs_entry_t *e = &s_nvs_entries[i];
        if (!e->used) continue;
        size_t hex_need = e->blob_len * 2 + 128;
        if (pos + hex_need >= cap) break;

        if (e->type == NVS_VAL_NUM) {
            int n = snprintf(buf + pos, cap - pos, "%s|%s|%d|%llu|\n",
                             e->ns, e->key, (int)e->type, (unsigned long long)e->num_val);
            if (n > 0) pos += (size_t)n;
        } else {
            int n = snprintf(buf + pos, cap - pos, "%s|%s|%d|%zu|",
                             e->ns, e->key, (int)e->type, e->blob_len);
            if (n > 0) pos += (size_t)n;
            if (e->blob_data && e->blob_len > 0) {
                bytes_to_hex(e->blob_data, e->blob_len, buf + pos);
                pos += e->blob_len * 2;
            }
            buf[pos++] = '\n';
            buf[pos] = '\0';
        }
    }
    js_storage_save(NVS_LS_KEY, buf);
    free(buf);
#endif
}

static bool nvs_load_from_host(void) {
#ifdef __EMSCRIPTEN__
    size_t cap = 65536;
    char *buf = (char *)malloc(cap);
    if (!buf) return false;
    int len = js_storage_load(NVS_LS_KEY, buf, (int)cap);
    if (len <= 0) {
        free(buf);
        return false;
    }

    int slot = 0;
    char *saveptr = NULL;
    char *line = strtok_r(buf, "\n", &saveptr);
    while (line && slot < NVS_MAX_ENTRIES) {
        char *p1 = strchr(line, '|');
        char *p2 = p1 ? strchr(p1 + 1, '|') : NULL;
        char *p3 = p2 ? strchr(p2 + 1, '|') : NULL;
        char *p4 = p3 ? strchr(p3 + 1, '|') : NULL;
        if (p1 && p2 && p3 && p4) {
            *p1 = '\0';
            *p2 = '\0';
            *p3 = '\0';
            *p4 = '\0';
            nvs_entry_t *e = &s_nvs_entries[slot++];
            memset(e, 0, sizeof(*e));
            e->used = true;
            snprintf(e->ns, sizeof(e->ns), "%s", line);
            snprintf(e->key, sizeof(e->key), "%s", p1 + 1);
            e->type = (nvs_val_type_t)atoi(p2 + 1);
            if (e->type == NVS_VAL_NUM) {
                e->num_val = strtoull(p3 + 1, NULL, 10);
            } else {
                size_t blen = (size_t)strtoull(p3 + 1, NULL, 10);
                const char *hex = p4 + 1;
                e->blob_data = (uint8_t *)calloc(1, blen + 1);
                if (e->blob_data) {
                    e->blob_len = hex_to_bytes(hex, strlen(hex), e->blob_data, blen);
                    if (e->type == NVS_VAL_STR) {
                        e->blob_data[e->blob_len] = '\0';
                    }
                }
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(buf);
    return slot > 0;
#else
    return false;
#endif
}

static nvs_entry_t *nvs_find_or_alloc(const char *ns, const char *key, bool create) {
    int free_idx = -1;
    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        if (s_nvs_entries[i].used) {
            if (strcmp(s_nvs_entries[i].ns, ns) == 0 && strcmp(s_nvs_entries[i].key, key) == 0) {
                return &s_nvs_entries[i];
            }
        } else if (free_idx < 0) {
            free_idx = i;
        }
    }
    if (!create || free_idx < 0) return NULL;
    nvs_entry_t *e = &s_nvs_entries[free_idx];
    memset(e, 0, sizeof(*e));
    e->used = true;
    snprintf(e->ns, sizeof(e->ns), "%s", ns);
    snprintf(e->key, sizeof(e->key), "%s", key);
    return e;
}

static void nvs_seed_fresh_attendee_defaults(void) {
    /* Pre-seed factory hardware test pass (`hw_pass = true`) while leaving
     * `registered = false` so first boot enters the attendee splash + onboarding flow. */
    badge_config_t cfg = BADGE_DEFAULTS;
    cfg.hw_pass = true;

    nvs_entry_t *ver = nvs_find_or_alloc("badge", "badge_version", true);
    if (ver) {
        ver->type = NVS_VAL_NUM;
        ver->num_val = BADGE_CONFIG_VERSION;
    }

    nvs_entry_t *blob = nvs_find_or_alloc("badge", "badge_config", true);
    if (blob) {
        blob->type = NVS_VAL_BLOB;
        blob->blob_data = (uint8_t *)malloc(sizeof(cfg));
        if (blob->blob_data) {
            memcpy(blob->blob_data, &cfg, sizeof(cfg));
            blob->blob_len = sizeof(cfg);
        }
    }
}

esp_err_t nvs_flash_init(void) {
    s_nvs_ready = true;
    return ESP_OK;
}

esp_err_t nvs_flash_erase(void) {
    nvs_clear_memory();
    nvs_seed_fresh_attendee_defaults();
    nvs_persist_to_host();
    return ESP_OK;
}

static nvs_handle_slot_t *get_handle_slot(nvs_handle_t h) {
    if (h == 0 || h > NVS_MAX_HANDLES) return NULL;
    nvs_handle_slot_t *s = &s_nvs_handles[h - 1];
    return s->open ? s : NULL;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) {
    if (!s_nvs_ready) return ESP_ERR_NVS_NOT_INITIALIZED;
    if (!namespace_name || !out_handle) return ESP_ERR_INVALID_ARG;

    if (open_mode == NVS_READONLY) {
        bool ns_exists = false;
        for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
            if (s_nvs_entries[i].used && strcmp(s_nvs_entries[i].ns, namespace_name) == 0) {
                ns_exists = true;
                break;
            }
        }
        if (!ns_exists) return ESP_ERR_NVS_NOT_FOUND;
    }

    for (int i = 0; i < NVS_MAX_HANDLES; i++) {
        if (!s_nvs_handles[i].open) {
            s_nvs_handles[i].open = true;
            s_nvs_handles[i].mode = open_mode;
            snprintf(s_nvs_handles[i].ns, sizeof(s_nvs_handles[i].ns), "%s", namespace_name);
            *out_handle = (nvs_handle_t)(i + 1);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

void nvs_close(nvs_handle_t handle) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s) return;
    if (s->mode == NVS_READWRITE) {
        nvs_persist_to_host();
    }
    memset(s, 0, sizeof(*s));
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    nvs_persist_to_host();
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key) return ESP_ERR_NVS_INVALID_HANDLE;
    if (s->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, false);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    free(e->blob_data);
    memset(e, 0, sizeof(*e));
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t handle) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s) return ESP_ERR_NVS_INVALID_HANDLE;
    if (s->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    for (int i = 0; i < NVS_MAX_ENTRIES; i++) {
        if (s_nvs_entries[i].used && strcmp(s_nvs_entries[i].ns, s->ns) == 0) {
            free(s_nvs_entries[i].blob_data);
            memset(&s_nvs_entries[i], 0, sizeof(s_nvs_entries[i]));
        }
    }
    return ESP_OK;
}

static esp_err_t nvs_set_num_common(nvs_handle_t handle, const char *key, uint64_t val) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key) return ESP_ERR_NVS_INVALID_HANDLE;
    if (s->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, true);
    if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    free(e->blob_data);
    e->blob_data = NULL;
    e->blob_len = 0;
    e->type = NVS_VAL_NUM;
    e->num_val = val;
    return ESP_OK;
}

static esp_err_t nvs_get_num_common(nvs_handle_t handle, const char *key, uint64_t *out) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key || !out) return ESP_ERR_NVS_INVALID_HANDLE;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, false);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->type != NVS_VAL_NUM) return ESP_ERR_NVS_TYPE_MISMATCH;
    *out = e->num_val;
    return ESP_OK;
}

esp_err_t nvs_set_i8(nvs_handle_t h, const char *k, int8_t v)   { return nvs_set_num_common(h, k, (uint64_t)(uint8_t)v); }
esp_err_t nvs_set_u8(nvs_handle_t h, const char *k, uint8_t v)  { return nvs_set_num_common(h, k, v); }
esp_err_t nvs_set_i16(nvs_handle_t h, const char *k, int16_t v) { return nvs_set_num_common(h, k, (uint64_t)(uint16_t)v); }
esp_err_t nvs_set_u16(nvs_handle_t h, const char *k, uint16_t v){ return nvs_set_num_common(h, k, v); }
esp_err_t nvs_set_i32(nvs_handle_t h, const char *k, int32_t v) { return nvs_set_num_common(h, k, (uint64_t)(uint32_t)v); }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *k, uint32_t v){ return nvs_set_num_common(h, k, v); }
esp_err_t nvs_set_i64(nvs_handle_t h, const char *k, int64_t v) { return nvs_set_num_common(h, k, (uint64_t)v); }
esp_err_t nvs_set_u64(nvs_handle_t h, const char *k, uint64_t v){ return nvs_set_num_common(h, k, v); }

esp_err_t nvs_get_i8(nvs_handle_t h, const char *k, int8_t *o)   { uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (int8_t)v; return r; }
esp_err_t nvs_get_u8(nvs_handle_t h, const char *k, uint8_t *o)  { uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (uint8_t)v; return r; }
esp_err_t nvs_get_i16(nvs_handle_t h, const char *k, int16_t *o) { uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (int16_t)v; return r; }
esp_err_t nvs_get_u16(nvs_handle_t h, const char *k, uint16_t *o){ uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (uint16_t)v; return r; }
esp_err_t nvs_get_i32(nvs_handle_t h, const char *k, int32_t *o) { uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (int32_t)v; return r; }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *k, uint32_t *o){ uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (uint32_t)v; return r; }
esp_err_t nvs_get_i64(nvs_handle_t h, const char *k, int64_t *o) { uint64_t v; esp_err_t r = nvs_get_num_common(h, k, &v); if (r == ESP_OK) *o = (int64_t)v; return r; }
esp_err_t nvs_get_u64(nvs_handle_t h, const char *k, uint64_t *o){ return nvs_get_num_common(h, k, o); }

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key || !value) return ESP_ERR_INVALID_ARG;
    if (s->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, true);
    if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    size_t slen = strlen(value);
    uint8_t *copy = (uint8_t *)malloc(slen + 1);
    if (!copy) return ESP_ERR_NO_MEM;
    memcpy(copy, value, slen + 1);
    free(e->blob_data);
    e->blob_data = copy;
    e->blob_len = slen;
    e->type = NVS_VAL_STR;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key || !length) return ESP_ERR_INVALID_ARG;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, false);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->type != NVS_VAL_STR) return ESP_ERR_NVS_TYPE_MISMATCH;
    size_t needed = e->blob_len + 1;
    if (!out_value) {
        *length = needed;
        return ESP_OK;
    }
    if (*length < needed) {
        *length = needed;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, e->blob_data, e->blob_len);
    out_value[e->blob_len] = '\0';
    *length = needed;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key || (!value && length > 0)) return ESP_ERR_INVALID_ARG;
    if (s->mode == NVS_READONLY) return ESP_ERR_NVS_READ_ONLY;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, true);
    if (!e) return ESP_ERR_NVS_NOT_ENOUGH_SPACE;
    uint8_t *copy = (uint8_t *)malloc(length > 0 ? length : 1);
    if (!copy) return ESP_ERR_NO_MEM;
    if (length > 0) memcpy(copy, value, length);
    free(e->blob_data);
    e->blob_data = copy;
    e->blob_len = length;
    e->type = NVS_VAL_BLOB;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length) {
    nvs_handle_slot_t *s = get_handle_slot(handle);
    if (!s || !key || !length) return ESP_ERR_INVALID_ARG;
    nvs_entry_t *e = nvs_find_or_alloc(s->ns, key, false);
    if (!e) return ESP_ERR_NVS_NOT_FOUND;
    if (e->type != NVS_VAL_BLOB) return ESP_ERR_NVS_TYPE_MISMATCH;
    if (!out_value) {
        *length = e->blob_len;
        return ESP_OK;
    }
    if (*length < e->blob_len) {
        *length = e->blob_len;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    if (e->blob_len > 0) memcpy(out_value, e->blob_data, e->blob_len);
    *length = e->blob_len;
    return ESP_OK;
}

void platform_storage_init(uint32_t seed) {
    (void)seed;
    nvs_clear_memory();
    s_nvs_ready = false;

    if (!nvs_load_from_host()) {
        nvs_seed_fresh_attendee_defaults();
    }
}
