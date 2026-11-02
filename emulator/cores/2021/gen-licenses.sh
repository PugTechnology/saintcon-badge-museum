#!/usr/bin/env bash
# Generate emulator/web/badges/2021/THIRD_PARTY_LICENSES.txt from the upstream
# headers and toolchain license files for everything compiled or converted into
# badges/2021/core.{js,wasm} and badges/2021/flash.bin.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_FW="${HERE}/upstream/Saintcon2021/Saintcon2021"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2021-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2021/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Always run fetch-upstream.sh (cheap when already staged) so the scrub and
# forbidden-path checks are re-verified on every license generation run.
bash "${HERE}/fetch-upstream.sh"

# Cache Emscripten and musl license texts when running inside emsdk container (or extract via docker)
if [[ -f "/emsdk/upstream/emscripten/LICENSE" ]]; then
  cp -f "/emsdk/upstream/emscripten/LICENSE" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "${BUILD_TMP}/musl-COPYRIGHT.txt"
elif [[ ! -f "${BUILD_TMP}/emscripten-LICENSE.txt" || ! -f "${BUILD_TMP}/musl-COPYRIGHT.txt" ]]; then
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${BUILD_TMP}:/out" \
    emscripten/emsdk:3.1.73 \
    bash -c 'cp -f /emsdk/upstream/emscripten/LICENSE /out/emscripten-LICENSE.txt && cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT /out/musl-COPYRIGHT.txt'
fi

append_section() {
  local title="$1"
  local file="$2"
  if [[ ! -f "${file}" ]]; then
    echo "ERROR: Missing license file ${file} for ${title}" >&2
    exit 1
  fi
  printf '================================================================================\n'
  printf '%s\n' "${title}"
  printf '================================================================================\n\n'
  cat "${file}"
  printf '\n\n'
}

append_lines() {
  local title="$1"
  local file="$2"
  local start_line="$3"
  local end_line="$4"
  if [[ ! -f "${file}" ]]; then
    echo "ERROR: Missing source file ${file} for ${title}" >&2
    exit 1
  fi
  printf '================================================================================\n'
  printf '%s\n' "${title}"
  printf '================================================================================\n\n'
  sed -n "${start_line},${end_line}p" "${file}"
  printf '\n\n'
}

{
  cat <<'HEADER'
THIRD-PARTY SOFTWARE NOTICES AND LICENSES
SAINTCON 2021 Combination Lock Badge Emulator
(badges/2021/core.wasm, badges/2021/core.js, badges/2021/flash.bin)

This file contains the licence texts and provenance notices for third-party
software libraries, firmware files, fonts, and converted image assets compiled
or packed into the SAINTCON 2021 combination lock badge WebAssembly core and
external flash image:
  - Original Firmware (core.wasm / core.js) and ExternalFlash assets (flash.bin)
    from compukidmike/Saintcon2021 (https://github.com/compukidmike/Saintcon2021,
    commit 092bf1c68a90cd5d13794006667fd52c3cbb3efd, Saintcon2021/Saintcon2021/
    and Saintcon2021/ExternalFlash/; badge created by compukidmike,
    Professor_Plum, and Sodium_Hydrogen, with firmware contribution from kfeuz
    in commit bfef3ea). Licence not stated.

Microchip's proprietary QTouch library (qtouch/) and ASF4 / CMSIS HAL
directories (hal/, hpl/, hri/, CMSIS/, Device_Startup/, Config/, usb/) from the
upstream repository are excluded prior to compilation and are neither compiled
into core.wasm nor distributed here; they are replaced by this project's own emulator
shims in emulator/cores/2021/platform/.

HEADER

  append_lines \
    "1. 2dAEngine FrameBuffer Module (Saintcon2021/Saintcon2021/FrameBuffer.c, FrameBuffer.h) - Licence not stated" \
    "${UPSTREAM_FW}/FrameBuffer.c" 1 7

  append_lines \
    "2. 5x7 ASCII LCD Font (Saintcon2021/Saintcon2021/font5x7.h) - Licence not stated" \
    "${UPSTREAM_FW}/font5x7.h" 1 13

  cat <<'UPSTREAM_ASSETS'
================================================================================
3. Upstream Fonts, Embedded Bitmaps, and External Flash Images - Licence not stated
================================================================================

  - 8x16 ASCII Bitmap Font (Saintcon2021/Saintcon2021/font8x16.h):
    Included in compukidmike/Saintcon2021
    (commit 092bf1c68a90cd5d13794006667fd52c3cbb3efd). Licence not stated.

  - Embedded RGB565 and 4-bit Icon / Digit Bitmaps
    (Saintcon2021/Saintcon2021/menu_icons.h, combonums.h, small.c):
    Included in compukidmike/Saintcon2021
    (commit 092bf1c68a90cd5d13794006667fd52c3cbb3efd). Licence not stated.

  - External Flash Sprite Sheets Packed into badges/2021/flash.bin
    (Saintcon2021/ExternalFlash/makeFlash.py; sclogo.png, machine_bkg.png,
    machine.png, build.png, parts.png, menu.png, crate.png, nfc.png, ball.png,
    inventory.png, ship.png, explode.png, trade.png, trade2.png, whammy.png,
    rick.png):
    Converted at build time into RGB565 badges/2021/flash.bin via makeFlash.py
    from compukidmike/Saintcon2021
    (commit 092bf1c68a90cd5d13794006667fd52c3cbb3efd). Licence not stated.


UPSTREAM_ASSETS

  append_section \
    "4. Emscripten Runtime (badges/2021/core.js / badges/2021/core.wasm) - MIT / NCSA Dual License" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "5. musl C Standard Library (linked into badges/2021/core.wasm) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"
} > "${OUT_FILE}"

echo "Generated ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
