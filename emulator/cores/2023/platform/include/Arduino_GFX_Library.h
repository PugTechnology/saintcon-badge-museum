#ifndef SC23_ARDUINO_GFX_LIBRARY_H
#define SC23_ARDUINO_GFX_LIBRARY_H

#include "Arduino.h"
#include "../emu_2023.h"

#define BLACK       0x0000
#define NAVY        0x000F
#define DARKGREEN   0x03E0
#define DARKCYAN    0x03EF
#define MAROON      0x7800
#define PURPLE      0x780F
#define OLIVE       0x7BE0
#define LIGHTGREY   0xC618
#define DARKGREY    0x7BEF
#define BLUE        0x001F
#define GREEN       0x07E0
#define CYAN        0x07FF
#define RED         0xF800
#define MAGENTA     0xF81F
#define YELLOW      0xFFE0
#define WHITE       0xFFFF
#define ORANGE      0xFD20
#define GREENYELLOW 0xAFE5
#define PINK        0xF81F

/* First-party 5x7 ASCII bitmap glyphs (0x20..0x7E), 5 columns x 7 rows (bit 0 = top row). */
static const uint8_t kSc23Font5x7[95][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* 0x20 ' ' */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* 0x21 '!' */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* 0x22 '"' */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* 0x23 '#' */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* 0x24 '$' */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* 0x25 '%' */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* 0x26 '&' */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* 0x27 '\'' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* 0x28 '(' */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* 0x29 ')' */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* 0x2A '*' */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* 0x2B '+' */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* 0x2C ',' */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* 0x2D '-' */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* 0x2E '.' */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* 0x2F '/' */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0x30 '0' */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 0x31 '1' */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 0x32 '2' */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 0x33 '3' */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 0x34 '4' */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 0x35 '5' */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 0x36 '6' */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 0x37 '7' */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 0x38 '8' */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 0x39 '9' */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* 0x3A ':' */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* 0x3B ';' */
    {0x08, 0x14, 0x22, 0x41, 0x00}, /* 0x3C '<' */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* 0x3D '=' */
    {0x00, 0x41, 0x22, 0x14, 0x08}, /* 0x3E '>' */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* 0x3F '?' */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* 0x40 '@' */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* 0x41 'A' */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* 0x42 'B' */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* 0x43 'C' */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 0x44 'D' */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* 0x45 'E' */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* 0x46 'F' */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* 0x47 'G' */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 0x48 'H' */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* 0x49 'I' */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* 0x4A 'J' */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* 0x4B 'K' */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* 0x4C 'L' */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* 0x4D 'M' */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 0x4E 'N' */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 0x4F 'O' */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* 0x50 'P' */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 0x51 'Q' */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* 0x52 'R' */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* 0x53 'S' */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* 0x54 'T' */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 0x55 'U' */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 0x56 'V' */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* 0x57 'W' */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* 0x58 'X' */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* 0x59 'Y' */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* 0x5A 'Z' */
    {0x00, 0x7F, 0x41, 0x41, 0x00}, /* 0x5B '[' */
    {0x02, 0x04, 0x08, 0x10, 0x20}, /* 0x5C '\\' */
    {0x00, 0x41, 0x41, 0x7F, 0x00}, /* 0x5D ']' */
    {0x04, 0x02, 0x01, 0x02, 0x04}, /* 0x5E '^' */
    {0x40, 0x40, 0x40, 0x40, 0x40}, /* 0x5F '_' */
    {0x00, 0x01, 0x02, 0x04, 0x00}, /* 0x60 '`' */
    {0x20, 0x54, 0x54, 0x54, 0x78}, /* 0x61 'a' */
    {0x7F, 0x48, 0x44, 0x44, 0x38}, /* 0x62 'b' */
    {0x38, 0x44, 0x44, 0x44, 0x20}, /* 0x63 'c' */
    {0x38, 0x44, 0x44, 0x48, 0x7F}, /* 0x64 'd' */
    {0x38, 0x54, 0x54, 0x54, 0x18}, /* 0x65 'e' */
    {0x08, 0x7E, 0x09, 0x01, 0x02}, /* 0x66 'f' */
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, /* 0x67 'g' */
    {0x7F, 0x08, 0x04, 0x04, 0x78}, /* 0x68 'h' */
    {0x00, 0x44, 0x7D, 0x40, 0x00}, /* 0x69 'i' */
    {0x20, 0x40, 0x44, 0x3D, 0x00}, /* 0x6A 'j' */
    {0x7F, 0x10, 0x28, 0x44, 0x00}, /* 0x6B 'k' */
    {0x00, 0x41, 0x7F, 0x40, 0x00}, /* 0x6C 'l' */
    {0x7C, 0x04, 0x18, 0x04, 0x78}, /* 0x6D 'm' */
    {0x7C, 0x08, 0x04, 0x04, 0x78}, /* 0x6E 'n' */
    {0x38, 0x44, 0x44, 0x44, 0x38}, /* 0x6F 'o' */
    {0x7C, 0x14, 0x14, 0x14, 0x08}, /* 0x70 'p' */
    {0x08, 0x14, 0x14, 0x18, 0x7C}, /* 0x71 'q' */
    {0x7C, 0x08, 0x04, 0x04, 0x08}, /* 0x72 'r' */
    {0x48, 0x54, 0x54, 0x54, 0x20}, /* 0x73 's' */
    {0x04, 0x3F, 0x44, 0x40, 0x20}, /* 0x74 't' */
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, /* 0x75 'u' */
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, /* 0x76 'v' */
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, /* 0x77 'w' */
    {0x44, 0x28, 0x10, 0x28, 0x44}, /* 0x78 'x' */
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, /* 0x79 'y' */
    {0x44, 0x64, 0x54, 0x4C, 0x44}, /* 0x7A 'z' */
    {0x00, 0x08, 0x36, 0x41, 0x00}, /* 0x7B '{' */
    {0x00, 0x00, 0x7F, 0x00, 0x00}, /* 0x7C '|' */
    {0x00, 0x41, 0x36, 0x08, 0x00}, /* 0x7D '}' */
    {0x10, 0x08, 0x08, 0x10, 0x08}  /* 0x7E '~' */
};

class Arduino_DataBus {
public:
    virtual ~Arduino_DataBus() {}
};

class Arduino_ESP32LCD8 : public Arduino_DataBus {
public:
    Arduino_ESP32LCD8(int8_t dc, int8_t cs, int8_t wr, int8_t rd,
                      int8_t d0, int8_t d1, int8_t d2, int8_t d3,
                      int8_t d4, int8_t d5, int8_t d6, int8_t d7) {
        (void)dc; (void)cs; (void)wr; (void)rd;
        (void)d0; (void)d1; (void)d2; (void)d3;
        (void)d4; (void)d5; (void)d6; (void)d7;
    }
};

class Arduino_G {
public:
    virtual ~Arduino_G() {}
};

class Arduino_ST7789 : public Arduino_G {
public:
    Arduino_ST7789(Arduino_DataBus *bus, int8_t rst, uint8_t r, bool ips) {
        (void)bus; (void)rst; (void)r; (void)ips;
    }
};

class Arduino_GFX : public Print {
public:
    Arduino_GFX(int16_t w, int16_t h)
        : width_(w),
          height_(h),
          cursor_x_(0),
          cursor_y_(0),
          textcolor_(0xFFFF),
          textbgcolor_(0xFFFF),
          textsize_x_(1),
          textsize_y_(1),
          wrap_(true) {}

    virtual ~Arduino_GFX() {}

    virtual bool begin(int32_t speed = 0) {
        (void)speed;
        return true;
    }

    virtual void flush() {}
    virtual void displayOn() {}
    virtual void displayOff() {}
    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

    int16_t width() const { return width_; }
    int16_t height() const { return height_; }

    virtual void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (w <= 0 || h <= 0) return;
        int16_t x2 = x + w;
        int16_t y2 = y + h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x2 > width_) x2 = width_;
        if (y2 > height_) y2 = height_;
        for (int16_t py = y; py < y2; py++) {
            for (int16_t px = x; px < x2; px++) {
                drawPixel(px, py, color);
            }
        }
    }

    virtual void fillScreen(uint16_t color) {
        fillRect(0, 0, width_, height_, color);
    }

    virtual void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        fillRect(x, y, w, 1, color);
    }

    virtual void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        fillRect(x, y, 1, h, color);
    }

    virtual void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        if (w <= 0 || h <= 0) return;
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }

    virtual void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        if (r < 0) return;
        for (int16_t dy = -r; dy <= r; dy++) {
            for (int16_t dx = -r; dx <= r; dx++) {
                if (dx * dx + dy * dy <= r * r) {
                    drawPixel(x0 + dx, y0 + dy, color);
                }
            }
        }
    }

    virtual void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, uint16_t color) {
        if (w <= 0 || h <= 0) return;
        if (r <= 0) {
            fillRect(x, y, w, h, color);
            return;
        }
        int16_t max_r = (w < h ? w : h) / 2;
        if (r > max_r) r = max_r;
        fillRect(x + r, y, w - 2 * r, h, color);
        fillRect(x, y + r, r, h - 2 * r, color);
        fillRect(x + w - r, y + r, r, h - 2 * r, color);
        for (int16_t dy = 0; dy < r; dy++) {
            for (int16_t dx = 0; dx < r; dx++) {
                int16_t ox = r - 1 - dx;
                int16_t oy = r - 1 - dy;
                if (ox * ox + oy * oy <= r * r) {
                    drawPixel(x + dx, y + dy, color);
                    drawPixel(x + w - 1 - dx, y + dy, color);
                    drawPixel(x + dx, y + h - 1 - dy, color);
                    drawPixel(x + w - 1 - dx, y + h - 1 - dy, color);
                }
            }
        }
    }

    virtual void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) {
        if (!bitmap || w <= 0 || h <= 0) return;
        for (int16_t row = 0; row < h; row++) {
            int16_t py = y + row;
            if (py < 0 || py >= height_) continue;
            for (int16_t col = 0; col < w; col++) {
                int16_t px = x + col;
                if (px < 0 || px >= width_) continue;
                drawPixel(px, py, bitmap[row * w + col]);
            }
        }
    }

    void draw16bitRGBBitmap(int16_t x, int16_t y, uint16_t *bitmap, int16_t w, int16_t h) {
        draw16bitRGBBitmap(x, y, (const uint16_t *)bitmap, w, h);
    }

    void setCursor(int16_t x, int16_t y) {
        cursor_x_ = x;
        cursor_y_ = y;
    }

    void setTextSize(uint8_t s) {
        setTextSize(s, s);
    }

    void setTextSize(uint8_t sx, uint8_t sy) {
        textsize_x_ = (sx > 0) ? sx : 1;
        textsize_y_ = (sy > 0) ? sy : 1;
    }

    void setTextColor(uint16_t c) {
        textcolor_ = c;
        textbgcolor_ = c;
    }

    void setTextColor(uint16_t c, uint16_t bg) {
        textcolor_ = c;
        textbgcolor_ = bg;
    }

    void setTextWrap(bool w) {
        wrap_ = w;
    }

    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg) {
        drawChar(x, y, c, color, bg, textsize_x_, textsize_y_);
    }

    void drawChar(int16_t x, int16_t y, unsigned char c, uint16_t color, uint16_t bg, uint8_t sx, uint8_t sy) {
        if (sx == 0) sx = 1;
        if (sy == 0) sy = 1;
        const uint8_t *glyph = kSc23Font5x7[(c >= 0x20 && c <= 0x7E) ? (c - 0x20) : ('?' - 0x20)];
        for (int8_t col = 0; col < 5; col++) {
            uint8_t line = glyph[col];
            for (int8_t row = 0; row < 8; row++, line >>= 1) {
                if (row < 7 && (line & 0x01)) {
                    if (sx == 1 && sy == 1) {
                        drawPixel(x + col, y + row, color);
                    } else {
                        fillRect(x + col * sx, y + row * sy, sx, sy, color);
                    }
                } else if (bg != color) {
                    if (sx == 1 && sy == 1) {
                        drawPixel(x + col, y + row, bg);
                    } else {
                        fillRect(x + col * sx, y + row * sy, sx, sy, bg);
                    }
                }
            }
        }
        if (bg != color) {
            fillRect(x + 5 * sx, y, sx, 8 * sy, bg);
        }
    }

    size_t write(uint8_t c) override {
        if (c == '\n') {
            cursor_x_ = 0;
            cursor_y_ += (int16_t)textsize_y_ * 8;
        } else if (c != '\r') {
            if (wrap_ && ((cursor_x_ + (int16_t)textsize_x_ * 6) > width_)) {
                cursor_x_ = 0;
                cursor_y_ += (int16_t)textsize_y_ * 8;
            }
            drawChar(cursor_x_, cursor_y_, c, textcolor_, textbgcolor_, textsize_x_, textsize_y_);
            cursor_x_ += (int16_t)textsize_x_ * 6;
        }
        return 1;
    }

    void getTextBounds(const char *str, int16_t x, int16_t y,
                       int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) {
        if (!x1 || !y1 || !w || !h) return;
        *x1 = x;
        *y1 = y;
        *w = 0;
        *h = 0;
        if (!str || !*str) return;

        int16_t cx = x;
        int16_t cy = y;
        int16_t minx = width_, miny = height_, maxx = -1, maxy = -1;
        int16_t cw = (int16_t)textsize_x_ * 6;
        int16_t ch = (int16_t)textsize_y_ * 8;

        for (const char *p = str; *p; p++) {
            char c = *p;
            if (c == '\n') {
                cx = 0;
                cy += ch;
            } else if (c != '\r') {
                if (wrap_ && (cx + cw > width_)) {
                    cx = 0;
                    cy += ch;
                }
                int16_t x2 = cx + cw - 1;
                int16_t y2 = cy + ch - 1;
                if (cx < minx) minx = cx;
                if (cy < miny) miny = cy;
                if (x2 > maxx) maxx = x2;
                if (y2 > maxy) maxy = y2;
                cx += cw;
            }
        }
        if (maxx >= minx && maxy >= miny) {
            *x1 = minx;
            *y1 = miny;
            *w = (uint16_t)(maxx - minx + 1);
            *h = (uint16_t)(maxy - miny + 1);
        }
    }

    void getTextBounds(const String &str, int16_t x, int16_t y,
                       int16_t *x1, int16_t *y1, uint16_t *w, uint16_t *h) {
        getTextBounds(str.c_str(), x, y, x1, y1, w, h);
    }

protected:
    int16_t width_;
    int16_t height_;
    int16_t cursor_x_;
    int16_t cursor_y_;
    uint16_t textcolor_;
    uint16_t textbgcolor_;
    uint8_t textsize_x_;
    uint8_t textsize_y_;
    bool wrap_;
};

class Arduino_Canvas : public Arduino_GFX {
public:
    Arduino_Canvas(int16_t w, int16_t h, Arduino_G *output)
        : Arduino_GFX(w, h), display_on_(true) {
        (void)output;
        buf_ = (uint16_t *)calloc((size_t)w * (size_t)h, sizeof(uint16_t));
    }

    ~Arduino_Canvas() override {
        free(buf_);
    }

    bool begin(int32_t speed = 0) override {
        (void)speed;
        if (buf_) {
            memset(buf_, 0, (size_t)width_ * (size_t)height_ * sizeof(uint16_t));
        }
        return buf_ != nullptr;
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (!buf_ || x < 0 || y < 0 || x >= width_ || y >= height_) return;
        buf_[(size_t)y * (size_t)width_ + (size_t)x] = color;
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) override {
        if (!buf_ || w <= 0 || h <= 0) return;
        int16_t x2 = x + w;
        int16_t y2 = y + h;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x2 > width_) x2 = width_;
        if (y2 > height_) y2 = height_;
        for (int16_t py = y; py < y2; py++) {
            uint16_t *row = &buf_[(size_t)py * (size_t)width_ + (size_t)x];
            for (int16_t px = x; px < x2; px++) {
                *row++ = color;
            }
        }
    }

    void draw16bitRGBBitmap(int16_t x, int16_t y, const uint16_t *bitmap, int16_t w, int16_t h) override {
        if (!buf_ || !bitmap || w <= 0 || h <= 0) return;
        if (x == 0 && y == 0 && w == width_ && h == height_) {
            memcpy(buf_, bitmap, (size_t)width_ * (size_t)height_ * sizeof(uint16_t));
            return;
        }
        Arduino_GFX::draw16bitRGBBitmap(x, y, bitmap, w, h);
    }

    void flush() override {
        uint16_t *dst = platform_emu_framebuffer_ptr();
        if (dst && buf_ && display_on_) {
            memcpy(dst, buf_, (size_t)width_ * (size_t)height_ * sizeof(uint16_t));
        } else if (dst && !display_on_) {
            memset(dst, 0, (size_t)width_ * (size_t)height_ * sizeof(uint16_t));
        }
        sc23_on_gfx_flush();
    }

    void displayOn() override { display_on_ = true; }
    void displayOff() override { display_on_ = false; }
    uint16_t *getFramebuffer() { return buf_; }

private:
    uint16_t *buf_;
    bool display_on_;
};

#endif /* SC23_ARDUINO_GFX_LIBRARY_H */
