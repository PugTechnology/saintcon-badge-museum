#pragma once

#include "pico/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct repeating_timer;
typedef bool (*repeating_timer_callback_t)(struct repeating_timer *rt);

struct repeating_timer {
    int64_t delay_us;
    repeating_timer_callback_t callback;
    void *user_data;
    int slot_id;
};

uint64_t time_us_64(void);
absolute_time_t get_absolute_time(void);
uint32_t to_ms_since_boot(absolute_time_t t);
int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to);
absolute_time_t make_timeout_time_ms(uint32_t ms);

void sleep_ms(uint32_t ms);
void sleep_us(uint64_t us);

bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t callback, void *user_data, struct repeating_timer *out);
bool cancel_repeating_timer(struct repeating_timer *timer);

#ifdef __cplusplus
}
#endif
