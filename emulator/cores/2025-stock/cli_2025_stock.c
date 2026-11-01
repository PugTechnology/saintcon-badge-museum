#define _POSIX_C_SOURCE 200809L

#include "../../core/emu.h"
#include "platform/platform_internal.h"

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

#define LCD_W        240
#define LCD_H        320
#define LED_COUNT    20
#define EEPROM_BYTES 512

#define BTN_UP     (1u << 0)
#define BTN_DOWN   (1u << 1)
#define BTN_LEFT   (1u << 2)
#define BTN_RIGHT  (1u << 3)
#define BTN_CENTER (1u << 4)

#define BOARD_W_PX 784
#define BOARD_H_PX 1792

#define SCREEN_X_PX 302.81f
#define SCREEN_Y_PX 560.81f
#define SCREEN_W_PX 213.80f
#define SCREEN_H_PX 285.07f

#define SW2_X_PX      409.71f
#define SW2_Y_PX      1004.75f
#define SW2_RADIUS_PX 61.26f

static const struct {
    float x;
    float y;
} k_led_coords[LED_COUNT] = {
    {146.37f, 121.11f}, // 0:  D1
    { 94.53f, 177.07f}, // 1:  D2
    { 67.84f, 251.97f}, // 2:  D3
    { 53.10f, 334.70f}, // 3:  D4
    { 54.14f, 420.50f}, // 4:  D5
    { 78.60f, 507.28f}, // 5:  D6
    {124.01f, 576.31f}, // 6:  D7
    {177.04f, 640.03f}, // 7:  D8
    {228.40f, 707.10f}, // 8:  D9
    {270.95f, 783.40f}, // 9:  D10
    {544.14f, 717.37f}, // 10: D11
    {587.88f, 624.17f}, // 11: D12
    {638.53f, 542.63f}, // 12: D13
    {691.08f, 462.28f}, // 13: D14
    {728.18f, 376.55f}, // 14: D15
    {725.10f, 288.86f}, // 15: D16
    {701.42f, 206.84f}, // 16: D17
    {660.40f, 127.67f}, // 17: D18
    {599.83f,  66.75f}, // 18: D19
    {510.67f,  38.73f}, // 19: D20
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

static inline void blend_pixel(uint8_t *rgba, int w, int h, int x, int y,
                               uint8_t sr, uint8_t sg, uint8_t sb, float alpha) {
    if (x < 0 || x >= w || y < 0 || y >= h || alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    uint8_t *p = &rgba[((size_t)y * (size_t)w + (size_t)x) * 4u];
    float da   = (float)p[3] / 255.0f;
    float outa = alpha + da * (1.0f - alpha);
    if (outa <= 1e-5f) return;
    float nr = ((float)sr * alpha + (float)p[0] * da * (1.0f - alpha)) / outa;
    float ng = ((float)sg * alpha + (float)p[1] * da * (1.0f - alpha)) / outa;
    float nb = ((float)sb * alpha + (float)p[2] * da * (1.0f - alpha)) / outa;
    p[0]     = (uint8_t)(nr < 0.0f ? 0 : (nr > 255.0f ? 255 : (int)(nr + 0.5f)));
    p[1]     = (uint8_t)(ng < 0.0f ? 0 : (ng > 255.0f ? 255 : (int)(ng + 0.5f)));
    p[2]     = (uint8_t)(nb < 0.0f ? 0 : (nb > 255.0f ? 255 : (int)(nb + 0.5f)));
    p[3]     = (uint8_t)(outa * 255.0f + 0.5f);
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

static void draw_filled_circle(uint8_t *img, int w, int h, float cx, float cy, float radius,
                               uint8_t r, uint8_t g, uint8_t b, float alpha) {
    int x0 = (int)floorf(cx - radius - 1.0f);
    int x1 = (int)ceilf(cx + radius + 1.0f);
    int y0 = (int)floorf(cy - radius - 1.0f);
    int y1 = (int)ceilf(cy + radius + 1.0f);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx   = ((float)x + 0.5f) - cx;
            float dy   = ((float)y + 0.5f) - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float cov  = radius + 0.5f - dist;
            if (cov > 0.0f) {
                if (cov > 1.0f) cov = 1.0f;
                blend_pixel(img, w, h, x, y, r, g, b, alpha * cov);
            }
        }
    }
}

static void draw_ring(uint8_t *img, int w, int h, float cx, float cy, float radius, float half_thick,
                      uint8_t r, uint8_t g, uint8_t b, float alpha) {
    int x0 = (int)floorf(cx - radius - half_thick - 1.0f);
    int x1 = (int)ceilf(cx + radius + half_thick + 1.0f);
    int y0 = (int)floorf(cy - radius - half_thick - 1.0f);
    int y1 = (int)ceilf(cy + radius + half_thick + 1.0f);
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            float dx   = ((float)x + 0.5f) - cx;
            float dy   = ((float)y + 0.5f) - cy;
            float dist = fabsf(sqrtf(dx * dx + dy * dy) - radius);
            float cov  = half_thick + 0.5f - dist;
            if (cov > 0.0f) {
                if (cov > 1.0f) cov = 1.0f;
                blend_pixel(img, w, h, x, y, r, g, b, alpha * cov);
            }
        }
    }
}

static void render_badge_rgba(uint8_t *out_rgba, const uint8_t *board_rgba, uint8_t buttons_mask) {
    if (board_rgba) {
        memcpy(out_rgba, board_rgba, (size_t)BOARD_W_PX * (size_t)BOARD_H_PX * 4u);
    } else {
        for (size_t i = 0; i < (size_t)BOARD_W_PX * (size_t)BOARD_H_PX; i++) {
            out_rgba[i * 4 + 0] = 28;
            out_rgba[i * 4 + 1] = 33;
            out_rgba[i * 4 + 2] = 40;
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

    const uint8_t *leds = emu_leds();
    for (int i = 0; i < LED_COUNT; i++) {
        float lx  = k_led_coords[i].x;
        float ly  = k_led_coords[i].y;
        uint8_t r = leds[i * 3 + 0];
        uint8_t g = leds[i * 3 + 1];
        uint8_t b = leds[i * 3 + 2];

        if (r == 0 && g == 0 && b == 0) {
            draw_filled_circle(out_rgba, BOARD_W_PX, BOARD_H_PX, lx, ly, 8.0f, 0x1B, 0x20, 0x26, 1.0f);
            draw_ring(out_rgba, BOARD_W_PX, BOARD_H_PX, lx, ly, 8.0f, 0.75f, 0x2D, 0x34, 0x3E, 1.0f);
        } else {
            const float rad = 28.0f;
            int x0          = (int)floorf(lx - rad);
            int x1          = (int)ceilf(lx + rad);
            int y0          = (int)floorf(ly - rad);
            int y1          = (int)ceilf(ly + rad);
            for (int y = y0; y <= y1; y++) {
                for (int x = x0; x <= x1; x++) {
                    float dx   = ((float)x + 0.5f) - lx;
                    float dy   = ((float)y + 0.5f) - ly;
                    float dist = sqrtf(dx * dx + dy * dy);
                    if (dist > rad) continue;
                    float t    = dist / rad;
                    uint8_t gr = r, gg = g, gb = b;
                    float ga = 0.0f;
                    if (t < 0.28f) {
                        float k = t / 0.28f;
                        gr      = (uint8_t)(255.0f * (1.0f - k) + (float)r * k);
                        gg      = (uint8_t)(255.0f * (1.0f - k) + (float)g * k);
                        gb      = (uint8_t)(255.0f * (1.0f - k) + (float)b * k);
                        ga      = 1.0f;
                    } else if (t < 0.65f) {
                        float k = (t - 0.28f) / (0.65f - 0.28f);
                        ga      = 1.0f * (1.0f - k) + 0.45f * k;
                    } else {
                        float k = (t - 0.65f) / (1.0f - 0.65f);
                        ga      = 0.45f * (1.0f - k);
                    }
                    blend_pixel(out_rgba, BOARD_W_PX, BOARD_H_PX, x, y, gr, gg, gb, ga);
                }
            }
            uint8_t cr = (uint8_t)(r + 60 > 255 ? 255 : r + 60);
            uint8_t cg = (uint8_t)(g + 60 > 255 ? 255 : g + 60);
            uint8_t cb = (uint8_t)(b + 60 > 255 ? 255 : b + 60);
            draw_filled_circle(out_rgba, BOARD_W_PX, BOARD_H_PX, lx, ly, 8.5f, cr, cg, cb, 1.0f);
        }
    }

    float jx = SW2_X_PX;
    float jy = SW2_Y_PX;
    if (buttons_mask & BTN_LEFT)  jx -= 10.0f;
    if (buttons_mask & BTN_RIGHT) jx += 10.0f;
    if (buttons_mask & BTN_UP)    jy -= 10.0f;
    if (buttons_mask & BTN_DOWN)  jy += 10.0f;
    draw_ring(out_rgba, BOARD_W_PX, BOARD_H_PX, SW2_X_PX, SW2_Y_PX, SW2_RADIUS_PX, 1.0f, 255, 255, 255, 0.25f);
    if (buttons_mask & BTN_CENTER) {
        draw_filled_circle(out_rgba, BOARD_W_PX, BOARD_H_PX, jx, jy, 18.0f, 0xD4, 0xA7, 0x2C, 1.0f);
    } else {
        draw_filled_circle(out_rgba, BOARD_W_PX, BOARD_H_PX, jx, jy, 18.0f, 0xB8, 0xC0, 0xC8, 1.0f);
    }
    draw_ring(out_rgba, BOARD_W_PX, BOARD_H_PX, jx, jy, 18.0f, 1.0f, 0x2D, 0x33, 0x3B, 1.0f);
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

static bool parse_button_token(const char *tok, uint8_t *out_mask) {
    if (!tok || !*tok || !out_mask) return false;
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", tok);
    uint8_t mask = 0;
    char *save   = NULL;
    for (char *part = strtok_r(buf, "+|", &save); part != NULL; part = strtok_r(NULL, "+|", &save)) {
        if (strcasecmp(part, "up") == 0 || strcasecmp(part, "u") == 0) {
            mask |= BTN_UP;
        } else if (strcasecmp(part, "down") == 0 || strcasecmp(part, "d") == 0) {
            mask |= BTN_DOWN;
        } else if (strcasecmp(part, "left") == 0 || strcasecmp(part, "l") == 0) {
            mask |= BTN_LEFT;
        } else if (strcasecmp(part, "right") == 0 || strcasecmp(part, "r") == 0) {
            mask |= BTN_RIGHT;
        } else if (strcasecmp(part, "center") == 0 || strcasecmp(part, "centre") == 0 ||
                   strcasecmp(part, "ctr") == 0 || strcasecmp(part, "enter") == 0 ||
                   strcasecmp(part, "ok") == 0 || strcasecmp(part, "select") == 0 ||
                   strcasecmp(part, "c") == 0) {
            mask |= BTN_CENTER;
        } else {
            return false;
        }
    }
    *out_mask = mask;
    return mask != 0;
}

static uint8_t *try_load_board_png(const char *argv0, const char *explicit_path) {
    int w = 0, h = 0;
    if (explicit_path) {
        uint8_t *img = read_png_rgba(explicit_path, &w, &h);
        if (img && w == BOARD_W_PX && h == BOARD_H_PX) return img;
        free(img);
    }
    const char *candidates[] = {
        "emulator/web/badges/2025/board.png",
        "web/badges/2025/board.png",
        "../../web/badges/2025/board.png",
        "../web/badges/2025/board.png",
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
                snprintf(rel + dlen, sizeof(rel) - dlen, "/../../web/badges/2025/board.png");
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
                          bool *board_tried,
                          uint8_t buttons_mask) {
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
    render_badge_rgba(badge_rgba, *cached_board, buttons_mask);
    bool ok = write_png_rgba(full_path, BOARD_W_PX, BOARD_H_PX, badge_rgba);
    free(badge_rgba);
    return ok;
}

static void dump_leds_stdout(void) {
    const uint8_t *leds = emu_leds();
    printf("LEDs:");
    for (int i = 0; i < LED_COUNT; i++) {
        printf(" D%d=#%02X%02X%02X", i + 1, leds[i * 3 + 0], leds[i * 3 + 1], leds[i * 3 + 2]);
    }
    printf("\n");
}

static void dump_eeprom_stdout(void) {
    const uint8_t *ee = emu_eeprom();
    printf("EEPROM (first 64 bytes):\n");
    for (int i = 0; i < 64; i++) {
        printf("%02X%s", ee[i], (i % 16 == 15) ? "\n" : " ");
    }
}

static uint32_t s_nut_seq = 0;
static void generate_nut_code(char out_code[6]) {
    static const char kAlpha[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    uint32_t v                 = (platform_rand_u32() ^ (++s_nut_seq * 0x9E3779B9u));
    for (int i = 0; i < 5; i++) {
        out_code[i] = kAlpha[v & 31u];
        v           = (v >> 5) ^ (platform_rand_u32() + (uint32_t)i);
    }
    out_code[5] = '\0';
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --script <script.txt> --out <dir/> [options]\n"
            "  %s --steps <N> [--screenshot <out.png>] [options]\n"
            "\n"
            "Options:\n"
            "  --script <file>       Timed input & NFC Nut script to execute\n"
            "  --out <dir>           Output directory for script screenshots (default: .)\n"
            "  --steps <N>           Run N x 20ms steps directly (without --script)\n"
            "  --buttons <mask>      Button bitmask for --steps mode (default: 0)\n"
            "  --screenshot <path>   Save PNG screenshot after --steps\n"
            "  --seed <u32>          Initial PRNG seed (default: 0x20255343)\n"
            "  --screen              Default 'shot' saves 240x320 screen PNG (default)\n"
            "  --badge               Default 'shot' saves 784x1792 full badge PNG\n"
            "  --board <png>         Path to 784x1792 board.png\n"
            "  --dump-leds           Print 20 WS2812 LED RGB values\n"
            "  --dump-eeprom         Print first 64 bytes of ST25DV NFC EEPROM\n"
            "  --verbose             Enable ESP_LOG output on stderr\n"
            "\n"
            "Script commands:\n"
            "  wait <ms>                     Advance virtual time by <ms> milliseconds\n"
            "  press <btn> [count]           Tap button (up|down|left|right|center)\n"
            "  hold <btn> <ms>               Hold button for <ms> ms, then release\n"
            "  nut <TYPE> [FACTION_1_TO_6]   Tap physical Nut (SORTING_HAT, TELECOM, CONTEST, EVENT, VENDOR, COMMUNITY)\n"
            "  shot <name>                   Save PNG screenshot (<out>/<name>.png)\n"
            "  shot-screen <name>            Save 240x320 screen-only PNG\n"
            "  shot-badge <name>             Save 784x1792 full-badge PNG\n"
            "  dump-leds                     Print current 20-LED chain colors\n",
            prog, prog);
}

int main(int argc, char **argv) {
    const char *script_path = NULL;
    const char *out_dir     = ".";
    const char *board_path  = NULL;
    const char *shot_path   = NULL;
    uint32_t seed           = 0x20255343u;
    int steps               = -1;
    uint8_t buttons_arg     = 0;
    bool default_badge      = false;
    bool dump_leds          = false;
    bool dump_eeprom        = false;
    bool verbose            = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--buttons") == 0 && i + 1 < argc) {
            buttons_arg = (uint8_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) {
            board_path = argv[++i];
        } else if (strcmp(argv[i], "--badge") == 0) {
            default_badge = true;
        } else if (strcmp(argv[i], "--screen") == 0) {
            default_badge = false;
        } else if (strcmp(argv[i], "--dump-leds") == 0) {
            dump_leds = true;
        } else if (strcmp(argv[i], "--dump-eeprom") == 0) {
            dump_eeprom = true;
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

    if (!script_path && steps < 0) {
        fprintf(stderr, "Error: specify --script <file> or --steps <N>\n");
        print_usage(argv[0]);
        return 1;
    }

    if (verbose) {
        setenv("EMU25_LOG", "1", 1);
    }

    emu_init(seed);

    uint8_t *cached_board = NULL;
    bool board_tried      = false;

    if (!script_path) {
        for (int s = 0; s < steps; s++) {
            emu_step(20, buttons_arg);
        }
        if (shot_path) {
            if (!default_badge) {
                uint8_t rgba[LCD_W * LCD_H * 4];
                render_screen_rgba(rgba);
                if (!write_png_rgba(shot_path, LCD_W, LCD_H, rgba)) {
                    fprintf(stderr, "Failed to write screenshot '%s'\n", shot_path);
                    return 1;
                }
            } else {
                cached_board     = try_load_board_png(argv[0], board_path);
                uint8_t *b_rgba  = (uint8_t *)malloc((size_t)BOARD_W_PX * (size_t)BOARD_H_PX * 4u);
                if (!b_rgba) return 1;
                render_badge_rgba(b_rgba, cached_board, buttons_arg);
                bool ok = write_png_rgba(shot_path, BOARD_W_PX, BOARD_H_PX, b_rgba);
                free(b_rgba);
                free(cached_board);
                if (!ok) return 1;
            }
        }
        if (dump_leds) dump_leds_stdout();
        if (dump_eeprom) dump_eeprom_stdout();
        return 0;
    }

    if (!mkdir_p(out_dir)) {
        fprintf(stderr, "Error: failed to create output directory '%s'\n", out_dir);
        return 1;
    }

    FILE *sfp = fopen(script_path, "r");
    if (!sfp) {
        fprintf(stderr, "Error: cannot open script '%s': %s\n", script_path, strerror(errno));
        return 1;
    }

    uint8_t held_mask = 0;
    char line[512];
    int line_no   = 0;
    int shots_out = 0;

    while (fgets(line, sizeof(line), sfp)) {
        line_no++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        size_t len = strlen(p);
        while (len > 0 && isspace((unsigned char)p[len - 1])) p[--len] = '\0';
        if (len == 0) continue;

        char *save = NULL;
        char *cmd  = strtok_r(p, " \t", &save);
        if (!cmd) continue;

        if (strcasecmp(cmd, "wait") == 0) {
            char *arg = strtok_r(NULL, " \t", &save);
            if (!arg) {
                fprintf(stderr, "%s:%d: 'wait' requires <ms>\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            uint32_t ms = (uint32_t)strtoul(arg, NULL, 10);
            emu_step(ms, held_mask);
        } else if (strcasecmp(cmd, "press") == 0) {
            char *btn_s = strtok_r(NULL, " \t", &save);
            char *cnt_s = strtok_r(NULL, " \t", &save);
            uint8_t bit = 0;
            if (!parse_button_token(btn_s, &bit)) {
                fprintf(stderr, "%s:%d: invalid button '%s'\n", script_path, line_no, btn_s ? btn_s : "");
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            uint32_t count = cnt_s ? (uint32_t)strtoul(cnt_s, NULL, 10) : 1u;
            if (count < 1u) count = 1u;
            for (uint32_t c = 0; c < count; c++) {
                emu_step(70, (uint8_t)(held_mask | bit));
                emu_step(70, held_mask);
            }
        } else if (strcasecmp(cmd, "hold") == 0) {
            char *btn_s = strtok_r(NULL, " \t", &save);
            char *ms_s  = strtok_r(NULL, " \t", &save);
            uint8_t bit = 0;
            if (!parse_button_token(btn_s, &bit) || !ms_s) {
                fprintf(stderr, "%s:%d: 'hold' requires <button> <ms>\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            uint32_t ms = (uint32_t)strtoul(ms_s, NULL, 10);
            emu_step(ms, (uint8_t)(held_mask | bit));
            emu_step(40, held_mask);
        } else if (strcasecmp(cmd, "down") == 0) {
            char *btn_s = strtok_r(NULL, " \t", &save);
            uint8_t bit = 0;
            if (!parse_button_token(btn_s, &bit)) return 1;
            held_mask |= bit;
            emu_step(20, held_mask);
        } else if (strcasecmp(cmd, "up") == 0) {
            char *btn_s = strtok_r(NULL, " \t", &save);
            uint8_t bit = 0;
            if (!parse_button_token(btn_s, &bit)) return 1;
            held_mask = (uint8_t)(held_mask & ~bit);
            emu_step(20, held_mask);
        } else if (strcasecmp(cmd, "release") == 0) {
            held_mask = 0;
            emu_step(20, 0);
        } else if (strcasecmp(cmd, "nut") == 0) {
            char *ntype_s = strtok_r(NULL, " \t", &save);
            char *fact_s  = strtok_r(NULL, " \t", &save);
            const char *ntype = ntype_s ? ntype_s : "TELECOM";
            int faction_override = fact_s ? atoi(fact_s) : 0;
            char code[6];
            generate_nut_code(code);
            cli_fake_server_register_nut(code, ntype, faction_override);
            platform_nfc_inject_nut(code, ntype);
            emu_step(50, held_mask);
        } else if (strcasecmp(cmd, "dump-leds") == 0) {
            dump_leds_stdout();
        } else if (strcasecmp(cmd, "shot") == 0 ||
                   strcasecmp(cmd, "shot-screen") == 0 ||
                   strcasecmp(cmd, "shot-badge") == 0) {
            char *name = strtok_r(NULL, " \t", &save);
            if (!name) {
                fprintf(stderr, "%s:%d: '%s' requires <name>\n", script_path, line_no, cmd);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            bool use_badge = default_badge;
            if (strcasecmp(cmd, "shot-screen") == 0) use_badge = false;
            if (strcasecmp(cmd, "shot-badge") == 0)  use_badge = true;
            if (!save_shot_png(out_dir, name, use_badge, argv[0], board_path,
                               &cached_board, &board_tried, held_mask)) {
                fprintf(stderr, "%s:%d: failed to write screenshot '%s'\n", script_path, line_no, name);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            shots_out++;
        } else {
            fprintf(stderr, "%s:%d: unknown command '%s'\n", script_path, line_no, cmd);
            fclose(sfp);
            free(cached_board);
            return 1;
        }
    }

    fclose(sfp);
    free(cached_board);

    if (dump_leds) dump_leds_stdout();
    if (dump_eeprom) dump_eeprom_stdout();

    printf("emu-cli (2025-stock): completed %s (%d screenshot%s written to %s)\n",
           script_path, shots_out, (shots_out == 1) ? "" : "s", out_dir);
    return 0;
}
