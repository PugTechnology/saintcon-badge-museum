#!/usr/bin/env bash
# Generate emulator/web/badges/2019/THIRD_PARTY_LICENSES.txt from the upstream
# source headers and license files for everything compiled into badges/2019/core.{js,wasm}.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_DIR="${HERE}/upstream"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2019-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2019/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Stage (or re-verify) the pinned upstream firmware; cheap when already staged.
bash "${HERE}/fetch-upstream.sh"

# Cache Emscripten, musl, and Apache-2.0 license texts when running inside emsdk container (or extract via docker)
if [[ -f "/emsdk/upstream/emscripten/LICENSE" ]]; then
  cp -f "/emsdk/upstream/emscripten/LICENSE" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "${BUILD_TMP}/musl-COPYRIGHT.txt"
  cp -f "/usr/share/common-licenses/Apache-2.0" "${BUILD_TMP}/Apache-2.0.txt"
elif [[ ! -f "${BUILD_TMP}/emscripten-LICENSE.txt" || ! -f "${BUILD_TMP}/musl-COPYRIGHT.txt" || ! -f "${BUILD_TMP}/Apache-2.0.txt" ]]; then
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${BUILD_TMP}:/out" \
    emscripten/emsdk:3.1.73 \
    bash -c 'cp -f /emsdk/upstream/emscripten/LICENSE /out/emscripten-LICENSE.txt && cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT /out/musl-COPYRIGHT.txt && cp -f /usr/share/common-licenses/Apache-2.0 /out/Apache-2.0.txt'
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
SAINTCON 2019 Enigma Badge Emulator (badges/2019/core.wasm, badges/2019/core.js)

This file contains the licence texts and provenance notices for third-party
software libraries, firmware files, fonts, and embedded sprite assets compiled
into the SAINTCON 2019 Enigma badge WebAssembly core:
  - Original Firmware (core.wasm / core.js) from compukidmike/Saintcon2019
    (https://github.com/compukidmike/Saintcon2019, commit
    4ec59a1626dca09a0c75969f449a0ed3cca18bac, Firmware/Saintcon2019/; badge by
    compukidmike, with firmware modules and Enigma modifications credited in
    source headers to @_bashNinja / bashNinja, Cory Lutton, AND!XOR LLC, and
    bikefrivolously/led_matrix). Licence not stated for the repository as a
    whole; per-file notices are reproduced below.

STMicroelectronics' STM32L4xx HAL (Drivers/STM32L4xx_HAL_Driver/), CMSIS
(Drivers/CMSIS/), STM32 USB Device Library (Middlewares/ST/STM32_USB_Device_Library/),
EEPROM_Emul middleware (Middlewares/ST/EEPROM_Emul/, Inc/eeprom_emul_conf.h),
and the iCE40 FPGA bitstream loader (Src/iceboot.c, Src/fpga.bin) from the
upstream repository are excluded prior to compilation and are neither compiled
into core.wasm nor distributed here; they are replaced by this project's own
emulator shims in emulator/cores/2019/platform/.

HEADER

  append_lines \
    "1. Enigma Machine Engine (Firmware/Saintcon2019/Inc/enigma.h, Src/enigma.c) - Apache License 2.0" \
    "${UPSTREAM_DIR}/Firmware/Saintcon2019/Inc/enigma.h" 1 27

  append_lines \
    "2. IS31FL3736 LED Matrix Driver (Firmware/Saintcon2019/Drivers/IS31FL3736/drv_is31fl.h, drv_is31fl.c) - Apache License 2.0" \
    "${UPSTREAM_DIR}/Firmware/Saintcon2019/Drivers/IS31FL3736/drv_is31fl.h" 1 34

  append_section \
    "2a. Apache License, Version 2.0 (text)" \
    "${BUILD_TMP}/Apache-2.0.txt"

  append_lines \
    "3. Screen Animations, Snake Game, and User Profile Modules (Firmware/Saintcon2019/Src/screen_animations.c, Inc/screen_animations.h, Src/snake.c, Inc/snake.h, Src/user_profile.c) - Licence not stated" \
    "${UPSTREAM_DIR}/Firmware/Saintcon2019/Src/screen_animations.c" 1 7

  printf 'Attribution comments in Firmware/Saintcon2019/Src/screen_animations.c (lines 431-434, 439-442, 506-509, 514-517):\n\n'
  sed -n '431,434p' "${UPSTREAM_DIR}/Firmware/Saintcon2019/Src/screen_animations.c"
  printf '\n\n'

  append_lines \
    "4. Main Application Entry & Pin Definitions (Firmware/Saintcon2019/Src/main.c, Inc/main.h) - BSD 3-Clause License" \
    "${UPSTREAM_DIR}/Firmware/Saintcon2019/Inc/main.h" 1 20

  cat <<'BSD3_AND_UPSTREAM'
BSD 3-Clause License (as referenced in Firmware/Saintcon2019/Src/main.c and Inc/main.h):

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


================================================================================
5. Remaining Upstream Badge Firmware Modules, Font, and Sprite Bitmaps - Licence not stated
================================================================================

  - Badge Firmware Modules (Firmware/Saintcon2019/Src/animations.c,
    Src/custommessage.c, Src/enigma_emulator.c, Src/images.c, Src/keyboard.c,
    Src/lampboard.c, Src/matrix.c, Src/menu.c, Src/minibadge.c,
    Src/plugboard.c, Src/testmode.c, and corresponding headers in Inc/):
    Authored by compukidmike (Author: Compukidmike stated in
    Src/custommessage.c, Inc/custommessage.h, Src/minibadge.c, Inc/minibadge.h,
    Src/testmode.c, Inc/testmode.h) in compukidmike/Saintcon2019
    (commit 4ec59a1626dca09a0c75969f449a0ed3cca18bac). Licence not stated.

  - 5x7 ASCII Bitmap Font and RGB888 Sprites (Firmware/Saintcon2019/Inc/font5x7.h,
    Inc/images.h, Src/images.c, Inc/snake_images.h):
    Included in compukidmike/Saintcon2019
    (commit 4ec59a1626dca09a0c75969f449a0ed3cca18bac). Licence not stated.


BSD3_AND_UPSTREAM

  append_section \
    "6. Emscripten Runtime (badges/2019/core.js / badges/2019/core.wasm) - MIT / NCSA Dual License" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "7. musl C Standard Library (linked into badges/2019/core.wasm) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"
} > "${OUT_FILE}"

echo "[2019 gen-licenses] Wrote ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
