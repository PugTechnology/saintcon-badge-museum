#!/usr/bin/env bash
# Build script for the SAINTCON 2017 ("HITB Minibadge 2017") core:
#   1. Fetches and scrubs upstream (`fetch-upstream.sh`).
#   2. Regenerates `emulator/web/badges/2017/THIRD_PARTY_LICENSES.txt` (`gen-licenses.sh`).
#   3. Renders `board.png` + `layout.json` from `Base-Rev0.brd` if missing.
#   4. Compiles native headless CLI (`emulator/cores/2017/emu-cli`) and runs `--test`
#      and `--smoke emulator/build/2017-smoke`.
#   5. Compiles WebAssembly core (`emulator/web/badges/2017/core.{js,wasm}`).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
OUT_DIR="${REPO_ROOT}/emulator/web/badges/2017"
SMOKE_DIR="${REPO_ROOT}/emulator/build/2017-smoke"

"${HERE}/fetch-upstream.sh"
"${HERE}/gen-licenses.sh"

if [[ ! -f "${OUT_DIR}/board.png" || ! -f "${OUT_DIR}/layout.json" ]]; then
  python3 "${REPO_ROOT}/emulator/tools/eagle_layout_2017.py"
fi

INO_SRC="${HERE}/upstream/HITBUTT_Minibadge/HITB_Test.ino"

echo "[2017 build] Compiling native headless CLI (emu-cli)..."
g++ -O2 -std=c++17 -Wall -Wextra \
  -I"${HERE}/platform" \
  -DROOT_DIR_STR="\"${REPO_ROOT}\"" \
  "${HERE}/cli_2017.cpp" \
  "${HERE}/platform/emu_2017.cpp" \
  -x c++ -include "${HERE}/platform/Arduino.h" "${INO_SRC}" \
  -o "${HERE}/emu-cli"

echo "[2017 build] Running verification tests and generating smoke screenshots..."
rm -rf "${SMOKE_DIR}"
mkdir -p "${SMOKE_DIR}"
"${HERE}/emu-cli" --test --smoke "${SMOKE_DIR}"

EXPORTED_FUNCS='["_emu_init","_emu_reset","_emu_step","_emu_set_speed","_emu_framebuffer","_emu_fb_width","_emu_fb_height","_emu_leds","_emu_led_count","_emu_backlight","_emu_eeprom","_emu_eeprom_size","_emu_eeprom_dirty","_emu_eeprom_clear_dirty","_emu_2017_set_pad_delta","_emu_2017_clear_pad_overrides","_emu_2017_get_ref","_emu_2017_get_last_raw","_emu_2017_get_last_delta","_emu_2017_get_raw_led_channel","_emu_2017_get_last_brightness","_emu_2017_get_write_count","_malloc","_free"]'

echo "[2017 build] Compiling WebAssembly core (core.js + core.wasm)..."
mkdir -p "${OUT_DIR}"

if [[ -f "/opt/emsdk/emsdk_env.sh" ]]; then
  # shellcheck disable=SC1091
  source "/opt/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi

if command -v em++ >/dev/null 2>&1; then
  em++ -O2 -std=c++17 \
    -I"${HERE}/platform" \
    "${HERE}/platform/emu_2017.cpp" \
    -x c++ -include "${HERE}/platform/Arduino.h" "${INO_SRC}" \
    -sWASM=1 \
    -sALLOW_MEMORY_GROWTH=0 \
    -sINITIAL_MEMORY=4194304 \
    -sSTACK_SIZE=262144 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME="createCore" \
    -sEXPORTED_FUNCTIONS="${EXPORTED_FUNCS}" \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","HEAPU8","HEAPU16"]' \
    -o "${OUT_DIR}/core.js"
else
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${REPO_ROOT}:/src" \
    -w "/src/emulator/cores/2017" \
    emscripten/emsdk:3.1.73 \
    bash -c "
      em++ -O2 -std=c++17 -Iplatform \
        platform/emu_2017.cpp \
        -x c++ -include platform/Arduino.h upstream/HITBUTT_Minibadge/HITB_Test.ino \
        -sWASM=1 \
        -sALLOW_MEMORY_GROWTH=0 \
        -sINITIAL_MEMORY=4194304 \
        -sSTACK_SIZE=262144 \
        -sMODULARIZE=1 \
        -sEXPORT_NAME=createCore \
        -sEXPORTED_FUNCTIONS='${EXPORTED_FUNCS}' \
        -sEXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"HEAPU8\",\"HEAPU16\"]' \
        -o /src/emulator/web/badges/2017/core.js
    "
fi

ls -lh "${HERE}/emu-cli" "${OUT_DIR}/core.js" "${OUT_DIR}/core.wasm"
echo "[2017 build] Done."
