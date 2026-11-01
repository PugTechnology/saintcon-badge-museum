# SAINTCON 2025 Stock Badge Upstream Source

- **Repository**: <https://github.com/compukidmike/Saintcon2025>
- **Subdirectory**: `Firmware/badge/`
- **Pinned commit**: `360c892981968822fbadfdaa6ff580a55f4396af` (`360c892`, 2025-11-11 — *"add AfterCon code for Nut"*)
- **Firmware version**: `0.2.1` (`version.txt`)
- **Author**: [compukidmike](https://github.com/compukidmike)
- **License**: No `LICENSE` file is published in the upstream repository (default copyright / all rights reserved; emulated here with the author's permission). Upstream source is **not** committed to this repository; `fetch-upstream.sh` stages `Firmware/badge/` into the git-ignored `./upstream/` directory at build time.

## Managed Components

`Firmware/badge/` targets ESP-IDF 5.5.1 with the following pinned managed components (staged into `./upstream/managed_components/` from `work/stock/managed_components/` when present locally, or fetched at their pinned upstream tags):

- `lvgl/lvgl` `9.3.0` (`v9.3.0`)
- `johboh/nlohmann-json` `3.12.0` (`v3.12.0`)
- `espressif/esp-boost` `0.3.1` (header-only `boost::bimap` used by `components/api/api_client.cpp`)
- `espp/st25dv` `1.0.27`, `espp/ndef` `1.0.28`, `espp/logger` `1.3.11`, `espp/format` `1.0.28`, `espp/base_component` `1.0.28`, `espp/base_peripheral` `1.0.28`

## Secret Blanking & Patches

Immediately after extracting `Firmware/badge/` at `360c892981968822fbadfdaa6ff580a55f4396af`, `fetch-upstream.sh` scrubs conference secrets and verifies every substitution (exiting non-zero if any scrub fails to apply) before any compiler runs:

1. **`sdkconfig.defaults` (and `platform/include/sdkconfig.h`)**: blanks `CONFIG_CONFERENCE_WIFI_PASSWORD` to `""` and sets `CONFIG_CONFERENCE_WIFI_SSID` to `"CXVOQM"`. Because `components/badge/badge.c` XORs the first 5 bytes of `CONFIG_CONFERENCE_WIFI_SSID` with `{'L'^'B', 'l'^'a', 'a'^'d', 'm'^'g', 'a'^'e'}` (`0x0E, 0x0D, 0x05, 0x0A, 0x04`) when deriving the conference SSID at runtime, `"CXVOQM"` decodes in-game to `"MUSEUM"`, matching the simulated Wi-Fi AP name while ensuring neither the real conference Wi-Fi pre-shared key nor the real conference SSID appears in `./upstream/` or in the compiled WebAssembly / CLI binaries.
2. **`components/badge/ops/nfc.c`**: patches both hard-coded NFC Wi-Fi NDEF credential sites (`nfc_write_wifi_config(...)` in `nfc_init()` and `.wifi_ssid` / `.wifi_key` in `nfc_write_defaults()`) to use SSID `"MUSEUM"` and key `"MUSEUM-DEMO"`.
3. **Hardware secure element (`shared_components/secure_element/secure_element.c` and `shared_components/esp-cryptoauthlib/`)**: removed during extraction and replaced by `platform/crypto_sim.c` and `platform/storage_sim.c`, which implement a deterministic software ATECC608B + NVS simulation with a synthetic museum identity and per-visitor HMAC/hash-prefix slots provisioned by `badges/2025/fake-server.js`.

## Patched Files Summary

Aside from the secret-blanking edits in `sdkconfig.defaults` and `components/badge/ops/nfc.c` above, all upstream application, UI, game, NFC/NDEF, LED layer, and API client sources compile unmodified against the plain-C/C++ ESP-IDF replacement layer in `platform/` (with `-Dapp_main=sc25_app_main`).

- **Legacy public server URL string (`https://sc25.redactd.dev`)**: `components/api/include/api.h` defines `API_BASE_URL` with a compile-time fallback of `"https://sc25.redactd.dev"`, so that string literal remains present in the compiled binary. It is **never** contacted over the network: `platform/http_bridge.c` intercepts all `esp_http_client` calls in C and routes them synchronously to the in-browser `badges/2025/fake-server.js` (or `platform/cli_fake_server.cpp` in `emu-cli`), and the web app's Content Security Policy (`connect-src 'self'`) blocks all external network connections.
