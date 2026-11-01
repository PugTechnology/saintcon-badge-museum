#ifndef SC23_FASTLED_H
#define SC23_FASTLED_H

#include <stdint.h>
#include "../emu_2023.h"

struct CRGB {
    uint8_t r;
    uint8_t g;
    uint8_t b;

    typedef enum {
        Black  = 0x000000,
        Blue   = 0x0000FF,
        Cyan   = 0x00FFFF,
        Green  = 0x008000,
        Orange = 0xFFA500,
        Pink   = 0xFFC0CB,
        Purple = 0x800080,
        Red    = 0xFF0000,
        White  = 0xFFFFFF,
        Yellow = 0xFFFF00
    } HTMLColorCode;

    constexpr CRGB() : r(0), g(0), b(0) {}
    constexpr CRGB(uint8_t ir, uint8_t ig, uint8_t ib) : r(ir), g(ig), b(ib) {}
    constexpr CRGB(uint32_t colorcode)
        : r((uint8_t)((colorcode >> 16) & 0xFFu)),
          g((uint8_t)((colorcode >> 8) & 0xFFu)),
          b((uint8_t)(colorcode & 0xFFu)) {}

    CRGB &operator=(uint32_t colorcode) {
        r = (uint8_t)((colorcode >> 16) & 0xFFu);
        g = (uint8_t)((colorcode >> 8) & 0xFFu);
        b = (uint8_t)(colorcode & 0xFFu);
        return *this;
    }

    bool operator!() const {
        return r == 0 && g == 0 && b == 0;
    }

    bool operator==(const CRGB &rhs) const {
        return r == rhs.r && g == rhs.g && b == rhs.b;
    }

    bool operator!=(const CRGB &rhs) const {
        return !(*this == rhs);
    }
};

enum EChipsets {
    NEOPIXEL = 0
};

class CFastLED {
public:
    CFastLED() : leds_(nullptr), count_(0), brightness_(255) {}

    template <EChipsets CHIPSET, uint8_t DATA_PIN>
    void addLeds(CRGB *data, int nLedsOrOffset) {
        (void)DATA_PIN;
        leds_ = data;
        count_ = nLedsOrOffset;
        sc23_sync_leds();
    }

    void setBrightness(uint8_t scale) {
        brightness_ = scale;
        sc23_sync_leds();
    }

    uint8_t getBrightness() const { return brightness_; }
    CRGB *leds() const { return leds_; }
    int size() const { return count_; }

    void show() {
        sc23_sync_leds();
    }

private:
    CRGB *leds_;
    int count_;
    uint8_t brightness_;
};

extern CFastLED FastLED;

#endif /* SC23_FASTLED_H */
