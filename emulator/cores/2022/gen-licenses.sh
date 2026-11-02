#!/usr/bin/env bash
# Deterministically generate emulator/web/badges/2022/THIRD_PARTY_LICENSES.txt
# by fetching the real upstream licence files at pinned commits and assembling
# them without typing licence texts by hand.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_DIR="${HERE}/upstream"
LIC_CACHE_DIR="${REPO_ROOT}/emulator/build/core-2022-obj/licenses"
OUT_DIR="${REPO_ROOT}/emulator/web/badges/2022"
OUT_FILE="${OUT_DIR}/THIRD_PARTY_LICENSES.txt"

PICO_SDK_URL="https://github.com/raspberrypi/pico-sdk.git"
PICO_SDK_TAG="1.3.1"
PICO_SDK_COMMIT="426e46126b5a1efaea4544cdb71ab81b61983034"

TINYUSB_URL="https://github.com/hathach/tinyusb.git"
TINYUSB_COMMIT="4bfab30c02279a0530e1a56f4a7c539f2d35a293"

PICO_BOOTROM_URL="https://github.com/raspberrypi/pico-bootrom.git"
PICO_BOOTROM_COMMIT="00a4a19114195e20fb817bdfbca1165e157eef37"

NEWLIB_URL="https://github.com/mirror/newlib-cygwin.git"
NEWLIB_FALLBACK_URL="https://sourceware.org/git/newlib-cygwin.git"
NEWLIB_TAG="newlib-4.1.0"
NEWLIB_COMMIT="415fdd4279b85eeec9d54775ce13c5c412451e08"

STAMP_FILE="${LIC_CACHE_DIR}/.fetched_licenses"
# The stamp covers the pinned commits and this script itself, so a change to the
# extraction logic below never reuses notices cut out by older logic.
SCRIPT_SHA256="$(sha256sum "${BASH_SOURCE[0]}" | awk '{print $1}')"
EXPECTED_STAMP="${PICO_SDK_COMMIT}:${TINYUSB_COMMIT}:${PICO_BOOTROM_COMMIT}:${NEWLIB_COMMIT}:${SCRIPT_SHA256}"
TMP_FETCH=""
TMP_FILES=()

cleanup() {
  if [[ -n "${TMP_FETCH}" ]]; then
    rm -rf "${TMP_FETCH}"
  fi
  if [[ ${#TMP_FILES[@]} -gt 0 ]]; then
    rm -f "${TMP_FILES[@]}"
  fi
}
trap cleanup EXIT INT TERM HUP
# Fallback so that no failure ends without an ERROR: line from this script.
trap 'echo "ERROR: gen-licenses.sh failed at line ${LINENO}: ${BASH_COMMAND}" >&2' ERR

die() {
  echo "ERROR: $*" >&2
  exit 1
}

# Sets the named variable. Not used through $(...), as a subshell would lose TMP_FILES and the trap could not clean up.
new_tmp_file() {
  local f
  f="$(mktemp)" || die "mktemp failed"
  TMP_FILES+=("${f}")
  printf -v "$1" '%s' "${f}"
}

mkdir -p "${OUT_DIR}" "${LIC_CACHE_DIR}"

# Ensure pinned saintcon2022 UF2 and wokwi/rp2040js files are staged.
bash "${HERE}/fetch-upstream.sh" || die "fetch-upstream.sh failed"

fetch_git_file() {
  local url="$1"
  local commit="$2"
  local path="$3"
  local dest="$4"
  local tmp
  tmp="$(mktemp -d)" || die "mktemp failed"
  TMP_FETCH="${tmp}"
  if ! git -C "${tmp}" init -q \
     || ! git -C "${tmp}" remote add origin "${url}" \
     || ! git -C "${tmp}" fetch -q --depth 1 origin "${commit}"; then
    echo "ERROR: Failed to fetch commit ${commit} from ${url}" >&2
    rm -rf "${tmp}"
    TMP_FETCH=""
    return 1
  fi
  if [[ "$(git -C "${tmp}" rev-parse FETCH_HEAD)" != "${commit}" ]]; then
    echo "ERROR: Fetched commit from ${url} does not match ${commit}" >&2
    rm -rf "${tmp}"
    TMP_FETCH=""
    return 1
  fi
  if ! git -C "${tmp}" show "FETCH_HEAD:${path}" > "${dest}"; then
    echo "ERROR: Failed to extract ${path} at ${commit} from ${url}" >&2
    rm -rf "${tmp}"
    TMP_FETCH=""
    return 1
  fi
  rm -rf "${tmp}"
  TMP_FETCH=""
  if [[ ! -s "${dest}" ]]; then
    echo "ERROR: Fetched file ${dest} (${path} from ${url}) is missing or empty" >&2
    return 1
  fi
}

if [[ ! -f "${STAMP_FILE}" ]] || [[ "$(cat "${STAMP_FILE}")" != "${EXPECTED_STAMP}" ]] \
   || [[ ! -s "${LIC_CACHE_DIR}/pico-sdk-LICENSE.TXT" \
      || ! -s "${LIC_CACHE_DIR}/pico-printf-header.txt" \
      || ! -s "${LIC_CACHE_DIR}/tinyusb-LICENSE.txt" \
      || ! -s "${LIC_CACHE_DIR}/pico-bootrom-LICENSE.TXT" \
      || ! -s "${LIC_CACHE_DIR}/pico-bootrom-mufplib-header.txt" \
      || ! -s "${LIC_CACHE_DIR}/newlib-COPYING.NEWLIB" ]]; then
  echo "[gen-licenses 2022] Fetching pinned upstream licence files..."
  rm -f "${STAMP_FILE}"
  TMP_FETCH="$(mktemp -d)" || die "mktemp failed"

  # 1. Pico SDK 1.3.1 (commit 426e46126b5a1efaea4544cdb71ab81b61983034):
  #    verify lib/tinyusb submodule commit, fetch LICENSE.TXT and printf.c
  if ! git -C "${TMP_FETCH}" init -q \
     || ! git -C "${TMP_FETCH}" remote add origin "${PICO_SDK_URL}" \
     || ! git -C "${TMP_FETCH}" fetch -q --depth 1 origin "${PICO_SDK_COMMIT}"; then
    die "Failed to fetch Pico SDK commit ${PICO_SDK_COMMIT} from ${PICO_SDK_URL}"
  fi
  if [[ "$(git -C "${TMP_FETCH}" rev-parse FETCH_HEAD)" != "${PICO_SDK_COMMIT}" ]]; then
    die "Pico SDK commit mismatch"
  fi
  SUBMOD_TINYUSB="$(git -C "${TMP_FETCH}" ls-tree FETCH_HEAD lib/tinyusb | awk '{print $3}')"
  if [[ "${SUBMOD_TINYUSB}" != "${TINYUSB_COMMIT}" ]]; then
    die "Pico SDK 1.3.1 lib/tinyusb commit (${SUBMOD_TINYUSB}) != ${TINYUSB_COMMIT}"
  fi
  git -C "${TMP_FETCH}" show "FETCH_HEAD:LICENSE.TXT" > "${LIC_CACHE_DIR}/pico-sdk-LICENSE.TXT" \
    || die "Failed to extract LICENSE.TXT at ${PICO_SDK_COMMIT} from ${PICO_SDK_URL}"
  [[ -s "${LIC_CACHE_DIR}/pico-sdk-LICENSE.TXT" ]] \
    || die "Pico SDK LICENSE.TXT at ${PICO_SDK_COMMIT} is empty"
  new_tmp_file TMP_PRINTF_RAW
  git -C "${TMP_FETCH}" show "FETCH_HEAD:src/rp2_common/pico_printf/printf.c" > "${TMP_PRINTF_RAW}" \
    || die "Failed to extract src/rp2_common/pico_printf/printf.c at ${PICO_SDK_COMMIT} from ${PICO_SDK_URL}"
  python3 - "${TMP_PRINTF_RAW}" "${LIC_CACHE_DIR}/pico-printf-header.txt" <<'PY' \
    || die "Could not cut the printf MIT notice out of printf.c at ${PICO_SDK_COMMIT}"
import pathlib, re, sys
lines = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace").splitlines()[:32]
text = "\n".join(lines)
if "MIT License" not in text or len(text) < 500:
    sys.stderr.write("ERROR: printf.c notice is empty or has no MIT licence text\n")
    sys.exit(1)
text = re.sub(r"\s*\([A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\)", " [author email omitted]", text)
pathlib.Path(sys.argv[2]).write_text(text + "\n", encoding="utf-8")
PY
  rm -rf "${TMP_FETCH}"
  TMP_FETCH=""

  # 2. TinyUSB LICENSE at the commit pinned by Pico SDK 1.3.1
  fetch_git_file "${TINYUSB_URL}" "${TINYUSB_COMMIT}" "LICENSE" "${LIC_CACHE_DIR}/tinyusb-LICENSE.txt" || exit 1

  # 3. raspberrypi/pico-bootrom LICENSE.TXT and the notice headers of
  #    bootrom/mufplib.S and bootrom/mufplib-double.S
  fetch_git_file "${PICO_BOOTROM_URL}" "${PICO_BOOTROM_COMMIT}" "LICENSE.TXT" "${LIC_CACHE_DIR}/pico-bootrom-LICENSE.TXT" || exit 1
  new_tmp_file TMP_MUFPLIB_RAW
  new_tmp_file TMP_MUFPLIB_DOUBLE_RAW
  fetch_git_file "${PICO_BOOTROM_URL}" "${PICO_BOOTROM_COMMIT}" "bootrom/mufplib.S" "${TMP_MUFPLIB_RAW}" || exit 1
  fetch_git_file "${PICO_BOOTROM_URL}" "${PICO_BOOTROM_COMMIT}" "bootrom/mufplib-double.S" "${TMP_MUFPLIB_DOUBLE_RAW}" || exit 1
  python3 - "${TMP_MUFPLIB_RAW}" "${TMP_MUFPLIB_DOUBLE_RAW}" "${LIC_CACHE_DIR}/pico-bootrom-mufplib-header.txt" <<'PY' \
    || die "Could not cut the Mark Owen notice out of bootrom/mufplib.S at ${PICO_BOOTROM_COMMIT}"
import pathlib, re, sys
def header(path):
    lines = pathlib.Path(path).read_text(encoding="utf-8", errors="replace").splitlines()[:16]
    text = "\n".join(lines)
    return re.sub(r"<?[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}>?", "[email omitted]", text)
single = header(sys.argv[1])
double = header(sys.argv[2])
if "Mark Owen" not in single or len(single) < 500:
    sys.stderr.write("ERROR: mufplib.S notice is empty or has no Mark Owen copyright line\n")
    sys.exit(1)
if single != double:
    sys.stderr.write("ERROR: mufplib-double.S notice differs from mufplib.S, but the licence text names both under one notice\n")
    sys.exit(1)
pathlib.Path(sys.argv[3]).write_text(single + "\n", encoding="utf-8")
PY

  # 4. newlib COPYING.NEWLIB at tag newlib-4.1.0 (with email addresses omitted)
  new_tmp_file TMP_NEWLIB_RAW
  if ! fetch_git_file "${NEWLIB_URL}" "${NEWLIB_COMMIT}" "COPYING.NEWLIB" "${TMP_NEWLIB_RAW}"; then
    echo "[gen-licenses 2022] Primary newlib repository failed; trying fallback ${NEWLIB_FALLBACK_URL}..." >&2
    fetch_git_file "${NEWLIB_FALLBACK_URL}" "${NEWLIB_COMMIT}" "COPYING.NEWLIB" "${TMP_NEWLIB_RAW}" || exit 1
  fi
  python3 - "${TMP_NEWLIB_RAW}" "${LIC_CACHE_DIR}/newlib-COPYING.NEWLIB" <<'PY' \
    || die "Could not process COPYING.NEWLIB at ${NEWLIB_COMMIT}"
import pathlib, re, sys
raw = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace")
cleaned = re.sub(r"<?[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}>?", "[email omitted]", raw)
pathlib.Path(sys.argv[2]).write_text(cleaned, encoding="utf-8")
PY

  echo "${EXPECTED_STAMP}" > "${STAMP_FILE}"
fi

for req_file in \
  "${LIC_CACHE_DIR}/pico-sdk-LICENSE.TXT" \
  "${LIC_CACHE_DIR}/pico-printf-header.txt" \
  "${LIC_CACHE_DIR}/tinyusb-LICENSE.txt" \
  "${LIC_CACHE_DIR}/pico-bootrom-LICENSE.TXT" \
  "${LIC_CACHE_DIR}/pico-bootrom-mufplib-header.txt" \
  "${LIC_CACHE_DIR}/newlib-COPYING.NEWLIB"; do
  if [[ ! -s "${req_file}" ]]; then
    echo "ERROR: Required licence file is missing or empty: ${req_file}" >&2
    exit 1
  fi
done

RP2040JS_LICENSE_FILE="${UPSTREAM_DIR}/rp2040js/LICENSE"
if [[ -f "${UPSTREAM_DIR}/rp2040js/demo/bootrom.ts" ]]; then
  BOOTROM_TS_FILE="${UPSTREAM_DIR}/rp2040js/demo/bootrom.ts"
elif [[ -f "${UPSTREAM_DIR}/rp2040js/src/bootrom.ts" ]]; then
  BOOTROM_TS_FILE="${UPSTREAM_DIR}/rp2040js/src/bootrom.ts"
else
  echo "ERROR: Missing bootrom.ts in ${UPSTREAM_DIR}/rp2040js" >&2
  exit 1
fi

python3 - \
  "${RP2040JS_LICENSE_FILE}" \
  "${BOOTROM_TS_FILE}" \
  "${LIC_CACHE_DIR}/pico-bootrom-LICENSE.TXT" \
  "${LIC_CACHE_DIR}/pico-bootrom-mufplib-header.txt" \
  "${LIC_CACHE_DIR}/pico-sdk-LICENSE.TXT" \
  "${LIC_CACHE_DIR}/pico-printf-header.txt" \
  "${LIC_CACHE_DIR}/tinyusb-LICENSE.txt" \
  "${LIC_CACHE_DIR}/newlib-COPYING.NEWLIB" \
  "${OUT_FILE}" <<'PY'
import pathlib
import re
import sys

rp2040js_lic = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").strip()
bootrom_lines = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8").splitlines()[:3]
bootrom_header = "\n".join(bootrom_lines).strip()
bootrom_lic = pathlib.Path(sys.argv[3]).read_text(encoding="utf-8").strip()
mufplib_hdr = pathlib.Path(sys.argv[4]).read_text(encoding="utf-8").strip()
pico_sdk_lic = pathlib.Path(sys.argv[5]).read_text(encoding="utf-8").strip()
pico_printf_hdr = pathlib.Path(sys.argv[6]).read_text(encoding="utf-8").strip()
tinyusb_lic = pathlib.Path(sys.argv[7]).read_text(encoding="utf-8").strip()
newlib_lic = pathlib.Path(sys.argv[8]).read_text(encoding="utf-8").strip()
out_path = pathlib.Path(sys.argv[9])

content = f"""THIRD-PARTY SOFTWARE NOTICES AND LICENSES
SAINTCON 2022 Badge Emulator (badges/2022/core.js, badges/2022/Saintcon2022BadgeFirmwareFix.uf2)

This file contains the licence texts and provenance notices for the upstream
firmware binary, RP2040 CPU/peripheral emulator, RP2040 bootrom image, and
libraries linked into the SAINTCON 2022 Clue-style notepad badge emulator.
Every third-party licence text below is read directly from the corresponding
upstream file fetched at a pinned commit by emulator/cores/2022/gen-licenses.sh.

================================================================================
1. SAINTCON 2022 Badge Firmware (Saintcon2022BadgeFirmwareFix.uf2) - Licence not stated
================================================================================

- Repository:  https://github.com/compukidmike/saintcon2022
- Commit:      383964e2e0e576dd3c153fc479d37fb429ce2969
- Commit Date: 2022-10-28 11:29:41 -0600
- Summary:     add firmware fix
- File:        Saintcon2022BadgeFirmwareFix.uf2 (97,792 bytes, 191 UF2 blocks,
               48,896-byte flash image)
- Author:      compukidmike (https://github.com/compukidmike)
- Licence:     Licence not stated (no LICENSE or COPYING file is present in the
               upstream compukidmike/saintcon2022 repository; the repository
               contains only assembly/minibadge markdown guides and the prebuilt
               Saintcon2022BadgeFirmwareFix.uf2 image).


================================================================================
2. wokwi/rp2040js RP2040 Emulator (LICENSE) - MIT License
================================================================================

Source: https://github.com/wokwi/rp2040js
Commit: 5fbd20508a58b9859758c5cd70b207ba1e0b0150
File:   LICENSE

{rp2040js_lic}


================================================================================
3. RP2040 Bootrom B1 (raspberrypi/pico-bootrom LICENSE.TXT, bootrom/mufplib.S & bootrom/mufplib-double.S)
================================================================================

Source: https://github.com/raspberrypi/pico-bootrom
Commit: 00a4a19114195e20fb817bdfbca1165e157eef37
Files:  LICENSE.TXT (BSD-3-Clause License), bootrom/mufplib.S and
        bootrom/mufplib-double.S (Mark Owen's mufplib floating-point library:
        RP2040-only license granted by Raspberry Pi (Trading) Ltd, or GNU
        General Public License version 2 from the copyright owner)

Header in wokwi/rp2040js demo/bootrom.ts:
{bootrom_header}

Embedded copyright string in the B1 bootrom binary:
(C) 2020 Raspberry Pi Trading Ltd

--- 3A. raspberrypi/pico-bootrom LICENSE.TXT (BSD-3-Clause License) ---

{bootrom_lic}

--- 3B. bootrom/mufplib.S and bootrom/mufplib-double.S (Mark Owen mufplib Floating-Point Routines in B1 Bootrom) ---

Source: https://github.com/raspberrypi/pico-bootrom/blob/00a4a19114195e20fb817bdfbca1165e157eef37/bootrom/mufplib.S
        https://github.com/raspberrypi/pico-bootrom/blob/00a4a19114195e20fb817bdfbca1165e157eef37/bootrom/mufplib-double.S
Terms as stated in bootrom/mufplib.S and bootrom/mufplib-double.S (the two files carry the same notice):
Copyright (c) 2020 Mark Owen (https://www.quinapalus.com). Raspberry Pi
(Trading) Ltd grants a non-exclusive license to use the software solely on a
Raspberry Pi RP2040 device; the software is also available from the copyright
owner under the GNU General Public License version 2 (GPLv2).
Note: Mark Owen's notice header from bootrom/mufplib.S at the pinned commit (the
same header as in bootrom/mufplib-double.S) is reproduced word for word below,
with any email address omitted (no email address appears in the upstream header).
This museum runs the bootrom image shipped by rp2040js inside an emulator, not
on an RP2040 device.


{mufplib_hdr}


================================================================================
4. Raspberry Pi Pico SDK 1.3.1 (LICENSE.TXT, linked into Saintcon2022BadgeFirmwareFix.uf2) - BSD-3-Clause License
================================================================================

Source: https://github.com/raspberrypi/pico-sdk
Tag:    1.3.1 (commit 426e46126b5a1efaea4544cdb71ab81b61983034)
File:   LICENSE.TXT

Provenance in Saintcon2022BadgeFirmwareFix.uf2:
Embedded Pico SDK binary_info strings in the UF2 payload identify the build
type ("Debug"), second-stage boot loader ("boot2_w25q080"), Raspberry Pi Pico
SDK version ("1.3.1"), target board ("pico"), binary_info program name
("blink"), and build date ("Oct 26 2022"). The ARM arithmetic, bit, memory, and
floating-point helper routines linked into the UF2 are Pico SDK wrapper
implementations (pico_bit_ops, pico_divider, pico_double, pico_int64_ops,
pico_float, pico_mem_ops) covered by this BSD-3-Clause licence. The C runtime
startup and teardown code (crti/crtbegin and __do_global_dtors_aux) linked from
libgcc in the GNU Arm Embedded Toolchain is covered by the GCC Runtime Library
Exception (GPL-3.0-or-later WITH GCC-exception-3.1), under which no separate
licence notice is required for eligible compilation output.

{pico_sdk_lic}


================================================================================
5. Marco Paland printf (src/rp2_common/pico_printf/printf.c in Pico SDK 1.3.1) - MIT License
================================================================================

Source: https://github.com/raspberrypi/pico-sdk/blob/426e46126b5a1efaea4544cdb71ab81b61983034/src/rp2_common/pico_printf/printf.c
Tag:    1.3.1 (commit 426e46126b5a1efaea4544cdb71ab81b61983034)
File:   src/rp2_common/pico_printf/printf.c

Provenance in Saintcon2022BadgeFirmwareFix.uf2:
Disassembly of the UF2 binary shows that the mpaland printf formatting routines
(_ntoa_format, _ntoa_long, _ntoa_long_long, _ftoa, _etoa, and _vsnprintf) from
Pico SDK's pico_printf module are linked into the firmware.
Note: The author's email address in the upstream file header has been omitted
below; see the linked file at tag 1.3.1 for the original header.

{pico_printf_hdr}


================================================================================
6. TinyUSB (LICENSE, linked into Saintcon2022BadgeFirmwareFix.uf2 via pico_stdio_usb) - MIT License
================================================================================

Source: https://github.com/hathach/tinyusb
Commit: 4bfab30c02279a0530e1a56f4a7c539f2d35a293 (lib/tinyusb submodule of Pico SDK 1.3.1)
File:   LICENSE

Provenance in Saintcon2022BadgeFirmwareFix.uf2:
Embedded USB CDC descriptor strings ("Raspberry Pi", "Pico", "Board CDC",
"Reset"), the USB configuration descriptor, and the TinyUSB CDC device stack
routines from Pico SDK's pico_stdio_usb module are linked into the UF2 binary.

{tinyusb_lic}


================================================================================
7. Newlib C Library (COPYING.NEWLIB, linked into Saintcon2022BadgeFirmwareFix.uf2)
================================================================================

Source: https://sourceware.org/git/newlib-cygwin.git (mirror: https://github.com/mirror/newlib-cygwin)
Tag:    newlib-4.1.0 (commit 415fdd4279b85eeec9d54775ce13c5c412451e08)
File:   COPYING.NEWLIB

Provenance in Saintcon2022BadgeFirmwareFix.uf2:
Disassembly of the UF2 binary shows that the Newlib C library routines rand,
strcmp, and strlen are linked into the firmware from the
bare-metal ARM toolchain. The exact GNU Arm Embedded Toolchain and Newlib
release version used to build the UF2 binary is not recorded in the binary;
COPYING.NEWLIB below is fetched from pinned tag newlib-4.1.0 (with email
addresses omitted).

{newlib_lic}
"""

email_re = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
if email_re.search(content):
    sys.stderr.write("ERROR: Unexpected email address in generated THIRD_PARTY_LICENSES.txt\n")
    sys.exit(1)

addr_re = re.compile(r"0x100[0-9a-fA-F]{5}")
if addr_re.search(content):
    sys.stderr.write("ERROR: Unexpected firmware address in generated THIRD_PARTY_LICENSES.txt\n")
    sys.exit(1)

out_path.write_text(content, encoding="utf-8")
print(f"[gen-licenses 2022] Wrote {out_path} ({len(content.splitlines())} lines)")
PY
