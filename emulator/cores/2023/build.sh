#!/usr/bin/env bash
# Build the SAINTCON 2023 ("Incidents" original firmware) badge emulator core:
#   1. Native headless CLI (emulator/cores/2023/emu-cli) via host gcc/g++
#   2. WebAssembly core (emulator/web/badges/2023/core.js + core.wasm) via Emscripten in Docker
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.73}"

# Fetch upstream at pinned commits & scrub conference Wi-Fi secrets before building.
bash "${SCRIPT_DIR}/fetch-upstream.sh"

cd "${REPO_ROOT}"

BADGE_DIR="emulator/cores/2023/upstream/Firmware/Source"
ARDUINOJSON_DIR="emulator/cores/2023/upstream/lib_deps/ArduinoJson"
PLATFORM_DIR="emulator/cores/2023/platform"
BUILD_TMP="emulator/build/core-2023-obj"

mkdir -p "emulator/web/badges/2023" "${BUILD_TMP}/wasm" "${BUILD_TMP}/native"

INCLUDES=(
    "-I${PLATFORM_DIR}"
    "-I${PLATFORM_DIR}/include"
    "-I${BADGE_DIR}/include"
    "-I${BADGE_DIR}/src"
    "-I${ARDUINOJSON_DIR}/src"
)

COMMON_DEFS=(
    "-DATTRACT_IDLE_MS=0UL"
    "-DARDUINO=10819"
    "-DESP32=1"
    "-DARDUINOJSON_ENABLE_PROGMEM=0"
    "-DARDUINOJSON_ENABLE_ARDUINO_STREAM=0"
    "-DARDUINOJSON_ENABLE_ARDUINO_PRINT=0"
    "-DARDUINOJSON_ENABLE_ARDUINO_STRING=1"
)

# Normal badge firmware sources (AttractScreen.cpp is excluded; -DATTRACT_IDLE_MS=0UL disables attract mode)
mapfile -t BADGE_CPP_SRCS < <(
    find "${BADGE_DIR}/src" -maxdepth 1 -name '*.cpp' ! -name 'AttractScreen.cpp' | sort
)

printf '%s\n' "${BADGE_CPP_SRCS[@]}" > "${BUILD_TMP}/upstream_cpp.list"

JOBS="$(nproc 2>/dev/null || echo 4)"

echo "[1/3] Building 2023 native headless CLI (emulator/cores/2023/emu-cli)..."
NATIVE_OBJS=()

CLI_OBJ="${BUILD_TMP}/native/cli_2023.o"
gcc -std=gnu11 -O2 -Wall -Wextra "${COMMON_DEFS[@]}" "${INCLUDES[@]}" \
    -c "emulator/cores/2023/cli_2023.c" -o "${CLI_OBJ}"
NATIVE_OBJS+=("${CLI_OBJ}")

PLAT_OBJ="${BUILD_TMP}/native/platform_2023.o"
g++ -std=gnu++17 -O2 -Wall -Wextra "${COMMON_DEFS[@]}" "${INCLUDES[@]}" \
    -c "${PLATFORM_DIR}/platform_2023.cpp" -o "${PLAT_OBJ}"
NATIVE_OBJS+=("${PLAT_OBJ}")

SRV_OBJ="${BUILD_TMP}/native/cli_fake_server.o"
g++ -std=gnu++17 -O2 -Wall -Wextra "${COMMON_DEFS[@]}" "${INCLUDES[@]}" \
    -c "${PLATFORM_DIR}/cli_fake_server.cpp" -o "${SRV_OBJ}"
NATIVE_OBJS+=("${SRV_OBJ}")

export INCLUDES_STR="${INCLUDES[*]}"
export DEFS_STR="${COMMON_DEFS[*]}"
export BUILD_TMP

xargs -P "${JOBS}" -a "${BUILD_TMP}/upstream_cpp.list" -I{} bash -c '
    src="$1"
    obj="${BUILD_TMP}/native/${src//\//__}.o"
    g++ -std=gnu++17 -O2 -w ${DEFS_STR} ${INCLUDES_STR} -c "${src}" -o "${obj}"
' _ {}

for src in "${BADGE_CPP_SRCS[@]}"; do
    NATIVE_OBJS+=("${BUILD_TMP}/native/${src//\//__}.o")
done

g++ -O2 "${NATIVE_OBJS[@]}" -lz -lm -o emulator/cores/2023/emu-cli

EXPORTED_FUNCS='["_emu_init","_emu_step","_emu_framebuffer","_emu_leds","_emu_backlight","_emu_eeprom","_emu_eeprom_dirty","_emu_set_speed","_emu_fb_width","_emu_fb_height","_emu_led_count","_emu_eeprom_size","_emu_send_ir","_emu_set_accel","_emu_set_minibadge","_emu_set_minibadge_label"]'
EXPORTED_RUNTIME='["HEAPU8","HEAPU16","wasmMemory","UTF8ToString","stringToUTF8","lengthBytesUTF8"]'

echo "[2/3] Building 2023 WebAssembly core (emulator/web/badges/2023/core.js + core.wasm) with ${EMSDK_IMAGE}..."
docker run --rm -i \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e BUILD_TMP="${BUILD_TMP}" \
    -e JOBS="${JOBS}" \
    -e INCLUDES_STR="${INCLUDES[*]}" \
    -e DEFS_STR="${COMMON_DEFS[*]}" \
    -e EXPORTED_FUNCS="${EXPORTED_FUNCS}" \
    -e EXPORTED_RUNTIME="${EXPORTED_RUNTIME}" \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${EMSDK_IMAGE}" \
    bash -s <<'DOCKER_EOF'
set -euo pipefail
cp -a /emsdk/upstream/emscripten/cache /tmp/emcache
export EM_CACHE=/tmp/emcache

em++ -std=gnu++17 -O2 -Wall -Wextra ${DEFS_STR} ${INCLUDES_STR} \
    -c emulator/cores/2023/platform/platform_2023.cpp \
    -o "${BUILD_TMP}/wasm/platform_2023.o"

xargs -P "${JOBS}" -a "${BUILD_TMP}/upstream_cpp.list" -I{} bash -c '
    src="$1"
    obj="${BUILD_TMP}/wasm/${src//\//__}.o"
    em++ -std=gnu++17 -O2 -w ${DEFS_STR} ${INCLUDES_STR} -c "${src}" -o "${obj}"
' _ {}

WASM_OBJS=("${BUILD_TMP}/wasm/platform_2023.o")
while IFS= read -r src; do
    WASM_OBJS+=("${BUILD_TMP}/wasm/${src//\//__}.o")
done < "${BUILD_TMP}/upstream_cpp.list"

em++ -O2 "${WASM_OBJS[@]}" \
    -sASYNCIFY=1 \
    -sASYNCIFY_STACK_SIZE=131072 \
    -sWASM=1 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createCore \
    -sEXPORTED_FUNCTIONS="${EXPORTED_FUNCS}" \
    -sEXPORTED_RUNTIME_METHODS="${EXPORTED_RUNTIME}" \
    -sINITIAL_MEMORY=16777216 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sENVIRONMENT=web,worker \
    -o emulator/web/badges/2023/core.js

cp -f /emsdk/upstream/emscripten/LICENSE "${BUILD_TMP}/emscripten-LICENSE.txt"
cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT "${BUILD_TMP}/musl-COPYRIGHT.txt"
cp -f /emsdk/upstream/emscripten/system/lib/libcxx/LICENSE.TXT "${BUILD_TMP}/libcxx-LICENSE.txt"
cp -f /usr/share/common-licenses/Apache-2.0 "${BUILD_TMP}/Apache-2.0.txt"
DOCKER_EOF

bash "${SCRIPT_DIR}/gen-licenses.sh"

echo "[3/3] Build outputs:"
ls -lh "${REPO_ROOT}/emulator/cores/2023/emu-cli" \
       "${REPO_ROOT}/emulator/web/badges/2023/core.js" \
       "${REPO_ROOT}/emulator/web/badges/2023/core.wasm" \
       "${REPO_ROOT}/emulator/web/badges/2023/THIRD_PARTY_LICENSES.txt"
echo "Done."
