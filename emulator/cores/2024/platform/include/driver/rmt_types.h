#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rmt_channel_t *rmt_channel_handle_t;
typedef struct rmt_encoder_t *rmt_encoder_handle_t;

typedef enum {
    RMT_CLK_SRC_DEFAULT = 0,
} rmt_clock_source_t;

typedef union {
    struct {
        uint16_t duration0 : 15;
        uint16_t level0    : 1;
        uint16_t duration1 : 15;
        uint16_t level1    : 1;
    };
    uint32_t val;
} rmt_symbol_word_t;

typedef struct {
    uint32_t frequency_hz;
    float duty_cycle;
    struct {
        uint32_t polarity_active_low : 1;
        uint32_t always_on           : 1;
    } flags;
} rmt_carrier_config_t;

typedef struct {
    rmt_symbol_word_t *received_symbols;
    size_t num_symbols;
    struct {
        uint32_t is_last : 1;
    } flags;
} rmt_rx_done_event_data_t;

#ifdef __cplusplus
}
#endif
