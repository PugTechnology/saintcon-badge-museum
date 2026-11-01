#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 700

#include "emu_2023.h"
#include "include/Arduino.h"
#include "include/Arduino_GFX_Library.h"
#include "include/AsyncHTTPSRequest_Generic.h"
#include "include/EEPROM.h"
#include "include/FastLED.h"
#include "include/HttpsOTAUpdate.h"
#include "include/esp_https_ota.h"
#include "include/IRrecv.h"
#include "include/IRsend.h"
#include "include/WiFi.h"
#include "include/Wire.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fiber.h>
#else
#include <ucontext.h>
#endif

/* ============================================================================
 * Constants & Global Peripheral Instances
 * ============================================================================ */

#define SC23_FB_W        320
#define SC23_FB_H        240
#define SC23_LED_COUNT   20
#define SC23_EEPROM_SIZE 128

#define SC23_MAX_TASKS     4
#define SC23_C_STACK_SZ    (128 * 1024)
#define SC23_AS_STACK_SZ   (64 * 1024)

/* Pin assignments from Firmware/Source/include/Keyboard.hpp & src/main.cpp */
#define PIN_JOY_UP      15
#define PIN_JOY_DOWN    18
#define PIN_JOY_LEFT    17
#define PIN_JOY_RIGHT   8
#define PIN_JOY_CENTER  16
#define PIN_BTN_B       39
#define PIN_BTN_A       40
#define PIN_IR_RX       43
#define PIN_BACKLIGHT   46

HardwareSerial Serial;
EspClass ESP;
CFastLED FastLED;
TwoWire Wire;
EEPROMClass EEPROM;
WiFiClass WiFi;
HttpsOTAClass HttpsOTA;
esp_https_ota_handle_t https_ota_handle = nullptr;

/* Normal badge firmware symbols (AttractScreen.cpp is intentionally omitted;
 * compiled with -DATTRACT_IDLE_MS=0UL so normal firmware behavior is active). */
volatile bool demoModeActive = false;
void AttractScreenRun(void) {}

/* ============================================================================
 * Emulator Buffers & Peripheral State
 * ============================================================================ */

static uint16_t s_framebuffer[SC23_FB_W * SC23_FB_H];
static uint8_t  s_leds_rgb[SC23_LED_COUNT * 3];
static uint8_t  s_eeprom[SC23_EEPROM_SIZE];
static int      s_eeprom_dirty = 0;
static uint8_t  s_backlight_raw = 128;
static uint32_t s_speed_mult = 1;
static bool     s_log_enabled = false;

static uint64_t s_now_us = 0;
static uint32_t s_same_ms_polls = 0;
static uint32_t s_last_polled_ms = 0;
static uint32_t s_prng_state = 0x20235343u;

/* GPIO & interrupt table */
static uint8_t s_pin_level[64];
static uint8_t s_pin_mode[64];
static void (*s_pin_isr[64])(void);
static uint8_t s_buttons_mask = 0;

/* Virtual IR receiver queue */
static bool     s_ir_rx_enabled = false;
static uint32_t s_ir_pending_code = 0;
static bool     s_ir_has_code = false;
static uint64_t s_ir_pulse_until_us = 0;

/* Virtual I2C mux + 4 minibadge slots + MC3419 fixed accelerometer */
struct Sc23VirtualMinibadge {
    bool inserted;
    bool is_redactd;
    char label[8];
    uint8_t serial[16];
};

static uint8_t s_i2c_mux_channel = 0;
static uint8_t s_accel_reg = 0;
static int16_t s_accel_xyz[3] = {0, 0, 16384};
static Sc23VirtualMinibadge s_mb_slots[4];

/* Canonical 30 minibadges matching src/badge.cpp + redactd special */
static const char *const kMinibadgeLabels[30] = {
    /* 0..3: simpleMinibadges */
    "IDBadge ", "Keyboard", "Glasses ", "Polo    ",
    /* 4..28: minibadgeList */
    "KlipperH", "Sword   ", "CustKeeb", "SCHoodie", "SCCap   ",
    "Pizza   ", "Mustache", "MtnDew  ", "Monster ", "T-Rex   ",
    "Coffee  ", "Stress  ", "1up     ", "CatVideo", "Cheetos ",
    "Taco    ", "Phone   ", "BlackHat", "WhiteHat", "Tinfoil ",
    "Lockpick", "Solderin", "Candy   ", "Hawaiian", "PepsiCan",
    /* 29: special 0x13 redactd minibadge */
    "redactd "
};

/* ============================================================================
 * Cooperative Fiber Scheduler (WASM emscripten_fiber / POSIX ucontext)
 * ============================================================================ */

enum Sc23TaskState {
    SC23_TASK_UNUSED = 0,
    SC23_TASK_READY,
    SC23_TASK_SLEEPING,
    SC23_TASK_ZOMBIE
};

struct Sc23Task {
    Sc23TaskState state;
    char name[24];
    TaskFunction_t entry;
    void *arg;
    bool started;
    uint64_t wake_us;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_t fiber;
#else
    ucontext_t ctx;
#endif
};

static Sc23Task s_tasks[SC23_MAX_TASKS];
alignas(16) static uint8_t s_task_c_stacks[SC23_MAX_TASKS][SC23_C_STACK_SZ];
static int s_current_task = -1;
static int s_rr_cursor = 0;
static int s_periodic_task_idx = -1;
static bool s_inited = false;

#ifdef __EMSCRIPTEN__
alignas(16) static uint8_t s_task_as_stacks[SC23_MAX_TASKS][SC23_AS_STACK_SZ];
alignas(16) static uint8_t s_sched_as_stack[32 * 1024];
static emscripten_fiber_t s_sched_fiber;
static bool s_sched_fiber_ready = false;
#else
static ucontext_t s_sched_ctx;
#endif

static void yield_to_sched(void) {
    int me = s_current_task;
    if (me < 0 || me >= SC23_MAX_TASKS) return;
#ifdef __EMSCRIPTEN__
    emscripten_fiber_swap(&s_tasks[me].fiber, &s_sched_fiber);
#else
    swapcontext(&s_tasks[me].ctx, &s_sched_ctx);
#endif
}

static void task_trampoline(int idx) {
    Sc23Task *t = &s_tasks[idx];
    if (t->entry) {
        t->entry(t->arg);
    }
    t->state = SC23_TASK_ZOMBIE;
    yield_to_sched();
}

#ifdef __EMSCRIPTEN__
static void task_fiber_entry(void *arg) {
    task_trampoline((int)(intptr_t)arg);
}
#else
static void task_ucontext_entry(int idx) {
    task_trampoline(idx);
}
#endif

static void init_task_ctx(int idx) {
#ifdef __EMSCRIPTEN__
    emscripten_fiber_init(
        &s_tasks[idx].fiber,
        task_fiber_entry,
        (void *)(intptr_t)idx,
        s_task_c_stacks[idx],
        SC23_C_STACK_SZ,
        s_task_as_stacks[idx],
        SC23_AS_STACK_SZ);
#else
    getcontext(&s_tasks[idx].ctx);
    s_tasks[idx].ctx.uc_stack.ss_sp = s_task_c_stacks[idx];
    s_tasks[idx].ctx.uc_stack.ss_size = SC23_C_STACK_SZ;
    s_tasks[idx].ctx.uc_link = &s_sched_ctx;
    makecontext(&s_tasks[idx].ctx, (void (*)(void))task_ucontext_entry, 1, idx);
#endif
}

static void arduino_main_task(void *arg) {
    (void)arg;
    setup();
    for (;;) {
        loop();
        sc23_sched_yield_ms(1);
    }
}

void sc23_sched_yield_ms(uint32_t ms) {
    if (s_current_task < 0) return;
    s_same_ms_polls = 0;
    if (ms == 0) {
        s_tasks[s_current_task].state = SC23_TASK_READY;
        yield_to_sched();
        return;
    }
    s_tasks[s_current_task].state = SC23_TASK_SLEEPING;
    s_tasks[s_current_task].wake_us = s_now_us + (uint64_t)ms * 1000ULL;
    yield_to_sched();
}

void sc23_on_gfx_flush(void) {
    sc23_sync_leds();
    sc23_sched_yield_ms(16);
}

static void sched_step_us(uint64_t delta_us) {
    uint64_t target_us = s_now_us + delta_us;

    while (true) {
        /* Wake sleeping tasks whose deadline has arrived */
        for (int i = 0; i < SC23_MAX_TASKS; i++) {
            if (s_tasks[i].state == SC23_TASK_SLEEPING && s_tasks[i].wake_us <= s_now_us) {
                s_tasks[i].state = SC23_TASK_READY;
            }
        }

        /* Pick next ready task round-robin */
        int next_idx = -1;
        for (int step = 0; step < SC23_MAX_TASKS; step++) {
            int idx = (s_rr_cursor + step) % SC23_MAX_TASKS;
            if (s_tasks[idx].state == SC23_TASK_READY) {
                next_idx = idx;
                s_rr_cursor = (idx + 1) % SC23_MAX_TASKS;
                break;
            }
        }

        if (next_idx >= 0) {
            if (!s_tasks[next_idx].started) {
                init_task_ctx(next_idx);
                s_tasks[next_idx].started = true;
            }
            s_current_task = next_idx;
#ifdef __EMSCRIPTEN__
            emscripten_fiber_swap(&s_sched_fiber, &s_tasks[next_idx].fiber);
#else
            swapcontext(&s_sched_ctx, &s_tasks[next_idx].ctx);
#endif
            s_current_task = -1;
            continue;
        }

        if (s_now_us >= target_us) {
            break;
        }

        /* Advance virtual clock to earliest task wakeup, capped at target_us */
        uint64_t earliest = target_us;
        for (int i = 0; i < SC23_MAX_TASKS; i++) {
            if (s_tasks[i].state == SC23_TASK_SLEEPING && s_tasks[i].wake_us < earliest) {
                earliest = s_tasks[i].wake_us;
            }
        }
        if (earliest < s_now_us) earliest = s_now_us;
        s_now_us = earliest;
    }

    if (s_ir_pulse_until_us > 0 && s_now_us >= s_ir_pulse_until_us) {
        s_ir_pulse_until_us = 0;
        s_pin_level[PIN_IR_RX] = HIGH;
    }
    sc23_sync_leds();
}

/* ============================================================================
 * Arduino Core & FreeRTOS Shim Functions
 * ============================================================================ */

extern "C" {

uint32_t millis(void) {
    uint32_t now_ms = (uint32_t)(s_now_us / 1000ULL);
    if (s_current_task >= 0) {
        if (now_ms == s_last_polled_ms) {
            s_same_ms_polls++;
            /* Yield cooperatively when tight loops (e.g. Battle::Run() waiting for
             * now > last + 100 ms) poll millis() without calling gfx->flush()/delay(). */
            if (s_same_ms_polls > 12) {
                s_same_ms_polls = 0;
                sc23_sched_yield_ms(5);
                now_ms = (uint32_t)(s_now_us / 1000ULL);
            }
        } else {
            s_last_polled_ms = now_ms;
            s_same_ms_polls = 0;
        }
    }
    return now_ms;
}

uint32_t micros(void) {
    return (uint32_t)(s_now_us & 0xFFFFFFFFULL);
}

void delay(uint32_t ms) {
    sc23_sched_yield_ms(ms > 0 ? ms : 1);
}

void delayMicroseconds(uint32_t us) {
    if (us >= 1000) {
        sc23_sched_yield_ms(us / 1000);
    } else {
        s_now_us += us;
    }
}

void yield(void) {
    sc23_sched_yield_ms(0);
}

void pinMode(int pin, int mode) {
    if (pin < 0 || pin >= 64) return;
    s_pin_mode[pin] = (uint8_t)mode;
    if (mode == INPUT_PULLUP) {
        /* Preserve active button depression if already held */
        s_pin_level[pin] = HIGH;
    }
}

void digitalWrite(int pin, int val) {
    if (pin < 0 || pin >= 64) return;
    uint8_t next = val ? HIGH : LOW;
    uint8_t prev = s_pin_level[pin];
    s_pin_level[pin] = next;
    if (prev != next && s_pin_isr[pin]) {
        s_pin_isr[pin]();
    }
}

int digitalRead(int pin) {
    if (pin < 0 || pin >= 64) return HIGH;
    return s_pin_level[pin];
}

int analogRead(int pin) {
    (void)pin;
    /* Battery voltage formula in main.cpp:400: ((analogRead(2) * 33) / 40960) -> ~3.30V at 4095 */
    return 4095;
}

void analogWrite(int pin, int val) {
    if (pin == PIN_BACKLIGHT) {
        int clamped = val < 0 ? 0 : (val > 255 ? 255 : val);
        sc23_set_backlight_raw((uint8_t)clamped);
    }
}

void attachInterrupt(int pin, void (*isr)(void), int mode) {
    (void)mode;
    if (pin >= 0 && pin < 64) {
        s_pin_isr[pin] = isr;
    }
}

void detachInterrupt(int pin) {
    if (pin >= 0 && pin < 64) {
        s_pin_isr[pin] = nullptr;
    }
}

static uint32_t next_prng(void) {
    uint32_t x = s_prng_state;
    if (x == 0) x = 0x20235343u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_prng_state = x;
    return x;
}

void randomSeed(unsigned long seed) {
    s_prng_state = (uint32_t)(seed ? seed : 0x20235343u);
    srand((unsigned int)s_prng_state);
}

void heap_caps_malloc_extmem_enable(size_t limit) { (void)limit; }

esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t cb) {
    (void)cb;
    return ESP_OK;
}

void heap_caps_print_heap_info(uint32_t caps) { (void)caps; }
void esp_backtrace_print(int depth) { (void)depth; }
uint32_t esp_get_free_heap_size(void) { return 4 * 1024 * 1024; }
uint32_t esp_get_free_internal_heap_size(void) { return 320 * 1024; }
uint32_t esp_get_minimum_free_heap_size(void) { return 256 * 1024; }

esp_err_t esp_read_mac(uint8_t *mac, int type) {
    (void)type;
    if (mac) {
        mac[0] = 0x20;
        mac[1] = 0x23;
        mac[2] = 0x53;
        mac[3] = 0x43;
        mac[4] = 0x00;
        mac[5] = 0x01;
    }
    return ESP_OK;
}

TickType_t xTaskGetTickCount(void) {
    return millis();
}

int xTaskCreateUniversal(TaskFunction_t fn,
                         const char *name,
                         uint32_t stack_depth,
                         void *param,
                         int priority,
                         TaskHandle_t *out_handle,
                         int core_id) {
    (void)stack_depth;
    (void)priority;
    (void)core_id;
    for (int i = 0; i < SC23_MAX_TASKS; i++) {
        if (s_tasks[i].state == SC23_TASK_UNUSED || s_tasks[i].state == SC23_TASK_ZOMBIE) {
            memset(&s_tasks[i], 0, sizeof(s_tasks[i]));
            s_tasks[i].state = SC23_TASK_READY;
            s_tasks[i].entry = fn;
            s_tasks[i].arg = param;
            s_tasks[i].started = false;
            snprintf(s_tasks[i].name, sizeof(s_tasks[i].name), "%s", name ? name : "task");
            if (name && strcmp(name, "periodic") == 0) {
                s_periodic_task_idx = i;
            }
            if (out_handle) {
                *out_handle = (TaskHandle_t)&s_tasks[i];
            }
            return 1;
        }
    }
    return 0;
}

void xTaskDelayUntil(TickType_t *prev_wake, TickType_t increment_ms) {
    TickType_t now = millis();
    sc23_sync_leds();
    if (!prev_wake) {
        sc23_sched_yield_ms(increment_ms);
        return;
    }
    TickType_t target = *prev_wake + increment_ms;
    *prev_wake = target;
    int32_t diff = (int32_t)(target - now);
    if (diff > 0) {
        sc23_sched_yield_ms((uint32_t)diff);
    } else {
        *prev_wake = now;
        sc23_sched_yield_ms(1);
    }
}

} /* extern "C" */

long random(long howbig) {
    if (howbig <= 0) return 0;
    return (long)(next_prng() % (uint32_t)howbig);
}

long random(long howsmall, long howbig) {
    if (howsmall >= howbig) return howsmall;
    long diff = howbig - howsmall;
    return howsmall + random(diff);
}

size_t HardwareSerial::write(uint8_t b) {
    if (s_log_enabled) {
        fputc(b, stderr);
    }
    return 1;
}

void EspClass::restart() {
    sc23_sched_yield_ms(100);
}

/* ============================================================================
 * FastLED & Backlight Scaling
 * ============================================================================ */

static inline uint8_t scale_led_channel(uint8_t c, uint8_t brightness) {
    if (c == 0 || brightness == 0) return 0;
    /* Firmware sets FastLED.setBrightness(25) (~10% duty cycle for blinding bare
     * SK6812MINI dies). Map non-zero LED channels with a perceptual boost so LED
     * colors glow faithfully on an sRGB display while Black stays strictly 0. */
    uint32_t eff_b = brightness < 180 ? (180u + ((uint32_t)brightness * 75u) / 180u) : brightness;
    uint32_t v = ((uint32_t)c * eff_b + 127u) / 255u;
    /* CRGB::Green is 0x008000 (128); boost mid-intensity status colors slightly */
    if (v > 0 && v < 210) {
        v = (v * 5u) / 4u;
    }
    return (uint8_t)(v > 255u ? 255u : v);
}

void sc23_sync_leds(void) {
    CRGB *src = FastLED.leds();
    int n = FastLED.size();
    uint8_t br = FastLED.getBrightness();
    if (!src || n <= 0) return;
    if (n > SC23_LED_COUNT) n = SC23_LED_COUNT;
    for (int i = 0; i < n; i++) {
        s_leds_rgb[i * 3 + 0] = scale_led_channel(src[i].r, br);
        s_leds_rgb[i * 3 + 1] = scale_led_channel(src[i].g, br);
        s_leds_rgb[i * 3 + 2] = scale_led_channel(src[i].b, br);
    }
}

void sc23_set_backlight_raw(uint8_t level) {
    s_backlight_raw = level;
}

void sc23_mark_eeprom_dirty(void) {
    s_eeprom_dirty = 1;
}

/* ============================================================================
 * EEPROM Implementation (persisted in localStorage['saintcon_2023_stock_eeprom'])
 * ============================================================================ */

bool EEPROMClass::begin(size_t size) {
    (void)size;
    return true;
}

String EEPROMClass::readString(int address) {
    if (address < 0 || address >= SC23_EEPROM_SIZE) return String();
    /* Uninitialized flash/EEPROM bytes (0xFF or 0x00) read as empty string */
    if (s_eeprom[address] == 0x00 || s_eeprom[address] == 0xFF) return String();
    char tmp[64];
    int pos = 0;
    for (int i = address; i < SC23_EEPROM_SIZE && pos < 62; i++) {
        uint8_t c = s_eeprom[i];
        if (c == 0x00 || c == 0xFF) break;
        tmp[pos++] = (char)c;
    }
    tmp[pos] = '\0';
    return String(tmp);
}

size_t EEPROMClass::writeString(int address, const String &value) {
    if (address < 0 || address >= SC23_EEPROM_SIZE) return 0;
    size_t max_len = (size_t)(SC23_EEPROM_SIZE - address - 1);
    size_t len = value.length();
    if (len > max_len) len = max_len;
    memcpy(&s_eeprom[address], value.c_str(), len);
    s_eeprom[address + len] = '\0';
    sc23_mark_eeprom_dirty();
    return len;
}

bool EEPROMClass::commit() {
    sc23_mark_eeprom_dirty();
    return true;
}

/* ============================================================================
 * Virtual I2C Bus: PCA9548A (0x70), 4x Minibadge Slots, MC3419 (0x4C), TFA9879 (0x6C)
 * ============================================================================ */

TwoWire::TwoWire() : tx_addr_(0), tx_len_(0), rx_len_(0), rx_pos_(0) {}

void TwoWire::setPins(int sda, int scl) { (void)sda; (void)scl; }
void TwoWire::setTimeOut(uint16_t timeoutMillis) { (void)timeoutMillis; }
bool TwoWire::begin() { return true; }

void TwoWire::beginTransmission(uint8_t address) {
    tx_addr_ = address;
    tx_len_ = 0;
}

size_t TwoWire::write(uint8_t data) {
    if (tx_len_ < (int)sizeof(tx_buf_)) {
        tx_buf_[tx_len_++] = data;
        return 1;
    }
    return 0;
}

uint8_t TwoWire::endTransmission(bool sendStop) {
    (void)sendStop;
    /* PCA9548A 8-channel I2C switch at 0x70 */
    if (tx_addr_ == 0x70) {
        if (tx_len_ > 0) {
            uint8_t mask = tx_buf_[0];
            for (uint8_t ch = 0; ch < 8; ch++) {
                if (mask & (1u << ch)) {
                    s_i2c_mux_channel = ch;
                    break;
                }
            }
        }
        return 0;
    }

    /* Minibadge slots 0..3 */
    if (s_i2c_mux_channel < 4) {
        const Sc23VirtualMinibadge &mb = s_mb_slots[s_i2c_mux_channel];
        if (!mb.inserted) {
            return 2; /* NACK when slot is empty */
        }
        if (tx_addr_ == 0x50) {
            return mb.is_redactd ? 2 : 0;
        }
        if (tx_addr_ == 0x58) {
            return mb.is_redactd ? 2 : 0;
        }
        if (tx_addr_ == 0x13) {
            return mb.is_redactd ? 0 : 2;
        }
        return 2;
    }

    /* Channel 5: MC3419 accelerometer at 0x4C */
    if (s_i2c_mux_channel == 5 && tx_addr_ == 0x4C) {
        if (tx_len_ > 0) {
            s_accel_reg = tx_buf_[0];
        }
        return 0;
    }

    /* Channel 6: TFA9879 I2S audio amplifier at 0x6C — silent no-op ACK */
    if (s_i2c_mux_channel == 6 && tx_addr_ == 0x6C) {
        return 0;
    }

    return 2;
}

uint8_t TwoWire::requestFrom(int address, int quantity) {
    rx_len_ = 0;
    rx_pos_ = 0;
    if (quantity <= 0) return 0;
    if (quantity > (int)sizeof(rx_buf_)) quantity = (int)sizeof(rx_buf_);
    memset(rx_buf_, 0, sizeof(rx_buf_));

    if (s_i2c_mux_channel < 4) {
        const Sc23VirtualMinibadge &mb = s_mb_slots[s_i2c_mux_channel];
        if (!mb.inserted) return 0;
        if (address == 0x50 && !mb.is_redactd) {
            int n = quantity < 8 ? quantity : 8;
            memcpy(rx_buf_, mb.label, (size_t)n);
            rx_len_ = quantity;
            return (uint8_t)rx_len_;
        }
        if ((address == 0x58 && !mb.is_redactd) || (address == 0x13 && mb.is_redactd)) {
            int n = quantity < 16 ? quantity : 16;
            memcpy(rx_buf_, mb.serial, (size_t)n);
            rx_len_ = quantity;
            return (uint8_t)rx_len_;
        }
        return 0;
    }

    if (s_i2c_mux_channel == 5 && address == 0x4C) {
        if (s_accel_reg == 0x0D && quantity >= 6) {
            rx_buf_[0] = (uint8_t)(s_accel_xyz[0] & 0xFF);
            rx_buf_[1] = (uint8_t)((s_accel_xyz[0] >> 8) & 0xFF);
            rx_buf_[2] = (uint8_t)(s_accel_xyz[1] & 0xFF);
            rx_buf_[3] = (uint8_t)((s_accel_xyz[1] >> 8) & 0xFF);
            rx_buf_[4] = (uint8_t)(s_accel_xyz[2] & 0xFF);
            rx_buf_[5] = (uint8_t)((s_accel_xyz[2] >> 8) & 0xFF);
        }
        rx_len_ = quantity;
        return (uint8_t)rx_len_;
    }

    return 0;
}

int TwoWire::available() const {
    return rx_len_ - rx_pos_;
}

int TwoWire::read() {
    if (rx_pos_ < rx_len_) {
        return rx_buf_[rx_pos_++];
    }
    return -1;
}

/* ============================================================================
 * Virtual IR Receiver / Transmitter
 * ============================================================================ */

void IRsend::sendNEC(uint64_t data, uint16_t nbits) {
    (void)data;
    (void)nbits;
}

void IRrecv::enableIRIn() {
    s_ir_rx_enabled = true;
}

void IRrecv::disableIRIn() {
    s_ir_rx_enabled = false;
    s_ir_has_code = false;
}

bool IRrecv::decode(decode_results *results) {
    if (!s_ir_rx_enabled || !s_ir_has_code || !results) {
        return false;
    }
    results->value = s_ir_pending_code;
    s_ir_has_code = false;
    return true;
}

/* ============================================================================
 * AsyncHTTPSRequest -> Fake Server Bridge (WASM JS / Native CLI)
 * ============================================================================ */

#ifdef __EMSCRIPTEN__
EM_JS(int, sc23_js_fake_server_request, (const char *method_ptr,
                                         const char *url_ptr,
                                         const char *hdrs_ptr,
                                         const char *body_ptr,
                                         char *out_body_ptr,
                                         int max_body_len), {
    try {
        const srv = (typeof window !== 'undefined' && window.__sc23FakeServer)
            ? window.__sc23FakeServer
            : (typeof globalThis !== 'undefined' ? globalThis.__sc23FakeServer : null);
        if (!srv || typeof srv.handleRequest !== 'function') {
            const fb = '{"status":false,"error":"Fake game server unavailable"}';
            stringToUTF8(fb, out_body_ptr, max_body_len);
            return 503;
        }
        const method = UTF8ToString(method_ptr);
        const url = UTF8ToString(url_ptr);
        const hdrs = UTF8ToString(hdrs_ptr);
        const body = body_ptr ? UTF8ToString(body_ptr) : "";
        const res = srv.handleRequest(method, url, hdrs, body) || {};
        const status = Number.isInteger(res.status) ? res.status : 200;
        const bodyStr = (typeof res.body === 'string') ? res.body : '{"status":false,"error":"Empty response"}';
        stringToUTF8(bodyStr, out_body_ptr, max_body_len);
        return status;
    } catch (err) {
        const msg = '{"status":false,"error":"Fake server exception"}';
        stringToUTF8(msg, out_body_ptr, max_body_len);
        return 500;
    }
});
#endif

int AsyncHTTPSRequest::readyState() {
    if (state_ == readyStateLoading && millis() >= complete_at_ms_) {
        state_ = readyStateDone;
    }
    return state_;
}

bool AsyncHTTPSRequest::open(const char *method, const char *url) {
    if (!method || !url) return false;
    method_ = method;
    url_ = url;
    headers_ = "";
    resp_body_ = "";
    http_code_ = 0;
    state_ = readyStateOpened;
    return true;
}

void AsyncHTTPSRequest::setReqHeader(const char *name, const char *value) {
    if (!name || !value) return;
    headers_ += name;
    headers_ += ": ";
    headers_ += value;
    headers_ += "\n";
}

bool AsyncHTTPSRequest::send() {
    return send(String());
}

bool AsyncHTTPSRequest::send(const String &body) {
    if (state_ != readyStateOpened) return false;

#ifdef __EMSCRIPTEN__
    const int kMaxResp = 65536;
    char *buf = (char *)calloc(1, (size_t)kMaxResp);
    if (!buf) return false;
    http_code_ = sc23_js_fake_server_request(
        method_.c_str(),
        url_.c_str(),
        headers_.c_str(),
        body.c_str(),
        buf,
        kMaxResp);
    resp_body_ = String(buf);
    free(buf);
#else
    char *out_body = nullptr;
    http_code_ = sc23_cli_fake_server_handle(
        method_.c_str(),
        url_.c_str(),
        headers_.c_str(),
        body.c_str(),
        &out_body);
    resp_body_ = String(out_body ? out_body : "{\"status\":false,\"error\":\"Empty CLI response\"}");
    free(out_body);
#endif

    state_ = readyStateLoading;
    complete_at_ms_ = millis() + 20;
    return true;
}

void AsyncHTTPSRequest::abort() {
    state_ = readyStateDone;
}

/* ============================================================================
 * Button Mapping & Museum Core ABI Exports
 * ============================================================================ */

static void apply_buttons_mask(uint8_t mask) {
    struct BtnPinMap {
        uint8_t bit;
        int pin;
    };
    static const BtnPinMap kMap[7] = {
        {0x01, PIN_JOY_UP},
        {0x02, PIN_JOY_DOWN},
        {0x04, PIN_JOY_LEFT},
        {0x08, PIN_JOY_RIGHT},
        {0x10, PIN_JOY_CENTER},
        {0x20, PIN_BTN_B},
        {0x40, PIN_BTN_A},
    };

    s_buttons_mask = mask;
    bool any_changed = false;
    void (*isr_to_call)(void) = nullptr;

    for (int i = 0; i < 7; i++) {
        uint8_t next_level = (mask & kMap[i].bit) ? LOW : HIGH;
        if (s_pin_level[kMap[i].pin] != next_level) {
            s_pin_level[kMap[i].pin] = next_level;
            any_changed = true;
            if (s_pin_isr[kMap[i].pin]) {
                isr_to_call = s_pin_isr[kMap[i].pin];
            }
        }
    }

    if (any_changed && isr_to_call) {
        isr_to_call();
    }
}

extern "C" {

uint16_t *platform_emu_framebuffer_ptr(void) { return s_framebuffer; }
uint8_t  *platform_emu_leds_ptr(void) { return s_leds_rgb; }
uint8_t  *platform_emu_eeprom_ptr(void) { return s_eeprom; }
void      platform_emu_set_log_enabled(bool enabled) { s_log_enabled = enabled; }

void emu_init(uint32_t seed) {
#ifdef __EMSCRIPTEN__
    if (!s_sched_fiber_ready) {
        emscripten_fiber_init_from_current_context(
            &s_sched_fiber, s_sched_as_stack, sizeof(s_sched_as_stack));
        s_sched_fiber_ready = true;
    }
#endif

    memset(s_framebuffer, 0, sizeof(s_framebuffer));
    memset(s_leds_rgb, 0, sizeof(s_leds_rgb));
    memset(s_pin_isr, 0, sizeof(s_pin_isr));
    memset(s_pin_mode, 0, sizeof(s_pin_mode));
    for (int i = 0; i < 64; i++) {
        s_pin_level[i] = HIGH;
    }

    s_backlight_raw = 128;
    s_speed_mult = 1;
    s_now_us = 0;
    s_same_ms_polls = 0;
    s_last_polled_ms = 0;
    s_buttons_mask = 0;
    s_ir_rx_enabled = false;
    s_ir_has_code = false;
    s_ir_pulse_until_us = 0;
    s_i2c_mux_channel = 0;
    s_periodic_task_idx = -1;
    randomSeed(seed ? seed : 0x20235343u);

    memset(s_tasks, 0, sizeof(s_tasks));
    s_current_task = -1;
    s_rr_cursor = 0;

    /* Task 0: Arduino setup() / loop() */
    s_tasks[0].state = SC23_TASK_READY;
    s_tasks[0].entry = arduino_main_task;
    s_tasks[0].arg = nullptr;
    s_tasks[0].started = false;
    snprintf(s_tasks[0].name, sizeof(s_tasks[0].name), "arduino");

    s_inited = true;

    /* Run initial boot slice so sclogo and initial LED state appear immediately */
    sched_step_us(20000ULL);
}

void emu_step(uint32_t dt_ms, uint8_t buttons) {
    if (!s_inited) {
        emu_init(0x20235343u);
    }
    apply_buttons_mask(buttons);
    if (dt_ms == 0) dt_ms = 1;
    if (dt_ms > 250) dt_ms = 250;
    uint64_t scaled_us = (uint64_t)dt_ms * 1000ULL * (uint64_t)s_speed_mult;
    sched_step_us(scaled_us);
}

uint16_t *emu_framebuffer(void) { return s_framebuffer; }
uint8_t  *emu_leds(void) { return s_leds_rgb; }

uint8_t emu_backlight(void) {
    /* Firmware uses 128 for full brightness, 25 for dimmed, 0 for screen-off */
    if (s_backlight_raw == 0) return 0;
    if (s_backlight_raw >= 128) return 255;
    uint32_t scaled = 48u + ((uint32_t)s_backlight_raw * 207u) / 128u;
    return (uint8_t)(scaled > 255u ? 255u : scaled);
}

uint8_t *emu_eeprom(void) { return s_eeprom; }

int emu_eeprom_dirty(void) {
    int was = s_eeprom_dirty;
    s_eeprom_dirty = 0;
    return was;
}

void emu_set_speed(uint32_t mult) {
    if (mult < 1) mult = 1;
    if (mult > 16) mult = 16;
    s_speed_mult = mult;
}

int emu_fb_width(void) { return SC23_FB_W; }
int emu_fb_height(void) { return SC23_FB_H; }
int emu_led_count(void) { return SC23_LED_COUNT; }
int emu_eeprom_size(void) { return SC23_EEPROM_SIZE; }

void emu_send_ir(uint32_t code) {
    s_ir_pending_code = code;
    s_ir_has_code = true;
    s_pin_level[PIN_IR_RX] = LOW;
    s_ir_pulse_until_us = s_now_us + 80000ULL;
}

void emu_set_accel(int16_t x, int16_t y, int16_t z) {
    s_accel_xyz[0] = x;
    s_accel_xyz[1] = y;
    s_accel_xyz[2] = z;
}

void emu_set_minibadge_label(int slot, const char *label8, int is_redactd) {
    if (slot < 0 || slot >= 4) return;
    Sc23VirtualMinibadge &mb = s_mb_slots[slot];
    memset(&mb, 0, sizeof(mb));
    if (!label8 || label8[0] == '\0') {
        mb.inserted = false;
    } else {
        mb.inserted = true;
        mb.is_redactd = (is_redactd != 0) || (strncmp(label8, "redactd", 7) == 0);
        memset(mb.label, ' ', 8);
        size_t n = strlen(label8);
        if (n > 8) n = 8;
        memcpy(mb.label, label8, n);
        mb.serial[0] = 0x20;
        mb.serial[1] = 0x23;
        mb.serial[2] = 0x53;
        mb.serial[3] = 0x43;
        mb.serial[4] = (uint8_t)(slot + 1);
        for (int i = 0; i < 8; i++) {
            mb.serial[5 + i] = (uint8_t)mb.label[i];
        }
        mb.serial[15] = 0xA5;
    }

    /* Wake periodicEvents immediately so ScanMinibadges() detects the slot change on next step */
    if (s_periodic_task_idx >= 0 && s_tasks[s_periodic_task_idx].state == SC23_TASK_SLEEPING) {
        s_tasks[s_periodic_task_idx].wake_us = s_now_us;
        s_tasks[s_periodic_task_idx].state = SC23_TASK_READY;
    }
}

void emu_set_minibadge(int slot, int catalog_index) {
    if (slot < 0 || slot >= 4) return;
    if (catalog_index < 0 || catalog_index >= 30) {
        emu_set_minibadge_label(slot, nullptr, 0);
    } else {
        emu_set_minibadge_label(slot, kMinibadgeLabels[catalog_index], catalog_index == 29 ? 1 : 0);
    }
}

} /* extern "C" */
