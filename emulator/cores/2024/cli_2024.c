#define _POSIX_C_SOURCE 200809L

#include "../../core/emu.h"
#include "platform/platform_internal.h"

#include "lvgl.h"
#include "src/widgets/buttonmatrix/lv_buttonmatrix_private.h"
#include "src/widgets/keyboard/lv_keyboard_private.h"
#include "theme.h"

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

/* Geometry from emulator/web/badges/2024/layout.json */
#define BOARD_W_PX 844
#define BOARD_H_PX 570

#define MODULE_X_PX 107.38f
#define MODULE_Y_PX  68.07f
#define MODULE_W_PX 634.59f
#define MODULE_H_PX 452.74f

#define CUTOUT_X_PX 121.58f
#define CUTOUT_Y_PX  72.80f
#define CUTOUT_W_PX 558.82f
#define CUTOUT_H_PX 424.32f

#define SCREEN_X_PX 128.21f
#define SCREEN_Y_PX  80.38f
#define SCREEN_W_PX 545.56f
#define SCREEN_H_PX 409.17f

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

static void fill_rect_rgba(uint8_t *rgba, int w, int h,
                           int x0, int y0, int rw, int rh,
                           uint8_t r, uint8_t g, uint8_t b) {
    for (int dy = 0; dy < rh; dy++) {
        int py = y0 + dy;
        if (py < 0 || py >= h) continue;
        for (int dx = 0; dx < rw; dx++) {
            int px = x0 + dx;
            if (px < 0 || px >= w) continue;
            uint8_t *dst = &rgba[((size_t)py * (size_t)w + (size_t)px) * 4u];
            dst[0]       = r;
            dst[1]       = g;
            dst[2]       = b;
            dst[3]       = 255;
        }
    }
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

    /* Dark glass bezel around the 2.8" capacitive touch LCD cutout */
    int cx0 = (int)lroundf(CUTOUT_X_PX);
    int cy0 = (int)lroundf(CUTOUT_Y_PX);
    int cw  = (int)lroundf(CUTOUT_W_PX);
    int ch  = (int)lroundf(CUTOUT_H_PX);
    fill_rect_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, cx0 - 2, cy0 - 2, cw + 4, ch + 4, 24, 28, 34);
    fill_rect_rgba(out_rgba, BOARD_W_PX, BOARD_H_PX, cx0, cy0, cw, ch, 10, 12, 16);

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
        "emulator/web/badges/2024/board.png",
        "web/badges/2024/board.png",
        "../../web/badges/2024/board.png",
        "../web/badges/2024/board.png",
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
                snprintf(rel + dlen, sizeof(rel) - dlen, "/../../web/badges/2024/board.png");
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

static void step_ms(uint32_t total_ms) {
    while (total_ms > 0) {
        uint32_t chunk = (total_ms > 20u) ? 20u : total_ms;
        emu_step(chunk, 0);
        total_ms -= chunk;
    }
}

static void do_touch_tap(int x, int y, uint32_t hold_ms) {
    if (x < 0) x = 0;
    if (x >= LCD_W) x = LCD_W - 1;
    if (y < 0) y = 0;
    if (y >= LCD_H) y = LCD_H - 1;
    if (hold_ms < 60u) hold_ms = 60u;
    emu_set_touch(1, x, y);
    step_ms(hold_ms);
    emu_set_touch(0, x, y);
    step_ms(90u);
}

static bool is_obj_visible(const lv_obj_t *obj) {
    const lv_obj_t *cur = obj;
    while (cur != NULL) {
        if (lv_obj_has_flag(cur, LV_OBJ_FLAG_HIDDEN)) {
            return false;
        }
        cur = lv_obj_get_parent(cur);
    }
    return obj != NULL;
}

typedef struct {
    const char *needle;
    lv_obj_t *found;
} label_search_t;

static lv_obj_tree_walk_res_t walk_find_label_cb(lv_obj_t *obj, void *user_data) {
    label_search_t *ctx = (label_search_t *)user_data;
    if (!is_obj_visible(obj)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *txt = lv_label_get_text(obj);
        if (txt && strstr(txt, ctx->needle) != NULL) {
            lv_area_t a;
            lv_obj_get_coords(obj, &a);
            if (a.x2 >= 0 && a.x1 < LCD_W && a.y2 >= 0 && a.y1 < LCD_H) {
                /* Keep walking so topmost (youngest) matching modal/button label wins */
                ctx->found = obj;
            }
        }
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static bool tap_label_by_text(const char *needle) {
    if (!needle || !*needle) return false;
    label_search_t ctx = {needle, NULL};
    if (lv_screen_active()) {
        lv_obj_tree_walk(lv_screen_active(), walk_find_label_cb, &ctx);
    }
    if (lv_layer_top()) {
        lv_obj_tree_walk(lv_layer_top(), walk_find_label_cb, &ctx);
    }
    if (!ctx.found) {
        return false;
    }

    lv_obj_t *target = ctx.found;
    lv_obj_t *cur    = lv_obj_get_parent(target);
    while (cur != NULL) {
        if (lv_obj_has_flag(cur, LV_OBJ_FLAG_CLICKABLE)) {
            lv_area_t ca;
            lv_obj_get_coords(cur, &ca);
            int cw = ca.x2 - ca.x1 + 1;
            int ch = ca.y2 - ca.y1 + 1;
            if (cw > 0 && cw <= 260 && ch > 0 && ch <= 120) {
                target = cur;
            }
            break;
        }
        cur = lv_obj_get_parent(cur);
    }

    lv_area_t area;
    lv_obj_get_coords(target, &area);
    int cx = (area.x1 + area.x2) / 2;
    int cy = (area.y1 + area.y2) / 2;
    do_touch_tap(cx, cy, 70u);
    return true;
}

static lv_obj_tree_walk_res_t walk_find_keyboard_cb(lv_obj_t *obj, void *user_data) {
    lv_obj_t **out_kb = (lv_obj_t **)user_data;
    if (!is_obj_visible(obj)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    if (lv_obj_check_type(obj, &lv_keyboard_class)) {
        *out_kb = obj;
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static lv_obj_t *find_active_keyboard(void) {
    lv_obj_t *kb = NULL;
    if (lv_screen_active()) {
        lv_obj_tree_walk(lv_screen_active(), walk_find_keyboard_cb, &kb);
    }
    if (lv_layer_top()) {
        lv_obj_tree_walk(lv_layer_top(), walk_find_keyboard_cb, &kb);
    }
    return kb;
}

static bool tap_keyboard_key_text(lv_obj_t *kb, const char *key_txt) {
    lv_buttonmatrix_t *btnm = (lv_buttonmatrix_t *)kb;
    if (!btnm || !btnm->map_p || !btnm->button_areas) return false;
    lv_area_t kb_coords;
    lv_obj_get_coords(kb, &kb_coords);

    uint32_t btn_i = 0;
    for (uint32_t map_i = 0; btnm->map_p[map_i] && btnm->map_p[map_i][0] != '\0'; map_i++) {
        const char *m = btnm->map_p[map_i];
        if (strcmp(m, "\n") == 0) continue;
        if (btn_i >= btnm->btn_cnt) break;
        if (strcmp(m, key_txt) == 0) {
            int cx = kb_coords.x1 + (btnm->button_areas[btn_i].x1 + btnm->button_areas[btn_i].x2) / 2;
            int cy = kb_coords.y1 + (btnm->button_areas[btn_i].y1 + btnm->button_areas[btn_i].y2) / 2;
            do_touch_tap(cx, cy, 60u);
            return true;
        }
        btn_i++;
    }
    return false;
}

static bool type_on_lv_keyboard(const char *text, bool press_ok) {
    lv_obj_t *kb = find_active_keyboard();
    if (!kb) return false;

    for (const char *p = text; *p; p++) {
        char ch    = *p;
        char ks[2] = {ch, '\0'};
        if (tap_keyboard_key_text(kb, ks)) {
            continue;
        }
        if (isupper((unsigned char)ch)) {
            tap_keyboard_key_text(kb, "ABC");
            if (tap_keyboard_key_text(kb, ks)) continue;
        } else if (islower((unsigned char)ch)) {
            tap_keyboard_key_text(kb, "abc");
            if (tap_keyboard_key_text(kb, ks)) continue;
        } else if (isdigit((unsigned char)ch) || ispunct((unsigned char)ch)) {
            tap_keyboard_key_text(kb, "1#");
            if (tap_keyboard_key_text(kb, ks)) continue;
        }
        return false;
    }

    if (press_ok) {
        tap_keyboard_key_text(kb, LV_SYMBOL_OK);
    }
    return true;
}

static bool is_clarity_arrow_text(const char *txt) {
    if (!txt) return false;
    return strcmp(txt, CLARITY_ARROW_LEFT) == 0 ||
           strcmp(txt, CLARITY_ARROW_UP) == 0 ||
           strcmp(txt, CLARITY_ARROW_RIGHT) == 0 ||
           strcmp(txt, CLARITY_ARROW_DOWN) == 0;
}

typedef struct {
    const char *next_arrow_txt;
    lv_obj_t *dpad_btns[4];
    const char *dpad_txts[4];
    int dpad_count;
} attack_scan_t;

static lv_obj_tree_walk_res_t walk_scan_attack_cb(lv_obj_t *obj, void *user_data) {
    attack_scan_t *scan = (attack_scan_t *)user_data;
    if (!is_obj_visible(obj)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *txt = lv_label_get_text(obj);
        if (is_clarity_arrow_text(txt)) {
            lv_obj_t *parent = lv_obj_get_parent(obj);
            if (parent && lv_obj_check_type(parent, &lv_button_class)) {
                if (scan->dpad_count < 4) {
                    scan->dpad_btns[scan->dpad_count] = parent;
                    scan->dpad_txts[scan->dpad_count] = txt;
                    scan->dpad_count++;
                }
            } else if (!scan->next_arrow_txt) {
                lv_color_t col     = lv_obj_get_style_text_color(obj, LV_PART_MAIN);
                lv_color_t neutral = lv_color_hex(GRAY_SHADE_2);
                if (col.red == neutral.red && col.green == neutral.green && col.blue == neutral.blue) {
                    scan->next_arrow_txt = txt;
                }
            }
        }
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static int solve_attack_arrows(int max_taps) {
    int taps = 0;
    while (taps < max_taps) {
        attack_scan_t scan;
        memset(&scan, 0, sizeof(scan));
        if (lv_screen_active()) {
            lv_obj_tree_walk(lv_screen_active(), walk_scan_attack_cb, &scan);
        }
        if (!scan.next_arrow_txt || scan.dpad_count == 0) {
            break;
        }
        lv_obj_t *target_btn = NULL;
        for (int i = 0; i < scan.dpad_count; i++) {
            if (strcmp(scan.dpad_txts[i], scan.next_arrow_txt) == 0) {
                target_btn = scan.dpad_btns[i];
                break;
            }
        }
        if (!target_btn) {
            break;
        }
        lv_area_t ba;
        lv_obj_get_coords(target_btn, &ba);
        do_touch_tap((ba.x1 + ba.x2) / 2, (ba.y1 + ba.y2) / 2, 55u);
        taps++;
    }
    return taps;
}

static lv_obj_tree_walk_res_t walk_dump_ui_cb(lv_obj_t *obj, void *user_data) {
    const char *layer_name = (const char *)user_data;
    if (!is_obj_visible(obj)) {
        return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    if (lv_obj_check_type(obj, &lv_label_class)) {
        const char *txt = lv_label_get_text(obj);
        printf("[%s] label (%d,%d..%d,%d) center=(%d,%d): \"%s\"\n",
               layer_name, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
               (int)((a.x1 + a.x2) / 2), (int)((a.y1 + a.y2) / 2),
               txt ? txt : "");
    } else if (lv_obj_check_type(obj, &lv_textarea_class)) {
        printf("[%s] textarea (%d,%d..%d,%d) center=(%d,%d)\n",
               layer_name, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2,
               (int)((a.x1 + a.x2) / 2), (int)((a.y1 + a.y2) / 2));
    } else if (lv_obj_check_type(obj, &lv_keyboard_class)) {
        printf("[%s] keyboard (%d,%d..%d,%d)\n",
               layer_name, (int)a.x1, (int)a.y1, (int)a.x2, (int)a.y2);
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static void dump_ui_stdout(void) {
    if (lv_screen_active()) {
        lv_obj_tree_walk(lv_screen_active(), walk_dump_ui_cb, "screen");
    }
    if (lv_layer_top()) {
        lv_obj_tree_walk(lv_layer_top(), walk_dump_ui_cb, "top");
    }
}

static void print_usage(const char *prog) {
    fprintf(stderr,
            "Usage:\n"
            "  %s --script <script.txt> --out <dir/> [options]\n"
            "  %s --steps <N> [--screenshot <out.png>] [options]\n"
            "\n"
            "Options:\n"
            "  --script <file>       Timed touch & IR tower script to execute\n"
            "  --out <dir>           Output directory for script screenshots (default: .)\n"
            "  --steps <N>           Run N x 20ms steps directly (without --script)\n"
            "  --screenshot <path>   Save PNG screenshot after --steps\n"
            "  --seed <u32>          Initial PRNG seed (default: 0x20245343)\n"
            "  --screen              Default 'shot' saves 320x240 screen PNG (default)\n"
            "  --badge               Default 'shot' saves 844x570 full badge PNG\n"
            "  --board <png>         Path to 844x570 board.png\n"
            "  --verbose             Enable ESP_LOG output on stderr\n"
            "\n"
            "Script commands:\n"
            "  wait <ms>                     Advance virtual time by <ms> milliseconds\n"
            "  touch <x> <y> [hold_ms]       Tap capacitive touch screen at (x, y)\n"
            "  drag <x1> <y1> <x2> <y2> <ms> Drag across touch screen\n"
            "  ir <1..11|levelup|savior|0xN> Inject NEC IR tower or beacon packet\n"
            "  tap-label <substring>         Tap visible LVGL label/button matching text\n"
            "  type-kb <text>                Type text on active LVGL keyboard + press OK\n"
            "  solve-attack <max_taps>       Tap up to <max_taps> matching stratagem arrows\n"
            "  dump-ui                       Print visible LVGL labels/widgets & coordinates\n"
            "  shot <name>                   Save PNG screenshot (<out>/<name>.png)\n"
            "  shot-screen <name>            Save 320x240 screen-only PNG\n"
            "  shot-badge <name>             Save 844x570 full-badge PNG\n",
            prog, prog);
}

int main(int argc, char **argv) {
    const char *script_path = NULL;
    const char *out_dir     = ".";
    const char *board_path  = NULL;
    const char *shot_path   = NULL;
    uint32_t seed           = 0x20245343u;
    int steps               = -1;
    bool default_badge      = false;
    bool verbose            = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = atoi(argv[++i]);
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
        setenv("EMU24_LOG", "1", 1);
    }

    cli_fake_server_reset(seed);
    emu_init(seed);

    uint8_t *cached_board = NULL;
    bool board_tried      = false;

    if (!script_path) {
        for (int s = 0; s < steps; s++) {
            emu_step(20, 0);
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
                cached_board    = try_load_board_png(argv[0], board_path);
                uint8_t *b_rgba = (uint8_t *)malloc((size_t)BOARD_W_PX * (size_t)BOARD_H_PX * 4u);
                if (!b_rgba) return 1;
                render_badge_rgba(b_rgba, cached_board);
                bool ok = write_png_rgba(shot_path, BOARD_W_PX, BOARD_H_PX, b_rgba);
                free(b_rgba);
                free(cached_board);
                if (!ok) return 1;
            }
        }
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

        if (strcasecmp(cmd, "wait") == 0 || strcasecmp(cmd, "run") == 0) {
            char *arg = strtok_r(NULL, " \t", &save);
            if (!arg) {
                fprintf(stderr, "%s:%d: '%s' requires <ms>\n", script_path, line_no, cmd);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            uint32_t ms = (uint32_t)strtoul(arg, NULL, 10);
            step_ms(ms);
        } else if (strcasecmp(cmd, "touch") == 0 || strcasecmp(cmd, "tap") == 0) {
            char *xs = strtok_r(NULL, " \t", &save);
            char *ys = strtok_r(NULL, " \t", &save);
            char *hs = strtok_r(NULL, " \t", &save);
            if (!xs || !ys) {
                fprintf(stderr, "%s:%d: 'touch' requires <x> <y> [hold_ms]\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            int x            = atoi(xs);
            int y            = atoi(ys);
            uint32_t hold_ms = hs ? (uint32_t)strtoul(hs, NULL, 10) : 70u;
            do_touch_tap(x, y, hold_ms);
        } else if (strcasecmp(cmd, "drag") == 0) {
            char *x1s = strtok_r(NULL, " \t", &save);
            char *y1s = strtok_r(NULL, " \t", &save);
            char *x2s = strtok_r(NULL, " \t", &save);
            char *y2s = strtok_r(NULL, " \t", &save);
            char *mss = strtok_r(NULL, " \t", &save);
            if (!x1s || !y1s || !x2s || !y2s) {
                fprintf(stderr, "%s:%d: 'drag' requires <x1> <y1> <x2> <y2> [ms]\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            int x1      = atoi(x1s);
            int y1      = atoi(y1s);
            int x2      = atoi(x2s);
            int y2      = atoi(y2s);
            uint32_t ms = mss ? (uint32_t)strtoul(mss, NULL, 10) : 200u;
            int nsteps  = (int)(ms / 20u);
            if (nsteps < 2) nsteps = 2;
            for (int k = 0; k <= nsteps; k++) {
                int cx = x1 + ((x2 - x1) * k) / nsteps;
                int cy = y1 + ((y2 - y1) * k) / nsteps;
                emu_set_touch(1, cx, cy);
                emu_step(20, 0);
            }
            emu_set_touch(0, x2, y2);
            step_ms(80u);
        } else if (strcasecmp(cmd, "ir") == 0) {
            char *arg = strtok_r(NULL, " \t", &save);
            if (!arg) {
                fprintf(stderr, "%s:%d: 'ir' requires <1..11|levelup|savior|auth|0xHEX>\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            uint32_t code = 0;
            if (strncasecmp(arg, "0x", 2) == 0) {
                code = (uint32_t)strtoul(arg, NULL, 16);
            } else if (isdigit((unsigned char)arg[0])) {
                code = cli_fake_server_tower_ir_code(atoi(arg));
            } else {
                code = cli_fake_server_beacon_ir_code(arg);
            }
            if (code == 0) {
                fprintf(stderr, "%s:%d: invalid IR target '%s'\n", script_path, line_no, arg);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            emu_send_ir(code);
            step_ms(120u);
        } else if (strcasecmp(cmd, "tap-label") == 0) {
            char *rest = save;
            while (rest && *rest && isspace((unsigned char)*rest)) rest++;
            if (!rest || !*rest) {
                fprintf(stderr, "%s:%d: 'tap-label' requires <text>\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            if (!tap_label_by_text(rest)) {
                fprintf(stderr, "%s:%d: label matching '%s' not found\n", script_path, line_no, rest);
                dump_ui_stdout();
                fclose(sfp);
                free(cached_board);
                return 1;
            }
        } else if (strcasecmp(cmd, "type-kb") == 0) {
            char *txt = strtok_r(NULL, " \t", &save);
            if (!txt) {
                fprintf(stderr, "%s:%d: 'type-kb' requires <text>\n", script_path, line_no);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
            if (!type_on_lv_keyboard(txt, true)) {
                fprintf(stderr, "%s:%d: failed to type '%s' on active LVGL keyboard\n", script_path, line_no, txt);
                fclose(sfp);
                free(cached_board);
                return 1;
            }
        } else if (strcasecmp(cmd, "solve-attack") == 0) {
            char *cnt_s  = strtok_r(NULL, " \t", &save);
            int max_taps = cnt_s ? atoi(cnt_s) : 32;
            if (max_taps < 1) max_taps = 1;
            int tapped = solve_attack_arrows(max_taps);
            if (tapped == 0) {
                fprintf(stderr, "%s:%d: 'solve-attack' found no active stratagem arrows\n", script_path, line_no);
                dump_ui_stdout();
                fclose(sfp);
                free(cached_board);
                return 1;
            }
        } else if (strcasecmp(cmd, "dump-ui") == 0) {
            dump_ui_stdout();
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
                               &cached_board, &board_tried)) {
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

    printf("emu-cli (2024): completed %s (%d screenshot%s written to %s)\n",
           script_path, shots_out, (shots_out == 1) ? "" : "s", out_dir);
    return 0;
}
