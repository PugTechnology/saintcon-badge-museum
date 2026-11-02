#!/usr/bin/env bash
# Build SAINTCON 2020 LED Badge original firmware (Firmware/Saintcon2020/main.cpp) to:
#   1. emulator/cores/2020/emu-cli              (headless native verification & smoke screenshot CLI)
#   2. emulator/web/badges/2020/core.{js,wasm}  (browser WebAssembly core)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
UPSTREAM_MAIN="${SCRIPT_DIR}/upstream/Firmware/Saintcon2020/main.cpp"
OUT_WEB="${ROOT_DIR}/emulator/web/badges/2020"
OBJ_DIR="${ROOT_DIR}/emulator/build/core-2020-obj"
SMOKE_DIR="${ROOT_DIR}/emulator/build/2020-smoke"

mkdir -p "${OUT_WEB}" "${OBJ_DIR}" "${SMOKE_DIR}"

# 1. Stage upstream firmware & regenerate THIRD_PARTY_LICENSES.txt
bash "${SCRIPT_DIR}/fetch-upstream.sh"
bash "${SCRIPT_DIR}/gen-licenses.sh"

CXX_WARN=(
  "-std=gnu++17"
  "-funsigned-char"
  "-Wno-narrowing"
  "-Wno-unused-variable"
  "-Wno-unused-but-set-variable"
  "-Wno-parentheses"
)

# 2. Build native headless CLI (emu-cli)
echo "[2020 build] Compiling native headless CLI (emu-cli)..."
g++ -O2 -g "${CXX_WARN[@]}" \
  -I"${SCRIPT_DIR}/platform" \
  -DSAINTCON_2020_FW_TU=1 \
  -c "${UPSTREAM_MAIN}" -o "${OBJ_DIR}/fw_main.o"

g++ -O2 -g "${CXX_WARN[@]}" \
  -I"${SCRIPT_DIR}/platform" \
  -DSCRIPT_DIR_STR="\"${SCRIPT_DIR}\"" \
  -DROOT_DIR_STR="\"${ROOT_DIR}\"" \
  "${SCRIPT_DIR}/cli_2020.cpp" \
  "${OBJ_DIR}/fw_main.o" \
  "${SCRIPT_DIR}/platform/emu_2020.cpp" \
  -o "${SCRIPT_DIR}/emu-cli"

echo "[2020 build] Verifying native symbol layout after LEDS[8]..."
python3 - "${SCRIPT_DIR}/emu-cli" <<'PY'
import subprocess, sys
out = subprocess.check_output(["nm", "-n", sys.argv[1]], text=True)
syms = []
for line in out.splitlines():
    parts = line.split()
    if len(parts) >= 3 and parts[1].upper() in ("B", "D", "G", "S", "V"):
        syms.append((int(parts[0], 16), parts[2]))
syms.sort()
leds_idx = next((i for i, (_, name) in enumerate(syms) if name == "LEDS"), None)
if leds_idx is None:
    sys.exit("FAIL: Symbol LEDS not found in native emu-cli")
leds_addr = syms[leds_idx][0]
if leds_idx + 1 >= len(syms):
    sys.exit("FAIL: No symbol after LEDS in native emu-cli")
next_addr, next_name = syms[leds_idx + 1]
gap = next_addr - (leds_addr + 8)
if gap < 1:
    sys.exit(f"FAIL: Native emu-cli byte after LEDS[8] (0x{leds_addr+8:x}) belongs to {next_name} at 0x{next_addr:x}")
print(f"  [PASS] Native emu-cli: LEDS at 0x{leds_addr:x}..0x{leds_addr+7:x}, next symbol {next_name} at 0x{next_addr:x} ({gap} unused padding bytes after LEDS)")
PY

echo "[2020 build] Running verification tests and generating smoke screenshots..."
"${SCRIPT_DIR}/emu-cli" --test --smoke "${SMOKE_DIR}"

# 3. Build WebAssembly core (badges/2020/core.js + core.wasm)
EXPORTED_FUNCS='["_emu_init","_emu_reset","_emu_step","_emu_set_speed","_emu_framebuffer","_emu_fb_width","_emu_fb_height","_emu_leds","_emu_led_count","_emu_backlight","_emu_eeprom","_emu_eeprom_size","_emu_eeprom_dirty","_emu_eeprom_clear_dirty","_emu_2020_set_tp1_boot_low","_emu_2020_get_tp1_boot_low","_emu_2020_get_in_special_mode","_emu_2020_get_porta","_emu_2020_get_portb","_emu_2020_get_led_duty","_emu_2020_get_tp_pulse_count","_emu_2020_get_millis","_malloc","_free"]'

echo "[2020 build] Compiling WebAssembly core (core.js + core.wasm)..."
if [[ -f "/opt/emsdk/emsdk_env.sh" ]]; then
  # shellcheck disable=SC1091
  source "/opt/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi

if command -v em++ >/dev/null 2>&1; then
  em++ -O2 "${CXX_WARN[@]}" \
    -I"${SCRIPT_DIR}/platform" \
    -DSAINTCON_2020_FW_TU=1 \
    -c "${UPSTREAM_MAIN}" -o "${OBJ_DIR}/fw_main_wasm.o"
  em++ -O2 "${CXX_WARN[@]}" \
    -I"${SCRIPT_DIR}/platform" \
    "${SCRIPT_DIR}/platform/emu_2020.cpp" \
    "${OBJ_DIR}/fw_main_wasm.o" \
    -Wl,-Map,"${OBJ_DIR}/core_wasm.map" \
    -sWASM=1 \
    -sASYNCIFY=1 \
    -sASYNCIFY_STACK_SIZE=65536 \
    -sALLOW_MEMORY_GROWTH=0 \
    -sINITIAL_MEMORY=16777216 \
    -sSTACK_SIZE=262144 \
    -sMODULARIZE=1 \
    -sEXPORT_NAME="createCore" \
    -sEXPORTED_FUNCTIONS="${EXPORTED_FUNCS}" \
    -sEXPORTED_RUNTIME_METHODS='["ccall","cwrap","UTF8ToString","stringToUTF8","HEAPU8","HEAPU16"]' \
    -o "${OUT_WEB}/core.js"
else
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${ROOT_DIR}:/src" \
    -w "/src/emulator/cores/2020" \
    emscripten/emsdk:3.1.73 \
    bash -c "
      em++ -O2 ${CXX_WARN[*]} -Iplatform -DSAINTCON_2020_FW_TU=1 \
        -c upstream/Firmware/Saintcon2020/main.cpp -o /src/emulator/build/core-2020-obj/fw_main_wasm.o && \
      em++ -O2 ${CXX_WARN[*]} -Iplatform \
        platform/emu_2020.cpp \
        /src/emulator/build/core-2020-obj/fw_main_wasm.o \
        -Wl,-Map,/src/emulator/build/core-2020-obj/core_wasm.map \
        -sWASM=1 \
        -sASYNCIFY=1 \
        -sASYNCIFY_STACK_SIZE=65536 \
        -sALLOW_MEMORY_GROWTH=0 \
        -sINITIAL_MEMORY=16777216 \
        -sSTACK_SIZE=262144 \
        -sMODULARIZE=1 \
        -sEXPORT_NAME=createCore \
        -sEXPORTED_FUNCTIONS='${EXPORTED_FUNCS}' \
        -sEXPORTED_RUNTIME_METHODS='[\"ccall\",\"cwrap\",\"UTF8ToString\",\"stringToUTF8\",\"HEAPU8\",\"HEAPU16\"]' \
        -o /src/emulator/web/badges/2020/core.js
    "
fi

echo "[2020 build] Verifying WASM link map layout after LEDS[8]..."
python3 - "${OBJ_DIR}/core_wasm.map" <<'PY'
import re, sys
from pathlib import Path

map_text = Path(sys.argv[1]).read_text()
# Match symbol lines inside the WASM data/bss sections: "<vma> <offset> <size>                 <name>"
sym_re = re.compile(r"^\s*([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+([0-9a-fA-F]+)\s{12,}(\S+)\s*$")
syms = []
for line in map_text.splitlines():
    m = sym_re.match(line)
    if m:
        addr = int(m.group(1), 16)
        size = int(m.group(2), 16)
        name = m.group(3)
        if addr > 0 and size > 0 and not name.startswith("."):
            syms.append((addr, size, name))
syms.sort()
leds_idx = next((i for i, (_, _, name) in enumerate(syms) if name == "LEDS"), None)
if leds_idx is None:
    sys.exit("FAIL: Symbol LEDS not found in WASM link map")
leds_addr, leds_size, _ = syms[leds_idx]
if leds_size != 8:
    sys.exit(f"FAIL: Expected LEDS size 8 in WASM link map, got {leds_size}")
oob_byte = leds_addr + 8
for i, (addr, size, name) in enumerate(syms):
    if i == leds_idx:
        continue
    if addr <= oob_byte < addr + size:
        sys.exit(f"FAIL: WASM byte after LEDS (0x{oob_byte:x}) belongs to variable {name} (0x{addr:x}..0x{addr+size-1:x})")
if leds_idx + 1 >= len(syms):
    sys.exit("FAIL: No symbol after LEDS in WASM link map")
next_addr, next_size, next_name = syms[leds_idx + 1]
gap = next_addr - oob_byte
if gap < 1:
    sys.exit(f"FAIL: WASM byte after LEDS (0x{oob_byte:x}) is not unused padding (next symbol {next_name} at 0x{next_addr:x})")
print(f"  [PASS] WASM link map: LEDS at 0x{leds_addr:x}..0x{leds_addr+7:x}, LEDS[8] at 0x{oob_byte:x} is unused padding, next symbol {next_name} at 0x{next_addr:x} ({gap + 1} unused bytes after LEDS)")
PY

echo "[2020 build] Running Node verification on shipped core.wasm..."
node - "${OUT_WEB}/core.js" <<'EOF'
const path = require('path');
const createCore = require(path.resolve(process.argv[2]));

(async () => {
  const mod = await createCore();
  const eepPtr = mod._emu_eeprom();
  const eepSz = mod._emu_eeprom_size();
  mod.HEAPU8.fill(0x00, eepPtr, eepPtr + eepSz); // EEPROM[0] = 0 (Mode 0: Ring)
  mod._emu_2020_set_tp1_boot_low(0);
  mod._emu_init(0x20205343);

  let prevMillis = mod._emu_2020_get_millis();
  let prevPorta = mod._emu_2020_get_porta();
  let prevD8Full = (mod._emu_2020_get_led_duty(7) === 20);
  let prevD1Lit = (mod._emu_2020_get_led_duty(0) > 0);
  let portaD8OffT = -1;
  let dutyD8OffT = -1;
  let portaGaps = [];
  let dutyGaps = [];
  let wrapsSeen = 0;

  for (let t = 1; t <= 70000; t++) {
    mod._emu_step(1, 0);
    const curMillis = mod._emu_2020_get_millis();
    if (curMillis < prevMillis) {
      if (prevMillis === 65535 && curMillis === 0) {
        wrapsSeen++;
      } else {
        console.error(`FAIL: core.wasm Ring mode millis went backwards at t=${t} ms: ${prevMillis} -> ${curMillis}`);
        process.exit(1);
      }
    }
    prevMillis = curMillis;

    const curPorta = mod._emu_2020_get_porta();
    if (prevPorta === 0x80 && curPorta === 0x00) {
      portaD8OffT = t;
    } else if (prevPorta === 0x00 && curPorta === 0x01 && portaD8OffT >= 0) {
      const gap = t - portaD8OffT;
      if (gap !== 102) {
        console.error(`FAIL: core.wasm Ring mode PORTA all-off gap at t=${t} ms was ${gap} ms (expected 102 ms)`);
        process.exit(1);
      }
      portaGaps.push(gap);
      portaD8OffT = -1;
    }
    prevPorta = curPorta;

    const curD8Full = (mod._emu_2020_get_led_duty(7) === 20);
    const curD1Lit = (mod._emu_2020_get_led_duty(0) > 0);
    if (prevD8Full && !curD8Full) {
      dutyD8OffT = t;
    } else if (!prevD1Lit && curD1Lit && dutyD8OffT >= 0) {
      const gap = t - dutyD8OffT;
      if (gap !== 102) {
        console.error(`FAIL: core.wasm Ring mode LED duty all-off gap at t=${t} ms was ${gap} ms (expected 102 ms)`);
        process.exit(1);
      }
      dutyGaps.push(gap);
      dutyD8OffT = -1;
    }
    prevD8Full = curD8Full;
    prevD1Lit = curD1Lit;
  }

  if (wrapsSeen !== 1) {
    console.error(`FAIL: Expected exactly 1 wrap at 65536 ms across 70000 ms, saw ${wrapsSeen}`);
    process.exit(1);
  }
  if (portaGaps.length < 80 || dutyGaps.length < 80) {
    console.error(`FAIL: Expected >= 80 Ring cycles across 70000 ms, saw portaGaps=${portaGaps.length}, dutyGaps=${dutyGaps.length}`);
    process.exit(1);
  }
  console.log(`  [PASS] Shipped core.wasm under Node: millis monotonic across 70000 ms (1 wrap at 65535->0); all ${portaGaps.length} Ring D8-off -> D1-on gaps == 102 ms.`);
})();
EOF

ls -lh "${OUT_WEB}/core.js" "${OUT_WEB}/core.wasm" "${SCRIPT_DIR}/emu-cli"
echo "[2020 build] Done."
