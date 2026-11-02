#!/usr/bin/env bash
# Build the SAINTCON 2022 RP2040 badge emulator web bundle, layout assets, and license file.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EMULATOR_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
REPO_ROOT="$(cd "${EMULATOR_ROOT}/.." && pwd)"
OUT_DIR="${EMULATOR_ROOT}/web/badges/2022"
LIC_CACHE_DIR="${REPO_ROOT}/emulator/build/core-2022-obj/licenses"
UF2_EXPECTED_SHA256="f5f03f7c4f45abf60de4258b88e8e4055f43bcef8a3ae1a1cc8ecee27fd9c18b"
REQUIRED_DENO_VERSION="2.9.7"
TMP_BUNDLE=""

cleanup_bundle() {
  if [[ -n "${TMP_BUNDLE}" ]]; then
    rm -f "${TMP_BUNDLE}"
  fi
}
trap cleanup_bundle EXIT INT TERM HUP

if ! command -v deno >/dev/null 2>&1; then
  echo "ERROR: deno ${REQUIRED_DENO_VERSION} is required to build the 2022 badge emulator, but deno is not installed." >&2
  exit 1
fi
ACTUAL_DENO_VERSION="$(deno --version | awk 'NR==1 {print $2}')"
if [[ "${ACTUAL_DENO_VERSION}" != "${REQUIRED_DENO_VERSION}" ]]; then
  echo "ERROR: deno ${REQUIRED_DENO_VERSION} is required to build the 2022 badge emulator (found deno ${ACTUAL_DENO_VERSION})." >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

# 1. Ensure upstream UF2 and rp2040js bundle are staged and audited
bash "${SCRIPT_DIR}/fetch-upstream.sh"

# 2. Generate THIRD_PARTY_LICENSES.txt (also populates LIC_CACHE_DIR)
bash "${SCRIPT_DIR}/gen-licenses.sh"

# 3. Generate board.png, layout.json, and badge.json from draw_layout_2022.py
python3 "${EMULATOR_ROOT}/tools/draw_layout_2022.py"

# 4. Copy unmodified UF2 firmware image to web/badges/2022/ and verify SHA-256
cp -f "${SCRIPT_DIR}/upstream/Saintcon2022BadgeFirmwareFix.uf2" "${OUT_DIR}/Saintcon2022BadgeFirmwareFix.uf2"
ACTUAL_SHA256="$(sha256sum "${OUT_DIR}/Saintcon2022BadgeFirmwareFix.uf2" | awk '{print $1}')"
if [[ "${ACTUAL_SHA256}" != "${UF2_EXPECTED_SHA256}" ]]; then
  echo "ERROR: UF2 SHA-256 mismatch: expected ${UF2_EXPECTED_SHA256}, got ${ACTUAL_SHA256}" >&2
  exit 1
fi

# 5. Bundle src/worker.mjs into web/badges/2022/core.js with full rp2040js MIT,
#    pico-bootrom BSD-3-Clause, and mufplib.S notices at top, and strip Deno file
#    comments so core.js is byte-identical regardless of working directory.
TMP_BUNDLE="$(mktemp "${OUT_DIR}/core.bundle.XXXXXX.js")"
deno bundle --no-remote --no-npm "${SCRIPT_DIR}/src/worker.mjs" > "${TMP_BUNDLE}"

python3 - \
  "${SCRIPT_DIR}/upstream/rp2040js/LICENSE" \
  "${LIC_CACHE_DIR}/pico-bootrom-LICENSE.TXT" \
  "${LIC_CACHE_DIR}/pico-bootrom-mufplib-header.txt" \
  "${TMP_BUNDLE}" \
  "${OUT_DIR}/core.js" <<'PY'
import pathlib
import sys

rp2040js_lic = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8").strip()
bootrom_lic = pathlib.Path(sys.argv[2]).read_text(encoding="utf-8").strip()
mufplib_hdr = pathlib.Path(sys.argv[3]).read_text(encoding="utf-8").strip()
bundle_lines = pathlib.Path(sys.argv[4]).read_text(encoding="utf-8").splitlines()
out_path = pathlib.Path(sys.argv[5])

# Strip Deno's relative module path header comments (`// ...`) so the bundle
# is byte-identical from any working directory and contains no file paths.
clean_bundle = "\n".join(line for line in bundle_lines if not line.startswith("// ")) + "\n"

def comment_block(text: str) -> str:
    return "\n".join(f" * {line}" if line else " *" for line in text.splitlines())

header = f"""/**
 * SAINTCON 2022 Badge RP2040 Web Worker Core Bundle
 *
 * 1. wokwi/rp2040js (commit 5fbd20508a58b9859758c5cd70b207ba1e0b0150) - MIT License:
 *
{comment_block(rp2040js_lic)}
 *
 * 2. RP2040 Bootrom B1 (raspberrypi/pico-bootrom commit 00a4a19114195e20fb817bdfbca1165e157eef37) - BSD-3-Clause License:
 *
{comment_block(bootrom_lic)}
 *
 * 3. RP2040 Bootrom B1 Floating-Point Routines (bootrom/mufplib.S in
 *    raspberrypi/pico-bootrom commit 00a4a19114195e20fb817bdfbca1165e157eef37):
 *    Copyright (c) 2020 Mark Owen (https://www.quinapalus.com). Raspberry Pi
 *    (Trading) Ltd grants a non-exclusive license to use the software solely on
 *    a Raspberry Pi RP2040 device; also available from the copyright owner under
 *    the GNU General Public License version 2 (GPLv2).
 *    Source: https://github.com/raspberrypi/pico-bootrom/blob/00a4a19114195e20fb817bdfbca1165e157eef37/bootrom/mufplib.S
 *    (Email addresses omitted; none present in upstream header.)
 *    Full third-party license texts and notices: badges/2022/THIRD_PARTY_LICENSES.txt
 */
{mufplib_hdr}
"""

out_path.write_text(header + clean_bundle, encoding="utf-8")
PY

rm -f "${TMP_BUNDLE}"
TMP_BUNDLE=""

node --check "${OUT_DIR}/core.js"

echo "[build.sh] Built ${OUT_DIR}/core.js ($(wc -c < "${OUT_DIR}/core.js") bytes) and verified UF2 (${ACTUAL_SHA256})."

if [[ "${1:-}" != "--skip-tests" ]]; then
  timeout 300 node "${SCRIPT_DIR}/test.mjs"
fi
