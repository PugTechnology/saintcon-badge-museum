#ifndef __ICEBOOT_H
#define __ICEBOOT_H
/* FPGA_Init shim: on hardware, iceboot.c bit-bangs fpga.bin into the iCE40UP5K,
 * which acts solely as the 64x16 RGB LED matrix SPI receiver/row driver. */
void FPGA_Init(void);
#endif
