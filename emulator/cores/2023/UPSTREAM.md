# SAINTCON 2023 "Incidents" Badge Upstream Source

- **Repository**: <https://github.com/compukidmike/Saintcon2023>
- **Subdirectory**: `Firmware/Source/`
- **Pinned commit**: `6f5f3407bb57000e0131fc317ad907f27bda7234` (`6f5f340`, 2026-09-05, commit message "Merge pull request #6 from klipperx/klipperx/attract-demo-screen")
- **Firmware version**: `1.4` (`firmwareVersion` in `Firmware/Source/src/main.cpp`)
- **Author**: [compukidmike](https://github.com/compukidmike)
- **License**: No `LICENSE` file is published in the upstream repository. Upstream source is **not** committed to this repository; `fetch-upstream.sh` stages `Firmware/Source/` into the git-ignored `./upstream/` directory at build time.

## Library Dependencies

`Firmware/Source/platformio.ini` targets Arduino on ESP32-S3 (`esp32-s3-devkitc-1-n16r8v`) with five `lib_deps`:

- `bblanchon/ArduinoJson` `6.21.3` — compiled from the real upstream library pinned in `fetch-upstream.sh` by both version tag (`v6.21.3`) and commit SHA (`4b00783345484b4fd4a4f89f842809ef3eee1681`, MIT License).
- `moononournation/GFX Library for Arduino` `1.3.6` — replaced by the museum's `Arduino_GFX_Library.h` / `Arduino_Canvas` `320x240` RGB565 framebuffer shim in `platform/`.
- `fastled/FastLED` `3.6.0` — replaced by the museum's `FastLED.h` 20-LED SK6812MINI shim in `platform/`.
- `khoih-prog/AsyncHTTPSRequest_Generic` `2.5.0` — replaced by `platform/include/AsyncHTTPSRequest_Generic.h` + the `EM_JS` bridge in `platform/platform_2023.cpp`, which routes `sc23-api` requests synchronously to `badges/2023/fake-server.js` in the browser (and `platform/cli_fake_server.cpp` in `emu-cli`).
- `crankyoldgit/IRremoteESP8266` `2.8.6` — replaced by virtual NEC IR `IRrecv` / `IRsend` shims in `platform/`.

The unused `Firmware/Source/lib/HTTPClient/` directory bundled in upstream is not compiled; `platform/include/HTTPClient.h` provides an empty header stub for `#include <HTTPClient.h>`.

## Secret Scrubbing & Verification

Immediately after extracting `Firmware/Source/` at `6f5f3407bb57000e0131fc317ad907f27bda7234`, `fetch-upstream.sh` scrubs the conference Wi-Fi credentials before any compiler runs, verifies the substitution applied, and confirms zero occurrences of the original values remain anywhere in `./upstream/Firmware/` (removing `./upstream/Firmware` and exiting non-zero on any failure):

1. **`Firmware/Source/src/badge.cpp`**: replaces the conference Wi-Fi SSID and password literals in `WifiAutoConnect()` (`WifiConnect("<SSID>", "<PASS>")`) with `"MUSEUM"` and `""`.
2. **Root `README.md` excluded**: `fetch-upstream.sh` archives only `Firmware/Source/` and never stages or copies the upstream `README.md`.

The `x-auth-id` request header sent by `Firmware/Source/src/request.cpp` is the badge's 12-character uppercase hex Wi-Fi STA MAC address (`badge_id`), not an API secret.

## Normal Firmware Build (Attract-Mode Disabled, Zero Source Patches)

Aside from the Wi-Fi credential scrub in `Firmware/Source/src/badge.cpp` above, **zero source patches** are applied to `Firmware/Source/`:

- Upstream commit `6f5f340` merged an optional attract-mode demo screen (`include/AttractScreen.h`, `src/AttractScreen.cpp`) guarded by `#ifndef ATTRACT_IDLE_MS` (`15000UL` default) and `Menu::Run()`'s `if (attractIdleMs > 0 && ...)` check.
- To run the normal conference badge firmware rather than attract-mode without patching upstream files, `build.sh` compiles with `-DATTRACT_IDLE_MS=0UL`, omits `src/AttractScreen.cpp` from the source list, and provides `demoModeActive = false` and a no-op `AttractScreenRun()` in `platform/`.
- **Legacy server URL strings (`https://sc23-api.redactd.net:8443`)**: `src/request.cpp` (`API_BASE`) and `src/main.cpp` (`updateURL`) compile unmodified, so those string literals exist in the binary, but they are never contacted over the network. All `AsyncHTTPSRequest` traffic is intercepted in C++ by `platform/` and handled locally by `badges/2023/fake-server.js` (or `cli_fake_server.cpp`), and the web shell's Content Security Policy (`connect-src 'self'`) blocks external connections.

## Note on the pinned commit

The pinned commit is the latest in the repository (2026-09-05). It includes klipperx's pull request #6 (idle
attract mode, offline Wi-Fi fallback, drawing helpers), so it is not byte for byte the build worn at SAINTCON 2023.
The attract screen itself is left out of this build.
