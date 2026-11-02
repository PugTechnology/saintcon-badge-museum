# SAINTCON 2017 — HITB Minibadge 2017 Upstream Provenance & Shim Notes

## 1. Upstream Repository & Pinned Commit

- **Repository**: `https://github.com/compukidmike/Saintcon2017`
- **Pinned Commit**: `d5f1d932035e589253eafbd97a414d78ee9b37e8` (`2017-10-12 10:00:18 -0600`, *"Update README.md"*)
- **Scope**: The upstream repository states *"These are the files for the stuff I worked on for Saintcon 2017"*. It provides the **HITB Minibadge 2017** (`HITBUTT_Minibadge/HITB_Test.ino`, `HITBUTT_Minibadge/Base-Rev0.brd`, `HITBUTT_Minibadge/Base-Rev0.sch`) and a PCB design village talk (`MinibadgeTutorial/`). It is a minibadge, not the main 2017 conference badge, and the main 2017 badge firmware is not in this repository.
- **Credits**:
  - The repository `compukidmike/Saintcon2017` is `compukidmike`'s (`https://github.com/compukidmike`).
  - Seven handles are on the board's back silkscreen (`HITBUTT_Minibadge/Base-Rev0.brd:215-222`, layer 22 `bPlace` under `@HITBUTT 2017`, listed in the order of the board file): `compukidmike`, `d4rkm4tter`, `kampf`, `D3M0S`, `Heinrich`, `dumby`, `RuShan`.
  - The embedded EAGLE library entries in `Base-Rev0.brd` and `Base-Rev0.sch` carry their own author lines.
- **Licence**: **Licence not stated** (no `LICENSE`/`COPYING` file and no licence header in any source or board file).

## 2. Firmware Patch Count: Zero (`HITB_Test.ino` Compiled Unchanged)

`HITBUTT_Minibadge/HITB_Test.ino` (58 lines) is compiled **unchanged** (`-x c++ -include Arduino.h`) against our own shims in `emulator/cores/2017/platform/`:

- `platform/Arduino.h`: Standard integer types, `byte`, ATtiny45 analog pin constants (`A2 = 2`, `A3 = 3`), and `setup()` / `loop()` declarations.
- `platform/APA102.h`: Emulator shim replacement for `APA102` (which `HITBUTT_Minibadge/README.md` notes the sketch was written to be used with, by `pololu`). Implements `rgb_color` and `APA102<dataPin, clockPin>::write(colors, count, brightness)` (`dataPin = 0`, `clockPin = 2`, `ledCount = 2`, `brightness = 10` at `HITB_Test.ino:5-18,56`).
- `platform/ADCTouch.h`: Emulator shim replacement for `ADCTouch` (which `HITBUTT_Minibadge/README.md` notes the sketch was written to be used with, by `martin2250`). Implements `ADCTouch.read(analog_pin, samples)` for `A3` (`PB3` / `TOUCH_BTN_1`) and `A2` (`PB4` / `TOUCH_BTN_2`) (`HITB_Test.ino:28-29,34-35`).
- `platform/emu_2017.cpp`:
  - Returns the live touch capacitance reading (`512` untouched or `512 + 68 = 580` when held on `A3`; `508` untouched or `508 + 36 = 544` when held on `A2`) during both `setup()` (`HITB_Test.ino:23-30`) and `loop()` (`HITB_Test.ino:32-58`).
  - When calibrated untouched (`ref0 = 512`, `ref1 = 508`), holding Pad 1 (`A3`, `buttons_mask & 1`) produces `value0 = 68 > 40` (`HITB_Test.ino:43`, red), holding Pad 2 (`A2`, `buttons_mask & 2`) produces `value1 = 36 > 10` (`HITB_Test.ino:49`, blue), and holding both produces magenta (`(255, 0, 255)`).
  - If `emu_reset()` runs while a pad is held, `setup()` reads the held capacitance into `ref0` / `ref1` (`580` or `544`), so `value0` / `value1` is `0` (`<= 40` / `<= 10`), the LEDs stay green `(0, 255, 0)`, and that pad does not trigger until pressed with a capacitance reading more than `40` (or `10`) above the new reference.
  - Applies one fixed 32-point strictly monotonic brightness curve `kApa102BrightnessCurve[b] = round(255 * (b / 31.0)^0.384)` for `b = 1..31` (`0` at `b = 0`, `165` at `b = 10`) to the RGB colours written via `ledStrip.write(colors, ledCount, brightness)` (`HITB_Test.ino:56`).

## 3. Hardware & Board Layout (`HITBUTT_Minibadge/Base-Rev0.brd` and `.sch`)

- **Dimensions**: `20.32 mm × 20.32 mm` (`0.8 in × 0.8 in`) 2-layer PCB with 8-pin top and 8-pin bottom `BADGE64-MINIBADGE` through-hole headers (`Base-Rev0.brd:229-245`).
- **MCU**: `U1` (`ATTINY45` / `SO08-EIAJ_MCU` package, `value="Tiny45-20-SMT"` at `Base-Rev0.brd:1690` and `Base-Rev0.sch:5215`, mounted on top layer):
  - `PB0 (MOSI)` (`dataPin = 0`) -> `APA102` data (`U2` pin 4)
  - `PB2 (SCK)` (`clockPin = 2`) -> `APA102` clock (`U2` pin 5)
  - `PB3 (ADC3)` (`A3`) -> `R1` (`M0805`, bottom) -> `TOUCH_BTN_1` (`TP06R`, bottom at `(7.62, 9.398) mm`, adjacent to the bottom-layer exposed copper ring on layers `16`/`30` at `(10.16, 10.16) mm`, `Base-Rev0.brd:191-192,1682`)
  - `PB4 (ADC2)` (`A2`) -> `R2` (`M0805`, bottom) -> `TOUCH_BTN_2` (`TP06R`, bottom at `(16.764, 9.398) mm`, connected through a via at `(16.764, 9.398) mm` to the top-layer copper polygon `x = 14.478..19.812 mm, y = 5.588..14.224 mm` under `"You Can't\nTouch This"`, `Base-Rev0.brd:223,1686,1854-1859`)
- **LEDs**: Two `APA102_5050` RGB LEDs mounted on the top layer (`Base-Rev0.brd:1665-1670`):
  - `U2` at EAGLE `(10.16, 15.24) mm` (upper center)
  - `U3` at EAGLE `(10.16, 5.08) mm` (lower center)
- **Empty Logo Layers**: Bitmap layers `200..230` (`200bmp`..`230bmp`) are defined in the `<layers>` table of `Base-Rev0.brd` with zero drawing elements; `emulator/tools/eagle_layout_2017.py` renders only real geometry from the `.brd` file and does not fabricate missing artwork.

## 4. Secrets & Personal Data Audit

- **Scrubbed**: `HITBUTT_Minibadge/Base-Rev0.brd` at lines `344` and `1425` (2 occurrences) and `HITBUTT_Minibadge/Base-Rev0.sch` at lines `421`, `932`, and `4335` (3 occurrences) contained an embedded EAGLE library author email address inside `<author>Created by ...</author>`. `emulator/cores/2017/fetch-upstream.sh` scrubs all 5 occurrences to `MUSEUM_REDACTED` with a fail-if-not-applied count check.
- **No flags or secrets**: No `HC:` codes, CTF flags, or Wi-Fi credentials exist in `HITB_Test.ino`, `Base-Rev0.brd`, or `Base-Rev0.sch`.
