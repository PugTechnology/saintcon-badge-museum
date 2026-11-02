#ifndef SAINTCON_2020_AVR_INTERRUPT_H
#define SAINTCON_2020_AVR_INTERRUPT_H

#include "../emu_2020.h"

#define TIM1_COMPA_vect tim1_compa
#define PCINT1_vect     pcint1
#define ISR_CONCAT(a, b) a##b
#define ISR(vect) extern "C" void ISR_CONCAT(__avr_isr_, vect)(void)

#define sei() emu_2020_sei()
#define cli() emu_2020_cli()

#endif /* SAINTCON_2020_AVR_INTERRUPT_H */
