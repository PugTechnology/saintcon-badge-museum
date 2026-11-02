#ifndef SC21_ATMEL_START_PINS_H
#define SC21_ATMEL_START_PINS_H

#include <stdbool.h>
#include <stdint.h>

enum gpio_direction {
    GPIO_DIRECTION_OFF = 0,
    GPIO_DIRECTION_IN  = 1,
    GPIO_DIRECTION_OUT = 2,
};

#define GPIO_PORTA 0u
#define GPIO_PORTB 1u
#define GPIO(port, pin) ((((uint32_t)(port)) << 5) | ((uint32_t)(pin) & 0x1Fu))

/* Upstream pin assignments on ATSAME53J18A */
#define PIN_PA08 GPIO(GPIO_PORTA, 8)
#define PIN_PA09 GPIO(GPIO_PORTA, 9)
#define PIN_PA10 GPIO(GPIO_PORTA, 10)
#define PIN_PA11 GPIO(GPIO_PORTA, 11)
#define PIN_PA12 GPIO(GPIO_PORTA, 12)
#define PIN_PA13 GPIO(GPIO_PORTA, 13)
#define PIN_PA14 GPIO(GPIO_PORTA, 14) /* NFC_CS_PIN */
#define PIN_PA15 GPIO(GPIO_PORTA, 15)
#define PIN_PA16 GPIO(GPIO_PORTA, 16) /* I2C_0 SDA */
#define PIN_PA17 GPIO(GPIO_PORTA, 17) /* I2C_0 SCL */
#define PIN_PA21 GPIO(GPIO_PORTA, 21) /* TCC0 WO[1] Green LED */
#define PIN_PA22 GPIO(GPIO_PORTA, 22) /* TCC0 WO[2] Red LED */
#define PIN_PA23 GPIO(GPIO_PORTA, 23) /* TCC0 WO[3] Blue LED */
#define PIN_PA27 GPIO(GPIO_PORTA, 27) /* SW3 Back tactile button (EIC_EXTINT11) */

#define PIN_PB08 GPIO(GPIO_PORTB, 8)  /* NFC_IRQ_IN_PIN */
#define PIN_PB09 GPIO(GPIO_PORTB, 9)  /* NFC_IRQ_OUT_PIN (EIC_EXTINT9) */
#define PIN_PB10 GPIO(GPIO_PORTB, 10) /* QSPI SCK */
#define PIN_PB11 GPIO(GPIO_PORTB, 11) /* QSPI CS */
#define PIN_PB16 GPIO(GPIO_PORTB, 16) /* LCD_WR */
#define PIN_PB17 GPIO(GPIO_PORTB, 17) /* LCD_BL */
#define PIN_PB22 GPIO(GPIO_PORTB, 22) /* LCD_RST */
#define PIN_PB23 GPIO(GPIO_PORTB, 23) /* MB_CLK_PIN */
#define PIN_PB30 GPIO(GPIO_PORTB, 30) /* LCD_RS */
#define PIN_PB31 GPIO(GPIO_PORTB, 31) /* LCD_CS */

#define SC21_MAX_GPIO_PINS 64u

void gpio_set_pin_direction(const uint32_t pin, const enum gpio_direction direction);
void gpio_set_pin_level(const uint32_t pin, const bool level);
bool gpio_get_pin_level(const uint32_t pin);
void gpio_toggle_pin_level(const uint32_t pin);

#endif /* SC21_ATMEL_START_PINS_H */
