# SAINTCON 2019 Enigma Badge — Upstream Provenance & Licensing

## Upstream Repository

- **Repository**: <https://github.com/compukidmike/Saintcon2019>
- **Author**: compukidmike (<https://github.com/compukidmike>)
- **Pinned Commit**: `4ec59a1626dca09a0c75969f449a0ed3cca18bac` (`2020-01-25`, `"New Firmware"` — the post-conference at-home challenge firmware described in the upstream `README.md`)
- **MCU**: STM32L433VCTx (`Saintcon2019.ioc:30,121`, `Saintcon2019.kicad_pcb` `U1`)
- **FPGA**: Lattice ICE40HX1K-TQ144 on `Saintcon2019TopBoard.kicad_pcb` (`U1`), used solely as the 64×16 RGB LED matrix SPI scan controller (`SaintconFPGA.ice`)

## How Upstream Code Is Staged

Upstream files are **not** committed to this repository. Instead, [`fetch-upstream.sh`](./fetch-upstream.sh) fetches commit `4ec59a1626dca09a0c75969f449a0ed3cca18bac` and stages only the badge's own source/header files and KiCad PCB files into git-ignored `emulator/cores/2019/upstream/`:

### Staged Firmware C Sources (`Firmware/Saintcon2019/`)
- `Src/animations.c` — Lampboard & matrix startup/challenge animations (`DisplayAnimation()`, `ForceLevelUp()`)
- `Src/custommessage.c` — Custom scrolling message editor (`EditCustomMessage()`, saved in EEPROM)
- `Src/enigma.c` — Enigma machine engine (`enigma_setup()`, `enigma_encode_char()`: 3-rotor mode with rotors `I`–`VIII` plus custom rotor 9 `IX` and UKW-B reflector, plus the badge's custom 4-rotor mode with thick UKW-B and `Beta`/`Gamma`)
- `Src/enigma_emulator.c` — Interactive Enigma machine UI, settings screens (rotors, ring settings, rotor start, plugboard monitor), and 9-level challenge decoder (`MSG1CODE`..`MSG9CODE`)
- `Src/images.c` — RGB888 sprites (`sc`, `enigma`, `enigma2`, `badge`, `scroll`, `minibadge_icon`, `menu_arrows`)
- `Src/keyboard.c` — 26-key tactile keyboard scan (`KBD_A_Pin`..`KBD_Z_Pin` in `Inc/main.h:69-78,117-134,139-146,170-185`, `keyboardUpdate()` and `keyboardInstant()` in `Src/keyboard.c:44-421`), debounce, and `LONG_PRESS_MS` (`3000` ms of `HAL_GetTick()` at `Src/keyboard.c:13,50-377`) hold-any-key back gesture
- `Src/lampboard.c` — 26-lamp RGB letter array (`RGB lampboard[26]` at `Src/lampboard.c:12`) and IS31FL3736 PWM frame flush (`lampboard_update()` at `Src/lampboard.c:32-112`)
- `Src/main.c` — Entry point (`main()`), `VirtAddVarTab[NB_OF_VARIABLES]` (`Src/main.c:88,181-184`), first-boot `TestMode()` check (`Src/main.c:295-301`), menu/screensaver loop, and badge-to-badge UART ring handler (`CheckSerial()` at `Src/main.c:544-736`)
- `Src/matrix.c` — 64×16 RGB888 framebuffers (`RGB framebuffer[16][64]` and `RGB bufferout[16][64]` at `Src/matrix.c:38-39`), 5×7 font renderer, line/rect primitives, and SPI frame transmitter (`matrix_update()` at `Src/matrix.c:168-180`)
- `Src/menu.c` — Main carousel menu (`Enigma Emulator`, `Games`, `Challenge Info`, `Custom Message`, `Minibadge Control`, `Exit Menu`)
- `Src/minibadge.c` — `Minibadge Control` mode selector (`MinibadgeDisplayMode()` at `Src/minibadge.c:36-107`: `OFF`, `ON`, `Pattern 1`–`4`) and I2C2 minibadge holder / minibadge bus driver
- `Src/plugboard.c` — 26-pin GPIO continuity scanner (`void plugboardUpdate()` at `Src/plugboard.c:30-71`, updating `char plugboard[26]`)
- `Src/screen_animations.c` — 8 LED matrix screensaver slots (`0..7` in `displayAnimations()` at `Src/screen_animations.c:41-68`), including `customMessage()`
- `Src/snake.c` — Snake game (`snake()`) on the 64×16 LED matrix with score display
- `Src/testmode.c` — Factory hardware test (`TestMode()` at `Src/testmode.c:19-70`) lighting all LEDs white until all 26 keys are pressed
- `Src/user_profile.c` — `Challenge Info` screen (`user_profile()` at `Src/user_profile.c:48-213`) and `scroll_message()` (`Src/user_profile.c:215-237`) displaying current `Lvl:` and unlocked ring messages (`INITIAL_RING`, `RING1_MESSAGE`..`RING3_MESSAGE`, unlocked at levels 1, 5, 9, and 13 at `Src/user_profile.c:80-83`)
- `Drivers/IS31FL3736/drv_is31fl.c` — IS31FL3736 I2C LED matrix driver (`LED_I2C_ADDR = 0x50 << 1` at `Drivers/IS31FL3736/drv_is31fl.c:41`)

### Staged Firmware Headers (`Firmware/Saintcon2019/`)
- `Inc/animations.h`, `Inc/custommessage.h`, `Inc/enigma.h`, `Inc/enigma_emulator.h`, `Inc/font5x7.h`, `Inc/images.h`, `Inc/keyboard.h`, `Inc/lampboard.h`, `Inc/main.h`, `Inc/matrix.h`, `Inc/menu.h`, `Inc/minibadge.h`, `Inc/plugboard.h`, `Inc/screen_animations.h`, `Inc/snake.h`, `Inc/snake_images.h`, `Inc/testmode.h`, `Inc/user_profile.h`
- `Drivers/IS31FL3736/drv_is31fl.h`

### Staged Hardware Files (`Hardware/KiCad Files/`)
- `Saintcon2019/Saintcon2019.kicad_pcb` — Main PCB (`STM32L433VCTx` `U1`, `IS31FL3736` `U4`, 26 RGB lamps `D1..D26`, 26 tactile switches `SW1..SW26`, plugboard headers `J3`/`J4`, minibadge header `J5`)
- `Saintcon2019TopBoard/Saintcon2019TopBoard.kicad_pcb` — Front faceplate PCB (`ICE40HX1K-TQ144` `U1`, 1,024 RGB LEDs `D1..D1024` in the 64×16 curved rotor window, 26 translucent letter windows, 26 switch cutouts)

## Excluded Vendor & Binary Files (Not Staged or Compiled)

- `Firmware/Saintcon2019/Drivers/STM32L4xx_HAL_Driver/` — ST STM32L4xx HAL drivers
- `Firmware/Saintcon2019/Drivers/CMSIS/` — ARM/ST CMSIS headers and startup code
- `Firmware/Saintcon2019/Middlewares/ST/STM32_USB_Device_Library/` and `Src/usb_device.c`, `Src/usbd_*.c` — ST USB CDC stack
- `Firmware/Saintcon2019/Middlewares/ST/EEPROM_Emul/` and `Inc/eeprom_emul_conf.h` — ST Flash EEPROM emulation middleware
- `Firmware/Saintcon2019/Src/iceboot.c`, `Inc/iceboot.h`, `Src/fpga.bin` — iCE40 FPGA bitstream loader (the FPGA receives the 3,075-byte SPI RGB888 frame in `matrix_update()` at `Src/matrix.c:168-192`)
- `Firmware/Saintcon2019/Src/dma.c`, `gpio.c`, `i2c.c`, `spi.c`, `tim.c`, `usart.c`, `stm32l4xx_*.c`, `syscalls.c`, `sysmem.c`, `system_stm32l4xx.c` — CubeMX peripheral init boilerplate replaced by `platform/emu_2019.c`
- `Saintcon2019.hex`, `fpga.hex`, `Code Sheet.pdf`, `Saintcon2019BadgeChallenge.pdf`, `Badge Instructions.pdf`, `README.md`

## Source Patches

All 17 staged C files and 19 staged header files are compiled unmodified. In the replacement platform headers and compiler flags:
- `platform/eeprom_emul.h:13` (which replaces the excluded `Inc/eeprom_emul_conf.h:97`) raises `NB_OF_VARIABLES` from `260U` to `320U` so `VirtAddVarTab[NB_OF_VARIABLES]` (`Src/main.c:88,181-184`) covers `EEP_MESSAGE_CODE_9 + 5` (`306 + 5 = 311`; defined in `platform/eeprom_emul.h`, used at `Src/testmode.c:61`) without writing or reading past the end of `VirtAddVarTab`.
- Both `gcc` and `emcc` compile with `-funsigned-char` (matching ARM's default `unsigned char`, so UTF-8 bytes in `RING2_MESSAGE` at `Src/main.c:81` index `font5x7` in `Src/matrix.c:101` with unsigned `char` as on the real badge) and `-ftrivial-auto-var-init=zero` (so uninitialized stack buffers such as `custommessage[255]` in `Src/screen_animations.c:570` are deterministically zeroed; `char pairs[39]` in `Src/enigma_emulator.c:56` is a zero-initialized global).
- On 64-bit Linux (`emu-cli`), `build.sh` compiles with `-fno-pie -no-pie` so `.rodata` string literals (`INITIAL_RING`, `RING1_MESSAGE`..`RING3_MESSAGE`) reside below `2 GB` and round-trip losslessly through `int messageStart` in `Src/user_profile.c:145-170`.

## Secrets & Challenge Content Audit

- The STM32L433VCTx badge has no Wi-Fi or BLE radio; no Wi-Fi credentials, private keys, or personal data exist in the staged tree.
- A Hacker Challenge (`HC:`) scoring code on the hidden `F.SilkS` layer of `Hardware/KiCad Files/Saintcon2019/Saintcon2019.kicad_pcb:20370` is scrubbed to `HC: REDACTED` by `fetch-upstream.sh` with a fail-if-not-applied check.
- Challenge message codes (`MSG1CODE`..`MSG9CODE` in `Src/enigma_emulator.c:42-50`) and ring messages (`INITIAL_RING`, `RING1_MESSAGE`..`RING3_MESSAGE` defined in `Src/main.c:77-85` and declared `extern` in `Src/animations.c:45-53` and `Src/user_profile.c:36-44`) are game content required for the at-home challenge and are preserved intact.

## Stated Authors, Credits & Licences in Staged Files

1. **`Src/enigma.c` & `Inc/enigma.h`**:
   - States: *"This code was copied from Cory Lutton's enigma-c project / http://corylutton.com/enigma-c.html / There have been heavy modifications to this code by @_bashNinja / Copyright 2013 Cory Lutton / Licensed under the Apache License, Version 2.0"*.
2. **`Src/snake.c`, `Inc/snake.h`, `Src/screen_animations.c`, `Inc/screen_animations.h`, `Src/user_profile.c`**:
   - File headers state `Author: bashNinja`.
   - `Src/screen_animations.c` (lines `431-434`, `439-442`, `506-509`, `514-517`) states that `mapf()`, `plasma()`, `bound()`, and `LED_waveEffect()` were *"Generously borrowed from: https://github.com/bikefrivolously/led_matrix"*.
3. **`Drivers/IS31FL3736/drv_is31fl.c` & `Drivers/IS31FL3736/drv_is31fl.h`**:
   - States: *"(C) Copyright 2017-2018 AND!XOR LLC (http://andnxor.com/). Licensed under the Apache License, Version 2.0"*, listing contributors `@andnxor`, `@zappbrandnxor`, `@hyr0n1`, `@exc3ls1or`, `@lacosteaef`, `@bitstr3m`.
4. **`Src/main.c` & `Inc/main.h`**:
   - Contains STM32CubeIDE generated file header stating *"Copyright (c) 2019 STMicroelectronics. All rights reserved. This software component is licensed by ST under BSD 3-Clause license"*.
5. **Remaining badge source files (`Src/animations.c`, `Src/custommessage.c`, `Src/enigma_emulator.c`, `Src/images.c`, `Src/keyboard.c`, `Src/lampboard.c`, `Src/matrix.c`, `Src/menu.c`, `Src/minibadge.c`, `Src/plugboard.c`, `Src/testmode.c`, and corresponding headers)**:
   - Authored by `compukidmike` (`@compukidmike`, <https://github.com/compukidmike/Saintcon2019>); `Author: Compukidmike` stated in `Src/custommessage.c`, `Inc/custommessage.h`, `Src/minibadge.c`, `Inc/minibadge.h`, `Src/testmode.c`, `Inc/testmode.h`. No repository-level `LICENSE` file is present in `compukidmike/Saintcon2019` (Licence not stated).
