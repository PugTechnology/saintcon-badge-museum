# SAINTCON Badge Emulator & Multi-Year Museum

Browser-based hardware shell (`emulator/web/index.html`), multi-year badge museum (`emulator/web/museum.html`), and native headless script runners for SAINTCON electronic conference badges:

- **2024 — "Tower Battle"** (`?year=2024`): ESP32-S3 landscape wrist badge (`Saintcon2024.kicad_pcb` + `Saintcon2024Top.kicad_pcb`), rectangular `320×240` ST7789 8080 parallel LCD with GT911 capacitive touchscreen, NEC IR TX/RX, MC3419 accelerometer, and TCA9544A Klip-on I2C mux, running `compukidmike`'s original SAINTCON 2024 firmware (`mode=stock`, "Original Firmware") backed by a per-visitor in-browser fake tower server (`badges/2024/fake-server.js`) and an 11-station virtual IR `"Towers"` panel.
- **2025 — "The Wrench"** (`?year=2025`): ESP32-S3 wrench PCB (`Saintcon2025.kicad_pcb`), rectangular `240×320` ST7789V LCD, 20 WS2812B-2020 LEDs (`D1..D20`), 5-way navigation switch (`SW2`), 512-byte ST25DV04K NFC EEPROM, with **two selectable modes**:
  - `mode=stock` (default): original SAINTCON 2025 conference firmware & 31-node faction game by `compukidmike` (`core-stock.js` / `core-stock.wasm`), backed by a per-visitor in-browser fake game server (`badges/2025/fake-server.js`) and solo virtual NFC "Nut" dispenser.
  - `mode=pug`: virtual pug pet & mini-games firmware (`core.js` / `core.wasm`).
- **2026 — "Orion Trail"** (`?year=2026`): RP2350A Apollo rocket PCB (`Apollo.kicad_pcb`), circular `360×360` ST77916 QSPI LCD, 9 WS2812C-2020 LEDs (`D4..D11, D3`), 5-way navigation switch (`SW4`, center=`C`) + two action buttons (`SW2`=`A`, `SW3`=`B`), 1024-byte 24CS08 I2C EEPROM, running Professor-plum's stock *Orion Trail* space-exploration game (`mode=stock`, offline standalone crew mode).

---

## Directory Layout

### Web Shell & Per-Year Badge Assets (`emulator/web/`)

All year-specific assets, metadata, and compiled WebAssembly cores live under `emulator/web/badges/<year>/`:

```text
emulator/web/
├── index.html               # Multi-year emulator shell (?year=2024|2025|2026 or <yy>.sc.pug.technology)
├── museum.html              # Badge museum index (2024, 2025, and 2026 live)
├── mock-core.js             # Fallback JS implementation of the 12-function Core ABI
└── badges/
    ├── 2024/
    │   ├── badge.json       # Year profile: "Tower Battle", 320×240 touch screen, 0 addressable LEDs, mode: stock
    │   ├── layout.json      # Board geometry (844×570), screen rect, SW1 power switch, IR TX/RX coords
    │   ├── board.png        # 844×570 KiCad top+main wrist badge PCB composite render
    │   ├── core.js          # Emscripten MODULARIZE + ASYNCIFY loader (createCore)
    │   ├── core.wasm        # Compiled 2024 original firmware + LVGL 9.2 WebAssembly binary
    │   ├── fake-server.js   # Per-visitor in-browser SAINTCON 2024 tower server & virtual IR beacons
    │   └── THIRD_PARTY_LICENSES.txt # Generated license notices for LVGL 9.2, nlohmann/json, fonts, Emscripten/musl
    ├── 2025/
    │   ├── badge.json       # Year profile: "The Wrench", 240×320 rect screen, 20 LEDs, 5 buttons, 512B EEPROM, modes: stock + pug
    │   ├── layout.json      # Board geometry, screen rect, SW2, and 20 LED coords in chain order
    │   ├── board.png        # 784×1792 tinted KiCad PCB render
    │   ├── core.js          # Emscripten MODULARIZE loader for mode=pug (createCore)
    │   ├── core.wasm        # Compiled 2025 pug pet WebAssembly binary
    │   ├── core-stock.js    # Emscripten MODULARIZE + ASYNCIFY loader for mode=stock (createCore)
    │   ├── core-stock.wasm  # Compiled 2025 stock firmware + LVGL 9.3 WebAssembly binary
    │   └── fake-server.js   # Per-visitor in-browser SAINTCON 2025 game server & NFC "Nut" dispenser
    └── 2026/
        ├── badge.json       # Year profile: "Orion Trail", 360×360 round screen, 9 LEDs, 7 buttons, 1024B EEPROM
        ├── layout.json      # Board geometry, circular screen mask, SW4/SW2/SW3, and 9 LED coords in chain order
        ├── board.png        # 884×1792 KiCad PCB render of Apollo.kicad_pcb
        ├── core.js          # Emscripten MODULARIZE + ASYNCIFY loader (createCore)
        └── core.wasm        # Compiled 2026 Orion Trail WebAssembly binary
```

### Per-Year Native / WASM Cores

```text
emulator/
├── core/                    # 2025 ("The Wrench") platform layer, ABI glue (emu.c/emu.h), and headless CLI
├── build-core.sh            # Builds 2025 WASM core (-> web/badges/2025/) and emulator/core/emu-cli
├── cores/
│   ├── 2024/                # 2024 ("Tower Battle" wrist badge — original conference firmware & game) core
│   │   ├── UPSTREAM.md      # Provenance, secret-scrubbing, and LVGL 9.2 empty-layer fix record (commit 5e9412c, compukidmike/Saintcon2024)
│   │   ├── fetch-upstream.sh# Pins Saintcon2024 + LVGL 9.2.0 + nlohmann/json 3.11.3 into git-ignored upstream/ and scrubs secrets
│   │   ├── gen-licenses.sh  # Generates web/badges/2024/THIRD_PARTY_LICENSES.txt from upstream license files
│   │   ├── platform/        # ESP-IDF 5.3.1 replacement layer (multi-fiber FreeRTOS, ST7789 i80, GT911 touch, RMT NEC IR, NVS, I2C, HTTP bridge)
│   │   ├── cli_2024.c       # Headless script runner (supports touch/tap/drag/type-kb/tap-label/ir/solve-attack/dump-ui) & PNG renderer
│   │   ├── smoke.script     # End-to-end headless smoke script (boot -> register -> home -> IR towers -> stratagem attack)
│   │   └── build.sh         # Builds 2024 WASM (-> web/badges/2024/core.{js,wasm}), emu-cli, and THIRD_PARTY_LICENSES.txt
│   ├── 2025-stock/          # 2025 ("The Wrench" — original stock conference firmware & game) core
│   │   ├── UPSTREAM.md      # Provenance & secret-blanking record (commit 360c892, compukidmike/Saintcon2025)
│   │   ├── fetch-upstream.sh# Fetches pinned Firmware/badge/ + managed_components into git-ignored upstream/
│   │   ├── platform/        # ESP-IDF 5.5.1 replacement layer (multi-fiber FreeRTOS, ST7789, ST25DV, NVS, ATECC608B stub, HTTP bridge)
│   │   ├── cli_2025_stock.c # Headless script runner (supports `nut <TYPE> [FACTION]`) & PNG renderer
│   │   ├── smoke.script     # End-to-end headless smoke script (boot -> register -> Sorting Hat -> Telecom Nuts -> 31-node game)
│   │   └── build.sh         # Builds 2025-stock WASM (-> web/badges/2025/core-stock.{js,wasm}) and emu-cli
│   └── 2026/                # 2026 ("Orion Trail") core
│       ├── UPSTREAM.md      # Provenance record (commit 2168f62, MIT License, Professor-plum)
│       ├── upstream/        # Unmodified vendored 2026 firmware/ and LICENSE
│       ├── platform/        # Plain-C Pico SDK + hardware BSP replacement & minimal Pico stub headers
│       ├── cli_2026.c       # Headless script runner & PNG renderer (circular LCD + 884×1792 badge composite)
│       └── build.sh         # Builds 2026 WASM core (-> web/badges/2026/) and emulator/cores/2026/emu-cli
└── tools/
    └── kicad_layout.py      # Extracts layout.json + renders board.png for --year 2024, 2025, 2026, or all
```

- **Year & Mode Resolution**: `emulator/web/index.html` resolves the badge year from `?year=<year>` (default `2025`) or from the host `<yy>.sc.pug.technology` (e.g. `24.sc.pug.technology` = 2024, `25.sc.pug.technology` = 2025), fetches `badges/<year>/badge.json`, configures screen dimensions, shape (`rect` vs `round` circular mask), capacitive touch or button bitmask/keybindings, LED count, EEPROM size, modes, and credits, and loads `badges/<year>/core.js` + `core.wasm`.
- **Per-Year & Per-Mode Browser Storage**: `localStorage` keys are scoped by year and mode:
  - `saintcon_<year>_<mode>_eeprom` (e.g. `saintcon_2025_pug_eeprom`, `saintcon_2025_stock_eeprom`, `saintcon_2026_stock_eeprom`)
  - `saintcon_2024_stock_nvs`, `saintcon_2024_stock_server` (isolated NVS and fake tower server state for 2024 stock mode)
  - `saintcon_2025_stock_nvs`, `saintcon_2025_stock_se`, `saintcon_2025_stock_server` (isolated NVS, fake ATECC608B slots, and fake game server state for 2025 stock mode)
  - `saintcon_<year>_<mode>_speed` (for cores supporting time scaling)
- **Zero External Dependencies**: No external CDNs, no analytics, and no cookies.

---

## Core ABI (12-Function Generalized WASM Interface)

Every badge core (`emulator/core/emu.h`) compiles to WebAssembly (`badges/<year>/core.js` + `core.wasm`) with `-sMODULARIZE=1 -sEXPORT_NAME=createCore` and exports these 12 C functions:

```c
void      emu_init(uint32_t seed);                   // boot; reads persisted EEPROM from emu_eeprom()
void      emu_step(uint32_t dt_ms, uint8_t buttons); // advance simulation; button bit meanings come from badge.json
uint16_t* emu_framebuffer(void);                     // emu_fb_width() * emu_fb_height() RGB565, little-endian, row-major
int       emu_fb_width(void);                        // LCD width in pixels (320 for 2024, 240 for 2025, 360 for 2026)
int       emu_fb_height(void);                       // LCD height in pixels (240 for 2024, 320 for 2025, 360 for 2026)
uint8_t*  emu_leds(void);                            // emu_led_count() * 3 bytes RGB in hardware data-chain order
int       emu_led_count(void);                       // RGB LED count (0 for 2024, 20 for 2025, 9 for 2026)
uint8_t   emu_backlight(void);                       // 0-255 LCD backlight level
uint8_t*  emu_eeprom(void);                          // emu_eeprom_size() bytes persisted by the browser shell
int       emu_eeprom_size(void);                     // EEPROM byte length (512 for 2024/2025, 1024 for 2026)
int       emu_eeprom_dirty(void);                    // returns 1 once after each EEPROM write, then 0
void      emu_set_speed(uint32_t mult);              // time multiplier (used by 2025 pet mode; no-op on stock cores)

// Optional extensions exported by touchscreen / IR / accelerometer badges (e.g. 2024):
void      emu_set_touch(int pressed, int x, int y);  // capacitive touch state in [0..fb_width-1] x [0..fb_height-1]
void      emu_send_ir(uint32_t code);                // inject 32-bit NEC IR beacon frame into RMT receiver
void      emu_set_accel(int16_t x, int16_t y, int16_t z); // set raw accelerometer XYZ sample (16384 = +1g at +-2g)
```

For firmwares whose `main()` or FreeRTOS tasks block inside polling, queues, or `vTaskDelay()` / `sleep_ms()` loops (such as the 2024/2025 ESP-IDF + LVGL firmwares and 2026 Pico SDK firmware), the platform layer yields cleanly using Emscripten fibers (`ASYNCIFY` + `emscripten_fiber_*`) in WASM and POSIX `ucontext` in the native `emu-cli` binary.

---

## Building Cores & Running Headless Smoke Tests

### 2024 Core ("Tower Battle" Wrist Badge — `mode=stock`)

```bash
./emulator/cores/2024/build.sh
./emulator/cores/2024/emu-cli \
    --script emulator/cores/2024/smoke.script \
    --out emulator/build/2024-smoke
```

`emulator/cores/2024/build.sh` runs `fetch-upstream.sh` (staging `Firmware/` at commit `5e9412c83733` + LVGL `9.2.0` + `nlohmann/json` `3.11.3` into git-ignored `emulator/cores/2024/upstream/`, scrubbing the conference Wi-Fi name and password and verifying each scrub; the API key is the badge MAC, so there is nothing to scrub), embeds `spiffs/` map assets, compiles upstream C/C++ against `emulator/cores/2024/platform/` to produce `emulator/web/badges/2024/core.{js,wasm}` and `emulator/cores/2024/emu-cli`, and generates `emulator/web/badges/2024/THIRD_PARTY_LICENSES.txt`.

#### 2024 Fake Tower Server (`emulator/web/badges/2024/fake-server.js`) — Faithful vs. Invented

The conference backend (`https://sc24.redactd.net`) is offline after SAINTCON 2024. `fake-server.js` (and `platform/cli_fake_server.cpp` for `emu-cli`) implements every endpoint used by `components/api/` per visitor in `localStorage`:

- **Faithful to upstream firmware**:
  - All HTTP endpoints, HTTP verbs, and JSON response envelopes (`{"status": bool, "detail": ..., "result": ...}`): `GET /badge`, `POST /badge/register`, `GET /badge/firmware_version` (`"2.6.9"`), `GET /badge/firmware`, `GET /badge/all_tower_status`, `GET /badge/tower_status`, `POST /badge/join_tower`, `POST /badge/leave_tower`, `POST /badge/join_battle`, `POST /badge/ir_code`, `POST /badge/equip`, `POST /badge/levelup`, `POST /auth/request_auth_code`, `POST /auth/request_levelup_code`, `POST /auth/auth_status`, `GET /vend/items`, `POST /vend/buy`, `POST /battle/attack`, `POST /battle/fail`, `GET /battle/status/<id>` (including upstream's `"strategem_min"`, `"strategem_max"`, `"strategem_amount"` spelling and `"savior_handle"` array), `POST /battle/self_save`, `GET /battle/savior`, and `POST /battle/aar`.
  - Exact tower count (`TOTAL_TOWERS = 11` in `components/badge/towers.h`), tower status strings (`"VULNERABLE"`, `"INVULNERABLE"`, `"OFFLINE"`, `"DEFEATED"`), battle status strings (`"JOINED"`, `"BATTLE"`, `"DISCONNECTED"`), NEC 32-bit IR framing and XOR obfuscation (`IR_KEY = 0xDEADBEEF`, message type nibble `0x0` = Tower, `0x1` = Level-Up, `0x2` = Savior, `0x4` = Vending, `0x5` = Auth in `components/badge/ir.h`), and Klip-on buff types (`QUAD_DAMAGE`, `SPELLCHECK`, `STRAT_SIMPLIFY`, `REVERSE_SHELL`, `SAVING_THROW`, `HYDRA`).
- **Invented for standalone museum play**:
  - Reconstructed 11 tower names, levels (`1..5`), solo-tuned HP pools (`240..900`), and deterministic NEC IR codes from the 11 labeled rooms on the firmware's own SPIFFS floor maps (`Communities`, `Ground Contests`, `Ballroom A/B/C`, `EduSec`, `Chill Lounge`, `ICS`, `Briefs`, `HAM`, `Social`).
  - On-page `"Towers (11 IR Stations)"` panel standing in for walking up to the physical IR tower stations (clicking a defeated tower respawns it so solo visitors can replay any station), plus Community Level-Up IR, Savior Revive IR, and MC3419 wrist tilt presets.
  - Shop items purchased with battle coins automatically equip into virtual `slot1`/`slot2` so buffs work without physical I2C Klip-on minibadges; `battle_active` remains `true` while the player's battle session is open because upstream `render_battle_result()` (`tower_battle.c:1482`) is an empty `TODO` stub.

### 2025 Pug Core ("The Wrench" — `mode=pug`)

```bash
./emulator/build-core.sh
./emulator/core/emu-cli --script emulator/test/smoke.txt --out emulator/build/smoke/
```

`emulator/build-core.sh` writes `emulator/web/badges/2025/core.js`, `emulator/web/badges/2025/core.wasm`, and `emulator/core/emu-cli`.

### 2025 Stock Core ("The Wrench" — `mode=stock`)

```bash
./emulator/cores/2025-stock/build.sh
./emulator/cores/2025-stock/emu-cli \
    --script emulator/cores/2025-stock/smoke.script \
    --out emulator/build/2025-stock-smoke --dump-leds
```

`emulator/cores/2025-stock/build.sh` runs `fetch-upstream.sh` (copying `Firmware/badge/` at commit `360c89298196` into git-ignored `emulator/cores/2025-stock/upstream/` and blanking any secret defaults), embeds `spiffs_source/` PNG/BMP assets, compiles unmodified upstream C/C++ + LVGL 9.3.0 against `emulator/cores/2025-stock/platform/` to produce `emulator/web/badges/2025/core-stock.{js,wasm}` (Emscripten `ASYNCIFY` multi-task fibers) and `emulator/cores/2025-stock/emu-cli` (POSIX `ucontext` fibers + in-process C++ fake server).

#### 2025 Fake Game Server (`emulator/web/badges/2025/fake-server.js`) — Faithful vs. Invented

The conference backend (`https://sc25.us/api`) was private and is offline after SAINTCON 2025. `fake-server.js` (and `platform/cli_fake_server.cpp` for `emu-cli`) implements every endpoint called by `components/api/` per visitor in `localStorage`:

- **Faithful to upstream firmware**:
  - All HTTP routes, JSON schemas, status envelopes (`{"status":"ok","data":...}`), and HMAC-SHA256 challenge-response provisioning/verification (`firmware/version`, `firmware/badge`, `challenge`, `provision`, `provision/verify`, `provision/wireless-psk`, `register`, `status`, `redeem_code`, `topology/data`, `achievements`, `messages`, `messages/read`, `files/list`, `files/download/*`, `konami`, `dawn-accord`, `ctf-hints-password`, `request_credit_swap`, `credit_swap_status/*`, `cancel_credit_swap/*`).
  - All 6 faction IDs (`1..6`), names (`Aether Watch`, `Verdant Pact`, `Dawn Accord`, `Iron Howl`, `Ember Claw`, `Dream Coil`), and exact hex colors (`badge/factions.c`).
  - Exact 31-node hexagonal graph topology (`1` Core, `2..7` Ring 1, `8..19` Ring 2, `20..25` HQs, `26..31` Towers), node coordinates, 66 bidirectional connections, and `NONE` / `PARTIAL` / `FULL` port link states (`badge_game.c`).
  - Physical Nut Type-5 NDEF EEPROM layout: two NDEF Well-Known Text records (`<5-char A-Z2-9 code>` excluding `0/O/1/I`, plus `NT:<TYPE>`) written into the 512-byte ST25DV04K EEPROM and detected by the badge's GPO interrupt + polling loop (`ops/nfc.c`), after which the badge overwrites the tag with the `MUSEUM` (`MUSEUM` / `MUSEUM-DEMO`) WSC Wi-Fi NDEF record.
- **Invented for standalone museum play**:
  - Per-visitor synthetic provisioning secrets and Wi-Fi SSID `"MUSEUM"` (zero conference secrets or real keys).
  - Solo Nut Dispenser UI on `?year=2025&mode=stock` (`🔩 Tap Nut (NFC)` and `🔢 Manual Code`) generating valid one-time codes for `SORTING_HAT` (with optional faction choice `1..6`), `TELECOM`, `CONTEST`, `EVENT`, `VENDOR`, and `COMMUNITY`.
  - Credit award amounts (`+40..+120` credits, `200` credits/level up to Level 5), progressive Telecom sector unlocking toward Core Node `1` alongside simulated background activity from the other 5 factions, sample achievements/messages/files, and automatic credit-swap completion on the second status poll so transfers can be tested solo.

### 2026 Core ("Orion Trail")

```bash
./emulator/cores/2026/build.sh
./emulator/cores/2026/emu-cli --script emulator/test/smoke-2026.txt --out emulator/build/2026-smoke/
```

`emulator/cores/2026/build.sh` compiles the unmodified upstream sources in `emulator/cores/2026/upstream/firmware/` (excluding hardware `bsp/` drivers replaced by `emulator/cores/2026/platform/platform_2026.c`) inside `emscripten/emsdk:3.1.73` to produce `emulator/web/badges/2026/core.js` + `core.wasm`, and builds the host binary `emulator/cores/2026/emu-cli` with `gcc`.

Supported `emu-cli` script commands: `wait <ms>`, `press <btn> [count]`, `hold <btn> <ms>`, `down <btn>`, `up <btn>`, `release`, `speed <mult>`, `shot <name>`, `shot-screen <name>`, `shot-badge <name>`, `reset [seed]`, and `wipe [seed]` (plus `touch <x> <y> [ms]` / `tap <x> <y> [ms]`, `drag <x1> <y1> <x2> <y2> [ms]`, `type-kb <text>`, `tap-label <text>`, `ir <1..11|levelup|savior|auth|0xHEX>`, `solve-attack [count]`, and `dump-ui` in `cores/2024/emu-cli`).
- 2024 inputs: GT911 capacitive touch (`320×240`) + NEC IR beacons (`ir 1..11`, `ir levelup`, `ir savior`)
- 2025 button names: `up`, `down`, `left`, `right`, `center`
- 2026 button names: `up`, `down`, `left`, `right`, `c` (or `center`), `b`, `a`

---

## Running in the Browser

Start the static HTTP server from the repository root:

```bash
./tools/emu-serve
```

`tools/emu-serve` serves `emulator/web/` on port `8765`:

- Museum index: `http://localhost:8765/museum.html`
- 2024 badge: `http://localhost:8765/?year=2024`
- 2025 badge: `http://localhost:8765/?year=2025`
- 2026 badge: `http://localhost:8765/?year=2026`

## Public site (live since 2026-11-01)

- Museum `https://sc.pug.technology/`, 2024 `https://24.sc.pug.technology/` (live since 2026-11-01), 2025 `https://25.sc.pug.technology/`, 2026 `https://26.sc.pug.technology/`.
- Served by the Cloudflare Worker `sc-badge-museum` (static assets only).
  Config: `emulator/deploy/wrangler.jsonc` (no request logs, no workers.dev or preview URLs, 404 page, custom domains).
- Deploy: `cd emulator/deploy && npx -y wrangler@4 deploy`. It uploads only `emulator/web/`; never put build files,
  configs or notes in that folder. Wrangler's login lives in `~/.config/.wrangler/config/default.toml`.
- A new year: add it to `LIVE_YEARS` in index.html and museum.html, add `{ "pattern": "<yy>.sc.pug.technology",
  "custom_domain": true }` to the config, deploy.

---

## Controls by Year

### 2024 ("Tower Battle" — GT911 Capacitive Touch + Virtual IR Towers)

| Input | Hardware | In Browser |
|---|---|---|
| **Touchscreen** | `320×240` GT911 I2C capacitive touch | Pointer click/tap/drag directly on the PCB LCD (`#screen-canvas`) or Big Screen (`#big-screen-canvas`), including mobile touch |
| **IR Beacons** | TSOP IR receiver (`GPIO 2`) | Click any of the 11 stations in the **Towers (11 IR Stations)** panel (plus `⬆️ Level-Up IR` and `🚑 Savior IR`) |
| **Accelerometer** | MC3419 I2C accelerometer | **Wrist Tilt** selector (`Flat`, `Left`, `Right`, `Forward`, `Back`, `Flipped`) |

### 2025 ("The Wrench" — 5-Way Switch `SW2`)

| Button | Bit | Keyboard | On-Screen |
|---|---|---|---|
| **UP** | `bit 0` (`0x01`) | `ArrowUp` or `W` | Drag/hold up on `SW2` or `▲ UP` on pad |
| **DOWN** | `bit 1` (`0x02`) | `ArrowDown` or `S` | Drag/hold down on `SW2` or `▼ DOWN` on pad |
| **LEFT** | `bit 2` (`0x04`) | `ArrowLeft` or `A` | Drag/hold left on `SW2` or `◀ LEFT` on pad |
| **RIGHT** | `bit 3` (`0x08`) | `ArrowRight` or `D` | Drag/hold right on `SW2` or `▶ RIGHT` on pad |
| **CENTER** | `bit 4` (`0x10`) | `Enter`, `Space`, or `E` | Click/hold center of `SW2` or `● CENTER` on pad |

### 2026 ("Orion Trail" — 5-Way `SW4` + Action Buttons `SW2`/`SW3`)

| Button | Bit | Keyboard | On-Screen |
|---|---|---|---|
| **Up** | `bit 0` (`0x01`) | `ArrowUp` or `W` | Drag/hold up on `SW4` (left fin) or `▲ UP` on pad |
| **Down** | `bit 1` (`0x02`) | `ArrowDown` or `S` | Drag/hold down on `SW4` or `▼ DOWN` on pad |
| **Left** | `bit 2` (`0x04`) | `ArrowLeft` or `A` | Drag/hold left on `SW4` or `◀ LEFT` on pad |
| **Right** | `bit 3` (`0x08`) | `ArrowRight` or `D` | Drag/hold right on `SW4` or `▶ RIGHT` on pad |
| **C** | `bit 4` (`0x10`) | `C` | Click/hold center of `SW4` or `● C` on pad |
| **B** | `bit 5` (`0x20`) | `X` | Tap/hold `B` (`SW3`) on right fin or `B` button on pad |
| **A** | `bit 6` (`0x40`) | `Z` | Tap/hold `A` (`SW2`) on right fin or `A` button on pad |

*(USB CDC console output from the 2026 firmware is forwarded to the browser DevTools console under `[badge-usb]`.)*

---

## Generating `layout.json` and `board.png` from KiCad

`emulator/tools/kicad_layout.py` parses `Saintcon2024.kicad_pcb` + `Saintcon2024Top.kicad_pcb`, `Saintcon2025.kicad_pcb`, and `Apollo.kicad_pcb` (automatically converting KiCad 10 syntax in a temporary build copy when rendering with `kicad/kicad:9.0`), traces WS2812 `DIN`→`DOUT` net chains from the MCU GPIO pin where present, extracts screen/button/IR/outline geometry, renders the 3D raytraced PCB, and writes `emulator/web/badges/<year>/{layout.json,board.png}`:

```bash
python3 emulator/tools/kicad_layout.py --year 2024
python3 emulator/tools/kicad_layout.py --year 2025
python3 emulator/tools/kicad_layout.py --year 2026
# or rebuild all years:
python3 emulator/tools/kicad_layout.py --year all
```

---

## How to Add Another Badge Year

1. **Vendor Upstream Source & Record Provenance**:
   - Copy the unmodified upstream firmware and `LICENSE` into `emulator/cores/<year>/upstream/`.
   - Document the repository URL, exact commit hash, author handle, and license in `emulator/cores/<year>/UPSTREAM.md`. Keep any unavoidable build patches separate in `emulator/cores/<year>/patches/`.
2. **Extract PCB Layout & Render Board Art**:
   - Extend `emulator/tools/kicad_layout.py` with a `run_<year>()` target that traces the board outline, screen geometry (`rect` or `round`), LED chain order from the MCU data net, and physical switch positions into `emulator/web/badges/<year>/layout.json` and `board.png`.
3. **Implement the Core ABI (`emulator/cores/<year>/`)**:
   - Replace hardware BSP calls (display, LEDs, buttons, EEPROM, clocks, console) with plain C implementing the 12 functions in `emulator/core/emu.h`.
   - Add `emulator/cores/<year>/build.sh` outputting `emulator/web/badges/<year>/core.{js,wasm}` and a native `emu-cli` runner for headless smoke tests.
4. **Create `emulator/web/badges/<year>/badge.json` & Enable in `museum.html`**:
   - Define `year`, `name`, `screen` (`width`, `height`, `shape`), `leds`, `eeprom_bytes`, `buttons` (`bit`, `name`, `keys`, `x_px`, `y_px`), `modes`, `features`, and `credits`.
   - Follow the **Credits rule**: visibly credit the badge creator(s) directly under the badge with links to their profile, source repository, and license, using only names/handles explicitly written in the upstream repo/files (never guess real names).
   - Flip the year's card in `emulator/web/museum.html` from `Coming Soon` to `Live` linking `./?year=<year>`.
