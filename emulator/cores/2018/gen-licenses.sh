#!/usr/bin/env bash
# Generate emulator/web/badges/2018/THIRD_PARTY_LICENSES.txt from the upstream
# license files and file headers for everything compiled/bundled into
# badges/2018/core.{js,wasm}.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"
EXPECTED_COMMIT="fee2837d4607ee8a0360e99709894fc5fc608948"
BADGE_SRC="${DEST}/Badge/Badge Source Code"
MPY_DIR="${DEST}/micropython"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2018-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2018/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Ensure upstream tree is staged and verified via the same marker file as fetch-upstream.sh
if [[ ! -f "${STAMP}" || "$(cat "${STAMP}")" != "${EXPECTED_COMMIT}" || ! -f "${MPY_DIR}/LICENSE" ]]; then
  bash "${HERE}/fetch-upstream.sh"
fi

if [[ ! -f "${STAMP}" || "$(cat "${STAMP}")" != "${EXPECTED_COMMIT}" ]]; then
  echo "ERROR: Marker file ${STAMP} missing or does not match ${EXPECTED_COMMIT}" >&2
  exit 1
fi

# Cache Emscripten and musl licenses from emscripten/emsdk:3.1.73 if not already extracted
if [[ -f "/emsdk/upstream/emscripten/LICENSE" ]]; then
  cp -f "/emsdk/upstream/emscripten/LICENSE" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "${BUILD_TMP}/musl-COPYRIGHT.txt"
elif [[ ! -f "${BUILD_TMP}/emscripten-LICENSE.txt" || ! -f "${BUILD_TMP}/musl-COPYRIGHT.txt" ]]; then
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${BUILD_TMP}:/out" \
    emscripten/emsdk:3.1.73 \
    bash -c 'cp -f /emsdk/upstream/emscripten/LICENSE /out/emscripten-LICENSE.txt && cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT /out/musl-COPYRIGHT.txt'
fi

append_section() {
  local title="$1"
  local file="$2"
  if [[ ! -f "${file}" ]]; then
    echo "ERROR: Missing license file ${file} for ${title}" >&2
    exit 1
  fi
  printf '================================================================================\n'
  printf '%s\n' "${title}"
  printf '================================================================================\n\n'
  cat "${file}"
  printf '\n\n'
}

append_lines() {
  local title="$1"
  local file="$2"
  local start_line="$3"
  local end_line="$4"
  if [[ ! -f "${file}" ]]; then
    echo "ERROR: Missing source file ${file} for ${title}" >&2
    exit 1
  fi
  printf '================================================================================\n'
  printf '%s\n' "${title}"
  printf '================================================================================\n\n'
  sed -n "${start_line},${end_line}p" "${file}"
  printf '\n\n'
}

{
  cat <<'HEADER'
THIRD-PARTY SOFTWARE NOTICES AND LICENSES
SAINTCON 2018 LED Matrix Badge Emulator (badges/2018/core.wasm, badges/2018/core.js)

This file contains the licence texts and provenance notices for the upstream
firmware files, Python modules, fonts, and WebAssembly runtime components
compiled or bundled into badges/2018/core.wasm and badges/2018/core.js.

================================================================================
1. SAINTCON 2018 Badge Firmware (compukidmike/Saintcon2018)
================================================================================

Source: https://github.com/compukidmike/Saintcon2018
Commit: fee2837d4607ee8a0360e99709894fc5fc608948
Path:   Badge/Badge Source Code/ (python_files/ and frozen_modules/)

Firmware authors in upstream repository history:
  - compukidmike (Badge/Badge Source Code/python_files/: main.py, matrix.py,
    IS31FL3730.py, Font5x7.py, configure.py, wifi_config.py, hc.py, www/*)
  - miketweaver (commit d21c9a3 "add frozen modules": Badge/Badge Source Code/
    frozen_modules/hcid.py and frozen module set)

Licence for SAINTCON 2018 badge-specific files (main.py, matrix.py,
IS31FL3730.py, Font5x7.py, configure.py, wifi_config.py, hc.py, hcid.py,
and www/*):
  Licence not stated.

Third-party Python modules included inside Badge/Badge Source Code/ are listed
individually in Sections 3 through 6 below with the exact notices stated in
their shipped files.

HEADER

  append_section \
    "2. MicroPython v1.24.1 (micropython/micropython @ ecfdd5d6f9be971852003c2049600dc7b3e2a838) - MIT License" \
    "${MPY_DIR}/LICENSE"

  append_lines \
    "2a. re1.5 Regular Expression Engine in MicroPython (lib/re1.5/re1.5.h)" \
    "${MPY_DIR}/lib/re1.5/re1.5.h" 1 4
  printf '(Note: MicroPython v1.24.1 ships no separate lib/re1.5/LICENSE file; its root\nLICENSE in Section 2 lists /lib/re15 as BSD-3-clause.)\n\n'

  append_lines \
    "2b. Brad Conte crypto-algorithms in MicroPython (lib/crypto-algorithms/sha256.c) - Public Domain" \
    "${MPY_DIR}/lib/crypto-algorithms/sha256.c" 1 14

  append_lines \
    "2c. uzlib in MicroPython (lib/uzlib/uzlib.h) - zlib License" \
    "${MPY_DIR}/lib/uzlib/uzlib.h" 1 36

  append_lines \
    "3. microWebSrv (frozen_modules/microWebSrv.py) - Header Notice (MIT License; full MIT text in Section 2)" \
    "${BADGE_SRC}/frozen_modules/microWebSrv.py" 1 4

  append_lines \
    "3a. microWebSocket (frozen_modules/microWebSocket.py) - Header Notice (MIT License; full MIT text in Section 2)" \
    "${BADGE_SRC}/frozen_modules/microWebSocket.py" 1 4

  append_lines \
    "4. uasyncio (python_files/uasyncio/__init__.py) - Header Notice (MIT License; full MIT text in Section 2)" \
    "${BADGE_SRC}/python_files/uasyncio/__init__.py" 1 1

  append_lines \
    "4a. uasyncio.core (python_files/uasyncio/core.py) - Header Notice (MIT License; full MIT text in Section 2)" \
    "${BADGE_SRC}/python_files/uasyncio/core.py" 1 1

  cat <<'UASYNCIO_OTHER'
================================================================================
4b. uasyncio.queues and uasyncio.synchro (python_files/uasyncio/queues.py, synchro.py)
================================================================================

python_files/uasyncio/queues.py and python_files/uasyncio/synchro.py contain no
copyright or licence header in the shipped files:
  Licence not stated.

UASYNCIO_OTHER

  append_lines \
    "5. aswitch.py (python_files/aswitch.py) - MIT License" \
    "${BADGE_SRC}/python_files/aswitch.py" 9 29

  append_lines \
    "5a. asyn.py (python_files/asyn.py) - MIT License" \
    "${BADGE_SRC}/python_files/asyn.py" 8 28

  cat <<'SHA256_AND_FONTS'
================================================================================
6. sha256.py (frozen_modules/sha256.py)
================================================================================

frozen_modules/sha256.py contains no copyright or licence header in the shipped
file:
  Licence not stated.

================================================================================
7. Fonts (python_files/Font5x7.py and python_files/www/style.css)
================================================================================

  - 5x7 LED Matrix Bitmap Font (Badge/Badge Source Code/python_files/Font5x7.py):
    Contains no copyright or licence header in the shipped file or upstream
    repository. Licence not stated.

  - Config Web Page Stylesheet (Badge/Badge Source Code/python_files/www/style.css):
    Declares CSS fallback `font-family: 'Comfortaa', sans-serif;`. No Comfortaa
    font file is bundled or fetched over the network by the emulator; the
    browser renders using its local sans-serif font.

SHA256_AND_FONTS

  append_section \
    "8. Emscripten Runtime (badges/2018/core.js / badges/2018/core.wasm) - MIT / NCSA Dual License" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "9. musl C Standard Library (linked into badges/2018/core.wasm) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"
} > "${OUT_FILE}"

echo "Generated ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
