#!/usr/bin/env bash
# Generate emulator/web/badges/2023/THIRD_PARTY_LICENSES.txt from the upstream
# license files for everything compiled into badges/2023/core.{js,wasm}.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_DIR="${HERE}/upstream"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2023-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2023/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Stage (or re-verify) the pinned upstream firmware and ArduinoJson; cheap when already staged.
bash "${HERE}/fetch-upstream.sh"

# Cache Emscripten, musl, libc++, and Apache-2.0 license texts when running inside emsdk container (or extract via docker)
if [[ -f "/emsdk/upstream/emscripten/LICENSE" ]]; then
  cp -f "/emsdk/upstream/emscripten/LICENSE" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "${BUILD_TMP}/musl-COPYRIGHT.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libcxx/LICENSE.TXT" "${BUILD_TMP}/libcxx-LICENSE.txt"
  cp -f "/usr/share/common-licenses/Apache-2.0" "${BUILD_TMP}/Apache-2.0.txt"
elif [[ ! -f "${BUILD_TMP}/emscripten-LICENSE.txt" || ! -f "${BUILD_TMP}/musl-COPYRIGHT.txt" || ! -f "${BUILD_TMP}/libcxx-LICENSE.txt" || ! -f "${BUILD_TMP}/Apache-2.0.txt" ]]; then
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${BUILD_TMP}:/out" \
    emscripten/emsdk:3.1.73 \
    bash -c 'cp -f /emsdk/upstream/emscripten/LICENSE /out/emscripten-LICENSE.txt && cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT /out/musl-COPYRIGHT.txt && cp -f /emsdk/upstream/emscripten/system/lib/libcxx/LICENSE.TXT /out/libcxx-LICENSE.txt && cp -f /usr/share/common-licenses/Apache-2.0 /out/Apache-2.0.txt'
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
SAINTCON 2023 Badge Emulator (badges/2023/core.wasm, badges/2023/core.js)

This file contains the licence texts and provenance notices for third-party
software libraries, components, assets, and runtime code compiled into the
SAINTCON 2023 "Incidents" badge WebAssembly core:
  - Original Firmware & "Incidents" Dungeon Crawler (core.wasm / core.js) from
    compukidmike/Saintcon2023 (https://github.com/compukidmike/Saintcon2023,
    commit 6f5f3407bb57000e0131fc317ad907f27bda7234, Firmware/Source/; badge by
    compukidmike, with firmware contributions from jarmknecht and klipperx
    in that repository). Licence not stated.

HEADER

  append_section \
    "1. ArduinoJson 6.21.3 (bblanchon/ArduinoJson @ 4b00783345484b4fd4a4f89f842809ef3eee1681) - MIT License" \
    "${UPSTREAM_DIR}/lib_deps/ArduinoJson/LICENSE.txt"

  append_lines \
    "2. BitBank GIF Animator (Firmware/Source/include/gif.inl) - Apache License 2.0" \
    "${UPSTREAM_DIR}/Firmware/Source/include/gif.inl" 1 19

  append_section \
    "2a. Apache License, Version 2.0 (text)" \
    "${BUILD_TMP}/Apache-2.0.txt"

  cat <<'UPSTREAM_ASSETS'
================================================================================
3. Embedded Graphics and Fonts
================================================================================

  - Upstream Sprite, Portrait, Map, UI, and Boot Logo Assets
    (Firmware/Source/include/sclogo.h,
    Firmware/Source/include/assets/bosses/,
    Firmware/Source/include/assets/faces/,
    Firmware/Source/include/assets/heros/,
    Firmware/Source/include/assets/maps/,
    Firmware/Source/include/assets/ui/):
    Pre-converted RGB565 bitmaps and GIF sprite headers included in
    compukidmike/Saintcon2023 (commit 6f5f3407bb57000e0131fc317ad907f27bda7234).
    Licence not stated.

  - 5x7 Monochrome Bitmap Font & Arduino_GFX Shim
    (emulator/cores/2023/platform/include/Arduino_GFX_Library.h):
    First-party platform shim and 5x7 ASCII bitmap font authored for this
    emulator repository. No third-party GFX Library for Arduino source or font
    files are compiled into badges/2023/core.wasm.


UPSTREAM_ASSETS

  append_section \
    "4. Emscripten Runtime (badges/2023/core.js / badges/2023/core.wasm) - MIT / NCSA Dual License" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "5. musl C Standard Library (linked into badges/2023/core.wasm) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"

  append_section \
    "6. LLVM libc++ C++ Standard Library (linked into badges/2023/core.wasm) - Apache-2.0 WITH LLVM-exception" \
    "${BUILD_TMP}/libcxx-LICENSE.txt"
} > "${OUT_FILE}"

echo "Generated ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
