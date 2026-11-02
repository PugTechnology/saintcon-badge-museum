# SAINTCON 2021 Combination Lock Badge Upstream Source

- **Repository**: <https://github.com/compukidmike/Saintcon2021>
- **Staged subdirectories**:
  - `Saintcon2021/Saintcon2021/` (badge-authored C firmware and scenes)
  - `Saintcon2021/ExternalFlash/` (external QSPI flash sprite PNGs and `makeFlash.py`)
  - `Hardware/Saintcon2021Front/Saintcon2020Front.kicad_pcb`, `Hardware/Saintcon2021Shackle/Saintcon2020Shackle.kicad_pcb`, `Hardware/Saintcon2021/Saintcon2020.kicad_pcb` (KiCad PCB layouts)
- **Pinned commit**: `092bf1c68a90cd5d13794006667fd52c3cbb3efd` (`092bf1c`, `2023-04-06 13:53:51 -0600`, commit message `"Add minibadge holder board"`)
- **Creators & contributors**:
  - [compukidmike](https://github.com/compukidmike)
  - [Professor_Plum](https://github.com/Professor-plum)
  - [Sodium_Hydrogen](https://github.com/Sodium-Hydrogen)
  - [kfeuz](https://github.com/compukidmike/Saintcon2021/commit/bfef3ea) — firmware contributor (commit `bfef3ea`: `"Fixed minibadge message collision"`, `Saintcon2021/Saintcon2021/Scenes/menuScene.c`)
- **Licence**: Licence not stated (no `LICENSE` file is published in the upstream repository). Upstream source is **not** committed to this repository; `fetch-upstream.sh` stages the badge files into the git-ignored `./upstream/` directory at build time.

## Excluded Vendor & Out-of-Scope Directories

`fetch-upstream.sh` extracts only the badge's own C sources, `ExternalFlash/` assets, and KiCad board files via `git archive`. The following upstream paths are **never** staged, compiled, or shipped:

- `Saintcon2021/Saintcon2021/qtouch/` — Microchip's proprietary QTouch binary library (`libqtm_*_cm4.a`) and generated QTouch headers
- `Saintcon2021/Saintcon2021/hal/`, `hpl/`, `hri/`, `CMSIS/`, `Device_Startup/`, `Config/`, `usb/`, `driver_init.c`, `driver_init.h`, `atmel_start.c`, `atmel_start.h`, `atmel_start_pins.h` — Atmel Start / ASF4 / CMSIS vendor HAL and USB CDC stack
- `Saintcon2021/Saintcon2021/ILI9331.c` — hardware 16-bit parallel GPIO bit-banging driver (replaced by the LCD framebuffer shim in `platform/emu_2021.c`, keeping upstream `ILI9331.h`)
- `saintcon2021bootloader/` — UF2/SAM-BA bootloader (out of scope)
- `README.md` — upstream README text (not copied)

All hardware peripheral interfaces (`atmel_start.h`, `driver_init.h`, `atmel_start_pins.h`, `hal_ext_irq.h`, `hal_sleep.h`, AES-128-ECB / SHA-1, GPIO, `SysTick` / `MS_Timer`, `ILI9331` `240x240` RGB565 LCD + `PB17` GPIO on/off backlight, `TCC0` PWM RGB LED `D1`, QTouch wheel + shackle/shim sensors, 512-byte `NVMCTRL` SmartEEPROM, 8 MB `QUAD_SPI_0` external flash, `ST25R95` SPI NFC reader/tag emulator, `I2C_0` minibadge bus, USB no-op `cdcd_loop()`, and `NVIC_SystemReset()`) are provided by first-party shims in `platform/`.

## Secret Scrubbing & Verification

Immediately after extracting the pinned archive, `fetch-upstream.sh` runs `scrub_and_verify`:

1. **`Saintcon2021/Saintcon2021/Scenes/tradeScene.c` (line 36)**: replaces the 16-byte AES-128 `trade_key[16]` initializer with deterministic museum bytes `MUSEUM_TRADE2021` (`{0x4d, 0x55, 0x53, 0x45, 0x55, 0x4d, 0x5f, 0x54, 0x52, 0x41, 0x44, 0x45, 0x32, 0x30, 0x32, 0x31}`), fails if the substitution was not applied, and scans every staged file (text and binary) to ensure zero residual occurrences of the original key remain. The original key value is never printed.
2. **Personal data / credential / flag audit**: scans all staged files for Wi-Fi credential identifiers, CTF flag patterns, phone numbers (`TEL:`), and email addresses. No real person's vCard or contact details exist in upstream; the only sample card is a fictional movie character in a comment in `Saintcon2021/Saintcon2021/flash.h`. Lock combinations in `Saintcon2021/Saintcon2021/unlocks.c`, `Saintcon2021/Saintcon2021/combonums.h`, and `Saintcon2021/ExternalFlash/makeFlash.py` are game puzzle content and are preserved unmodified.

## Portability Patches Applied by `fetch-upstream.sh`

Aside from scrubbing `trade_key[16]` in `Saintcon2021/Saintcon2021/Scenes/tradeScene.c` and passing `-Dmain=sc21_firmware_main -fshort-enums -funsigned-char` on the compiler command line, `fetch-upstream.sh` applies five single-line C99 / WebAssembly / x86_64 portability fixes:

1. **`Saintcon2021/Saintcon2021/main.h` (line 16)** and **`Saintcon2021/Saintcon2021/Scenes/tradeScene.c` (line 22)**:
   Adds `extern` to `struct io_descriptor *io;` in `main.h` (defined once in `platform/emu_2021.c`) and `static` to `struct sha_context context;` in `Scenes/tradeScene.c` (which collided with `Scenes/nfcScene.c` line 21) so modern `-fno-common` toolchains (GCC 10+, Clang 11+, and Emscripten `wasm-ld`) link cleanly.
2. **`Saintcon2021/Saintcon2021/Scenes/testScene.c` (line 45)**:
   Guards `1000/(sys_ms+blt_ms+draw_ms)` when `sys_ms + blt_ms + draw_ms == 0` on the initial frame. On ARMv7E-M (Cortex-M4), hardware `UDIV` by zero architectural default returns `0` without faulting, whereas WebAssembly `i32.div_u` and x86_64 `div` trap immediately.
3. **`Saintcon2021/Saintcon2021/Scenes/menuScene.c` (line 82)**:
   Guards `1000/(now-menu_last)` when `now == menu_last` for the same ARMv7E-M vs. WebAssembly/x86_64 integer divide-by-zero reason.
4. **`Saintcon2021/Saintcon2021/Scenes/menuScene.c` (line 111)**:
   Replaces bare `return;` inside `bool menu_check_minibadge(uint8_t addr)` (`if (b==1) { minibadge_button = 128; return; }`) with `return found;` so C99 compilers (Clang/Emscripten `-Wreturn-type` error by default) compile cleanly with a defined boolean return value.
