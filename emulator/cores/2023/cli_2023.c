#define _POSIX_C_SOURCE 200809L

#include "platform/emu_2023.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zlib.h>

#define LCD_W 320
#define LCD_H 240
#define LED_COUNT 20

/* Geometry from emulator/web/badges/2023/layout.json */
#define BOARD_W_PX 1182
#define BOARD_H_PX 983

#define SCREEN_X_PX 316.14f
#define SCREEN_Y_PX 199.34f
#define SCREEN_W_PX 548.83f
#define SCREEN_H_PX 411.62f

#define BTN_BIT_UP     0x01u
#define BTN_BIT_DOWN   0x02u
#define BTN_BIT_LEFT   0x04u
#define BTN_BIT_RIGHT  0x08u
#define BTN_BIT_CENTER 0x10u
#define BTN_BIT_B      0x20u
#define BTN_BIT_A      0x40u

typedef struct {
    float x_px;
    float y_px;
} led_pos_t;

/* D1..D20 coordinates in chain order from emulator/web/badges/2023/layout.json */
static const led_pos_t kLedPos[LED_COUNT] = {
    { 227.53f, 234.59f }, /* D1  (WIFI_LED) */
    { 201.80f, 747.97f }, /* D2  (MB1) */
    { 179.89f, 895.85f }, /* D3 */
    { 293.27f, 895.85f }, /* D4 */
    { 446.36f, 747.97f }, /* D5  (MB2) */
    { 690.92f, 747.97f }, /* D6  (MB3) */
    { 839.24f, 896.80f }, /* D7 */
    { 956.44f, 897.75f }, /* D8 */
    { 935.48f, 747.97f }, /* D9  (MB4) */
    { 943.10f, 417.53f }, /* D10 */
    { 907.84f, 160.27f }, /* D11 (Ring 0) */
    { 941.19f,  79.28f }, /* D12 (Ring 1) */
    {1023.14f,  45.93f }, /* D13 (Ring 2) */
    {1104.13f,  79.28f }, /* D14 (Ring 3) */
    {1136.52f, 158.36f }, /* D15 (Ring 4) */
    {1103.17f, 241.26f }, /* D16 (Ring 5) */
    {1022.18f, 274.61f }, /* D17 (Ring 6) */
    { 941.19f, 241.26f }, /* D18 (Ring 7) */
    { 736.34f, 154.55f }, /* D19 (IR_LED) */
    { 595.32f, 154.55f }, /* D20 */
};

/* EntryScreen.cpp 13x5 keypad layout */
static const char kEntryKeypad[5][14] = {
    "0123456789@._",
    "ABCDEFGHIJKLM",
    "NOPQRSTUVWXYZ",
    "abcdefghijklm",
    "nopqrstuvwxyz"
};

static void put_be32(uint8_t out[4], uint32_t v) {
    out[0] = (uint8_t)((v >> 24) & 0xFFu);
    out[1] = (uint8_t)((v >> 16) & 0xFFu);
    out[2] = (uint8_t)((v >> 8) & 0xFFu);
    out[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t get_be32(const uint8_t in[4]) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static bool write_png_chunk(FILE *fp, const char type[4], const uint8_t *data, uint32_t len) {
    uint8_t len_be[4];
    uint8_t crc_be[4];
    put_be32(len_be, len);
    if (fwrite(len_be, 1, 4, fp) != 4 || fwrite(type, 1, 4, fp) != 4) {
        return false;
    }
    uLong crc = crc32(0L, Z_NULL, 0);
    crc       = crc32(crc, (const Bytef *)type, 4);
    if (len > 0 && data) {
        if (fwrite(data, 1, len, fp) != len) {
            return false;
        }
        crc = crc32(crc, (const Bytef *)data, len);
    }
    put_be32(crc_be, (uint32_t)crc);
    return fwrite(crc_be, 1, 4, fp) == 4;
}

static bool write_png_rgba(const char *path, int w, int h, const uint8_t *rgba) {
    if (!path || !rgba || w <= 0 || h <= 0) {
        return false;
    }
    size_t row_bytes = (size_t)w * 4u;
    size_t raw_len   = (1u + row_bytes) * (size_t)h;
    uint8_t *raw     = (uint8_t *)malloc(raw_len);
    if (!raw) {
        return false;
    }
    for (int y = 0; y < h; y++) {
        uint8_t *dst       = &raw[(size_t)y * (1u + row_bytes)];
        dst[0]             = 0;
        const uint8_t *src = &rgba[(size_t)y * row_bytes];
        memcpy(&dst[1], src, row_bytes);
    }

    uLongf zcap   = compressBound((uLong)raw_len);
    uint8_t *zbuf = (uint8_t *)malloc((size_t)zcap);
    if (!zbuf) {
        free(raw);
        return false;
    }
    if (compress2((Bytef *)zbuf, &zcap, (const Bytef *)raw, (uLong)raw_len, 6) != Z_OK) {
        free(zbuf);
        free(raw);
        return false;
    }
    free(raw);

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        free(zbuf);
        return false;
    }

    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    uint8_t ihdr[13];
    put_be32(&ihdr[0], (uint32_t)w);
    put_be32(&ihdr[4], (uint32_t)h);
    ihdr[8]  = 8;
    ihdr[9]  = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;

    bool ok = (fwrite(sig, 1, 8, fp) == 8) &&
              write_png_chunk(fp, "IHDR", ihdr, sizeof(ihdr)) &&
              write_png_chunk(fp, "IDAT", zbuf, (uint32_t)zcap) &&
              write_png_chunk(fp, "IEND", NULL, 0);

    fclose(fp);
    free(zbuf);
    return ok;
}

static uint8_t paeth_predictor(int a, int b, int c) {
    int p  = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) return (uint8_t)a;
    if (pb <= pc) return (uint8_t)b;
    return (uint8_t)c;
}

static uint8_t *read_png_rgba(const char *path, int *out_w, int *out_h) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long fsz = ftell(fp);
    if (fsz < 33 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    uint8_t *file_buf = (uint8_t *)malloc((size_t)fsz);
    if (!file_buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(file_buf, 1, (size_t)fsz, fp) != (size_t)fsz) {
        free(file_buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (memcmp(file_buf, sig, 8) != 0) {
        free(file_buf);
        return NULL;
    }

    int w = 0, h = 0, bpp = 0;
    uint8_t *idat_buf = NULL;
    size_t idat_len   = 0;
    size_t idat_cap   = 0;
    size_t pos        = 8;

    while (pos + 12 <= (size_t)fsz) {
        uint32_t clen     = get_be32(&file_buf[pos]);
        const uint8_t *ct = &file_buf[pos + 4];
        pos += 8;
        if (pos + (size_t)clen + 4 > (size_t)fsz) break;
        const uint8_t *cdata = &file_buf[pos];
        if (memcmp(ct, "IHDR", 4) == 0 && clen == 13) {
            w = (int)get_be32(&cdata[0]);
            h = (int)get_be32(&cdata[4]);
            uint8_t depth = cdata[8];
            uint8_t ctype = cdata[9];
            uint8_t inter = cdata[12];
            if (depth != 8 || inter != 0 || (ctype != 6 && ctype != 2)) {
                free(idat_buf);
                free(file_buf);
                return NULL;
            }
            bpp = (ctype == 6) ? 4 : 3;
        } else if (memcmp(ct, "IDAT", 4) == 0 && clen > 0) {
            if (idat_len + clen > idat_cap) {
                size_t new_cap = (idat_cap == 0) ? (size_t)fsz : idat_cap * 2;
                if (new_cap < idat_len + clen) new_cap = idat_len + clen;
                uint8_t *nb = (uint8_t *)realloc(idat_buf, new_cap);
                if (!nb) {
                    free(idat_buf);
                    free(file_buf);
                    return NULL;
                }
                idat_buf = nb;
                idat_cap = new_cap;
            }
            memcpy(&idat_buf[idat_len], cdata, clen);
            idat_len += clen;
        } else if (memcmp(ct, "IEND", 4) == 0) {
            break;
        }
        pos += (size_t)clen + 4;
    }
    free(file_buf);

    if (w <= 0 || h <= 0 || bpp == 0 || !idat_buf || idat_len == 0) {
        free(idat_buf);
        return NULL;
    }

    size_t stride             = (size_t)w * (size_t)bpp;
    uLongf raw_len            = (uLongf)((1u + stride) * (size_t)h);
    const uLongf expected_len = raw_len;
    uint8_t *raw              = (uint8_t *)malloc((size_t)raw_len);
    if (!raw) {
        free(idat_buf);
        return NULL;
    }
    if (uncompress((Bytef *)raw, &raw_len, (const Bytef *)idat_buf, (uLong)idat_len) != Z_OK ||
        raw_len != expected_len) {
        free(raw);
        free(idat_buf);
        return NULL;
    }
    free(idat_buf);

    uint8_t *rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4u);
    uint8_t *prev = (uint8_t *)calloc(stride, 1);
    uint8_t *curr = (uint8_t *)malloc(stride);
    if (!rgba || !prev || !curr) {
        free(rgba);
        free(prev);
        free(curr);
        free(raw);
        return NULL;
    }

    for (int y = 0; y < h; y++) {
        const uint8_t *src = &raw[(size_t)y * (1u + stride)];
        uint8_t ftype      = src[0];
        const uint8_t *row = &src[1];
        for (size_t i = 0; i < stride; i++) {
            int a = (i >= (size_t)bpp) ? curr[i - (size_t)bpp] : 0;
            int b = prev[i];
            int c = (i >= (size_t)bpp) ? prev[i - (size_t)bpp] : 0;
            switch (ftype) {
                case 0: curr[i] = row[i]; break;
                case 1: curr[i] = (uint8_t)(row[i] + a); break;
                case 2: curr[i] = (uint8_t)(row[i] + b); break;
                case 3: curr[i] = (uint8_t)(row[i] + ((a + b) >> 1)); break;
                case 4: curr[i] = (uint8_t)(row[i] + paeth_predictor(a, b, c)); break;
                default:
                    free(rgba);
                    free(prev);
                    free(curr);
                    free(raw);
                    return NULL;
            }
        }
        if (bpp == 4) {
            memcpy(&rgba[(size_t)y * (size_t)w * 4u], curr, stride);
        } else {
            for (int x = 0; x < w; x++) {
                uint8_t *d = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4u];
                d[0]       = curr[x * 3 + 0];
                d[1]       = curr[x * 3 + 1];
                d[2]       = curr[x * 3 + 2];
                d[3]       = 255;
            }
        }
        memcpy(prev, curr, stride);
    }

    free(prev);
    free(curr);
    free(raw);
    *out_w = w;
    *out_h = h;
    return rgba;
}

static inline void rgb565_to_rgb888(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r5 = (uint8_t)((v >> 11) & 0x1Fu);
    uint8_t g6 = (uint8_t)((v >> 5) & 0x3Fu);
    uint8_t b5 = (uint8_t)(v & 0x1Fu);
    *r         = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g         = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b         = (uint8_t)((b5 << 3) | (b5 >> 2));
}

static void render_screen_rgba(uint8_t *out_rgba) {
    const uint16_t *fb = emu_framebuffer();
    uint32_t bl        = emu_backlight();
    for (int i = 0; i < LCD_W * LCD_H; i++) {
        uint8_t r = 0, g = 0, b = 0;
        rgb565_to_rgb888(fb[i], &r, &g, &b);
        out_rgba[i * 4 + 0] = (uint8_t)((5u * (255u - bl) + (uint32_t)r * bl + 127u) / 255u);
        out_rgba[i * 4 + 1] = (uint8_t)((7u * (255u - bl) + (uint32_t)g * bl + 127u) / 255u);
        out_rgba[i * 4 + 2] = (uint8_t)((10u * (255u - bl) + (uint32_t)b * bl + 127u) / 255u);
        out_rgba[i * 4 + 3] = 255;
    }
}

static void blend_pixel_rgba(uint8_t *rgba, int w, int h, int px, int py,
                             uint8_t sr, uint8_t sg, uint8_t sb, float alpha) {
    if (px < 0 || px >= w || py < 0 || py >= h || alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    uint8_t *dst = &rgba[((size_t)py * (size_t)w + (size_t)px) * 4u];
    float inv    = 1.0f - alpha;
    dst[0]       = (uint8_t)lroundf((float)dst[0] * inv + (float)sr * alpha);
    dst[1]       = (uint8_t)lroundf((float)dst[1] * inv + (float)sg * alpha);
    dst[2]       = (uint8_t)lroundf((float)dst[2] * inv + (float)sb * alpha);
    dst[3]       = 255;
}

static void render_badge_rgba(uint8_t *out_rgba, const uint8_t *board_rgba) {
    if (board_rgba) {
        memcpy(out_rgba, board_rgba, (size_t)BOARD_W_PX * (size_t)BOARD_H_PX * 4u);
    } else {
        for (size_t i = 0; i < (size_t)BOARD_W_PX * (size_t)BOARD_H_PX; i++) {
            out_rgba[i * 4 + 0] = 22;
            out_rgba[i * 4 + 1] = 26;
            out_rgba[i * 4 + 2] = 32;
            out_rgba[i * 4 + 3] = 255;
        }
    }

    uint8_t screen_rgba[LCD_W * LCD_H * 4];
    render_screen_rgba(screen_rgba);

    int sx0 = (int)lroundf(SCREEN_X_PX);
    int sy0 = (int)lroundf(SCREEN_Y_PX);
    int sw  = (int)lroundf(SCREEN_W_PX);
    int sh  = (int)lroundf(SCREEN_H_PX);
    for (int dy = 0; dy < sh; dy++) {
        int py = sy0 + dy;
        if (py < 0 || py >= BOARD_H_PX) continue;
        int src_y = (dy * LCD_H) / sh;
        for (int dx = 0; dx < sw; dx++) {
            int px = sx0 + dx;
            if (px < 0 || px >= BOARD_W_PX) continue;
            int src_x          = (dx * LCD_W) / sw;
            const uint8_t *src = &screen_rgba[(src_y * LCD_W + src_x) * 4];
            uint8_t *dst       = &out_rgba[((size_t)py * BOARD_W_PX + (size_t)px) * 4u];
            dst[0]             = src[0];
            dst[1]             = src[1];
            dst[2]             = src[2];
            dst[3]             = 255;
        }
    }

    /* Composite 20 SK6812MINI LEDs (D1..D20) */
    const uint8_t *leds = emu_leds();
    for (int i = 0; i < LED_COUNT; i++) {
        uint8_t lr = leds[i * 3 + 0];
        uint8_t lg = leds[i * 3 + 1];
        uint8_t lb = leds[i * 3 + 2];
        if (lr == 0 && lg == 0 && lb == 0) continue;

        float cx    = kLedPos[i].x_px;
        float cy    = kLedPos[i].y_px;
        int rad     = 26;
        int x_start = (int)floorf(cx) - rad;
        int x_end   = (int)ceilf(cx) + rad;
        int y_start = (int)floorf(cy) - rad;
        int y_end   = (int)ceilf(cy) + rad;

        for (int py = y_start; py <= y_end; py++) {
            for (int px = x_start; px <= x_end; px++) {
                float dist = hypotf((float)px - cx, (float)py - cy);
                if (dist > (float)rad) continue;
                if (dist <= 6.5f) {
                    uint8_t cr = (uint8_t)(lr + (255 - lr) * 0.45f);
                    uint8_t cg = (uint8_t)(lg + (255 - lg) * 0.45f);
                    uint8_t cb = (uint8_t)(lb + (255 - lb) * 0.45f);
                    blend_pixel_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, px, py, cr, cg, cb, 0.95f);
                } else {
                    float t     = 1.0f - (dist - 6.5f) / ((float)rad - 6.5f);
                    float alpha = t * t * 0.72f;
                    blend_pixel_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, px, py, lr, lg, lb, alpha);
                }
            }
        }
    }
}

static bool mkdir_p(const char *dir) {
    if (!dir || !*dir) return false;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
    return true;
}

static uint8_t *try_load_board_png(const char *argv0, const char *explicit_path) {
    int w = 0, h = 0;
    if (explicit_path) {
        uint8_t *img = read_png_rgba(explicit_path, &w, &h);
        if (img && w == BOARD_W_PX && h == BOARD_H_PX) return img;
        free(img);
    }
    const char *candidates[] = {
        "emulator/web/badges/2023/board.png",
        "web/badges/2023/board.png",
        "../../web/badges/2023/board.png",
        "../web/badges/2023/board.png",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        uint8_t *img = read_png_rgba(candidates[i], &w, &h);
        if (img && w == BOARD_W_PX && h == BOARD_H_PX) return img;
        free(img);
    }
    if (argv0) {
        const char *slash = strrchr(argv0, '/');
        if (slash) {
            char rel[1024];
            size_t dlen = (size_t)(slash - argv0);
            if (dlen < sizeof(rel) - 48) {
                memcpy(rel, argv0, dlen);
                snprintf(rel + dlen, sizeof(rel) - dlen, "/../../web/badges/2023/board.png");
                uint8_t *img = read_png_rgba(rel, &w, &h);
                if (img && w == BOARD_W_PX && h == BOARD_H_PX) return img;
                free(img);
            }
        }
    }
    return NULL;
}

static bool save_shot_png(const char *out_dir,
                          const char *raw_name,
                          bool badge_view,
                          const char *argv0,
                          const char *board_path,
                          uint8_t **cached_board,
                          bool *board_tried) {
    if (!raw_name || !*raw_name) return false;
    char clean_name[256];
    snprintf(clean_name, sizeof(clean_name), "%s", raw_name);
    size_t nlen = strlen(clean_name);
    if (nlen > 4 && strcasecmp(&clean_name[nlen - 4], ".png") == 0) {
        clean_name[nlen - 4] = '\0';
    }

    char full_path[1024];
    if (out_dir && *out_dir) {
        size_t dlen = strlen(out_dir);
        if (dlen > 0 && out_dir[dlen - 1] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s.png", out_dir, clean_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s.png", out_dir, clean_name);
        }
    } else {
        snprintf(full_path, sizeof(full_path), "%s.png", clean_name);
    }

    if (!badge_view) {
        uint8_t rgba[LCD_W * LCD_H * 4];
        render_screen_rgba(rgba);
        return write_png_rgba(full_path, LCD_W, LCD_H, rgba);
    }

    if (!*board_tried) {
        *cached_board = try_load_board_png(argv0, board_path);
        *board_tried  = true;
    }
    uint8_t *badge_rgba = (uint8_t *)malloc((size_t)BOARD_W_PX * (size_t)BOARD_H_PX * 4u);
    if (!badge_rgba) return false;
    render_badge_rgba(badge_rgba, *cached_board);
    bool ok = write_png_rgba(full_path, BOARD_W_PX, BOARD_H_PX, badge_rgba);
    free(badge_rgba);
    return ok;
}

static void step_ms_with_buttons(uint32_t total_ms, uint8_t buttons) {
    while (total_ms > 0) {
        uint32_t chunk = (total_ms > 20u) ? 20u : total_ms;
        emu_step(chunk, buttons);
        total_ms -= chunk;
    }
}

static void press_button_mask(uint8_t mask, uint32_t hold_ms, uint32_t gap_ms) {
    if (hold_ms < 20u) hold_ms = 20u;
    if (gap_ms < 20u) gap_ms = 20u;
    step_ms_with_buttons(hold_ms, mask);
    step_ms_with_buttons(gap_ms, 0);
}

static uint8_t parse_button_token(const char *tok) {
    if (!tok || !*tok) return 0;
    if (strcasecmp(tok, "up") == 0 || strcasecmp(tok, "u") == 0) return BTN_BIT_UP;
    if (strcasecmp(tok, "down") == 0 || strcasecmp(tok, "d") == 0) return BTN_BIT_DOWN;
    if (strcasecmp(tok, "left") == 0 || strcasecmp(tok, "l") == 0) return BTN_BIT_LEFT;
    if (strcasecmp(tok, "right") == 0 || strcasecmp(tok, "r") == 0) return BTN_BIT_RIGHT;
    if (strcasecmp(tok, "center") == 0 || strcasecmp(tok, "mid") == 0 || strcasecmp(tok, "joy") == 0) return BTN_BIT_CENTER;
    if (strcasecmp(tok, "b") == 0) return BTN_BIT_B;
    if (strcasecmp(tok, "a") == 0) return BTN_BIT_A;
    return 0;
}

static bool find_in_entry_keypad(char ch, int *out_x, int *out_y) {
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 13; x++) {
            if (kEntryKeypad[y][x] == ch) {
                *out_x = x;
                *out_y = y;
                return true;
            }
        }
    }
    return false;
}

static bool type_on_entry_screen(const char *text, bool press_done) {
    int cur_x = 0;
    int cur_y = 0;

    if (text) {
        for (const char *p = text; *p; p++) {
            int tx = 0, ty = 0;
            if (!find_in_entry_keypad(*p, &tx, &ty)) {
                fprintf(stderr, "ERROR: Character '%c' not on EntryScreen 13x5 keypad\n", *p);
                return false;
            }

            /* Shortest horizontal move modulo 13 */
            int dx_right = (tx - cur_x + 13) % 13;
            int dx_left  = (cur_x - tx + 13) % 13;
            if (dx_right <= dx_left) {
                for (int i = 0; i < dx_right; i++) press_button_mask(BTN_BIT_RIGHT, 30u, 30u);
            } else {
                for (int i = 0; i < dx_left; i++) press_button_mask(BTN_BIT_LEFT, 30u, 30u);
            }
            cur_x = tx;

            /* Shortest vertical move modulo 6 */
            int dy_down = (ty - cur_y + 6) % 6;
            int dy_up   = (cur_y - ty + 6) % 6;
            if (dy_down <= dy_up) {
                for (int i = 0; i < dy_down; i++) press_button_mask(BTN_BIT_DOWN, 30u, 30u);
            } else {
                for (int i = 0; i < dy_up; i++) press_button_mask(BTN_BIT_UP, 30u, 30u);
            }
            cur_y = ty;

            press_button_mask(BTN_BIT_A, 35u, 40u);
        }
    }

    if (press_done) {
        int dy_down = (5 - cur_y + 6) % 6;
        int dy_up   = (cur_y - 5 + 6) % 6;
        if (dy_down <= dy_up) {
            for (int i = 0; i < dy_down; i++) press_button_mask(BTN_BIT_DOWN, 30u, 30u);
        } else {
            for (int i = 0; i < dy_up; i++) press_button_mask(BTN_BIT_UP, 30u, 30u);
        }
        press_button_mask(BTN_BIT_A, 40u, 60u);
    }
    return true;
}

static char *trim_ws(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

static bool run_script_file(const char *script_path,
                            const char *out_dir,
                            bool default_badge_view,
                            const char *argv0,
                            const char *board_path,
                            bool verbose) {
    FILE *fp = fopen(script_path, "r");
    if (!fp) {
        fprintf(stderr, "ERROR: Failed to open script '%s': %s\n", script_path, strerror(errno));
        return false;
    }

    if (out_dir && *out_dir && !mkdir_p(out_dir)) {
        fprintf(stderr, "ERROR: Failed to create output directory '%s'\n", out_dir);
        fclose(fp);
        return false;
    }

    uint8_t *cached_board = NULL;
    bool board_tried      = false;
    char line_buf[1024];
    int line_no = 0;
    bool ok     = true;

    while (fgets(line_buf, sizeof(line_buf), fp)) {
        line_no++;
        char *line = trim_ws(line_buf);
        if (*line == '\0' || *line == '#') continue;

        char *cmd  = line;
        char *rest = line;
        while (*rest && !isspace((unsigned char)*rest)) rest++;
        if (*rest) {
            *rest++ = '\0';
            rest    = trim_ws(rest);
        }

        if (strcasecmp(cmd, "wait") == 0) {
            uint32_t ms = (uint32_t)strtoul(rest, NULL, 10);
            if (verbose) fprintf(stderr, "[script:%d] wait %u ms\n", line_no, ms);
            step_ms_with_buttons(ms, 0);
        } else if (strcasecmp(cmd, "press") == 0) {
            char btn_tok[32] = {0};
            unsigned hold_ms = 60u;
            unsigned gap_ms  = 80u;
            int n            = sscanf(rest, "%31s %u %u", btn_tok, &hold_ms, &gap_ms);
            uint8_t mask     = (n >= 1) ? parse_button_token(btn_tok) : 0;
            if (mask == 0) {
                fprintf(stderr, "ERROR:%d: Invalid button '%s' in press\n", line_no, rest);
                ok = false;
                break;
            }
            if (verbose) fprintf(stderr, "[script:%d] press %s (%u/%u ms)\n", line_no, btn_tok, hold_ms, gap_ms);
            press_button_mask(mask, hold_ms, gap_ms);
        } else if (strcasecmp(cmd, "type-entry") == 0) {
            if (verbose) fprintf(stderr, "[script:%d] type-entry '%s'\n", line_no, rest);
            if (!type_on_entry_screen(rest, true)) {
                ok = false;
                break;
            }
        } else if (strcasecmp(cmd, "ir") == 0) {
            uint32_t code = (uint32_t)strtoul(rest, NULL, 0);
            if (verbose) fprintf(stderr, "[script:%d] ir %u\n", line_no, code);
            emu_send_ir(code);
            step_ms_with_buttons(80u, 0);
        } else if (strcasecmp(cmd, "ir-levelup") == 0) {
            uint32_t code = sc23_cli_issue_levelup_code();
            if (verbose) fprintf(stderr, "[script:%d] ir-levelup %u\n", line_no, code);
            emu_send_ir(code);
            step_ms_with_buttons(80u, 0);
        } else if (strcasecmp(cmd, "minibadge") == 0) {
            int slot_1based = 0;
            char label[64]  = {0};
            if (sscanf(rest, "%d %63s", &slot_1based, label) < 2 || slot_1based < 1 || slot_1based > 4) {
                fprintf(stderr, "ERROR:%d: Usage: minibadge <1..4> <label|none>\n", line_no);
                ok = false;
                break;
            }
            if (strcasecmp(label, "none") == 0 || strcasecmp(label, "empty") == 0) {
                emu_set_minibadge(slot_1based - 1, -1);
            } else {
                int is_red = (strncasecmp(label, "redactd", 7) == 0) ? 1 : 0;
                emu_set_minibadge_label(slot_1based - 1, label, is_red);
            }
            if (verbose) fprintf(stderr, "[script:%d] minibadge slot %d -> %s\n", line_no, slot_1based, label);
            step_ms_with_buttons(600u, 0);
        } else if (strcasecmp(cmd, "bot-allies") == 0) {
            int en = atoi(rest);
            sc23_cli_set_bot_allies(en != 0 ? 1 : 0);
            if (verbose) fprintf(stderr, "[script:%d] bot-allies %d\n", line_no, en != 0);
        } else if (strcasecmp(cmd, "shot") == 0 ||
                   strcasecmp(cmd, "shot-screen") == 0 ||
                   strcasecmp(cmd, "shot-badge") == 0) {
            bool badge_mode = default_badge_view;
            if (strcasecmp(cmd, "shot-screen") == 0) badge_mode = false;
            if (strcasecmp(cmd, "shot-badge") == 0) badge_mode = true;
            if (!*rest) {
                fprintf(stderr, "ERROR:%d: Missing filename for %s\n", line_no, cmd);
                ok = false;
                break;
            }
            if (!save_shot_png(out_dir, rest, badge_mode, argv0, board_path, &cached_board, &board_tried)) {
                fprintf(stderr, "ERROR:%d: Failed to write screenshot '%s'\n", line_no, rest);
                ok = false;
                break;
            }
            printf("Saved %s screenshot: %s/%s.png\n",
                   badge_mode ? "badge" : "screen",
                   (out_dir && *out_dir) ? out_dir : ".",
                   rest);
        } else {
            fprintf(stderr, "ERROR:%d: Unknown script command '%s'\n", line_no, cmd);
            ok = false;
            break;
        }
    }

    free(cached_board);
    fclose(fp);
    return ok;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n"
           "Options:\n"
           "  --script <file>       Run automation script (.script)\n"
           "  --out <dir>           Output directory for script screenshots (default: .)\n"
           "  --steps <N>           Advance N x 20 ms steps (when not using --script)\n"
           "  --screenshot <path>   Write single PNG after --steps\n"
           "  --screen              Default 'shot' to 320x240 LCD PNG (default)\n"
           "  --badge               Default 'shot' to 1182x983 composited board+LCD+LEDs PNG\n"
           "  --board <path>        Explicit path to badges/2023/board.png\n"
           "  --seed <uint32>       PRNG seed for emu_init (default: 0x20235343)\n"
           "  --verbose             Enable firmware Serial logs and script trace\n",
           prog);
}

int main(int argc, char **argv) {
    const char *script_path = NULL;
    const char *out_dir     = ".";
    const char *single_shot = NULL;
    const char *board_path  = NULL;
    bool badge_view         = false;
    bool verbose            = false;
    uint32_t steps          = 100u;
    uint32_t seed           = 0x20235343u;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            single_shot = argv[++i];
        } else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) {
            board_path = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--badge") == 0) {
            badge_view = true;
        } else if (strcmp(argv[i], "--screen") == 0) {
            badge_view = false;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    platform_emu_set_log_enabled(verbose);
    sc23_cli_fake_server_reset();
    emu_init(seed);

    if (script_path) {
        return run_script_file(script_path, out_dir, badge_view, argv[0], board_path, verbose) ? 0 : 1;
    }

    for (uint32_t s = 0; s < steps; s++) {
        emu_step(20u, 0);
    }

    if (single_shot) {
        uint8_t *cached_board = NULL;
        bool board_tried      = false;
        bool ok               = save_shot_png(NULL, single_shot, badge_view, argv[0], board_path, &cached_board, &board_tried);
        free(cached_board);
        if (!ok) {
            fprintf(stderr, "ERROR: Failed to save screenshot to '%s'\n", single_shot);
            return 1;
        }
        printf("Saved screenshot: %s\n", single_shot);
    }
    return 0;
}
