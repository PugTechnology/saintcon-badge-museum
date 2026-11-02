#!/usr/bin/env bash
# Build the SAINTCON 2021 combination lock badge emulator core:
#   0. External QSPI flash image (emulator/web/badges/2021/flash.bin) via upstream makeFlash.py
#   1. Native headless CLI (emulator/cores/2021/emu-cli) via host gcc
#   2. WebAssembly core (emulator/web/badges/2021/core.js + core.wasm) via Emscripten in Docker
#   3. Third-party license notices (emulator/web/badges/2021/THIRD_PARTY_LICENSES.txt)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.73}"

# Stage upstream at pinned HEAD commit & scrub secrets before building.
bash "${SCRIPT_DIR}/fetch-upstream.sh"

cd "${REPO_ROOT}"

BADGE_DIR="emulator/cores/2021/upstream/Saintcon2021/Saintcon2021"
EXTFLASH_DIR="emulator/cores/2021/upstream/Saintcon2021/ExternalFlash"
PLATFORM_DIR="emulator/cores/2021/platform"
BUILD_TMP="emulator/build/core-2021-obj"
OUT_WEB_DIR="emulator/web/badges/2021"

mkdir -p "${OUT_WEB_DIR}" "${BUILD_TMP}/wasm" "${BUILD_TMP}/native"

echo "[0/4] Building external flash image (${OUT_WEB_DIR}/flash.bin) via upstream makeFlash.py..."
EXTFLASH_TMP="${BUILD_TMP}/extflash-run"
rm -rf "${EXTFLASH_TMP}"
mkdir -p "${EXTFLASH_TMP}"
cp -f "${EXTFLASH_DIR}/makeFlash.py" "${EXTFLASH_DIR}/"*.png "${EXTFLASH_TMP}/"
(
    cd "${EXTFLASH_TMP}"
    python3 makeFlash.py > "${REPO_ROOT}/${BUILD_TMP}/makeFlash.log"
)
mv -f "${EXTFLASH_TMP}/flash.bin" "${OUT_WEB_DIR}/flash.bin"
rm -rf "${EXTFLASH_TMP}"
python3 - "${OUT_WEB_DIR}/flash.bin" "${OUT_WEB_DIR}/badge.json" <<'PY'
import json
import os
import sys

flash_path, badge_json_path = sys.argv[1], sys.argv[2]
flash_bytes = os.path.getsize(flash_path)
with open(badge_json_path, "r", encoding="utf-8") as f:
    meta = json.load(f)
meta["flash_bin_bytes"] = flash_bytes
with open(badge_json_path, "w", encoding="utf-8") as f:
    json.dump(meta, f, indent=2, ensure_ascii=False)
    f.write("\n")
print(f"    flash_bin_bytes = {flash_bytes}")
PY

INCLUDES=(
    "-I${PLATFORM_DIR}"
    "-I${BADGE_DIR}"
    "-Iemulator/cores/2021"
)

# ARM EABI defaults to -fshort-enums and unsigned char; required so packed
# `requirement` (machine_common.h) is 2 bytes and `trade_message` (tradeScene.c)
# is 16 bytes (one AES-128 block).
ABI_FLAGS=(
    "-std=gnu11"
    "-O2"
    "-fshort-enums"
    "-funsigned-char"
)

PLATFORM_C_SRCS=(
    "${PLATFORM_DIR}/crypto_shim.c"
    "${PLATFORM_DIR}/emu_2021.c"
)

UPSTREAM_C_SRCS=(
    "${BADGE_DIR}/FrameBuffer.c"
    "${BADGE_DIR}/RougeMaze.c"
    "${BADGE_DIR}/eeprom.c"
    "${BADGE_DIR}/flash.c"
    "${BADGE_DIR}/machine_common.c"
    "${BADGE_DIR}/main.c"
    "${BADGE_DIR}/nfc.c"
    "${BADGE_DIR}/platform.c"
    "${BADGE_DIR}/small.c"
    "${BADGE_DIR}/unlocks.c"
    "${BADGE_DIR}/Scenes/buildScene.c"
    "${BADGE_DIR}/Scenes/comboScene.c"
    "${BADGE_DIR}/Scenes/gameScene.c"
    "${BADGE_DIR}/Scenes/inventoryScene.c"
    "${BADGE_DIR}/Scenes/machineScene.c"
    "${BADGE_DIR}/Scenes/menuScene.c"
    "${BADGE_DIR}/Scenes/messageScene.c"
    "${BADGE_DIR}/Scenes/nfcScene.c"
    "${BADGE_DIR}/Scenes/rewardScene.c"
    "${BADGE_DIR}/Scenes/rickScene.c"
    "${BADGE_DIR}/Scenes/testScene.c"
    "${BADGE_DIR}/Scenes/tradeScene.c"
)

printf '%s\n' "${PLATFORM_C_SRCS[@]}" > "${BUILD_TMP}/platform_c.list"
printf '%s\n' "${UPSTREAM_C_SRCS[@]}" > "${BUILD_TMP}/upstream_c.list"

echo "[1/4] Building 2021 native headless CLI (emulator/cores/2021/emu-cli)..."
NATIVE_OBJS=()

for src in "${PLATFORM_C_SRCS[@]}" "emulator/cores/2021/cli_2021.c"; do
    obj="${BUILD_TMP}/native/${src//\//__}.o"
    gcc "${ABI_FLAGS[@]}" -Wall -Wextra "${INCLUDES[@]}" -c "${src}" -o "${obj}"
    NATIVE_OBJS+=("${obj}")
done

for src in "${UPSTREAM_C_SRCS[@]}"; do
    obj="${BUILD_TMP}/native/${src//\//__}.o"
    gcc "${ABI_FLAGS[@]}" -w \
        -Wno-error=implicit-function-declaration \
        -Wno-error=incompatible-pointer-types \
        -Wno-error=int-conversion \
        -Wno-error=return-type \
        -Dmain=sc21_firmware_main \
        "${INCLUDES[@]}" -c "${src}" -o "${obj}"
    NATIVE_OBJS+=("${obj}")
done

gcc "${ABI_FLAGS[@]}" "${NATIVE_OBJS[@]}" -lz -lm -o emulator/cores/2021/emu-cli

EXPORTED_FUNCS='["_malloc","_free","_emu_init","_emu_step","_emu_framebuffer","_emu_leds","_emu_backlight","_emu_eeprom","_emu_eeprom_dirty","_emu_set_speed","_emu_fb_width","_emu_fb_height","_emu_led_count","_emu_eeprom_size","_emu_set_touch","_emu_2021_load_flash","_emu_2021_vcard_ptr","_emu_2021_vcard_size","_emu_2021_vcard_dirty","_emu_2021_erase_vcard","_emu_2021_set_shackle","_emu_2021_get_shackle","_emu_2021_pulse_shim","_emu_2021_set_wheel","_emu_2021_nfc_quote_count","_emu_2021_nfc_quote_label","_emu_2021_nfc_present_quote_tag","_emu_2021_nfc_present_text_tag","_emu_2021_nfc_clear_tag","_emu_2021_nfc_program_vcard","_emu_2021_nfc_last_vcard_status","_emu_2021_nfc_read_emulated_tag","_emu_2021_nfc_start_bot_trade","_emu_2021_nfc_bot_trade_status","_emu_2021_mb_set_present","_emu_2021_mb_get_present","_emu_2021_mb_press_button","_emu_2021_mb_send_text"]'
EXPORTED_RUNTIME='["HEAPU8","HEAPU16","wasmMemory","UTF8ToString","stringToUTF8","lengthBytesUTF8"]'

echo "[2/4] Building 2021 WebAssembly core (${OUT_WEB_DIR}/core.js + core.wasm) with ${EMSDK_IMAGE}..."
docker run --rm -i \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e BUILD_TMP="${BUILD_TMP}" \
    -e INCLUDES_STR="${INCLUDES[*]}" \
    -e ABI_FLAGS_STR="${ABI_FLAGS[*]}" \
    -e EXPORTED_FUNCS="${EXPORTED_FUNCS}" \
    -e EXPORTED_RUNTIME="${EXPORTED_RUNTIME}" \
    -e OUT_WEB_DIR="${OUT_WEB_DIR}" \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${EMSDK_IMAGE}" \
    bash -s <<'DOCKER_EOF'
set -euo pipefail
cp -a /emsdk/upstream/emscripten/cache /tmp/emcache
export EM_CACHE=/tmp/emcache

WASM_OBJS=()

while IFS= read -r src; do
    obj="${BUILD_TMP}/wasm/${src//\//__}.o"
    emcc ${ABI_FLAGS_STR} -Wall -Wextra ${INCLUDES_STR} -c "${src}" -o "${obj}"
    WASM_OBJS+=("${obj}")
done < "${BUILD_TMP}/platform_c.list"

while IFS= read -r src; do
    obj="${BUILD_TMP}/wasm/${src//\//__}.o"
    emcc ${ABI_FLAGS_STR} -w \
        -Wno-error=incompatible-function-pointer-types \
        -Wno-error=incompatible-pointer-types \
        -Wno-error=int-conversion \
        -Wno-error=implicit-function-declaration \
        -Wno-error=return-type \
        -Dmain=sc21_firmware_main \
        ${INCLUDES_STR} -c "${src}" -o "${obj}"
    WASM_OBJS+=("${obj}")
done < "${BUILD_TMP}/upstream_c.list"

emcc ${ABI_FLAGS_STR} "${WASM_OBJS[@]}" \
    -sASYNCIFY=1 \
    -sASYNCIFY_STACK_SIZE=131072 \
    -sWASM=1 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createCore \
    -sEXPORTED_FUNCTIONS="${EXPORTED_FUNCS}" \
    -sEXPORTED_RUNTIME_METHODS="${EXPORTED_RUNTIME}" \
    -sINITIAL_MEMORY=33554432 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sENVIRONMENT=web,worker \
    -o "${OUT_WEB_DIR}/core.js"

cp -f /emsdk/upstream/emscripten/LICENSE "${BUILD_TMP}/emscripten-LICENSE.txt"
cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT "${BUILD_TMP}/musl-COPYRIGHT.txt"
DOCKER_EOF

echo "[3/4] Generating third-party license notices..."
bash "${SCRIPT_DIR}/gen-licenses.sh"

echo "[4/4] Build outputs:"
ls -lh "${REPO_ROOT}/emulator/cores/2021/emu-cli" \
       "${REPO_ROOT}/${OUT_WEB_DIR}/flash.bin" \
       "${REPO_ROOT}/${OUT_WEB_DIR}/core.js" \
       "${REPO_ROOT}/${OUT_WEB_DIR}/core.wasm" \
       "${REPO_ROOT}/${OUT_WEB_DIR}/THIRD_PARTY_LICENSES.txt"
echo "Done."
