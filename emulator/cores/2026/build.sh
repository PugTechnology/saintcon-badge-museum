#!/usr/bin/env bash
# Build the SAINTCON 2026 ("Orion Trail" / Wayfarer) badge emulator core:
#   1. WebAssembly core (emulator/web/badges/2026/core.js + core.wasm) via Emscripten in Docker
#   2. Native headless CLI (emulator/cores/2026/emu-cli) via host gcc
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.73}"

# The upstream source is fetched at the pinned commit, not committed here.
bash "${SCRIPT_DIR}/fetch-upstream.sh"

UPSTREAM_DIR="emulator/cores/2026/upstream/firmware"
PLATFORM_DIR="emulator/cores/2026/platform"
BUILD_TMP="emulator/build/core-2026-obj"

UPSTREAM_SRCS=(
    "${UPSTREAM_DIR}/game.c"
    "${UPSTREAM_DIR}/generated_events.c"
    "${UPSTREAM_DIR}/ui.c"
    "${UPSTREAM_DIR}/docking.c"
    "${UPSTREAM_DIR}/crew.c"
    "${UPSTREAM_DIR}/asteroids.c"
    "${UPSTREAM_DIR}/nebulagame.c"
    "${UPSTREAM_DIR}/bsp/base64.c"
    "${UPSTREAM_DIR}/bsp/bsp_gost28147.c"
    "${UPSTREAM_DIR}/bsp/bsp_komihash.c"
    "${UPSTREAM_DIR}/bsp/buttons.c"
    "${UPSTREAM_DIR}/bsp/common.c"
    "${UPSTREAM_DIR}/bsp/config.c"
    "${UPSTREAM_DIR}/bsp/console_ui.c"
    "${UPSTREAM_DIR}/bsp/eeprom.c"
    "${UPSTREAM_DIR}/bsp/framebuffer.c"
    "${UPSTREAM_DIR}/bsp/terminal.c"
    "${UPSTREAM_DIR}/bsp/ws2812.c"
)

INCLUDES=(
    "-I${PLATFORM_DIR}"
    "-I${PLATFORM_DIR}/pico_stubs"
    "-I${UPSTREAM_DIR}"
    "-I${UPSTREAM_DIR}/include"
    "-I${UPSTREAM_DIR}/bsp"
)

EXPORTED_FUNCS='["_emu_init","_emu_step","_emu_framebuffer","_emu_leds","_emu_backlight","_emu_eeprom","_emu_eeprom_dirty","_emu_set_speed","_emu_fb_width","_emu_fb_height","_emu_led_count","_emu_eeprom_size"]'
EXPORTED_RUNTIME='["HEAPU8","HEAPU16","wasmMemory"]'

mkdir -p "${REPO_ROOT}/emulator/web/badges/2026"
cp "${REPO_ROOT}/emulator/cores/2026/upstream/LICENSE" "${REPO_ROOT}/emulator/web/badges/2026/LICENSE.txt"
mkdir -p "${REPO_ROOT}/${BUILD_TMP}/wasm" "${REPO_ROOT}/${BUILD_TMP}/native"

echo "[1/2] Building 2026 WebAssembly core (emulator/web/badges/2026/core.js + core.wasm) with ${EMSDK_IMAGE}..."
docker run --rm \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${EMSDK_IMAGE}" \
    bash -lc "set -euo pipefail
        cp -a /emsdk/upstream/emscripten/cache /tmp/emcache
        export EM_CACHE=/tmp/emcache
        emcc -std=gnu11 -O2 -Wall -Wextra -Werror ${INCLUDES[*]} \
            -c ${PLATFORM_DIR}/platform_2026.c -o ${BUILD_TMP}/wasm/platform_2026.o
        emcc -std=gnu11 -O2 -w ${INCLUDES[*]} -Dmain=wayfarer_main \
            -c ${UPSTREAM_DIR}/Wayfarer.c -o ${BUILD_TMP}/wasm/Wayfarer.o
        OBJS=(\"${BUILD_TMP}/wasm/platform_2026.o\" \"${BUILD_TMP}/wasm/Wayfarer.o\")
        for src in ${UPSTREAM_SRCS[*]}; do
            obj=\"${BUILD_TMP}/wasm/\$(basename \"\${src}\" .c).o\"
            emcc -std=gnu11 -O2 -w ${INCLUDES[*]} -c \"\${src}\" -o \"\${obj}\"
            OBJS+=(\"\${obj}\")
        done
        emcc -O2 \"\${OBJS[@]}\" \
            -sASYNCIFY=1 \
            -sASYNCIFY_STACK_SIZE=262144 \
            -sWASM=1 \
            -sMODULARIZE=1 \
            -sEXPORT_NAME=createCore \
            -sEXPORTED_FUNCTIONS='${EXPORTED_FUNCS}' \
            -sEXPORTED_RUNTIME_METHODS='${EXPORTED_RUNTIME}' \
            -sINITIAL_MEMORY=16777216 \
            -sALLOW_MEMORY_GROWTH=0 \
            -sFILESYSTEM=0 \
            -sENVIRONMENT=web,worker \
            -o emulator/web/badges/2026/core.js
        cp -f /emsdk/upstream/emscripten/LICENSE ${BUILD_TMP}/emscripten-LICENSE.txt
        cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT ${BUILD_TMP}/musl-COPYRIGHT.txt"

echo "[2/2] Building 2026 native headless CLI (emulator/cores/2026/emu-cli) with host gcc..."
cd "${REPO_ROOT}"
gcc -std=c11 -O2 -Wall -Wextra -Werror "${INCLUDES[@]}" \
    -c "${PLATFORM_DIR}/platform_2026.c" -o "${BUILD_TMP}/native/platform_2026.o"
gcc -std=c11 -O2 -Wall -Wextra -Werror "${INCLUDES[@]}" \
    -c "emulator/cores/2026/cli_2026.c" -o "${BUILD_TMP}/native/cli_2026.o"
gcc -std=gnu11 -O2 -w "${INCLUDES[@]}" -Dmain=wayfarer_main \
    -c "${UPSTREAM_DIR}/Wayfarer.c" -o "${BUILD_TMP}/native/Wayfarer.o"

NATIVE_OBJS=(
    "${BUILD_TMP}/native/cli_2026.o"
    "${BUILD_TMP}/native/platform_2026.o"
    "${BUILD_TMP}/native/Wayfarer.o"
)
for src in "${UPSTREAM_SRCS[@]}"; do
    obj="${BUILD_TMP}/native/$(basename "${src}" .c).o"
    gcc -std=gnu11 -O2 -w "${INCLUDES[@]}" -c "${src}" -o "${obj}"
    NATIVE_OBJS+=("${obj}")
done

gcc -O2 "${NATIVE_OBJS[@]}" -lz -lm -o emulator/cores/2026/emu-cli

bash "${SCRIPT_DIR}/gen-licenses.sh"

ls -lh "${REPO_ROOT}/emulator/web/badges/2026/core.js" \
       "${REPO_ROOT}/emulator/web/badges/2026/core.wasm" \
       "${REPO_ROOT}/emulator/web/badges/2026/LICENSE.txt" \
       "${REPO_ROOT}/emulator/web/badges/2026/THIRD_PARTY_LICENSES.txt" \
       "${REPO_ROOT}/emulator/cores/2026/emu-cli"
echo "Done."
