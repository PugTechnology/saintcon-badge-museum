#!/usr/bin/env bash
# Generate emulator/web/badges/2024/THIRD_PARTY_LICENSES.txt from the upstream
# license files for everything compiled into badges/2024/core.{js,wasm}.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_MC="${HERE}/upstream/managed_components"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2024-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2024/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Ensure upstream managed_components are staged
if [[ ! -d "${UPSTREAM_MC}/lvgl__lvgl" || ! -d "${UPSTREAM_MC}/johboh__nlohmann-json" ]]; then
  bash "${HERE}/fetch-upstream.sh"
fi

# Cache Emscripten and musl licenses when running inside emsdk container (or extract via docker)
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
SAINTCON 2024 Wrist Badge Emulator (badges/2024/core.wasm, badges/2024/core.js)

This file contains the license texts and provenance notices for third-party
software libraries, components, fonts, and runtime code compiled into the
SAINTCON 2024 wrist badge WebAssembly core:
  - Original Firmware & Tower Battle Game (core.wasm / core.js) from
    compukidmike/Saintcon2024 (https://github.com/compukidmike/Saintcon2024,
    commit 5e9412c837334379d7f6cadf68327fc0771f636d; badge by compukidmike).
    No LICENSE file is stated in the upstream compukidmike/Saintcon2024
    repository.

HEADER

  append_section \
    "1. LVGL 9.2.0 (lvgl/lvgl @ aa7446344c6ec7631112ef031983ef24077e24d5) - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/LICENCE.txt"

  append_lines \
    "1a. LVGL Built-in TLSF Allocator (lv_tlsf) - BSD-3-Clause License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/stdlib/builtin/lv_tlsf.h" 7 42

  append_lines \
    "1b. LVGL Built-in printf / sprintf (mpaland/printf) - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/stdlib/builtin/lv_sprintf_builtin.c" 1 31

  append_lines \
    "1c. LVGL Embedded Library: LodePNG - zlib License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/lodepng/lodepng.h" 1 24

  append_section \
    "1d. LVGL Embedded Library: LZ4 - BSD-2-Clause License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/lz4/LICENSE"

  append_lines \
    "1e. LVGL Embedded Library: ThorVG - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/thorvg/tvgCommon.h" 1 21

  append_section \
    "2. JSON for Modern C++ 3.11.3 (nlohmann/json @ 9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03) - MIT License" \
    "${UPSTREAM_MC}/johboh__nlohmann-json/LICENSE.MIT"

  append_lines \
    "2a. Hedley macros bundled in nlohmann/json (hedley.hpp) - MIT License, text in section 2" \
    "${UPSTREAM_MC}/johboh__nlohmann-json/include/nlohmann/thirdparty/hedley/hedley.hpp" 8 14

  cat <<'LVGL_FONTS'
================================================================================
3. LVGL Built-In Fonts (Montserrat & FontAwesome 5 Free Symbols)
================================================================================

LVGL 9.2.0 bundles pre-rasterized glyphs made from Montserrat-Medium.ttf and
FontAwesome5-Solid+Brands+Regular.woff (its scripts/built_in_font folder).
LVGL 9.2.0 ships no licence file for these fonts. Their own projects publish
them under the SIL Open Font License 1.1:
  - Montserrat: Copyright 2011 The Montserrat Project Authors
    (https://github.com/JulietaUla/Montserrat).
  - Font Awesome 5 Free fonts: Copyright (c) Font Awesome (as stated in the
    font file; licence terms at https://fontawesome.com/license/free).

LVGL_FONTS

  printf '================================================================================\n'
  printf '%s\n' "3a. SIL Open Font License 1.1 (text only)"
  printf '================================================================================\n\n'
  ofl="${UPSTREAM_MC}/lvgl__lvgl/tests/src/test_files/fonts/noto/OFL.txt"
  [[ -f "${ofl}" ]] || { echo "ERROR: Missing ${ofl}" >&2; exit 1; }
  # Skip that file's first line, which is Adobe's copyright notice for its Source fonts.
  sed -n '/^This Font Software is licensed/,$p' "${ofl}"
  printf '\n\n'

  cat <<'UPSTREAM_FONTS'
================================================================================
4. Fonts and Icon Glyphs in Upstream Firmware (Firmware/components/ui/fonts/)
================================================================================

The upstream compukidmike/Saintcon2024 repository (commit
5e9412c837334379d7f6cadf68327fc0771f636d) includes pre-converted LVGL bitmap
font files under Firmware/components/ui/fonts/ with no separate font license
files in the repository:

  - Font Awesome Sharp Solid Icon Glyphs (Firmware/components/ui/fonts/fa_sharp_icons_16.c):
    Converted from Font Awesome Sharp Solid (Sharp-Solid-900.otf). Font Awesome
    Sharp is a Font Awesome Pro style; these converted icon glyphs are included
    in compukidmike's upstream firmware and are not covered by the Font Awesome
    Free license in Section 3. License not stated in the upstream repository.

  - Clarity Font (Firmware/components/ui/fonts/clarity_14.c, clarity_16.c,
    clarity_18.c, clarity_20.c, clarity_22.c, clarity_24.c):
    Converted from Clarity.otf. License not stated in the upstream repository.

  - Cyberphont 3B Font (Firmware/components/ui/fonts/cyberphont3b_10.c,
    cyberphont3b_12.c, cyberphont3b_14.c, cyberphont3b_16.c):
    Converted from Cyberphont 3B. License not stated in the upstream repository.

  - BM mini Font (Firmware/components/ui/fonts/bm_mini_8.c, bm_mini_16.c):
    Converted from BMmini.TTF. License not stated in the upstream repository.


UPSTREAM_FONTS

  append_section \
    "5. Emscripten Runtime (badges/2024/core.js / badges/2024/core.wasm) - MIT / NCSA Dual License" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "6. musl C Standard Library (linked into badges/2024/core.wasm) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"
} > "${OUT_FILE}"

echo "Generated ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
