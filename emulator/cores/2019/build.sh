#!/usr/bin/env bash
# Build SAINTCON 2019 Enigma Badge original firmware to:
#   1. emulator/cores/2019/emu-cli              (headless native verification & smoke screenshot CLI)
#   2. emulator/web/badges/2019/core.{js,wasm}  (browser WebAssembly core)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
UPSTREAM_FW="${SCRIPT_DIR}/upstream/Firmware/Saintcon2019"
OUT_WEB="${ROOT_DIR}/emulator/web/badges/2019"
OBJ_DIR="${ROOT_DIR}/emulator/build/core-2019-obj"

mkdir -p "${OUT_WEB}" "${OBJ_DIR}"

# 1. Stage upstream firmware & regenerate THIRD_PARTY_LICENSES.txt
"${SCRIPT_DIR}/fetch-upstream.sh"
"${SCRIPT_DIR}/gen-licenses.sh"

INCLUDES=(
  "-I${SCRIPT_DIR}/platform"
  "-I${UPSTREAM_FW}/Inc"
  "-I${UPSTREAM_FW}/Drivers/IS31FL3736"
)

FW_SRCS=(
  "${UPSTREAM_FW}/Src/animations.c"
  "${UPSTREAM_FW}/Src/custommessage.c"
  "${UPSTREAM_FW}/Src/enigma.c"
  "${UPSTREAM_FW}/Src/images.c"
  "${UPSTREAM_FW}/Src/lampboard.c"
  "${UPSTREAM_FW}/Src/matrix.c"
  "${UPSTREAM_FW}/Src/menu.c"
  "${UPSTREAM_FW}/Src/minibadge.c"
  "${UPSTREAM_FW}/Src/plugboard.c"
  "${UPSTREAM_FW}/Src/screen_animations.c"
  "${UPSTREAM_FW}/Src/snake.c"
  "${UPSTREAM_FW}/Src/testmode.c"
  "${UPSTREAM_FW}/Drivers/IS31FL3736/drv_is31fl.c"
)

COMMON_WARN=(
  "-std=gnu11"
  "-funsigned-char"
  "-Wno-implicit-function-declaration"
  "-Wno-int-conversion"
  "-Wno-incompatible-pointer-types"
  "-Wno-pointer-sign"
  "-Wno-return-type"
  "-Wno-unused-variable"
  "-Wno-unused-but-set-variable"
  "-Wno-parentheses"
  "-ftrivial-auto-var-init=zero"
)

# 2. Build native headless CLI (emu-cli)
# Note: -fno-pie -no-pie places .rodata string literals below 2 GB on x86_64 so
# user_profile.c's unpatched `int messageStart = INITIAL_RING;` preserves the full pointer.
echo "[2019 build] Compiling native headless CLI (emu-cli)..."
gcc -O2 -g -fno-pie -fno-toplevel-reorder "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
  -Dmain=badge_2019_fw_main \
  -c "${UPSTREAM_FW}/Src/main.c" -o "${OBJ_DIR}/fw_main.o"

gcc -O2 -g -fno-pie -fno-toplevel-reorder "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
  -Dtest=user_profile_test \
  -c "${UPSTREAM_FW}/Src/user_profile.c" -o "${OBJ_DIR}/user_profile.o"

gcc -O2 -g -fno-pie -fno-toplevel-reorder "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
  -DkeyboardUpdate=fw_keyboardUpdate \
  -c "${UPSTREAM_FW}/Src/keyboard.c" -o "${OBJ_DIR}/keyboard.o"

gcc -O2 -g -fno-pie -fno-toplevel-reorder "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
  -Denigma_menu=fw_enigma_menu \
  -c "${UPSTREAM_FW}/Src/enigma_emulator.c" -o "${OBJ_DIR}/enigma_emulator.o"

gcc -O2 -g -fno-pie -no-pie -fno-toplevel-reorder "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
  "${SCRIPT_DIR}/cli_2019.c" \
  "${SCRIPT_DIR}/platform/emu_2019.c" \
  "${OBJ_DIR}/fw_main.o" \
  "${OBJ_DIR}/user_profile.o" \
  "${OBJ_DIR}/keyboard.o" \
  "${OBJ_DIR}/enigma_emulator.o" \
  "${FW_SRCS[@]}" \
  -lm -lz \
  -o "${SCRIPT_DIR}/emu-cli"

echo "[2019 build] Built ${SCRIPT_DIR}/emu-cli"

# 3. Build WebAssembly core (badges/2019/core.js + core.wasm)
EXPORTED_FUNCS='["_emu_init","_emu_reset","_emu_step","_emu_set_speed","_emu_framebuffer","_emu_fb_width","_emu_fb_height","_emu_leds","_emu_led_count","_emu_backlight","_emu_eeprom","_emu_eeprom_size","_emu_eeprom_dirty","_emu_eeprom_clear_dirty","_emu_2019_set_keys","_emu_2019_get_keys","_emu_2019_plugboard_clear","_emu_2019_plugboard_connect","_emu_2019_plugboard_disconnect","_emu_2019_plugboard_get_partner","_emu_2019_plugboard_get_state","_emu_2019_get_badge_level","_emu_2019_get_menu_state","_emu_2019_start_factory_test","_malloc","_free"]'

echo "[2019 build] Compiling WebAssembly core (core.js + core.wasm)..."
for cand in "$HOME/.local/share/emsdk/emsdk_env.sh" "/opt/emsdk/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh"; do
  if [[ -f "$cand" ]]; then
    # shellcheck disable=SC1090
    source "$cand" >/dev/null 2>&1
    break
  fi
done

if command -v emcc >/dev/null 2>&1; then
  emcc -O2 "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
    -Dmain=badge_2019_fw_main \
    -c "${UPSTREAM_FW}/Src/main.c" -o "${OBJ_DIR}/fw_main_wasm.o"
  emcc -O2 "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
    -Dtest=user_profile_test \
    -c "${UPSTREAM_FW}/Src/user_profile.c" -o "${OBJ_DIR}/user_profile_wasm.o"
  emcc -O2 "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
    -DkeyboardUpdate=fw_keyboardUpdate \
    -c "${UPSTREAM_FW}/Src/keyboard.c" -o "${OBJ_DIR}/keyboard_wasm.o"
  emcc -O2 "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
    -Denigma_menu=fw_enigma_menu \
    -c "${UPSTREAM_FW}/Src/enigma_emulator.c" -o "${OBJ_DIR}/enigma_emulator_wasm.o"
  emcc -O2 "${INCLUDES[@]}" "${COMMON_WARN[@]}" \
    "${SCRIPT_DIR}/platform/emu_2019.c" \
    "${OBJ_DIR}/fw_main_wasm.o" \
    "${OBJ_DIR}/user_profile_wasm.o" \
    "${OBJ_DIR}/keyboard_wasm.o" \
    "${OBJ_DIR}/enigma_emulator_wasm.o" \
    "${FW_SRCS[@]}" \
    -sWASM=1 \
    -sASYNCIFY=1 \
    -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=0 \
    -sINITIAL_MEMORY=16777216 \
    -sSTACK_SIZE=524288 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME="createCore" \
    -sEXPORTED_FUNCTIONS="${EXPORTED_FUNCS}" \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","HEAPU8","HEAPU16"]' \
    -o "${OUT_WEB}/core.js"
else
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${ROOT_DIR}:/src" \
    -w "/src/emulator/cores/2019" \
    emscripten/emsdk:3.1.73 \
    bash -c "
      emcc -O2 -Iplatform -Iupstream/Firmware/Saintcon2019/Inc -Iupstream/Firmware/Saintcon2019/Drivers/IS31FL3736 \
        ${COMMON_WARN[*]} -Dmain=badge_2019_fw_main \
        -c upstream/Firmware/Saintcon2019/Src/main.c -o /src/emulator/build/core-2019-obj/fw_main_wasm.o && \
      emcc -O2 -Iplatform -Iupstream/Firmware/Saintcon2019/Inc -Iupstream/Firmware/Saintcon2019/Drivers/IS31FL3736 \
        ${COMMON_WARN[*]} -Dtest=user_profile_test \
        -c upstream/Firmware/Saintcon2019/Src/user_profile.c -o /src/emulator/build/core-2019-obj/user_profile_wasm.o && \
      emcc -O2 -Iplatform -Iupstream/Firmware/Saintcon2019/Inc -Iupstream/Firmware/Saintcon2019/Drivers/IS31FL3736 \
        ${COMMON_WARN[*]} -DkeyboardUpdate=fw_keyboardUpdate \
        -c upstream/Firmware/Saintcon2019/Src/keyboard.c -o /src/emulator/build/core-2019-obj/keyboard_wasm.o && \
      emcc -O2 -Iplatform -Iupstream/Firmware/Saintcon2019/Inc -Iupstream/Firmware/Saintcon2019/Drivers/IS31FL3736 \
        ${COMMON_WARN[*]} -Denigma_menu=fw_enigma_menu \
        -c upstream/Firmware/Saintcon2019/Src/enigma_emulator.c -o /src/emulator/build/core-2019-obj/enigma_emulator_wasm.o && \
      emcc -O2 -Iplatform -Iupstream/Firmware/Saintcon2019/Inc -Iupstream/Firmware/Saintcon2019/Drivers/IS31FL3736 \
        ${COMMON_WARN[*]} \
        platform/emu_2019.c \
        /src/emulator/build/core-2019-obj/fw_main_wasm.o \
        /src/emulator/build/core-2019-obj/user_profile_wasm.o \
        /src/emulator/build/core-2019-obj/keyboard_wasm.o \
        /src/emulator/build/core-2019-obj/enigma_emulator_wasm.o \
        upstream/Firmware/Saintcon2019/Src/animations.c \
        upstream/Firmware/Saintcon2019/Src/custommessage.c \
        upstream/Firmware/Saintcon2019/Src/enigma.c \
        upstream/Firmware/Saintcon2019/Src/images.c \
        upstream/Firmware/Saintcon2019/Src/lampboard.c \
        upstream/Firmware/Saintcon2019/Src/matrix.c \
        upstream/Firmware/Saintcon2019/Src/menu.c \
        upstream/Firmware/Saintcon2019/Src/minibadge.c \
        upstream/Firmware/Saintcon2019/Src/plugboard.c \
        upstream/Firmware/Saintcon2019/Src/screen_animations.c \
        upstream/Firmware/Saintcon2019/Src/snake.c \
        upstream/Firmware/Saintcon2019/Src/testmode.c \
        upstream/Firmware/Saintcon2019/Drivers/IS31FL3736/drv_is31fl.c \
        -sWASM=1 \
        -sASYNCIFY=1 \
        -sASYNCIFY_STACK_SIZE=65536 \
        -sALLOW_MEMORY_GROWTH=0 \
        -sINITIAL_MEMORY=16777216 \
        -sSTACK_SIZE=524288 \
        -sMODULARIZE=1 \
        -sEXPORT_NAME=createCore \
        -sEXPORTED_FUNCTIONS='${EXPORTED_FUNCS}' \
        -sEXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"HEAPU8\",\"HEAPU16\"]' \
        -o /src/emulator/web/badges/2019/core.js
    "
fi

ls -lh "${OUT_WEB}/core.js" "${OUT_WEB}/core.wasm" "${SCRIPT_DIR}/emu-cli"
echo "[2019 build] Done."
