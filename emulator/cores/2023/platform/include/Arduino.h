#ifndef SC23_ARDUINO_H
#define SC23_ARDUINO_H

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus
#include <algorithm>
#include <string>
#endif

#ifndef ARDUINO
#define ARDUINO 10819
#endif

#define IRAM_ATTR
#define PROGMEM
#define PGM_P const char *
#define PSTR(s) (s)
#define memcpy_P memcpy
#define pgm_read_byte(addr) (*(const uint8_t *)(addr))
#define pgm_read_word(addr) (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))

#define LOW  0x0
#define HIGH 0x1

#define INPUT        0x01
#define OUTPUT       0x02
#define INPUT_PULLUP 0x05

#define RISING  0x01
#define FALLING 0x02
#define CHANGE  0x03

#define DEC 10
#define HEX 16
#define OCT 8
#define BIN 2

#define ESP_OK 0
#define MALLOC_CAP_DMA 1
#define ESP_MAC_WIFI_STA 0
#define ARDUINO_RUNNING_CORE 1

typedef int esp_err_t;
typedef uint8_t byte;
typedef bool boolean;
typedef unsigned int uint;
typedef unsigned long ulong;

typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef void (*TaskFunction_t)(void *);
typedef void (*esp_alloc_failed_hook_t)(size_t, uint32_t, const char *);

#ifdef __cplusplus
extern "C" {
#endif

uint32_t millis(void);
uint32_t micros(void);
void delay(uint32_t ms);
void delayMicroseconds(uint32_t us);
void yield(void);

void pinMode(int pin, int mode);
void digitalWrite(int pin, int val);
int digitalRead(int pin);
int analogRead(int pin);
void analogWrite(int pin, int val);

void attachInterrupt(int pin, void (*isr)(void), int mode);
void detachInterrupt(int pin);

void randomSeed(unsigned long seed);

void heap_caps_malloc_extmem_enable(size_t limit);
esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t cb);
void heap_caps_print_heap_info(uint32_t caps);
void esp_backtrace_print(int depth);
uint32_t esp_get_free_heap_size(void);
uint32_t esp_get_free_internal_heap_size(void);
uint32_t esp_get_minimum_free_heap_size(void);
esp_err_t esp_read_mac(uint8_t *mac, int type);

TickType_t xTaskGetTickCount(void);
int xTaskCreateUniversal(TaskFunction_t fn,
                         const char *name,
                         uint32_t stack_depth,
                         void *param,
                         int priority,
                         TaskHandle_t *out_handle,
                         int core_id);
void xTaskDelayUntil(TickType_t *prev_wake, TickType_t increment_ms);

void setup(void);
void loop(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

long random(long howbig);
long random(long howsmall, long howbig);

class String {
public:
    String() : buf_() {}
    String(const char *cstr) : buf_(cstr ? cstr : "") {}
    String(const String &other) : buf_(other.buf_) {}
    String(String &&other) noexcept : buf_(std::move(other.buf_)) {}
    explicit String(char c) : buf_(1, c) {}

    String(int value, int base = 10) { initSigned((long long)value, base); }
    String(unsigned int value, int base = 10) { initUnsigned((unsigned long long)value, base); }
    String(long value, int base = 10) { initSigned((long long)value, base); }
    String(unsigned long value, int base = 10) { initUnsigned((unsigned long long)value, base); }
    String(long long value, int base = 10) { initSigned(value, base); }
    String(unsigned long long value, int base = 10) { initUnsigned(value, base); }

    String(float value, int decimalPlaces = 2) { initFloat((double)value, decimalPlaces); }
    String(double value, int decimalPlaces = 2) { initFloat(value, decimalPlaces); }

    String &operator=(const String &rhs) {
        buf_ = rhs.buf_;
        return *this;
    }
    String &operator=(String &&rhs) noexcept {
        buf_ = std::move(rhs.buf_);
        return *this;
    }
    String &operator=(const char *cstr) {
        buf_ = cstr ? cstr : "";
        return *this;
    }

    const char *c_str() const { return buf_.c_str(); }
    const char *data() const { return buf_.data(); }
    size_t length() const { return buf_.size(); }
    size_t size() const { return buf_.size(); }
    bool isEmpty() const { return buf_.empty(); }
    unsigned char reserve(unsigned int sz) { buf_.reserve(sz); return 1; }

    char charAt(unsigned int idx) const {
        return (idx < buf_.size()) ? buf_[idx] : '\0';
    }
    void setCharAt(unsigned int idx, char c) {
        if (idx < buf_.size()) buf_[idx] = c;
    }
    char operator[](unsigned int idx) const { return charAt(idx); }
    char &operator[](unsigned int idx) { return buf_[idx]; }

    unsigned char concat(const String &s) {
        buf_.append(s.buf_);
        return 1;
    }
    unsigned char concat(const char *cstr) {
        if (cstr) buf_.append(cstr);
        return 1;
    }
    unsigned char concat(char c) {
        buf_.push_back(c);
        return 1;
    }
    unsigned char concat(int num) { return concat(String(num)); }
    unsigned char concat(unsigned int num) { return concat(String(num)); }
    unsigned char concat(long num) { return concat(String(num)); }
    unsigned char concat(unsigned long num) { return concat(String(num)); }
    unsigned char concat(long long num) { return concat(String(num)); }
    unsigned char concat(unsigned long long num) { return concat(String(num)); }
    unsigned char concat(float num) { return concat(String(num)); }
    unsigned char concat(double num) { return concat(String(num)); }

    void remove(unsigned int index) {
        if (index < buf_.size()) buf_.erase(index);
    }
    void remove(unsigned int index, unsigned int count) {
        if (index < buf_.size()) buf_.erase(index, count);
    }

    String substring(unsigned int beginIndex) const {
        if (beginIndex >= buf_.size()) return String();
        return String(buf_.substr(beginIndex).c_str());
    }
    String substring(unsigned int beginIndex, unsigned int endIndex) const {
        if (beginIndex >= buf_.size() || endIndex <= beginIndex) return String();
        if (endIndex > buf_.size()) endIndex = (unsigned int)buf_.size();
        return String(buf_.substr(beginIndex, endIndex - beginIndex).c_str());
    }

    long toInt() const {
        return strtol(buf_.c_str(), nullptr, 10);
    }
    float toFloat() const {
        return strtof(buf_.c_str(), nullptr);
    }

    bool equals(const String &s) const { return buf_ == s.buf_; }
    bool equals(const char *cstr) const { return buf_ == (cstr ? cstr : ""); }

    String &operator+=(const String &rhs) { concat(rhs); return *this; }
    String &operator+=(const char *cstr) { concat(cstr); return *this; }
    String &operator+=(char c) { concat(c); return *this; }
    String &operator+=(int num) { concat(num); return *this; }
    String &operator+=(unsigned int num) { concat(num); return *this; }
    String &operator+=(long num) { concat(num); return *this; }
    String &operator+=(unsigned long num) { concat(num); return *this; }
    String &operator+=(long long num) { concat(num); return *this; }
    String &operator+=(unsigned long long num) { concat(num); return *this; }

    friend String operator+(const String &lhs, const String &rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, const char *rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const char *lhs, const String &rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, char rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, int rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, unsigned int rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, long rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, unsigned long rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, long long rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }
    friend String operator+(const String &lhs, unsigned long long rhs) {
        String out(lhs);
        out.concat(rhs);
        return out;
    }

    bool operator==(const String &rhs) const { return buf_ == rhs.buf_; }
    bool operator==(const char *rhs) const { return buf_ == (rhs ? rhs : ""); }
    friend bool operator==(const char *lhs, const String &rhs) { return rhs == lhs; }
    bool operator!=(const String &rhs) const { return !(*this == rhs); }
    bool operator!=(const char *rhs) const { return !(*this == rhs); }
    friend bool operator!=(const char *lhs, const String &rhs) { return !(rhs == lhs); }
    bool operator<(const String &rhs) const { return buf_ < rhs.buf_; }
    bool operator>(const String &rhs) const { return buf_ > rhs.buf_; }

private:
    std::string buf_;

    static void formatUnsignedBase(unsigned long long v, int base, std::string &out) {
        if (base < 2 || base > 36) base = 10;
        char tmp[72];
        int pos = 0;
        const char *digits = "0123456789abcdef";
        do {
            tmp[pos++] = digits[v % (unsigned)base];
            v /= (unsigned)base;
        } while (v > 0 && pos < 70);
        out.clear();
        while (pos > 0) out.push_back(tmp[--pos]);
    }

    void initUnsigned(unsigned long long v, int base) {
        formatUnsignedBase(v, base, buf_);
    }

    void initSigned(long long v, int base) {
        if (base == 10 && v < 0) {
            std::string mag;
            formatUnsignedBase((unsigned long long)(-(v + 1)) + 1ULL, 10, mag);
            buf_ = "-" + mag;
        } else {
            formatUnsignedBase((unsigned long long)v, base, buf_);
        }
    }

    void initFloat(double v, int decimalPlaces) {
        if (decimalPlaces < 0) decimalPlaces = 0;
        if (decimalPlaces > 10) decimalPlaces = 10;
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%.*f", decimalPlaces, v);
        buf_ = tmp;
    }
};

class IPAddress {
public:
    IPAddress() : octets_{10, 23, 0, 42} {}
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : octets_{a, b, c, d} {}

    String toString() const {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u", octets_[0], octets_[1], octets_[2], octets_[3]);
        return String(tmp);
    }

private:
    uint8_t octets_[4];
};

class Print {
public:
    virtual ~Print() {}
    virtual size_t write(uint8_t b) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        if (!buffer) return 0;
        for (size_t i = 0; i < size; i++) {
            n += write(buffer[i]);
        }
        return n;
    }

    size_t print(const String &s) { return write((const uint8_t *)s.c_str(), s.length()); }
    size_t print(const char *s) { return s ? write((const uint8_t *)s, strlen(s)) : 0; }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v, int base = DEC) { return print(String(v, base)); }
    size_t print(unsigned int v, int base = DEC) { return print(String(v, base)); }
    size_t print(long v, int base = DEC) { return print(String(v, base)); }
    size_t print(unsigned long v, int base = DEC) { return print(String(v, base)); }
    size_t print(long long v, int base = DEC) { return print(String(v, base)); }
    size_t print(unsigned long long v, int base = DEC) { return print(String(v, base)); }
    size_t print(float v, int digits = 2) { return print(String(v, digits)); }
    size_t print(double v, int digits = 2) { return print(String(v, digits)); }
    size_t print(const IPAddress &ip) { return print(ip.toString()); }

    size_t println() { return print("\n"); }
    size_t println(const String &s) { return print(s) + println(); }
    size_t println(const char *s) { return print(s) + println(); }
    size_t println(char c) { return print(c) + println(); }
    size_t println(int v, int base = DEC) { return print(v, base) + println(); }
    size_t println(unsigned int v, int base = DEC) { return print(v, base) + println(); }
    size_t println(long v, int base = DEC) { return print(v, base) + println(); }
    size_t println(unsigned long v, int base = DEC) { return print(v, base) + println(); }
    size_t println(long long v, int base = DEC) { return print(v, base) + println(); }
    size_t println(unsigned long long v, int base = DEC) { return print(v, base) + println(); }
    size_t println(float v, int digits = 2) { return print(v, digits) + println(); }
    size_t println(double v, int digits = 2) { return print(v, digits) + println(); }
    size_t println(const IPAddress &ip) { return print(ip) + println(); }
};

class HardwareSerial : public Print {
public:
    using Print::write;
    void begin(unsigned long baud = 115200) { (void)baud; }
    size_t write(uint8_t b) override;
    int printf(const char *fmt, ...) {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n > 0) {
            write((const uint8_t *)buf, (size_t)std::min<int>(n, sizeof(buf) - 1));
        }
        return n;
    }
};

extern HardwareSerial Serial;

class EspClass {
public:
    void restart();
};

extern EspClass ESP;

#endif /* __cplusplus */

#endif /* SC23_ARDUINO_H */
