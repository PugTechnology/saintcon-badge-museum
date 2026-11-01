#!/usr/bin/env bash
# Generate emulator/web/badges/2025/THIRD_PARTY_LICENSES.txt from the upstream
# license files for everything compiled into core-stock.{js,wasm} and core.{js,wasm}.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_MC="${HERE}/upstream/managed_components"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2025-stock-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2025/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Ensure upstream managed_components are staged
if [[ ! -d "${UPSTREAM_MC}/lvgl__lvgl" ]]; then
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

{
  cat <<'HEADER'
THIRD-PARTY SOFTWARE NOTICES AND LICENSES
SAINTCON 2025 Badge Emulator (badges/2025/core-stock.wasm, core-stock.js, core.wasm, core.js)

This file contains the license texts and provenance notices for third-party
software libraries, components, fonts, and runtime code compiled into the
SAINTCON 2025 badge WebAssembly cores:
  - Stock Firmware & Faction Game (core-stock.wasm / core-stock.js) from
    compukidmike/Saintcon2025 (https://github.com/compukidmike/Saintcon2025,
    commit 360c892981968822fbadfdaa6ff580a55f4396af; badge by compukidmike),
    emulated with permission.
  - Pug Pet (core.wasm / core.js), authored by PugTechnology.

HEADER

  append_section \
    "1. LVGL 9.3.0 (lvgl/lvgl) - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/LICENCE.txt"

  append_section \
    "1a. LVGL Third-Party Copyrights Index (COPYRIGHTS.md)" \
    "${UPSTREAM_MC}/lvgl__lvgl/COPYRIGHTS.md"

  append_section \
    "1b. LVGL Built-in TLSF Allocator (lv_tlsf) - BSD-3-Clause License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/stdlib/builtin/LICENSE_TLSF.txt"

  append_section \
    "1c. LVGL Built-in sprintf (stb_sprintf) - MIT / Public Domain" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/stdlib/builtin/LICENSE_SPRINTF.txt"

  append_section \
    "1d. LVGL Embedded Library: LodePNG - zlib License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/lodepng/LICENSE.txt"

  append_section \
    "1e. LVGL Embedded Library: TinyJPGD (TJPGD) - BSD-style License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/tjpgd/LICENSE.txt"

  append_section \
    "1f. LVGL Embedded Library: LZ4 - BSD-2-Clause License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/lz4/LICENSE.txt"

  append_section \
    "1g. LVGL Embedded Library: GIFDEC - Public Domain / CC0" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/gif/LICENSE.txt"

  append_section \
    "1h. LVGL Embedded Library: QR-Code-generator - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/qrcode/LICENSE.txt"

  append_section \
    "1i. LVGL Embedded Library: Barcode (code128) - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/barcode/LICENSE.txt"

  append_section \
    "1j. LVGL Embedded Library: Tiny TTF (stb_truetype / stb_rect_pack) - MIT / Public Domain" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/tiny_ttf/LICENSE.txt"

  append_section \
    "1k. LVGL Embedded Library: ThorVG - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/thorvg/LICENSE.txt"

  append_section \
    "1l. LVGL Embedded Library: Expat XML Parser - MIT License" \
    "${UPSTREAM_MC}/lvgl__lvgl/src/libs/expat/LICENSE.txt"

  append_section \
    "2. JSON for Modern C++ 3.12.0 (nlohmann/json) - MIT License" \
    "${UPSTREAM_MC}/johboh__nlohmann-json/LICENSE.MIT"

  append_section \
    "2a. nlohmann/json Additional Component License - BSD-3-Clause" \
    "${UPSTREAM_MC}/johboh__nlohmann-json/LICENSES/BSD-3-Clause.txt"

  if [[ -f "${UPSTREAM_MC}/espp__base_component/LICENSE" ]]; then
    append_section \
      "3. espp Components (espp/base_component, base_peripheral, format, logger, ndef, st25dv) - MIT License" \
      "${UPSTREAM_MC}/espp__base_component/LICENSE"
  else
    cat <<'ESPP_SECTION'
================================================================================
3. espp Components (espp/base_component, base_peripheral, format, logger, ndef, st25dv)
================================================================================

MIT per idf_component.yml in each staged component directory
(upstream repository: git://github.com/esp-cpp/espp.git,
commits 52615e7bd930fc8b1b2b75f2f23eb96704779e71 for base_component/base_peripheral/format/ndef v1.0.28,
6fe3c4f6f2fcda3e07237bdec251b31cff1c3807 for logger v1.3.11, and
2b699b4cb8b6cccb2e60f179984c58ddd26b6749 for st25dv v1.0.27).
No separate LICENSE file is included in the staged ESP-IDF managed_components
directories.


ESPP_SECTION
  fi

  append_section \
    "3a. {fmt} Formatting Library (bundled in espp/format) - MIT License with Exception" \
    "${UPSTREAM_MC}/espp__format/detail/fmt/LICENSE"

  append_section \
    "3b. {fmt} Documentation / Python Software Foundation License" \
    "${UPSTREAM_MC}/espp__format/detail/fmt/doc/python-license.txt"

  append_section \
    "4. Boost C++ Libraries (espressif/esp-boost 0.3.1, boost::bimap) - Boost Software License 1.0" \
    "${UPSTREAM_MC}/espressif__esp-boost/license.txt"

  append_section \
    "5. Montserrat Font (LVGL built-in font lv_font_montserrat_*) - SIL Open Font License 1.1" \
    "${UPSTREAM_MC}/lvgl__lvgl/scripts/built_in_font/font_license/Montserrat/OFL.txt"

  append_section \
    "5a. FontAwesome 5 Free Symbols (LVGL built-in font symbols only) - SIL OFL 1.1 / MIT" \
    "${UPSTREAM_MC}/lvgl__lvgl/scripts/built_in_font/font_license/FontAwesome5/LICENSE.txt"

  append_section \
    "5b. DejaVu Sans Fonts (LVGL built-in font) - Bitstream Vera / Arev / Public Domain License" \
    "${UPSTREAM_MC}/lvgl__lvgl/scripts/built_in_font/font_license/DejaVuSans/LICENSE"

  append_section \
    "5c. Source Han Sans SC Fonts (LVGL built-in font) - SIL Open Font License 1.1" \
    "${UPSTREAM_MC}/lvgl__lvgl/scripts/built_in_font/font_license/SourceHanSansSC/LICENSE.txt"

  cat <<'UPSTREAM_FONTS'
================================================================================
5d. Font Awesome Sharp / Sharp Duotone Icon Glyphs (Firmware/badge/components/ui/fonts/fa_duotone_icons_16.c, fa_sharp_icons_16.c)
================================================================================

Source in upstream repository:
  - Firmware/badge/components/ui/fonts/fa_duotone_icons_16.c
  - Firmware/badge/components/ui/fonts/fa_sharp_icons_16.c
in compukidmike/Saintcon2025 (commit 360c892981968822fbadfdaa6ff580a55f4396af).

Their file headers state that they were generated from Font Awesome 7 Sharp
Duotone (Solid) and Font Awesome 6 Sharp (Solid) kit font files.

Font Awesome Sharp and Sharp Duotone are Font Awesome Pro styles. These
converted glyph files are included in compukidmike's upstream firmware and are
not covered by the free Font Awesome license in Section 5a.


================================================================================
5e. OpenDyslexic and Clarity Fonts in Upstream Firmware (Firmware/badge/components/ui/fonts/open_dyslexic_reg_16.c, clarity_*.c)
================================================================================

  - OpenDyslexic (OpenDyslexic-Regular.otf):
    Source in upstream repository: Firmware/badge/components/ui/fonts/open_dyslexic_reg_16.c
    in compukidmike/Saintcon2025 (header states --font .../opendyslexic-0.91.12/compiled/OpenDyslexic-Regular.otf).
    Licence not stated in the upstream repository (the upstream OpenDyslexic
    project publishes OpenDyslexic under the SIL Open Font License 1.1).

  - Clarity (Clarity.otf):
    Source in upstream repository: Firmware/badge/components/ui/fonts/clarity_14.c,
    clarity_16.c, clarity_18.c, clarity_20.c, clarity_22.c, clarity_24.c
    in compukidmike/Saintcon2025 (headers state --font .../clarity-font/Clarity.otf).
    Licence not stated in the upstream repository.


================================================================================
5f. White Rabbit Font (Firmware/badge/components/ui/fonts/white_rabbit_16.c, white_rabbit_18.c, white_rabbit_20.c, white_rabbit_22.c)
================================================================================

Name: White Rabbit (whitrabt.ttf)
Source in upstream repository: Firmware/badge/components/ui/fonts/white_rabbit_16.c,
white_rabbit_18.c, white_rabbit_20.c, and white_rabbit_22.c in
compukidmike/Saintcon2025 (commit 360c892981968822fbadfdaa6ff580a55f4396af;
headers state they were converted from whitrabt.ttf).
Licence not stated in the upstream repository.


================================================================================
5g. BM mini Font (Firmware/badge/components/ui/fonts/bm_mini_8.c, bm_mini_16.c)
================================================================================

Name: BM mini (BMmini.TTF)
Source in upstream repository: Firmware/badge/components/ui/fonts/bm_mini_8.c
and bm_mini_16.c in compukidmike/Saintcon2025 (commit
360c892981968822fbadfdaa6ff580a55f4396af; headers state they
were converted from BMmini.TTF).
Licence not stated in the upstream repository.


UPSTREAM_FONTS

  append_section \
    "6. Emscripten Runtime (core-stock.js / core-stock.wasm / core.js / core.wasm) - MIT / NCSA Dual License" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "7. musl C Standard Library (linked into core-stock.wasm and core.wasm) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"
} > "${OUT_FILE}"

echo "Generated ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
