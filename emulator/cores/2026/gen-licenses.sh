#!/usr/bin/env bash
# Generate emulator/web/badges/2026/THIRD_PARTY_LICENSES.txt and LICENSE.txt
# from the upstream license files and Emscripten/musl runtime licenses.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2026-obj"
OUT_DIR="${REPO_ROOT}/emulator/web/badges/2026"
OUT_FILE="${OUT_DIR}/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "${OUT_DIR}"

if [[ ! -f "${HERE}/upstream/LICENSE" ]]; then
  bash "${HERE}/fetch-upstream.sh"
fi

cp -f "${HERE}/upstream/LICENSE" "${OUT_DIR}/LICENSE.txt"

if [[ -f "/emsdk/upstream/emscripten/LICENSE" ]]; then
  cp -f "/emsdk/upstream/emscripten/LICENSE" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "${BUILD_TMP}/musl-COPYRIGHT.txt"
elif [[ ! -f "${BUILD_TMP}/emscripten-LICENSE.txt" || ! -f "${BUILD_TMP}/musl-COPYRIGHT.txt" ]]; then
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${BUILD_TMP}:/out" \
    emscripten/emsdk:3.1.73 \
    bash -c 'cp -f /emsdk/upstream/emscripten/LICENSE /out/emscripten-LICENSE.txt && cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT /out/musl-COPYRIGHT.txt'
fi

{
  cat <<'HEADER'
THIRD-PARTY SOFTWARE NOTICES AND LICENSES
SAINTCON 2026 Badge Emulator (badges/2026/core.wasm, core.js)

This file contains the license texts and provenance notices for the upstream
firmware, libraries, headers, fonts, and runtime code compiled into the
SAINTCON 2026 ("Orion Trail" / Wayfarer) badge WebAssembly core.

================================================================================
1. SAINTCON 2026 Badge Firmware ("Orion Trail" / Wayfarer) - MIT License
================================================================================

HEADER
  cat "${HERE}/upstream/LICENSE"
  cat <<'MID_SECTIONS'


================================================================================
2. komihash (firmware/bsp/bsp_komihash.c, firmware/bsp/bsp_komihash.h)
================================================================================

Source in upstream repository: firmware/bsp/bsp_komihash.c and
firmware/bsp/bsp_komihash.h in Professor-plum/SAINTCON-BADGE-2026
(commit 2168f62e23151fcc26a8860a7b7bf076642e2bbc).

The files in the upstream repository carry no copyright or license header.
Despite the name, this is not code from the komihash project (it is a short
hash written in the firmware itself), so it falls under the firmware's MIT
License in section 1.


================================================================================
3. Adafruit GFX Font Structures (firmware/bsp/gfxfont.h)
================================================================================

Source in upstream repository: firmware/bsp/gfxfont.h in
Professor-plum/SAINTCON-BADGE-2026
(commit 2168f62e23151fcc26a8860a7b7bf076642e2bbc).

The file in the upstream repository carries no copyright or license header.
The upstream Adafruit GFX Library project publishes gfxfont.h under the
BSD-2-Clause License.


================================================================================
4. CC Regeneration Font (firmware/bsp/CCRegeneration8pt7b.h)
================================================================================

Name: CC Regeneration (CCRegeneration8pt7b.h, Comicraft)
Source in upstream repository: firmware/bsp/CCRegeneration8pt7b.h (included by
firmware/bsp/framebuffer.c) in Professor-plum/SAINTCON-BADGE-2026
(commit 2168f62e23151fcc26a8860a7b7bf076642e2bbc).
Licence not stated in the upstream repository.


================================================================================
5. Emscripten Runtime (core.js / core.wasm) - MIT / NCSA Dual License
================================================================================

MID_SECTIONS
  cat "${BUILD_TMP}/emscripten-LICENSE.txt"
  cat <<'MUSL_HEADER'


================================================================================
6. musl C Standard Library (linked into core.wasm) - MIT License
================================================================================

MUSL_HEADER
  cat "${BUILD_TMP}/musl-COPYRIGHT.txt"
  printf '\n'
} > "${OUT_FILE}"

echo "Generated ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
