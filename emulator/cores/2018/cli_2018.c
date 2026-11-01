#define _POSIX_C_SOURCE 200809L

#include "platform/sc18_sim.h"

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <zlib.h>

typedef struct {
    float x_px;
    float y_px;
    float r_px;
} led_geom_t;

static led_geom_t s_led_geom[SC18_LED_COUNT];
static bool s_led_geom_loaded = false;

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
    crc = crc32(crc, (const Bytef *)type, 4);
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
    size_t raw_len = (1u + row_bytes) * (size_t)h;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) {
        return false;
    }
    for (int y = 0; y < h; y++) {
        uint8_t *dst = &raw[(size_t)y * (1u + row_bytes)];
        dst[0] = 0;
        memcpy(&dst[1], &rgba[(size_t)y * row_bytes], row_bytes);
    }

    uLongf zcap = compressBound((uLong)raw_len);
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
    ihdr[8] = 8;
    ihdr[9] = 6;
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
    int p = a + b - c;
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
    size_t idat_len = 0;
    size_t idat_cap = 0;
    size_t pos = 8;

    while (pos + 12 <= (size_t)fsz) {
        uint32_t clen = get_be32(&file_buf[pos]);
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

    size_t stride = (size_t)w * (size_t)bpp;
    uLongf raw_len = (uLongf)((1u + stride) * (size_t)h);
    const uLongf expected_len = raw_len;
    uint8_t *raw = (uint8_t *)malloc((size_t)raw_len);
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
        uint8_t ftype = src[0];
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
                d[0] = curr[x * 3 + 0];
                d[1] = curr[x * 3 + 1];
                d[2] = curr[x * 3 + 2];
                d[3] = 255;
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

static bool load_led_geometry(const char *layout_path) {
    FILE *fp = fopen(layout_path, "rb");
    if (!fp) return false;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 4 * 1024 * 1024) {
        fclose(fp);
        return false;
    }
    char *json = (char *)malloc((size_t)sz + 1);
    if (!json) {
        fclose(fp);
        return false;
    }
    size_t n = fread(json, 1, (size_t)sz, fp);
    fclose(fp);
    json[n] = '\0';

    const char *leds_sec = strstr(json, "\"leds\"");
    if (!leds_sec) {
        free(json);
        return false;
    }

    const char *p = leds_sec;
    int found = 0;
    while (found < SC18_LED_COUNT) {
        const char *idx_key = strstr(p, "\"index\"");
        if (!idx_key) break;
        const char *colon = strchr(idx_key, ':');
        if (!colon) break;
        int idx = atoi(colon + 1);
        const char *x_key = strstr(colon, "\"x_px\"");
        const char *y_key = strstr(colon, "\"y_px\"");
        const char *r_key = strstr(colon, "\"radius_px\"");
        if (!x_key || !y_key) break;
        float x_px = (float)atof(strchr(x_key, ':') + 1);
        float y_px = (float)atof(strchr(y_key, ':') + 1);
        float r_px = r_key ? (float)atof(strchr(r_key, ':') + 1) : 4.13f;
        if (idx >= 0 && idx < SC18_LED_COUNT) {
            s_led_geom[idx].x_px = x_px;
            s_led_geom[idx].y_px = y_px;
            s_led_geom[idx].r_px = r_px;
            found++;
        }
        p = y_key + 6;
    }

    free(json);
    s_led_geom_loaded = (found == SC18_LED_COUNT);
    return s_led_geom_loaded;
}

static void rgb565_to_rgb888(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint8_t r5 = (uint8_t)((c >> 11) & 0x1F);
    uint8_t g6 = (uint8_t)((c >> 5) & 0x3F);
    uint8_t b5 = (uint8_t)(c & 0x1F);
    *r = (uint8_t)((r5 << 3) | (r5 >> 2));
    *g = (uint8_t)((g6 << 2) | (g6 >> 4));
    *b = (uint8_t)((b5 << 3) | (b5 >> 2));
}

static bool snap_matrix_png(const char *out_path) {
    const uint16_t *fb = emu_framebuffer();
    const int scale = 2;
    const int out_w = SC18_FB_WIDTH * scale;
    const int out_h = SC18_FB_HEIGHT * scale;
    uint8_t *rgba = (uint8_t *)malloc((size_t)out_w * (size_t)out_h * 4u);
    if (!rgba) return false;

    for (int y = 0; y < SC18_FB_HEIGHT; y++) {
        for (int x = 0; x < SC18_FB_WIDTH; x++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(fb[y * SC18_FB_WIDTH + x], &r, &g, &b);
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    uint8_t *d = &rgba[((size_t)(y * scale + dy) * (size_t)out_w + (size_t)(x * scale + dx)) * 4u];
                    d[0] = r;
                    d[1] = g;
                    d[2] = b;
                    d[3] = 255;
                }
            }
        }
    }
    bool ok = write_png_rgba(out_path, out_w, out_h, rgba);
    free(rgba);
    return ok;
}

static bool snap_badge_png(const char *board_path, const char *layout_path, const char *out_path) {
    if (!s_led_geom_loaded && !load_led_geometry(layout_path)) {
        fprintf(stderr, "Failed to load LED geometry from %s\n", layout_path);
        return false;
    }
    int bw = 0, bh = 0;
    uint8_t *board = read_png_rgba(board_path, &bw, &bh);
    if (!board) {
        fprintf(stderr, "Failed to read board PNG %s\n", board_path);
        return false;
    }

    /* Composite 256 glowing red 0603 LEDs onto the padlock shackle */
    const uint8_t *leds = emu_leds();
    for (int i = 0; i < SC18_LED_COUNT; i++) {
        uint8_t lr = leds[i * 3 + 0];
        uint8_t lg = leds[i * 3 + 1];
        uint8_t lb = leds[i * 3 + 2];
        if (lr == 0 && lg == 0 && lb == 0) {
            continue;
        }
        float cx = s_led_geom[i].x_px;
        float cy = s_led_geom[i].y_px;
        float core_r = s_led_geom[i].r_px * 1.15f;
        float glow_r = s_led_geom[i].r_px * 3.1f;
        float intensity = (float)lr / 255.0f;

        int x0 = (int)floorf(cx - glow_r);
        int x1 = (int)ceilf(cx + glow_r);
        int y0 = (int)floorf(cy - glow_r);
        int y1 = (int)ceilf(cy + glow_r);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= bw) x1 = bw - 1;
        if (y1 >= bh) y1 = bh - 1;

        for (int y = y0; y <= y1; y++) {
            for (int x = x0; x <= x1; x++) {
                float dx = ((float)x + 0.5f) - cx;
                float dy = ((float)y + 0.5f) - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist > glow_r) continue;

                uint8_t *px = &board[((size_t)y * (size_t)bw + (size_t)x) * 4u];
                if (dist <= core_r) {
                    float center_hot = 1.0f - (dist / core_r) * 0.45f;
                    int nr = (int)(255.0f * intensity + (float)px[0] * (1.0f - intensity));
                    int ng = (int)(85.0f * intensity * center_hot + (float)px[1] * (1.0f - intensity * 0.7f));
                    int nb = (int)(55.0f * intensity * center_hot + (float)px[2] * (1.0f - intensity * 0.7f));
                    px[0] = (uint8_t)(nr > 255 ? 255 : nr);
                    px[1] = (uint8_t)(ng > 255 ? 255 : ng);
                    px[2] = (uint8_t)(nb > 255 ? 255 : nb);
                    px[3] = 255;
                } else {
                    float t = 1.0f - (dist - core_r) / (glow_r - core_r);
                    float halo = t * t * 0.65f * intensity;
                    int nr = px[0] + (int)(255.0f * halo);
                    int ng = px[1] + (int)(28.0f * halo);
                    int nb = px[2] + (int)(18.0f * halo);
                    px[0] = (uint8_t)(nr > 255 ? 255 : nr);
                    px[1] = (uint8_t)(ng > 255 ? 255 : ng);
                    px[2] = (uint8_t)(nb > 255 ? 255 : nb);
                    px[3] = 255;
                }
            }
        }
    }

    /* Build full composite canvas: 2x Big Matrix banner at top + full Padlock board below */
    const int scale = 2;
    const int mat_w = SC18_FB_WIDTH * scale;   /* 640 */
    const int mat_h = SC18_FB_HEIGHT * scale;  /* 192 */
    const int pad_top = 24;
    const int gap = 20;
    const int pad_bot = 16;
    const int out_w = bw > (mat_w + 48) ? bw : (mat_w + 48);
    const int out_h = pad_top + mat_h + gap + bh + pad_bot;

    uint8_t *canvas = (uint8_t *)malloc((size_t)out_w * (size_t)out_h * 4u);
    if (!canvas) {
        free(board);
        return false;
    }

    /* Dark museum background #0b0f17 */
    for (int i = 0; i < out_w * out_h; i++) {
        canvas[i * 4 + 0] = 11;
        canvas[i * 4 + 1] = 15;
        canvas[i * 4 + 2] = 23;
        canvas[i * 4 + 3] = 255;
    }

    /* Draw 2x Big Matrix view centered at top with subtle border */
    int mx0 = (out_w - mat_w) / 2;
    int my0 = pad_top;
    for (int y = my0 - 2; y < my0 + mat_h + 2; y++) {
        for (int x = mx0 - 2; x < mx0 + mat_w + 2; x++) {
            uint8_t *d = &canvas[((size_t)y * (size_t)out_w + (size_t)x) * 4u];
            d[0] = 48;
            d[1] = 56;
            d[2] = 72;
            d[3] = 255;
        }
    }
    const uint16_t *fb = emu_framebuffer();
    for (int y = 0; y < SC18_FB_HEIGHT; y++) {
        for (int x = 0; x < SC18_FB_WIDTH; x++) {
            uint8_t r, g, b;
            rgb565_to_rgb888(fb[y * SC18_FB_WIDTH + x], &r, &g, &b);
            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    uint8_t *d = &canvas[((size_t)(my0 + y * scale + dy) * (size_t)out_w + (size_t)(mx0 + x * scale + dx)) * 4u];
                    d[0] = r;
                    d[1] = g;
                    d[2] = b;
                    d[3] = 255;
                }
            }
        }
    }

    /* Alpha-blend padlock board centered below */
    int bx0 = (out_w - bw) / 2;
    int by0 = pad_top + mat_h + gap;
    for (int y = 0; y < bh; y++) {
        for (int x = 0; x < bw; x++) {
            const uint8_t *s = &board[((size_t)y * (size_t)bw + (size_t)x) * 4u];
            uint8_t *d = &canvas[((size_t)(by0 + y) * (size_t)out_w + (size_t)(bx0 + x)) * 4u];
            uint32_t a = s[3];
            uint32_t ia = 255u - a;
            d[0] = (uint8_t)((s[0] * a + d[0] * ia) / 255u);
            d[1] = (uint8_t)((s[1] * a + d[1] * ia) / 255u);
            d[2] = (uint8_t)((s[2] * a + d[2] * ia) / 255u);
            d[3] = 255;
        }
    }

    free(board);
    bool ok = write_png_rgba(out_path, out_w, out_h, canvas);
    free(canvas);
    return ok;
}

static void step_ms_sliced(uint32_t total_ms, uint32_t buttons_mask) {
    uint32_t rem = total_ms;
    while (rem > 0) {
        uint32_t dt = rem > 20 ? 20 : rem;
        emu_step(dt, buttons_mask);
        rem -= dt;
    }
}

static uint32_t parse_button_name(const char *name) {
    if (strcasecmp(name, "up") == 0 || strcasecmp(name, "sw1") == 0) return SC18_BTN_UP;
    if (strcasecmp(name, "down") == 0 || strcasecmp(name, "sw2") == 0) return SC18_BTN_DOWN;
    if (strcasecmp(name, "select") == 0 || strcasecmp(name, "center") == 0 || strcasecmp(name, "sw3") == 0) return SC18_BTN_SELECT;
    return 0;
}

static void rm_rf_tmp_vfs(const char *dir_path) {
    if (!dir_path || strncmp(dir_path, "/tmp/sc18_vfs_", 14) != 0) {
        return;
    }
    DIR *d = opendir(dir_path);
    if (!d) return;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char sub[512];
        snprintf(sub, sizeof(sub), "%s/%s", dir_path, ent->d_name);
        DIR *subd = opendir(sub);
        if (subd) {
            closedir(subd);
            rm_rf_tmp_vfs(sub);
        } else {
            unlink(sub);
        }
    }
    closedir(d);
    rmdir(dir_path);
}

static char s_orig_cwd[1024] = "";

static void resolve_host_path(const char *in_path, char *out_buf, size_t out_sz) {
    if (!in_path || in_path[0] == '/' || s_orig_cwd[0] == '\0') {
        snprintf(out_buf, out_sz, "%s", in_path ? in_path : "");
    } else {
        snprintf(out_buf, out_sz, "%s/%s", s_orig_cwd, in_path);
    }
}

int main(int argc, char **argv) {
    if (!getcwd(s_orig_cwd, sizeof(s_orig_cwd))) {
        s_orig_cwd[0] = '\0';
    }
    const char *board_arg = "../../web/badges/2018/board.png";
    const char *layout_arg = "../../web/badges/2018/layout.json";

    int argi = 1;
    while (argi < argc) {
        if (strcmp(argv[argi], "--board") == 0 && argi + 1 < argc) {
            board_arg = argv[argi + 1];
            argi += 2;
        } else if (strcmp(argv[argi], "--layout") == 0 && argi + 1 < argc) {
            layout_arg = argv[argi + 1];
            argi += 2;
        } else if (strcmp(argv[argi], "--mb-plug") == 0 && argi + 1 < argc) {
            emu_2018_mb_set_present(atoi(argv[argi + 1]), 1);
            argi += 2;
        } else {
            break;
        }
    }

    char board_path[1280];
    char layout_path[1280];
    resolve_host_path(board_arg, board_path, sizeof(board_path));
    resolve_host_path(layout_arg, layout_path, sizeof(layout_path));

    emu_init(0x20185C18u);

    int rc = 0;
    while (argi < argc) {
        const char *cmd = argv[argi++];
        if (strcmp(cmd, "step") == 0) {
            if (argi >= argc) { fprintf(stderr, "step needs <ms>\n"); rc = 1; break; }
            uint32_t ms = (uint32_t)strtoul(argv[argi++], NULL, 10);
            step_ms_sliced(ms, 0);
        } else if (strcmp(cmd, "press") == 0) {
            if (argi >= argc) { fprintf(stderr, "press needs <up|down|select>\n"); rc = 1; break; }
            uint32_t mask = parse_button_name(argv[argi++]);
            if (mask == 0) { fprintf(stderr, "unknown button\n"); rc = 1; break; }
            uint32_t hold_ms = 140;
            uint32_t gap_ms = 160;
            if (argi < argc && isdigit((unsigned char)argv[argi][0])) {
                hold_ms = (uint32_t)strtoul(argv[argi++], NULL, 10);
            }
            if (argi < argc && isdigit((unsigned char)argv[argi][0])) {
                gap_ms = (uint32_t)strtoul(argv[argi++], NULL, 10);
            }
            step_ms_sliced(hold_ms, mask);
            step_ms_sliced(gap_ms, 0);
        } else if (strcmp(cmd, "ap_info") == 0) {
            char buf[512];
            emu_2018_ap_info(buf, sizeof(buf));
            printf("[ap_info] %s\n", buf);
        } else if (strcmp(cmd, "score") == 0) {
            if (argi >= argc) { fprintf(stderr, "score needs <n>\n"); rc = 1; break; }
            int sc = atoi(argv[argi++]);
            emu_2018_set_cli_score(sc);
        } else if (strcmp(cmd, "http_get") == 0) {
            if (argi >= argc) { fprintf(stderr, "http_get needs <path>\n"); rc = 1; break; }
            const char *path = argv[argi++];
            char req[1024];
            int req_len = snprintf(req, sizeof(req),
                "GET %s HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n", path);
            static uint8_t resp[65536];
            int rlen = emu_2018_http_request((const uint8_t *)req, req_len, resp, (int)sizeof(resp) - 1);
            resp[rlen] = '\0';
            char *eol = strstr((char *)resp, "\r\n");
            printf("[http_get %s] %d bytes (%.*s)\n", path, rlen, eol ? (int)(eol - (char *)resp) : 0, (char *)resp);
        } else if (strcmp(cmd, "http_post") == 0) {
            if (argi + 1 >= argc) { fprintf(stderr, "http_post needs <path> <body>\n"); rc = 1; break; }
            const char *path = argv[argi++];
            const char *body = argv[argi++];
            size_t blen = strlen(body);
            char req[4096];
            int req_len = snprintf(req, sizeof(req),
                "POST %s HTTP/1.1\r\n"
                "Host: 192.168.4.1\r\n"
                "Content-Type: application/x-www-form-urlencoded\r\n"
                "Content-Length: %zu\r\n"
                "Connection: close\r\n\r\n%s",
                path, blen, body);
            static uint8_t resp[65536];
            int rlen = emu_2018_http_request((const uint8_t *)req, req_len, resp, (int)sizeof(resp) - 1);
            resp[rlen] = '\0';
            char *eol = strstr((char *)resp, "\r\n");
            printf("[http_post %s] %d bytes (%.*s)\n", path, rlen, eol ? (int)(eol - (char *)resp) : 0, (char *)resp);
        } else if (strcmp(cmd, "mb_plug") == 0) {
            if (argi + 1 >= argc) { fprintf(stderr, "mb_plug needs <slot> <0|1>\n"); rc = 1; break; }
            int slot = atoi(argv[argi++]);
            int pres = atoi(argv[argi++]);
            emu_2018_mb_set_present(slot, pres);
        } else if (strcmp(cmd, "mb_text") == 0) {
            if (argi + 1 >= argc) { fprintf(stderr, "mb_text needs <slot> <msg>\n"); rc = 1; break; }
            int slot = atoi(argv[argi++]);
            const char *msg = argv[argi++];
            emu_2018_mb_queue_text(slot, msg);
        } else if (strcmp(cmd, "mb_button") == 0) {
            if (argi + 1 >= argc) { fprintf(stderr, "mb_button needs <slot> <status>\n"); rc = 1; break; }
            int slot = atoi(argv[argi++]);
            int st = atoi(argv[argi++]);
            emu_2018_mb_queue_button(slot, st);
        } else if (strcmp(cmd, "mb_info") == 0) {
            if (argi >= argc) { fprintf(stderr, "mb_info needs <slot>\n"); rc = 1; break; }
            int slot = atoi(argv[argi++]);
            uint8_t info[16];
            emu_2018_mb_get_info(slot, info);
            uint16_t sc = ((uint16_t)info[4] << 8) | info[5];
            uint32_t wr = (uint32_t)info[8] | ((uint32_t)info[9] << 8) | ((uint32_t)info[10] << 16) | ((uint32_t)info[11] << 24);
            uint32_t pl = (uint32_t)info[12] | ((uint32_t)info[13] << 8) | ((uint32_t)info[14] << 16) | ((uint32_t)info[15] << 24);
            printf("[mb slot %d addr 0x%02x] present=%u pwr=%u brightness=%u score=%u writes=%u polls=%u\n",
                slot, info[0], info[1], info[2], info[3], sc, wr, pl);
        } else if (strcmp(cmd, "snap_matrix") == 0) {
            if (argi >= argc) { fprintf(stderr, "snap_matrix needs <out.png>\n"); rc = 1; break; }
            const char *out_arg = argv[argi++];
            char out[1280];
            resolve_host_path(out_arg, out, sizeof(out));
            if (!snap_matrix_png(out)) {
                fprintf(stderr, "snap_matrix failed: %s\n", out);
                rc = 1;
                break;
            }
            printf("[snap_matrix] wrote %s\n", out_arg);
        } else if (strcmp(cmd, "snap_badge") == 0) {
            if (argi >= argc) { fprintf(stderr, "snap_badge needs <out.png>\n"); rc = 1; break; }
            const char *out_arg = argv[argi++];
            char out[1280];
            resolve_host_path(out_arg, out, sizeof(out));
            if (!snap_badge_png(board_path, layout_path, out)) {
                fprintf(stderr, "snap_badge failed: %s\n", out);
                rc = 1;
                break;
            }
            printf("[snap_badge] wrote %s\n", out_arg);
        } else {
            fprintf(stderr, "Unknown CLI command: %s\n", cmd);
            rc = 1;
            break;
        }
    }

    if (s_orig_cwd[0]) {
        (void)chdir(s_orig_cwd);
    }
    rm_rf_tmp_vfs(sc18_vfs_root());
    return rc;
}
