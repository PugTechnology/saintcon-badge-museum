#ifndef SAINTCON_2020_AVR_SLEEP_H
#define SAINTCON_2020_AVR_SLEEP_H

/*
 * main.cpp:77 calls `set_sleep_mode(SLEEP_MODE_PWR_DOWN)` during setup (alongside
 * `GIMSK |= (1<<PCIE1); PCMSK1 |= (1<<PCINT10);` at lines 78-79 and an empty
 * `ISR(PCINT1_vect){}` at lines 491-493), and main.cpp:29 defines `#define LONGPRESSTIME 2000`,
 * but neither `sleep_mode()` / `sleep_enable()` / `sleep_cpu()` nor `LONGPRESSTIME`
 * is ever called or referenced anywhere else in main.cpp.
 */
#define SLEEP_MODE_PWR_DOWN 0
#define set_sleep_mode(mode) ((void)(mode))

#endif /* SAINTCON_2020_AVR_SLEEP_H */
