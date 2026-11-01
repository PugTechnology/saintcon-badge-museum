# SAINTCON 2018 LED Matrix Badge Upstream Source

- **Repository**: <https://github.com/compukidmike/Saintcon2018>
- **Subdirectory**: `Badge/Badge Source Code/` (`python_files/` and `frozen_modules/`)
- **Pinned commit**: `fee2837d4607ee8a0360e99709894fc5fc608948` (`fee2837d4607`, 2022-11-13 21:23:01 -0700, commit message `"Merge pull request #5 from jrwagz/update-flashing-readme"`)
- **Firmware authors**: [compukidmike](https://github.com/compukidmike) and [miketweaver](https://github.com/compukidmike/Saintcon2018/commits?author=miketweaver) (commit `d21c9a3` `"add frozen modules"` added `Badge/Badge Source Code/frozen_modules/hcid.py` and the frozen module set)
- **Licence**: Licence not stated in the upstream repository. Upstream source is **not** committed to this repository; `fetch-upstream.sh` stages `Badge/Badge Source Code/` into the git-ignored `./upstream/` directory at build time (excluding all upstream `Readme.md` files).

## MicroPython WebAssembly Runtime

`fetch-upstream.sh` pins MicroPython (`https://github.com/micropython/micropython`, `ports/webassembly` and `py/`/`extmod/`/`shared/`) by both release tag and commit SHA:

- `micropython/micropython` `v1.24.1` (commit `ecfdd5d6f9be971852003c2049600dc7b3e2a838`, MIT License), compiled to WebAssembly (`emulator/web/badges/2018/core.js`, `emulator/web/badges/2018/core.wasm`) inside `emscripten/emsdk:3.1.73`, and natively (`emulator/cores/2018/emu-cli`) with host `gcc`.

The badge's bundled asynchronous libraries in `Badge/Badge Source Code/python_files/` (`uasyncio/__init__.py`, `uasyncio/core.py`, `uasyncio/queues.py`, `uasyncio/synchro.py`, `aswitch.py`, `asyn.py`) and frozen modules in `Badge/Badge Source Code/frozen_modules/` (`hcid.py`, `sha256.py`, `microWebSrv.py`, `microWebSocket.py`) are loaded directly as normal Python modules on `sys.path`.

## Secret Scrubbing & Verification

Immediately after extracting `Badge/Badge Source Code/` at `fee2837d4607ee8a0360e99709894fc5fc608948`, `fetch-upstream.sh` scrubs all secrets, challenge flags, and conference Wi-Fi credentials before anything is built or served, verifies each substitution applied, and confirms via shell variables that zero occurrences of the original values exist in `./upstream/Badge/`, `emulator/web/`, or git-tracked files (removing `./upstream/Badge/` and `./upstream/.fetched_commit` and exiting non-zero on any failure):

1. **`Badge/Badge Source Code/frozen_modules/hcid.py`**:
   - Replaces `salt` (the keyed-SHA-256 secret used to derive the 10-character Hacker Challenge ID and 8-character badge Wi-Fi AP password from the MAC address) with `"MUSEUM_FAKE_SALT_2018_00"`.
   - Replaces the conference Wi-Fi password returned by `getBadgeWifiPassword()` with `"MUSEUMWIFI"`.
   - Replaces the Hacker Challenge flag returned by `flag()` with `"flag{MUSEUM_PLACEHOLDER_NOT_A_REAL_FLAG}"`.
2. **`Badge/Badge Source Code/python_files/www/index_1.html` and `Badge/Badge Source Code/python_files/www/partyparrot.html`**:
   - Replaces the two `flag{...}` HTML-comment challenge flags with `"flag{MUSEUM_HTML_PLACEHOLDER_1}"` and `"flag{MUSEUM_HTML_PLACEHOLDER_2}"`.
3. **`Badge/Badge Source Code/python_files/configure.py`**:
   - Replaces `default_ssid` with `"MUSEUM"` (`default_password = "SECRETPASSWORD"` is retained because `"SECRETPASSWORD"` is the literal placeholder sentinel checked by `python_files/wifi_config.py:23` to invoke `HCID.getBadgeWifiPassword()`).
4. **Upstream README files excluded**: `fetch-upstream.sh` archives only `Badge/Badge Source Code` and verifies no `Readme*` file exists in `./upstream/Badge/`.

## Unavoidable Upstream Patches

Aside from the secret-scrubbing substitutions above, `fetch-upstream.sh` applies two 1-line fixes for discrepancies between the upstream git tree and the flashed `Badge/Saintcon2018.bin` firmware image where the committed Python files otherwise fail to parse or import:

1. **`Badge/Badge Source Code/python_files/configure.py` (line 60)**: In git commit `fee2837d4607`, line 60 ends with `)f = open( web_dir + 'index_2.html', 'r')` on a single line (missing the newline present at offset `0x220913` in `Badge/Saintcon2018.bin`), causing a `SyntaxError` when `main.py:12` executes `import configure`. `fetch-upstream.sh` restores the missing newline and indentation (`)\n  f = open( web_dir + 'index_2.html', 'r')`).
2. **`Badge/Badge Source Code/python_files/hc.py` (line 3)**: In git commit `fee2837d4607`, `hc.py:5` calls `hcid = HCID.getHCID()` at module import time without importing `hcid as HCID` (present at offset `0x22201e` in `Badge/Saintcon2018.bin`), raising `NameError: name 'HCID' isn't defined` when `main.py:14` executes `import hc` prior to `import hcid as HCID`. `fetch-upstream.sh` inserts `import hcid as HCID` after `import ujson`.

All other upstream files — `main.py`, `matrix.py`, `IS31FL3730.py`, `Font5x7.py`, `wifi_config.py`, `aswitch.py`, `asyn.py`, `uasyncio/*`, `microWebSrv.py`, `microWebSocket.py`, and `sha256.py` — run unmodified.

- **Legacy public score-server domain (`www.hackerschallenge.org`)**: `Badge/Badge Source Code/python_files/hc.py:4` sets `webserver = "www.hackerschallenge.org"` and sends `User-Agent: SCBadge-v1.0 <hcid>` to `https://www.hackerschallenge.org/scoreapi/keyscore/<id>`. That URL is **never** contacted over the network: the emulator's `urequests` module intercepts the request locally inside the page (and in `emu-cli`) and returns a local stand-in museum JSON payload, and the web shell's Content Security Policy (`connect-src 'self'`) blocks external network connections.
