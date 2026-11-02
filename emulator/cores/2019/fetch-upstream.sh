#!/usr/bin/env bash
# Fetch and stage the upstream SAINTCON 2019 Enigma Badge firmware & KiCad PCB sources
# from https://github.com/compukidmike/Saintcon2019 at pinned commit
# 4ec59a1626dca09a0c75969f449a0ed3cca18bac ("New Firmware", 2020-01-25) into
# emulator/cores/2019/upstream/ (git-ignored).
#
# Excludes ST's STM32L4xx HAL, CMSIS, STM32 USB Device Library, ST EEPROM_Emul,
# iceboot.c / fpga.bin, pre-built .hex images, PDFs, and README.md.
# All 17 badge C files and 19 header files are staged unpatched.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
UPSTREAM_DIR="${SCRIPT_DIR}/upstream"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2019.git"
UPSTREAM_COMMIT="4ec59a1626dca09a0c75969f449a0ed3cca18bac"

FIRMWARE_C_FILES=(
  "Firmware/Saintcon2019/Src/animations.c"
  "Firmware/Saintcon2019/Src/custommessage.c"
  "Firmware/Saintcon2019/Src/enigma.c"
  "Firmware/Saintcon2019/Src/enigma_emulator.c"
  "Firmware/Saintcon2019/Src/images.c"
  "Firmware/Saintcon2019/Src/keyboard.c"
  "Firmware/Saintcon2019/Src/lampboard.c"
  "Firmware/Saintcon2019/Src/main.c"
  "Firmware/Saintcon2019/Src/matrix.c"
  "Firmware/Saintcon2019/Src/menu.c"
  "Firmware/Saintcon2019/Src/minibadge.c"
  "Firmware/Saintcon2019/Src/plugboard.c"
  "Firmware/Saintcon2019/Src/screen_animations.c"
  "Firmware/Saintcon2019/Src/snake.c"
  "Firmware/Saintcon2019/Src/testmode.c"
  "Firmware/Saintcon2019/Src/user_profile.c"
  "Firmware/Saintcon2019/Drivers/IS31FL3736/drv_is31fl.c"
)

FIRMWARE_H_FILES=(
  "Firmware/Saintcon2019/Inc/animations.h"
  "Firmware/Saintcon2019/Inc/custommessage.h"
  "Firmware/Saintcon2019/Inc/enigma.h"
  "Firmware/Saintcon2019/Inc/enigma_emulator.h"
  "Firmware/Saintcon2019/Inc/font5x7.h"
  "Firmware/Saintcon2019/Inc/images.h"
  "Firmware/Saintcon2019/Inc/keyboard.h"
  "Firmware/Saintcon2019/Inc/lampboard.h"
  "Firmware/Saintcon2019/Inc/main.h"
  "Firmware/Saintcon2019/Inc/matrix.h"
  "Firmware/Saintcon2019/Inc/menu.h"
  "Firmware/Saintcon2019/Inc/minibadge.h"
  "Firmware/Saintcon2019/Inc/plugboard.h"
  "Firmware/Saintcon2019/Inc/screen_animations.h"
  "Firmware/Saintcon2019/Inc/snake.h"
  "Firmware/Saintcon2019/Inc/snake_images.h"
  "Firmware/Saintcon2019/Inc/testmode.h"
  "Firmware/Saintcon2019/Inc/user_profile.h"
  "Firmware/Saintcon2019/Drivers/IS31FL3736/drv_is31fl.h"
)

HARDWARE_FILES=(
  "Hardware/KiCad Files/Saintcon2019/Saintcon2019.kicad_pcb"
  "Hardware/KiCad Files/Saintcon2019TopBoard/Saintcon2019TopBoard.kicad_pcb"
)

verify_staged_tree() {
  python3 - "$UPSTREAM_DIR" <<'PY'
import re
import sys
from pathlib import Path

up = Path(sys.argv[1])

# 1. Ensure excluded vendor / binary / doc trees are never staged
forbidden = [
    up / "Firmware/Saintcon2019/Drivers/STM32L4xx_HAL_Driver",
    up / "Firmware/Saintcon2019/Drivers/CMSIS",
    up / "Firmware/Saintcon2019/Middlewares",
    up / "Firmware/Saintcon2019/Inc/eeprom_emul_conf.h",
    up / "Firmware/Saintcon2019/Src/iceboot.c",
    up / "Firmware/Saintcon2019/Inc/iceboot.h",
    up / "Firmware/Saintcon2019/Src/fpga.bin",
    up / "Saintcon2019.hex",
    up / "fpga.hex",
    up / "Code Sheet.pdf",
    up / "Saintcon2019BadgeChallenge.pdf",
    up / "Badge Instructions.pdf",
    up / "README.md",
]
for f in forbidden:
    if f.exists():
        print(f"ERROR: forbidden path staged in upstream/: {f}", file=sys.stderr)
        sys.exit(1)

# 2. Verify BADGE_TEST_CODE and PRINTER_CODE stay commented out in main.c
main_c = (up / "Firmware/Saintcon2019/Src/main.c").read_text(encoding="utf-8")
if re.search(r"^\s*#define\s+(BADGE_TEST_CODE|PRINTER_CODE)\b", main_c, re.M):
    print("ERROR: BADGE_TEST_CODE or PRINTER_CODE is defined in Src/main.c", file=sys.stderr)
    sys.exit(1)

# 3. Verify HC silkscreen code in Saintcon2019.kicad_pcb has been scrubbed
main_pcb = (up / "Hardware/KiCad Files/Saintcon2019/Saintcon2019.kicad_pcb").read_text(encoding="utf-8")
if re.search(r"HC:\s*(?!REDACTED\b)[A-Z0-9_]+", main_pcb):
    print("ERROR: unscrubbed HC code in Saintcon2019.kicad_pcb", file=sys.stderr)
    sys.exit(1)
if "HC: REDACTED" not in main_pcb:
    print("ERROR: expected scrubbed HC: REDACTED marker in Saintcon2019.kicad_pcb", file=sys.stderr)
    sys.exit(1)

# 4. Scan staged C/H files for Wi-Fi credentials, private keys, or personal emails
for p in sorted((up / "Firmware").rglob("*")):
    if not p.is_file():
        continue
    txt = p.read_text(encoding="utf-8", errors="replace")
    if re.search(r"(BEGIN\s+(RSA|EC|OPENSSH)\s+PRIVATE\s+KEY|gmail\.com)", txt, re.I):
        print(f"ERROR: sensitive token or personal email found in {p}", file=sys.stderr)
        sys.exit(1)
PY
}

scrub_and_verify() {
  python3 - "$UPSTREAM_DIR" <<'PY'
import re
import sys
from pathlib import Path

up = Path(sys.argv[1])
pcb_path = up / "Hardware/KiCad Files/Saintcon2019/Saintcon2019.kicad_pcb"
txt = pcb_path.read_text(encoding="utf-8")
new_txt, n = re.subn(r"HC:\s*(?!REDACTED\b)[A-Z0-9_]+", "HC: REDACTED", txt)
if n != 1:
    print(f"ERROR: expected 1 HC code scrub in {pcb_path}, applied {n}", file=sys.stderr)
    sys.exit(1)
pcb_path.write_text(new_txt, encoding="utf-8")
PY
  verify_staged_tree
}

# Fast-path if already staged at the exact commit
if [[ -f "${UPSTREAM_DIR}/.upstream-commit" ]] && [[ "$(cat "${UPSTREAM_DIR}/.upstream-commit")" == "${UPSTREAM_COMMIT}" ]]; then
  missing=0
  for f in "${FIRMWARE_C_FILES[@]}" "${FIRMWARE_H_FILES[@]}" "${HARDWARE_FILES[@]}"; do
    if [[ ! -f "${UPSTREAM_DIR}/${f}" ]]; then
      missing=1
      break
    fi
  done
  if [[ "$missing" -eq 0 ]]; then
    verify_staged_tree
    echo "[2019 fetch-upstream] Already staged at ${UPSTREAM_COMMIT}."
    exit 0
  fi
fi

SRC_REPO=""
TMP_CLONE=""
STAGE_COMPLETE=0
cleanup() {
  local rc=$?
  if [[ -n "$TMP_CLONE" && -d "$TMP_CLONE" ]]; then
    rm -rf "$TMP_CLONE"
  fi
  if [[ "$STAGE_COMPLETE" -ne 1 && -d "$UPSTREAM_DIR" ]]; then
    rm -rf "$UPSTREAM_DIR"
  fi
  return "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

for cand in \
  "${SAINTCON2019_UPSTREAM_DIR:-}" \
  "${HOME}/Projects/saintcon-museum-research/Saintcon2019" \
  "${ROOT_DIR}/upstream/Saintcon2019"
do
  if [[ -n "$cand" && -d "$cand/.git" ]]; then
    if git -C "$cand" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
      SRC_REPO="$cand"
      break
    fi
  fi
done

if [[ -z "$SRC_REPO" ]]; then
  echo "[2019 fetch-upstream] Cloning ${UPSTREAM_URL} at ${UPSTREAM_COMMIT}..."
  TMP_CLONE="$(mktemp -d)"
  git init -q "$TMP_CLONE"
  git -C "$TMP_CLONE" remote add origin "$UPSTREAM_URL"
  git -C "$TMP_CLONE" fetch --depth 1 origin "$UPSTREAM_COMMIT"
  git -C "$TMP_CLONE" checkout -q FETCH_HEAD
  SRC_REPO="$TMP_CLONE"
else
  echo "[2019 fetch-upstream] Staging from local clone ${SRC_REPO} at ${UPSTREAM_COMMIT}..."
fi

rm -rf "${UPSTREAM_DIR}"
mkdir -p \
  "${UPSTREAM_DIR}/Firmware/Saintcon2019/Src" \
  "${UPSTREAM_DIR}/Firmware/Saintcon2019/Inc" \
  "${UPSTREAM_DIR}/Firmware/Saintcon2019/Drivers/IS31FL3736" \
  "${UPSTREAM_DIR}/Hardware/KiCad Files/Saintcon2019" \
  "${UPSTREAM_DIR}/Hardware/KiCad Files/Saintcon2019TopBoard"

for f in "${FIRMWARE_C_FILES[@]}" "${FIRMWARE_H_FILES[@]}" "${HARDWARE_FILES[@]}"; do
  git -C "$SRC_REPO" show "${UPSTREAM_COMMIT}:${f}" > "${UPSTREAM_DIR}/${f}"
done

scrub_and_verify

echo "${UPSTREAM_COMMIT}" > "${UPSTREAM_DIR}/.upstream-commit"
STAGE_COMPLETE=1
echo "[2019 fetch-upstream] Staged ${#FIRMWARE_C_FILES[@]} C files, ${#FIRMWARE_H_FILES[@]} headers, and ${#HARDWARE_FILES[@]} KiCad PCBs into ${UPSTREAM_DIR}"
