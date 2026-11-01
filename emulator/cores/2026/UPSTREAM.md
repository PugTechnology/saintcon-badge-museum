# SAINTCON 2026 Upstream Firmware ("Orion Trail" / Wayfarer)

`emulator/cores/2026/upstream/` contains an unmodified copy of `firmware/` and
`LICENSE` from the official SAINTCON 2026 badge repository:

- **Repository**: https://github.com/Professor-plum/SAINTCON-BADGE-2026
- **Author**: Professor-plum (Professor Plum)
- **Commit**: `2168f62e23151fcc26a8860a7b7bf076642e2bbc` (`2168f62`, 2026-10-26, `"Station update before con"`)
- **License**: MIT License (see `upstream/LICENSE`)

## Build & Modification Policy

- `upstream/` is fetched (not committed) by `fetch-upstream.sh` at commit `2168f62`; `build.sh` runs it. Files there are kept **100% unmodified**.
- Hardware drivers and Pico SDK headers required by the game are provided by
  `emulator/cores/2026/platform/` (stub Pico SDK headers and C replacements for
  ST77916 QSPI/DMA display flushing, PIO/GPIO/I2C/UART/timers, RNG, and
  cooperative fiber yielding).
- No build-time patches (`patches/`) are needed.
