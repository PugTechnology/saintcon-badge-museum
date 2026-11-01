#!/usr/bin/env bash
# Build the SAINTCON 2024 ("Tower Battle" original firmware) wrist badge emulator core:
#   1. Native headless CLI (emulator/cores/2024/emu-cli) via host gcc/g++
#   2. WebAssembly core (emulator/web/badges/2024/core.js + core.wasm) via Emscripten in Docker
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"

EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.73}"

# Fetch upstream at pinned commits & scrub any conference secrets before building.
bash "${SCRIPT_DIR}/fetch-upstream.sh"

cd "${REPO_ROOT}"

BADGE_DIR="emulator/cores/2024/upstream/Firmware"
MANAGED_DIR="emulator/cores/2024/upstream/managed_components"
PLATFORM_DIR="emulator/cores/2024/platform"
BUILD_TMP="emulator/build/core-2024-obj"

mkdir -p "emulator/web/badges/2024" "${BUILD_TMP}/wasm" "${BUILD_TMP}/native"

echo "[0/3] Generating embedded SPIFFS asset table (${BUILD_TMP}/spiffs_assets.h)..."
python3 - "${BADGE_DIR}/spiffs" "${BUILD_TMP}/spiffs_assets.h" <<'PY'
import pathlib, sys

src_dir = pathlib.Path(sys.argv[1])
out_hdr = pathlib.Path(sys.argv[2])
files = sorted(p for p in src_dir.iterdir() if p.is_file())

lines = [
    "#pragma once",
    "#include <stddef.h>",
    "#include <stdint.h>",
    "",
    "typedef struct {",
    "    const char *name;",
    "    const uint8_t *data;",
    "    size_t size;",
    "} spiffs_asset_t;",
    "",
]

entries = []
for idx, p in enumerate(files):
    raw = p.read_bytes()
    sym = f"k_spiffs_blob_{idx}"
    hex_bytes = ", ".join(f"0x{b:02x}" for b in raw)
    lines.append(f"static const uint8_t {sym}[{len(raw)}] = {{ {hex_bytes} }};")
    entries.append((p.name, sym, len(raw)))

lines.append("")
lines.append("static const spiffs_asset_t k_spiffs_assets[] = {")
for name, sym, sz in entries:
    lines.append(f'    {{ "{name}", {sym}, {sz}u }},')
lines.append("};")
lines.append(f"static const size_t k_spiffs_asset_count = {len(entries)}u;")
lines.append("")

content = "\n".join(lines)
if not out_hdr.exists() or out_hdr.read_text() != content:
    out_hdr.write_text(content)
print(f"Embedded {len(entries)} SPIFFS files ({sum(e[2] for e in entries)} bytes).")
PY

INCLUDES=(
    "-I${BUILD_TMP}"
    "-I${PLATFORM_DIR}"
    "-I${PLATFORM_DIR}/include"
    "-I${PLATFORM_DIR}/include/freertos"
    "-I${BADGE_DIR}/main"
    "-I${BADGE_DIR}/components/accel/include"
    "-I${BADGE_DIR}/components/api/include"
    "-I${BADGE_DIR}/components/badge/include"
    "-I${BADGE_DIR}/components/battery/include"
    "-I${BADGE_DIR}/components/charger/include"
    "-I${BADGE_DIR}/components/display/include"
    "-I${BADGE_DIR}/components/i2c_manager/include"
    "-I${BADGE_DIR}/components/ir_comm/include"
    "-I${BADGE_DIR}/components/load_switch/include"
    "-I${BADGE_DIR}/components/minibadge/include"
    "-I${BADGE_DIR}/components/nvs/include"
    "-I${BADGE_DIR}/components/power_manager/include"
    "-I${BADGE_DIR}/components/type_c/include"
    "-I${BADGE_DIR}/components/ui"
    "-I${BADGE_DIR}/components/ui/include"
    "-I${BADGE_DIR}/components/wifi_manager/include"
    "-I${MANAGED_DIR}/lvgl__lvgl"
    "-I${MANAGED_DIR}/lvgl__lvgl/src"
    "-I${MANAGED_DIR}/johboh__nlohmann-json/include"
)

COMMON_DEFS=(
    '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"'
    "-DLV_CONF_SUPPRESS_DEFINE_CHECK=1"
)

PLATFORM_C_SRCS=(
    "${PLATFORM_DIR}/freertos_sim.c"
    "${PLATFORM_DIR}/storage_sim.c"
    "${PLATFORM_DIR}/peripherals_sim.c"
    "${PLATFORM_DIR}/http_bridge.c"
)

mapfile -t BADGE_C_SRCS < <(
    find "${BADGE_DIR}/main" "${BADGE_DIR}/components" -name '*.c' | sort
)

mapfile -t LVGL_C_SRCS < <(
    find "${MANAGED_DIR}/lvgl__lvgl/src" -name '*.c' \
        ! -path '*/osal/lv_linux.c' \
        ! -path '*/drivers/*' \
        ! -path '*/draw/dma2d/*' \
        ! -path '*/draw/nema_gfx/*' \
        ! -path '*/draw/nxp/*' \
        ! -path '*/draw/opengles/*' \
        ! -path '*/draw/renesas/*' \
        ! -path '*/draw/sdl/*' \
        ! -path '*/draw/vg_lite/*' \
        ! -path '*/draw/sw/arm2d/*' \
        ! -path '*/draw/sw/blend/arm2d/*' \
        ! -path '*/draw/sw/blend/helium/*' \
        ! -path '*/draw/sw/blend/neon/*' \
        | sort
)

BADGE_CPP_SRCS=(
    "${BADGE_DIR}/components/api/api.cpp"
    "${BADGE_DIR}/components/api/api_client.cpp"
    "${BADGE_DIR}/components/api/types.cpp"
)

ALL_UPSTREAM_C=("${BADGE_C_SRCS[@]}" "${LVGL_C_SRCS[@]}")

# Write source manifest files so both host GCC and Docker Emscripten share identical lists
printf '%s\n' "${PLATFORM_C_SRCS[@]}" > "${BUILD_TMP}/platform_c.list"
printf '%s\n' "${ALL_UPSTREAM_C[@]}"  > "${BUILD_TMP}/upstream_c.list"
printf '%s\n' "${BADGE_CPP_SRCS[@]}"  > "${BUILD_TMP}/upstream_cpp.list"

JOBS="$(nproc 2>/dev/null || echo 4)"

echo "[1/3] Building 2024 native headless CLI (emulator/cores/2024/emu-cli)..."
NATIVE_OBJS=()

for src in "${PLATFORM_C_SRCS[@]}" "emulator/cores/2024/cli_2024.c"; do
    obj="${BUILD_TMP}/native/${src//\//__}.o"
    gcc -std=gnu11 -O2 -Wall -Wextra "${COMMON_DEFS[@]}" "${INCLUDES[@]}" -c "${src}" -o "${obj}"
    NATIVE_OBJS+=("${obj}")
done

CLI_SRV_OBJ="${BUILD_TMP}/native/cli_fake_server.o"
g++ -std=gnu++20 -O2 -Wall -Wextra "${COMMON_DEFS[@]}" "${INCLUDES[@]}" \
    -c "${PLATFORM_DIR}/cli_fake_server.cpp" -o "${CLI_SRV_OBJ}"
NATIVE_OBJS+=("${CLI_SRV_OBJ}")

export INCLUDES_STR="${INCLUDES[*]}"
export BUILD_TMP
export SDKCFG="${PLATFORM_DIR}/include/sdkconfig.h"

xargs -P "${JOBS}" -a "${BUILD_TMP}/upstream_c.list" -I{} bash -c '
    src="$1"
    obj="${BUILD_TMP}/native/${src//\//__}.o"
    if [[ ! -f "${obj}" || "${src}" -nt "${obj}" || "${SDKCFG}" -nt "${obj}" ]]; then
        gcc -std=gnu11 -O2 -w -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration '\''-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"'\'' -DLV_CONF_SUPPRESS_DEFINE_CHECK=1 ${INCLUDES_STR} -c "${src}" -o "${obj}"
    fi
' _ {}

xargs -P "${JOBS}" -a "${BUILD_TMP}/upstream_cpp.list" -I{} bash -c '
    src="$1"
    obj="${BUILD_TMP}/native/${src//\//__}.o"
    if [[ ! -f "${obj}" || "${src}" -nt "${obj}" || "${SDKCFG}" -nt "${obj}" ]]; then
        g++ -std=gnu++20 -O2 -w -fno-exceptions -include stdint.h '\''-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"'\'' -DLV_CONF_SUPPRESS_DEFINE_CHECK=1 ${INCLUDES_STR} -c "${src}" -o "${obj}"
    fi
' _ {}

for src in "${ALL_UPSTREAM_C[@]}" "${BADGE_CPP_SRCS[@]}"; do
    NATIVE_OBJS+=("${BUILD_TMP}/native/${src//\//__}.o")
done

g++ -O2 "${NATIVE_OBJS[@]}" \
    -Wl,--wrap=fopen \
    -Wl,--wrap=opendir \
    -Wl,--wrap=stat \
    -Wl,--wrap=remove \
    -Wl,--wrap=unlink \
    -Wl,--wrap=rename \
    -lz -lm \
    -o emulator/cores/2024/emu-cli

EXPORTED_FUNCS='["_emu_init","_emu_step","_emu_framebuffer","_emu_leds","_emu_backlight","_emu_eeprom","_emu_eeprom_dirty","_emu_set_speed","_emu_fb_width","_emu_fb_height","_emu_led_count","_emu_eeprom_size","_emu_set_touch","_emu_send_ir","_emu_set_accel"]'
EXPORTED_RUNTIME='["HEAPU8","HEAPU16","wasmMemory"]'

echo "[2/3] Building 2024 WebAssembly core (emulator/web/badges/2024/core.js + core.wasm) with ${EMSDK_IMAGE}..."
docker run --rm -i \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e BUILD_TMP="${BUILD_TMP}" \
    -e JOBS="${JOBS}" \
    -e INCLUDES_STR="${INCLUDES[*]}" \
    -e EXPORTED_FUNCS="${EXPORTED_FUNCS}" \
    -e EXPORTED_RUNTIME="${EXPORTED_RUNTIME}" \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${EMSDK_IMAGE}" \
    bash -s <<'DOCKER_EOF'
set -euo pipefail
cp -a /emsdk/upstream/emscripten/cache /tmp/emcache
export EM_CACHE=/tmp/emcache

while IFS= read -r src; do
    obj="${BUILD_TMP}/wasm/${src//\//__}.o"
    emcc -std=gnu11 -O2 -Wall -Wextra \
        '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"' -DLV_CONF_SUPPRESS_DEFINE_CHECK=1 \
        ${INCLUDES_STR} -c "${src}" -o "${obj}"
done < "${BUILD_TMP}/platform_c.list"

xargs -P "${JOBS}" -a "${BUILD_TMP}/upstream_c.list" -I{} bash -c '
    src="$1"
    obj="${BUILD_TMP}/wasm/${src//\//__}.o"
    if [[ ! -f "${obj}" || "${src}" -nt "${obj}" || "emulator/cores/2024/platform/include/sdkconfig.h" -nt "${obj}" ]]; then
        emcc -std=gnu11 -O2 -w -Wno-error=incompatible-function-pointer-types -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-function-declaration '\''-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"'\'' -DLV_CONF_SUPPRESS_DEFINE_CHECK=1 ${INCLUDES_STR} -c "${src}" -o "${obj}"
    fi
' _ {}

xargs -P "${JOBS}" -a "${BUILD_TMP}/upstream_cpp.list" -I{} bash -c '
    src="$1"
    obj="${BUILD_TMP}/wasm/${src//\//__}.o"
    if [[ ! -f "${obj}" || "${src}" -nt "${obj}" || "emulator/cores/2024/platform/include/sdkconfig.h" -nt "${obj}" ]]; then
        em++ -std=gnu++20 -O2 -w -fno-exceptions -include stdint.h '\''-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="sdkconfig.h"'\'' -DLV_CONF_SUPPRESS_DEFINE_CHECK=1 ${INCLUDES_STR} -c "${src}" -o "${obj}"
    fi
' _ {}

WASM_OBJS=()
while IFS= read -r src; do WASM_OBJS+=("${BUILD_TMP}/wasm/${src//\//__}.o"); done < "${BUILD_TMP}/platform_c.list"
while IFS= read -r src; do WASM_OBJS+=("${BUILD_TMP}/wasm/${src//\//__}.o"); done < "${BUILD_TMP}/upstream_c.list"
while IFS= read -r src; do WASM_OBJS+=("${BUILD_TMP}/wasm/${src//\//__}.o"); done < "${BUILD_TMP}/upstream_cpp.list"

em++ -O2 "${WASM_OBJS[@]}" \
    -sASYNCIFY=1 \
    -sASYNCIFY_STACK_SIZE=131072 \
    -sEMULATE_FUNCTION_POINTER_CASTS=1 \
    -sWASM=1 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME=createCore \
    -sEXPORTED_FUNCTIONS="${EXPORTED_FUNCS}" \
    -sEXPORTED_RUNTIME_METHODS="${EXPORTED_RUNTIME}" \
    -sINITIAL_MEMORY=33554432 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sFILESYSTEM=1 \
    -sENVIRONMENT=web,worker \
    -o emulator/web/badges/2024/core.js
cp -f /emsdk/upstream/emscripten/LICENSE "${BUILD_TMP}/emscripten-LICENSE.txt"
cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT "${BUILD_TMP}/musl-COPYRIGHT.txt"
DOCKER_EOF

bash "${SCRIPT_DIR}/gen-licenses.sh"

echo "[3/3] Build outputs:"
ls -lh "${REPO_ROOT}/emulator/cores/2024/emu-cli" \
       "${REPO_ROOT}/emulator/web/badges/2024/core.js" \
       "${REPO_ROOT}/emulator/web/badges/2024/core.wasm" \
       "${REPO_ROOT}/emulator/web/badges/2024/THIRD_PARTY_LICENSES.txt"
echo "Done."
