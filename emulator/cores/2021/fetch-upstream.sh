#!/usr/bin/env bash
# Fetch the SAINTCON 2021 combination lock badge firmware, ExternalFlash/ image
# pipeline, and KiCad board files into ./upstream/ (git-ignored), excluding
# Microchip's proprietary QTouch library, ASF4/CMSIS HAL directories, USB stack,
# bootloader, and README, and scrubbing the AES-128 trade key before any build.
#
# Pinned upstream HEAD (from git log):
#   repo:    https://github.com/compukidmike/Saintcon2021.git
#   commit:  092bf1c68a90cd5d13794006667fd52c3cbb3efd
#   date:    2023-04-06 13:53:51 -0600
#   message: Add minibadge holder board
#
# Re-running is a no-op once the pinned commit is staged and scrub-verified;
# pass --force to re-extract from scratch.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2021.git"
UPSTREAM_COMMIT="092bf1c68a90cd5d13794006667fd52c3cbb3efd"

DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"
TMP_GIT=""

# Exact badge-owned firmware files, ExternalFlash assets/script, and KiCad PCBs.
# Proprietary Microchip QTouch (qtouch/), ASF4/CMSIS (hal/, hpl/, hri/, CMSIS/,
# Device_Startup/, Config/, usb/), Atmel Start generated glue, bootloader, and
# README.md are intentionally excluded and never staged.
ARCHIVE_PATHS=(
  "Saintcon2021/Saintcon2021/FrameBuffer.c"
  "Saintcon2021/Saintcon2021/FrameBuffer.h"
  "Saintcon2021/Saintcon2021/ILI9331.h"
  "Saintcon2021/Saintcon2021/RougeMaze.c"
  "Saintcon2021/Saintcon2021/Scenes"
  "Saintcon2021/Saintcon2021/combonums.h"
  "Saintcon2021/Saintcon2021/eeprom.c"
  "Saintcon2021/Saintcon2021/eeprom.h"
  "Saintcon2021/Saintcon2021/flash.c"
  "Saintcon2021/Saintcon2021/flash.h"
  "Saintcon2021/Saintcon2021/font5x7.h"
  "Saintcon2021/Saintcon2021/font8x16.h"
  "Saintcon2021/Saintcon2021/machine_common.c"
  "Saintcon2021/Saintcon2021/machine_common.h"
  "Saintcon2021/Saintcon2021/main.c"
  "Saintcon2021/Saintcon2021/main.h"
  "Saintcon2021/Saintcon2021/menu_icons.h"
  "Saintcon2021/Saintcon2021/nfc.c"
  "Saintcon2021/Saintcon2021/nfc.h"
  "Saintcon2021/Saintcon2021/platform.c"
  "Saintcon2021/Saintcon2021/platform.h"
  "Saintcon2021/Saintcon2021/small.c"
  "Saintcon2021/Saintcon2021/unlocks.c"
  "Saintcon2021/ExternalFlash"
  "Hardware/Saintcon2021Front/Saintcon2020Front.kicad_pcb"
  "Hardware/Saintcon2021Shackle/Saintcon2020Shackle.kicad_pcb"
  "Hardware/Saintcon2021/Saintcon2020.kicad_pcb"
)

# On any failed exit, remove the staged tree (it may hold unscrubbed secrets) and any temp clone.
on_exit() {
  local rc=$?
  [[ -n "${TMP_GIT}" ]] && rm -rf "${TMP_GIT}"
  if [[ "${rc}" -ne 0 ]]; then
    rm -rf "${DEST}"
  fi
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
  FORCE=1
fi

scrub_and_verify() {
  set +x
  local fresh="${1:-0}"
  local TRADE_C="${DEST}/Saintcon2021/Saintcon2021/Scenes/tradeScene.c"

  if [[ ! -f "${TRADE_C}" ]]; then
    echo "ERROR: Missing ${TRADE_C}" >&2
    exit 1
  fi

  # 1. Confirm excluded proprietary/vendor trees and README are absent.
  local forbidden
  for forbidden in \
    "${DEST}/README.md" \
    "${DEST}/saintcon2021bootloader" \
    "${DEST}/Saintcon2021/Saintcon2021/qtouch" \
    "${DEST}/Saintcon2021/Saintcon2021/hal" \
    "${DEST}/Saintcon2021/Saintcon2021/hri" \
    "${DEST}/Saintcon2021/Saintcon2021/hpl" \
    "${DEST}/Saintcon2021/Saintcon2021/CMSIS" \
    "${DEST}/Saintcon2021/Saintcon2021/Device_Startup" \
    "${DEST}/Saintcon2021/Saintcon2021/Config" \
    "${DEST}/Saintcon2021/Saintcon2021/usb"
  do
    if [[ -e "${forbidden}" ]]; then
      echo "ERROR: Forbidden path staged in upstream/: $(basename "${forbidden}")" >&2
      exit 1
    fi
  done

  if find "${DEST}" -iname 'readme*' | grep -q .; then
    echo "ERROR: README file found in staged upstream/" >&2
    exit 1
  fi

  # 2. Scrub trade_key[16] in Scenes/tradeScene.c to deterministic museum bytes
  # ("MUSEUM_TRADE2021") with fail-if-not-applied check and residual scan.
  # Never print the original key literal or raw bytes.
  python3 - "${DEST}" "${TRADE_C}" "${fresh}" <<'PY'
import pathlib
import re
import sys

dest = pathlib.Path(sys.argv[1])
trade_c = pathlib.Path(sys.argv[2])
fresh = int(sys.argv[3])

MUSEUM_INIT = "{0x4d, 0x55, 0x53, 0x45, 0x55, 0x4d, 0x5f, 0x54, 0x52, 0x41, 0x44, 0x45, 0x32, 0x30, 0x32, 0x31}"
MUSEUM_BYTES = b"MUSEUM_TRADE2021"
MUSEUM_DECL = f"const uint8_t trade_key[16] = {MUSEUM_INIT};"

text = trade_c.read_text(encoding="utf-8", errors="surrogateescape")
pat = re.compile(r"const\s+uint8_t\s+trade_key\s*\[\s*16\s*\]\s*=\s*(\{[^;{}]+\})\s*;")
m = pat.search(text)
if not m:
    sys.stderr.write("ERROR: Could not locate trade_key[16] declaration in tradeScene.c\n")
    sys.exit(1)

orig_init = m.group(1)
raw_nums = [int(tok.strip(), 0) for tok in orig_init.strip("{}").split(",") if tok.strip()]
if len(raw_nums) != 16 or any(not (0 <= b <= 255) for b in raw_nums):
    sys.stderr.write("ERROR: trade_key[16] initializer did not parse to 16 bytes\n")
    sys.exit(1)
orig_bytes = bytes(raw_nums)

if fresh == 1 and orig_bytes == MUSEUM_BYTES:
    sys.stderr.write("ERROR: Expected unscrubbed trade_key[16] in fresh upstream tree\n")
    sys.exit(1)

if orig_bytes != MUSEUM_BYTES or m.group(0) != MUSEUM_DECL:
    new_text = text[:m.start()] + MUSEUM_DECL + text[m.end():]
    trade_c.write_text(new_text, encoding="utf-8", errors="surrogateescape")

after = trade_c.read_text(encoding="utf-8", errors="surrogateescape")
if MUSEUM_DECL not in after:
    sys.stderr.write("ERROR: Failed to verify scrubbed trade_key[16] in tradeScene.c\n")
    sys.exit(1)

# Audit every file in staged upstream/ for residual key bytes/text, Wi-Fi fields,
# CTF flags, and personal contact details (vCard / email / phone).
email_re = re.compile(rb"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
flag_re = re.compile(rb"(?i)(?:sc21|saintcon|flag)\{[^}\r\n]+\}")
wifi_re = re.compile(rb"(?i)\b(?:wifi_ssid|wifi_pass(?:word)?|wpa_psk)\b")
tel_re = re.compile(rb"TEL[;:][0-9+(). -]{5,}")

for p in sorted(dest.rglob("*")):
    if not p.is_file() or p.name == ".fetched_commit":
        continue
    data = p.read_bytes()
    if orig_bytes != MUSEUM_BYTES:
        if orig_init.encode("utf-8") in data or orig_bytes in data:
            sys.stderr.write(f"ERROR: Residual trade_key material detected in {p.relative_to(dest)}\n")
            sys.exit(1)
    if flag_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected flag pattern detected in {p.relative_to(dest)}\n")
        sys.exit(1)
    if wifi_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected Wi-Fi credential pattern detected in {p.relative_to(dest)}\n")
        sys.exit(1)
    if p.suffix.lower() in (".c", ".h", ".py", ".kicad_pcb"):
        if tel_re.search(data):
            sys.stderr.write(f"ERROR: Unexpected phone number in {p.relative_to(dest)}\n")
            sys.exit(1)
        for em in email_re.findall(data):
            # flash.h comment holds fictional movie sample "bob@home.com" ("Bob Wiley").
            if em.lower() != b"bob@home.com":
                sys.stderr.write(f"ERROR: Unexpected email address in {p.relative_to(dest)}\n")
                sys.exit(1)

# 3. Apply and verify minimal C99 / WebAssembly / x86_64 portability fixes for
#    ARMv7E-M UDIV-by-zero (returns 0 on Cortex-M4, traps in WASM/x86_64),
#    non-void `return;` in bool menu_check_minibadge(), and tentative `io` in
#    main.h for `-fno-common` / wasm-ld (documented in UPSTREAM.md).
main_h = dest / "Saintcon2021/Saintcon2021/main.h"
main_h_txt = main_h.read_text(encoding="utf-8", errors="surrogateescape")
if "\nstruct io_descriptor *io;" in main_h_txt:
    main_h_txt = main_h_txt.replace("\nstruct io_descriptor *io;", "\nextern struct io_descriptor *io;", 1)
    main_h.write_text(main_h_txt, encoding="utf-8", errors="surrogateescape")
if "extern struct io_descriptor *io;" not in main_h.read_text(encoding="utf-8", errors="surrogateescape"):
    sys.stderr.write("ERROR: Failed to verify extern io declaration in main.h\n")
    sys.exit(1)

trade_txt2 = trade_c.read_text(encoding="utf-8", errors="surrogateescape")
if "\nstruct sha_context context;" in trade_txt2:
    trade_txt2 = trade_txt2.replace("\nstruct sha_context context;", "\nstatic struct sha_context context;", 1)
    trade_c.write_text(trade_txt2, encoding="utf-8", errors="surrogateescape")
if "static struct sha_context context;" not in trade_c.read_text(encoding="utf-8", errors="surrogateescape"):
    sys.stderr.write("ERROR: Failed to verify static sha_context in Scenes/tradeScene.c\n")
    sys.exit(1)

test_c = dest / "Saintcon2021/Saintcon2021/Scenes/testScene.c"
menu_c = dest / "Saintcon2021/Saintcon2021/Scenes/menuScene.c"

test_txt = test_c.read_text(encoding="utf-8", errors="surrogateescape")
orig_test = 'snprintf(lines[3], 20, "%3lufps", 1000/(sys_ms+blt_ms+draw_ms));'
fixed_test = 'snprintf(lines[3], 20, "%3lufps", (sys_ms+blt_ms+draw_ms) ? (1000/(sys_ms+blt_ms+draw_ms)) : 0UL);'
if orig_test in test_txt:
    test_txt = test_txt.replace(orig_test, fixed_test, 1)
    test_c.write_text(test_txt, encoding="utf-8", errors="surrogateescape")
if fixed_test not in test_c.read_text(encoding="utf-8", errors="surrogateescape"):
    sys.stderr.write("ERROR: Failed to verify div-by-zero guard in Scenes/testScene.c\n")
    sys.exit(1)

menu_txt = menu_c.read_text(encoding="utf-8", errors="surrogateescape")
orig_menu_div = 'sprintf(fps, "%d", 1000/(now-menu_last));'
fixed_menu_div = 'sprintf(fps, "%d", (now != menu_last) ? (int)(1000/(now-menu_last)) : 0);'
if orig_menu_div in menu_txt:
    menu_txt = menu_txt.replace(orig_menu_div, fixed_menu_div, 1)

menu_ret_pat = re.compile(r"(if\s*\(\s*b\s*==\s*1\s*\)\s*\{\s*minibadge_button\s*=\s*128\s*;\s*)return\s*;")
if menu_ret_pat.search(menu_txt):
    menu_txt = menu_ret_pat.sub(r"\1return found;", menu_txt, count=1)

menu_c.write_text(menu_txt, encoding="utf-8", errors="surrogateescape")
menu_after = menu_c.read_text(encoding="utf-8", errors="surrogateescape")
if fixed_menu_div not in menu_after or "minibadge_button = 128;" not in menu_after or re.search(r"minibadge_button\s*=\s*128\s*;\s*return\s*;", menu_after):
    sys.stderr.write("ERROR: Failed to verify portability fixes in Scenes/menuScene.c\n")
    sys.exit(1)
PY
}

if [[ "${FORCE}" -eq 0 && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${UPSTREAM_COMMIT}" ]] \
   && [[ -d "${DEST}/Saintcon2021/Saintcon2021/Scenes" && -d "${DEST}/Saintcon2021/ExternalFlash" \
      && -f "${DEST}/Hardware/Saintcon2021Front/Saintcon2020Front.kicad_pcb" \
      && -f "${DEST}/Hardware/Saintcon2021Shackle/Saintcon2020Shackle.kicad_pcb" ]]; then
  scrub_and_verify 0
  echo "2021 upstream already at ${UPSTREAM_COMMIT:0:12} (scrub verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2021 @ ${UPSTREAM_COMMIT:0:12} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"

LOCAL_CLONE=""
for candidate in \
  "${REPO_ROOT}/upstream/Saintcon2021" \
  "${HOME}/Projects/saintcon-museum-research/Saintcon2021"
do
  if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
    LOCAL_CLONE="${candidate}"
    break
  fi
done

if [[ -n "${LOCAL_CLONE}" ]]; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" -- "${ARCHIVE_PATHS[@]}" | tar -x -C "${DEST}"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched Saintcon2021 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD -- "${ARCHIVE_PATHS[@]}" | tar -x -C "${DEST}"
  rm -rf "${TMP_GIT}"
  TMP_GIT=""
fi

scrub_and_verify 1

# Apply optional patches in patches/*.patch if any exist.
if [[ -d "${HERE}/patches" ]]; then
  shopt -s nullglob
  for p in "${HERE}/patches/"*.patch; do
    echo "==> Applying patch $(basename "${p}") ..."
    patch -d "${DEST}" -p1 < "${p}"
  done
  shopt -u nullglob
fi

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
