#define _POSIX_C_SOURCE 200809L

#include "platform/emu_2021.h"

extern int getTouchLocation(void);

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

#define LCD_W 240
#define LCD_H 240

/* Geometry from emulator/web/badges/2021/layout.json */
#define BOARD_W_PX   1024
#define BOARD_H_PX   1428

#define SCREEN_CX_PX 512.10f
#define SCREEN_CY_PX 915.46f
#define SCREEN_R_PX  239.08f
#define SCREEN_X_PX  273.02f
#define SCREEN_Y_PX  676.38f
#define SCREEN_W_PX  478.16f
#define SCREEN_H_PX  478.16f

#define LED_D1_X_PX  513.79f
#define LED_D1_Y_PX  478.24f

#define BTN_BIT_UP      (1u << 0)
#define BTN_BIT_DOWN    (1u << 1)
#define BTN_BIT_LEFT    (1u << 2)
#define BTN_BIT_RIGHT   (1u << 3)
#define BTN_BIT_CENTER  (1u << 4)
#define BTN_BIT_BACK    (1u << 5)
#define BTN_BIT_SHACKLE (1u << 6)

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

static bool mkdir_p_parent(const char *file_path) {
    if (!file_path || !*file_path) {
        return false;
    }
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", file_path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) {
        return true;
    }
    *slash = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return false;
            }
            *p = '/';
        }
    }
    return (mkdir(tmp, 0755) == 0 || errno == EEXIST);
}

static bool write_png_rgba(const char *path, int w, int h, const uint8_t *rgba) {
    if (!path || !rgba || w <= 0 || h <= 0) {
        return false;
    }
    if (!mkdir_p_parent(path)) {
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
    size_t pos        = 8;

    while (pos + 12 <= (size_t)fsz) {
        uint32_t clen           = get_be32(&file_buf[pos]);
        const uint8_t *ctype    = &file_buf[pos + 4];
        const uint8_t *cdata    = &file_buf[pos + 8];
        if (pos + 12u + (size_t)clen > (size_t)fsz) {
            free(idat_buf);
            free(file_buf);
            return NULL;
        }
        if (memcmp(ctype, "IHDR", 4) == 0 && clen == 13) {
            w              = (int)get_be32(&cdata[0]);
            h              = (int)get_be32(&cdata[4]);
            uint8_t depth  = cdata[8];
            uint8_t colort = cdata[9];
            if (depth == 8 && colort == 6) bpp = 4;
            else if (depth == 8 && colort == 2) bpp = 3;
        } else if (memcmp(ctype, "IDAT", 4) == 0 && clen > 0) {
            uint8_t *nb = (uint8_t *)realloc(idat_buf, idat_len + clen);
            if (!nb) {
                free(idat_buf);
                free(file_buf);
                return NULL;
            }
            idat_buf = nb;
            memcpy(&idat_buf[idat_len], cdata, clen);
            idat_len += clen;
        } else if (memcmp(ctype, "IEND", 4) == 0) {
            break;
        }
        pos += 12u + (size_t)clen;
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

static void render_round_screen_rgba(uint8_t *out_rgba, bool mask_corners) {
    const uint16_t *fb = emu_framebuffer();
    uint32_t bl        = emu_backlight();
    for (int y = 0; y < LCD_H; y++) {
        for (int x = 0; x < LCD_W; x++) {
            int idx = y * LCD_W + x;
            if (mask_corners) {
                float dx   = (float)x - 119.5f;
                float dy   = (float)y - 119.5f;
                float dist = hypotf(dx, dy);
                if (dist > 120.2f) {
                    out_rgba[idx * 4 + 0] = 16;
                    out_rgba[idx * 4 + 1] = 17;
                    out_rgba[idx * 4 + 2] = 20;
                    out_rgba[idx * 4 + 3] = 255;
                    continue;
                }
            }
            uint8_t r = 0, g = 0, b = 0;
            rgb565_to_rgb888(fb[idx], &r, &g, &b);
            out_rgba[idx * 4 + 0] = (uint8_t)((5u * (255u - bl) + (uint32_t)r * bl + 127u) / 255u);
            out_rgba[idx * 4 + 1] = (uint8_t)((7u * (255u - bl) + (uint32_t)g * bl + 127u) / 255u);
            out_rgba[idx * 4 + 2] = (uint8_t)((10u * (255u - bl) + (uint32_t)b * bl + 127u) / 255u);
            out_rgba[idx * 4 + 3] = 255;
        }
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
    render_round_screen_rgba(screen_rgba, false);

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
            float dist = hypotf((float)px - SCREEN_CX_PX, (float)py - SCREEN_CY_PX);
            if (dist > SCREEN_R_PX) continue;
            int src_x          = (dx * LCD_W) / sw;
            const uint8_t *src = &screen_rgba[(src_y * LCD_W + src_x) * 4];
            if (dist > SCREEN_R_PX - 1.5f) {
                float a = SCREEN_R_PX - dist;
                blend_pixel_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, px, py, src[0], src[1], src[2], a);
            } else {
                uint8_t *dst = &out_rgba[((size_t)py * BOARD_W_PX + (size_t)px) * 4u];
                dst[0]       = src[0];
                dst[1]       = src[1];
                dst[2]       = src[2];
                dst[3]       = 255;
            }
        }
    }

    /* Composite D1 RGB LED */
    const uint8_t *leds = emu_leds();
    uint8_t lr          = leds[0];
    uint8_t lg          = leds[1];
    uint8_t lb          = leds[2];
    if (lr != 0 || lg != 0 || lb != 0) {
        int rad     = 34;
        int x_start = (int)floorf(LED_D1_X_PX) - rad;
        int x_end   = (int)ceilf(LED_D1_X_PX) + rad;
        int y_start = (int)floorf(LED_D1_Y_PX) - rad;
        int y_end   = (int)ceilf(LED_D1_Y_PX) + rad;
        for (int py = y_start; py <= y_end; py++) {
            for (int px = x_start; px <= x_end; px++) {
                float dist = hypotf((float)px - LED_D1_X_PX, (float)py - LED_D1_Y_PX);
                if (dist > (float)rad) continue;
                if (dist <= 9.0f) {
                    uint8_t cr = (uint8_t)(lr + (255 - lr) * 0.45f);
                    uint8_t cg = (uint8_t)(lg + (255 - lg) * 0.45f);
                    uint8_t cb = (uint8_t)(lb + (255 - lb) * 0.45f);
                    blend_pixel_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, px, py, cr, cg, cb, 0.95f);
                } else {
                    float t     = 1.0f - (dist - 9.0f) / ((float)rad - 9.0f);
                    float alpha = t * t * 0.75f;
                    blend_pixel_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, px, py, lr, lg, lb, alpha);
                }
            }
        }
    }
}

static bool load_flash_image(const char *explicit_path) {
    const char *candidates[] = {
        explicit_path,
        "emulator/web/badges/2021/flash.bin",
        "web/badges/2021/flash.bin",
        "../../web/badges/2021/flash.bin",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!candidates[i]) continue;
        FILE *fp = fopen(candidates[i], "rb");
        if (!fp) continue;
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            continue;
        }
        long sz = ftell(fp);
        if (sz <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
            fclose(fp);
            continue;
        }
        uint8_t *buf = (uint8_t *)malloc((size_t)sz);
        if (!buf) {
            fclose(fp);
            return false;
        }
        size_t n = fread(buf, 1, (size_t)sz, fp);
        fclose(fp);
        if (n != (size_t)sz) {
            free(buf);
            continue;
        }
        emu_2021_load_flash(buf, (uint32_t)sz);
        free(buf);
        fprintf(stderr, "[cli-2021] Loaded external flash %s (%ld bytes)\n", candidates[i], sz);
        return true;
    }
    fprintf(stderr, "ERROR: Could not find badges/2021/flash.bin\n");
    return false;
}

static uint8_t *try_load_board_png(const char *explicit_path) {
    const char *candidates[] = {
        explicit_path,
        "emulator/web/badges/2021/board.png",
        "web/badges/2021/board.png",
        "../../web/badges/2021/board.png",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (!candidates[i]) continue;
        int w = 0, h = 0;
        uint8_t *img = read_png_rgba(candidates[i], &w, &h);
        if (img && w == BOARD_W_PX && h == BOARD_H_PX) {
            return img;
        }
        free(img);
    }
    return NULL;
}

static void step_ms_with_buttons(uint32_t total_ms, uint32_t buttons) {
    while (total_ms > 0) {
        uint32_t chunk = (total_ms > 20u) ? 20u : total_ms;
        emu_step(chunk, buttons);
        total_ms -= chunk;
    }
}

static uint32_t parse_button_token(const char *tok) {
    if (!tok || !*tok) return 0;
    if (strcasecmp(tok, "up") == 0) return BTN_BIT_UP;
    if (strcasecmp(tok, "down") == 0) return BTN_BIT_DOWN;
    if (strcasecmp(tok, "left") == 0) return BTN_BIT_LEFT;
    if (strcasecmp(tok, "right") == 0) return BTN_BIT_RIGHT;
    if (strcasecmp(tok, "center") == 0 || strcasecmp(tok, "tap") == 0) return BTN_BIT_CENTER;
    if (strcasecmp(tok, "back") == 0 || strcasecmp(tok, "b") == 0) return BTN_BIT_BACK;
    if (strcasecmp(tok, "shackle") == 0 || strcasecmp(tok, "open") == 0 || strcasecmp(tok, "a") == 0) return BTN_BIT_SHACKLE;
    return 0;
}

static void cmd_menu_open(int target_idx) {
    if (target_idx < 1 || target_idx > 7) {
        fprintf(stderr, "ERROR: menu-open expects 1..7, got %d\n", target_idx);
        return;
    }
    /* Start at wheel_deg = 95 (getTouchLocation() ~ 5 deg) so rotating +45..+315 deg
     * stays monotonically inside [5..325] without crossing the 360->0 seam. */
    int wheel_deg = 95;
    emu_2021_set_wheel(1, wheel_deg);
    emu_step(25u, 0u);
    int prev_loc   = getTouchLocation();
    int rot        = 0;
    int target_rot = target_idx * 45;
    int guard      = 0;
    while (rot < target_rot - 2 && guard++ < 200) {
        wheel_deg += 5;
        emu_2021_set_wheel(1, wheel_deg);
        emu_step(25u, 0u);
        int loc = getTouchLocation();
        rot += (loc - prev_loc);
        while (rot < 0) rot += 360;
        while (rot >= 360) rot -= 360;
        prev_loc = loc;
    }
    /* Release wheel so menuScene returns menu_selected */
    emu_2021_set_wheel(0, 0);
    emu_step(40u, 0u);
}

static int combo_step_wheel(int wheel_deg, int *prev_loc, int *sim_pos, int *sim_sel) {
    emu_2021_set_wheel(1, wheel_deg);
    emu_step(25u, 0u);
    int loc = getTouchLocation();
    int cs  = -1 * (loc - *prev_loc);
    while (cs < -180) cs += 360;
    while (cs > 180) cs -= 360;
    *prev_loc = loc;
    if (cs != 0) {
        int pos = (*sim_pos + cs) % 360;
        while (pos > 360) pos -= 360;
        while (pos < 0) pos += 360;
        *sim_pos = pos;
        *sim_sel = ((pos + 4) / 9) % 40;
    }
    return cs;
}

static bool cmd_dial_combo(int n1, int n2, int n3) {
    if (n1 < 0 || n1 >= 40 || n2 < 0 || n2 >= 40 || n3 < 0 || n3 >= 40) {
        fprintf(stderr, "ERROR: dial-combo numbers must be in 0..39\n");
        return false;
    }

    int wheel_deg = 0;
    emu_2021_set_wheel(1, wheel_deg);
    emu_step(25u, 0u);
    int prev_loc = getTouchLocation();
    int sim_pos  = 0;
    int sim_sel  = 0;

    /* Stage 0: turn CW (increasing wheel_deg -> combo_scroll < 0) to n1 */
    int guard = 0;
    while (sim_sel != n1 && guard++ < 400) {
        wheel_deg += 4;
        combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }
    if (sim_sel != n1) {
        fprintf(stderr, "ERROR: dial-combo failed to reach n1=%d (at %d)\n", n1, sim_sel);
        return false;
    }

    /* Stage 0 -> Stage 1: reverse CCW with combo_scroll > 10 to latch combo_combo[0] = n1 */
    wheel_deg -= 18;
    int cs = combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    if (cs <= 10) {
        wheel_deg -= 18;
        combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }

    /* Stage 1 -> Stage 2: full CCW turn past n1 (scroll_dist > 350) */
    int dist = 0;
    guard    = 0;
    while (dist <= 355 && guard++ < 400) {
        wheel_deg -= 9;
        dist += combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }

    /* Stage 2: continue CCW to n2 */
    guard = 0;
    while (sim_sel != n2 && guard++ < 400) {
        wheel_deg -= 4;
        dist += combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }
    if (sim_sel != n2) {
        fprintf(stderr, "ERROR: dial-combo failed to reach n2=%d (at %d)\n", n2, sim_sel);
        return false;
    }

    /* Stage 2 -> Stage 3 -> Stage 4: reverse CW with combo_scroll < -10 to latch combo_combo[1] = n2 */
    wheel_deg += 18;
    cs = combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    if (cs >= -10) {
        wheel_deg += 18;
        combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }
    /* Ensure combo_selected != n2 and step one frame so case 3 transitions to case 4 */
    while (sim_sel == n2) {
        wheel_deg += 5;
        combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }
    combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);

    /* Stage 4: continue CW directly to n3 */
    guard = 0;
    while (sim_sel != n3 && guard++ < 400) {
        wheel_deg += 4;
        combo_step_wheel(wheel_deg, &prev_loc, &sim_pos, &sim_sel);
    }
    if (sim_sel != n3) {
        fprintf(stderr, "ERROR: dial-combo failed to reach n3=%d (at %d)\n", n3, sim_sel);
        return false;
    }

    /* Release wheel on n3 */
    emu_2021_set_wheel(0, 0);
    emu_step(25u, 0u);
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

int main(int argc, char **argv) {
    const char *script_path = NULL;
    const char *flash_path  = NULL;
    const char *board_path  = NULL;
    uint32_t seed           = 0x2021u;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--flash") == 0 && i + 1 < argc) {
            flash_path = argv[++i];
        } else if (strcmp(argv[i], "--board") == 0 && i + 1 < argc) {
            board_path = argv[++i];
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[++i], NULL, 0);
        } else if (!script_path) {
            script_path = argv[i];
        } else {
            fprintf(stderr, "Usage: %s [--flash flash.bin] [--board board.png] [--seed N] <script>\n", argv[0]);
            return 1;
        }
    }

    if (!script_path) {
        fprintf(stderr, "Usage: %s [--flash flash.bin] [--board board.png] [--seed N] <script>\n", argv[0]);
        return 1;
    }

    if (!load_flash_image(flash_path)) {
        return 1;
    }
    emu_init(seed);

    FILE *sf = fopen(script_path, "r");
    if (!sf) {
        fprintf(stderr, "ERROR: Cannot open script %s: %s\n", script_path, strerror(errno));
        return 1;
    }

    uint8_t *cached_board = NULL;
    bool board_tried      = false;
    char line_buf[1024];
    int line_no = 0;

    while (fgets(line_buf, sizeof(line_buf), sf)) {
        line_no++;
        char *line = trim_ws(line_buf);
        if (!*line || line[0] == '#') continue;

        char *cmd  = line;
        char *rest = line;
        while (*rest && !isspace((unsigned char)*rest)) rest++;
        if (*rest) {
            *rest++ = '\0';
            rest    = trim_ws(rest);
        }

        if (strcmp(cmd, "wait") == 0) {
            uint32_t ms = (uint32_t)strtoul(rest, NULL, 10);
            step_ms_with_buttons(ms, 0u);
        } else if (strcmp(cmd, "press") == 0) {
            char btok[64] = {0};
            unsigned ms   = 60u;
            if (sscanf(rest, "%63s %u", btok, &ms) < 1) {
                fprintf(stderr, "ERROR:%d: press <btn> [ms]\n", line_no);
                fclose(sf);
                free(cached_board);
                return 1;
            }
            uint32_t mask = parse_button_token(btok);
            if (!mask) {
                fprintf(stderr, "ERROR:%d: unknown button '%s'\n", line_no, btok);
                fclose(sf);
                free(cached_board);
                return 1;
            }
            step_ms_with_buttons(ms, mask);
            step_ms_with_buttons(40u, 0u);
        } else if (strcmp(cmd, "wheel") == 0) {
            int touched = 0, deg = 0;
            if (sscanf(rest, "%d %d", &touched, &deg) != 2) {
                fprintf(stderr, "ERROR:%d: wheel <0|1> <angle_deg>\n", line_no);
                fclose(sf);
                free(cached_board);
                return 1;
            }
            emu_2021_set_wheel(touched, deg);
            emu_step(25u, 0u);
        } else if (strcmp(cmd, "menu-open") == 0) {
            int idx = atoi(rest);
            cmd_menu_open(idx);
        } else if (strcmp(cmd, "dial-combo") == 0) {
            int n1 = 0, n2 = 0, n3 = 0;
            if (sscanf(rest, "%d %d %d", &n1, &n2, &n3) != 3 || !cmd_dial_combo(n1, n2, n3)) {
                fprintf(stderr, "ERROR:%d: dial-combo <n1> <n2> <n3> failed\n", line_no);
                fclose(sf);
                free(cached_board);
                return 1;
            }
        } else if (strcmp(cmd, "shackle") == 0) {
            emu_2021_set_shackle(atoi(rest));
            step_ms_with_buttons(50u, 0u);
        } else if (strcmp(cmd, "pull-shackle") == 0) {
            emu_2021_set_shackle(1);
            step_ms_with_buttons(80u, 0u);
            emu_2021_set_shackle(0);
            step_ms_with_buttons(40u, 0u);
        } else if (strcmp(cmd, "shim") == 0) {
            emu_2021_pulse_shim();
            step_ms_with_buttons(100u, 0u);
        } else if (strcmp(cmd, "vcard-program") == 0) {
            if (emu_2021_nfc_program_vcard(*rest ? rest : NULL) != 0) {
                fprintf(stderr, "ERROR:%d: vcard-program failed\n", line_no);
                fclose(sf);
                free(cached_board);
                return 1;
            }
            step_ms_with_buttons(120u, 0u);
        } else if (strcmp(cmd, "vcard-read") == 0) {
            char out[512];
            emu_2021_nfc_read_emulated_tag(out, sizeof(out));
            fprintf(stderr, "[cli-2021] Read emulated tag: %s\n", out);
            step_ms_with_buttons(60u, 0u);
        } else if (strcmp(cmd, "nfc-quote") == 0) {
            int idx = atoi(rest);
            if (emu_2021_nfc_present_quote_tag(idx) != 0) {
                fprintf(stderr, "ERROR:%d: nfc-quote %d failed\n", line_no, idx);
                fclose(sf);
                free(cached_board);
                return 1;
            }
        } else if (strcmp(cmd, "nfc-text") == 0) {
            emu_2021_nfc_present_text_tag(rest);
        } else if (strcmp(cmd, "nfc-clear") == 0) {
            emu_2021_nfc_clear_tag();
        } else if (strcmp(cmd, "bot-trade") == 0) {
            int p0 = 0, c0 = 1, p1 = 0, c1 = 0;
            int n  = sscanf(rest, "%d %d %d %d", &p0, &c0, &p1, &c1);
            if (n < 2 || emu_2021_nfc_start_bot_trade(p0, c0, p1, c1) != 0) {
                fprintf(stderr, "ERROR:%d: bot-trade <part0> <cnt0> [part1 cnt1] failed\n", line_no);
                fclose(sf);
                free(cached_board);
                return 1;
            }
        } else if (strcmp(cmd, "bot-status") == 0) {
            char st[256];
            int code = emu_2021_nfc_bot_trade_status(st, sizeof(st));
            fprintf(stderr, "[cli-2021] Bot trade status (%d): %s\n", code, st);
        } else if (strcmp(cmd, "minibadge") == 0) {
            emu_2021_mb_set_present(atoi(rest));
        } else if (strcmp(cmd, "mb-button") == 0) {
            emu_2021_mb_press_button();
        } else if (strcmp(cmd, "mb-text") == 0) {
            emu_2021_mb_send_text(rest);
        } else if (strcmp(cmd, "snap") == 0 || strcmp(cmd, "screenshot_screen") == 0) {
            uint8_t rgba[LCD_W * LCD_H * 4];
            render_round_screen_rgba(rgba, true);
            if (!write_png_rgba(rest, LCD_W, LCD_H, rgba)) {
                fprintf(stderr, "ERROR:%d: failed writing %s\n", line_no, rest);
                fclose(sf);
                free(cached_board);
                return 1;
            }
            fprintf(stderr, "[cli-2021] Wrote 240x240 round screen %s\n", rest);
        } else if (strcmp(cmd, "snap-badge") == 0 || strcmp(cmd, "screenshot") == 0) {
            if (!board_tried) {
                cached_board = try_load_board_png(board_path);
                board_tried  = true;
            }
            uint8_t *badge_rgba = (uint8_t *)malloc((size_t)BOARD_W_PX * (size_t)BOARD_H_PX * 4u);
            if (!badge_rgba) {
                fclose(sf);
                free(cached_board);
                return 1;
            }
            render_badge_rgba(badge_rgba, cached_board);
            bool ok = write_png_rgba(rest, BOARD_W_PX, BOARD_H_PX, badge_rgba);
            free(badge_rgba);
            if (!ok) {
                fprintf(stderr, "ERROR:%d: failed writing %s\n", line_no, rest);
                fclose(sf);
                free(cached_board);
                return 1;
            }
            fprintf(stderr, "[cli-2021] Wrote %dx%d badge composite %s\n", BOARD_W_PX, BOARD_H_PX, rest);
        } else {
            fprintf(stderr, "ERROR:%d: unknown command '%s'\n", line_no, cmd);
            fclose(sf);
            free(cached_board);
            return 1;
        }
    }

    fclose(sf);
    free(cached_board);
    return 0;
}
