# SAINTCON 2024 Wrist Badge Upstream Source

- **Repository**: <https://github.com/compukidmike/Saintcon2024>
- **Subdirectory**: `Firmware/`
- **Pinned commit**: `5e9412c837334379d7f6cadf68327fc0771f636d` (`5e9412c`, 2025-02-07, commit message "Badge firmware")
- **Firmware version**: `2.6.9` (`Firmware/version.txt`)
- **Author**: [compukidmike](https://github.com/compukidmike)
- **License**: No `LICENSE` file is published in the upstream repository. Upstream source is **not** committed to this repository; `fetch-upstream.sh` stages `Firmware/` into the git-ignored `./upstream/` directory at build time.

## Managed Components

`Firmware/dependencies.lock` targets ESP-IDF 5.3.1 (`esp32s3`) with the following managed components, pinned in `fetch-upstream.sh` by both version tag and commit SHA:

- `lvgl/lvgl` `9.2.0` (`v9.2.0`, commit `aa7446344c6ec7631112ef031983ef24077e24d5`)
- `johboh/nlohmann-json` `3.11.3` (`v3.11.3` from `nlohmann/json`, commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`)
- `espressif/esp_lcd_touch` `1.1.2` and `espressif/esp_lcd_touch_gt911` `1.1.1~1` are replaced directly by the emulator's touch shim headers and pointer-event driver in `platform/` (`platform/include/esp_lcd_touch.h`, `platform/include/esp_lcd_touch_gt911.h`, `platform/peripherals_sim.c`).

## Secret Scrubbing & Verification

Immediately after extracting `Firmware/` at `5e9412c837334379d7f6cadf68327fc0771f636d`, `fetch-upstream.sh` scrubs all conference and debug Wi-Fi credentials before any compiler runs, verifies each substitution applied, and confirms zero occurrences of the original values remain anywhere in `./upstream/Firmware/` (exiting non-zero on any failure):

1. **`Firmware/sdkconfig.defaults` (and `platform/include/sdkconfig.h`)**: replaces `CONFIG_CONFERENCE_WIFI_SSID` with `"MUSEUM"` (used directly by `components/wifi_manager/wifi_manager.c` with no XOR encoding in 2024) and blanks `CONFIG_CONFERENCE_WIFI_PASSWORD` to `""`.
2. **`Firmware/main/main.c`**: replaces the commented-out debug Wi-Fi `.ssid` and `.password` literals in `app_main()` with `"MUSEUM"` and `""`.

Unlike the 2025 badge, the 2024 wrist badge has no ATECC608B secure element, no HMAC keys, and no NFC NDEF Wi-Fi tag credentials. Its IR protocol obfuscation constant (`0xDEADBEEF` in `components/badge/ir.c`) is a public protocol framing constant rather than a secret key.

## Patched Files Summary

Aside from the credential-scrubbing edits in `Firmware/sdkconfig.defaults` and `Firmware/main/main.c` above, zero source patches are applied to `Firmware/`: all upstream application, UI, tower battle, IR NEC codec, display/touch wrapper, Wi-Fi manager, peripheral state machine, and API client sources compile unmodified against the plain-C/C++ ESP-IDF 5.3.1 replacement layer in `platform/`. (`platform/include/sdkconfig.h` defines `#define battery_get_status(...) battery_get_status()` so `power_manager.c:492`'s extra argument to non-prototype `battery_get_status()` does not produce a WebAssembly direct-call signature mismatch.)

- **Managed component bugfix (`managed_components/lvgl__lvgl/src/draw/lv_draw.c`)**: `fetch-upstream.sh` applies a 2-line backport from LVGL 9.3.0 to LVGL 9.2.0 (`lv_draw_dispatch()` / `lv_draw_dispatch_layer()`). In LVGL 9.2.0, `if(layer->draw_task_head && lv_draw_dispatch_layer(disp, layer))` skipped child transform/simple layers that produced zero draw tasks — such as the rotated `status_label` on `SCREEN_MAIN` when `state.label` is initially `""` (noted by the original firmware author at `Firmware/components/ui/statusbar.c:127`) — leaving the parent `LV_DRAW_TASK_TYPE_LAYER` task permanently in `LV_DRAW_TASK_STATE_WAITING`. Allowing `lv_draw_dispatch_layer()` to run when `layer->parent && layer->all_tasks_added` promotes the waiting parent layer task to `LV_DRAW_TASK_STATE_QUEUED` so `lv_draw_sw_layer()` (`lv_draw_sw_img.c:81`) cleanly retires the empty layer.

- **Legacy public server URL string (`https://sc24.redactd.net`)**: `components/api/include/api.h` defines `API_BASE_URL` as `"https://sc24.redactd.net"`, so that string literal remains present in the compiled binary. It is **never** contacted over the network: `platform/http_bridge.c` intercepts all `esp_http_client` calls in C and routes them synchronously to the in-browser `badges/2024/fake-server.js` (or `platform/cli_fake_server.cpp` in `emu-cli`), and the web app's Content Security Policy (`connect-src 'self'`) blocks all external network connections.
