#pragma once

#include "pico/types.h"

typedef struct uart_inst {
    int id;
} uart_inst_t;

extern uart_inst_t emu_uart0_inst;
extern uart_inst_t emu_uart1_inst;

#define uart0 (&emu_uart0_inst)
#define uart1 (&emu_uart1_inst)

#ifdef __cplusplus
extern "C" {
#endif

uint uart_init(uart_inst_t *uart, uint baudrate);
void uart_set_irq_enables(uart_inst_t *uart, bool rx_has_data, bool tx_needs_data);
bool uart_is_readable(uart_inst_t *uart);
char uart_getc(uart_inst_t *uart);
void uart_putc(uart_inst_t *uart, char c);
void uart_write_blocking(uart_inst_t *uart, const uint8_t *src, size_t len);

#ifdef __cplusplus
}
#endif
