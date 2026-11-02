#!/usr/bin/env bash
# Generate emulator/web/badges/2020/THIRD_PARTY_LICENSES.txt from the upstream
# source header and runtime license files for everything compiled into badges/2020/core.{js,wasm}.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM_DIR="${HERE}/upstream"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2020-obj"
OUT_FILE="${REPO_ROOT}/emulator/web/badges/2020/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT_FILE}")"

# Stage (or re-verify) the pinned upstream firmware; cheap when already staged.
bash "${HERE}/fetch-upstream.sh"

# Cache Emscripten and musl license texts when running inside emsdk container (or extract via docker)
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
SAINTCON 2020 LED Badge Emulator (badges/2020/core.wasm, badges/2020/core.js)

This file contains the licence texts and provenance notices for third-party
software and hardware files compiled or rendered into the SAINTCON 2020 badge
WebAssembly core and board art:
  - Original Firmware (core.wasm / core.js) from compukidmike/Saintcon2020
    (https://github.com/compukidmike/Saintcon2020, commit
    9b11ef8514be376b9b798ec66248ab2f7a8d1fd3, 2026-04-24 09:30:47 -0600,
    "Update README.md", Firmware/Saintcon2020/main.cpp; badge firmware and
    hardware by compukidmike).
  - Licence: Licence not stated (no LICENSE file or per-file licence grant is
    present in the upstream repository).

Note on upstream contributors:
  - Firmware/Saintcon2020/main.cpp and the Hardware/* KiCad files state
    compukidmike (main.cpp:5 "Author : compukidmike" and
    Saintcon2020MainBoard.kicad_pcb F.SilkS "github.com/compukidmike").
    The PCB files also contain footprint entries libraries:MKFactorLogo and
    libraries:MKFactorTiny.
  - Repository pull requests from HebrewzHammer and ferdymercury modified only
    README.md and did not touch firmware or hardware files.

Note on AVR headers:
  - Standard AVR-LibC headers (<avr/io.h>, <avr/interrupt.h>, <avr/pgmspace.h>,
    <avr/eeprom.h>, <avr/sleep.h>, <util/delay.h>) are not shipped or compiled;
    they are replaced by our own emulator shims in
    emulator/cores/2020/platform/.

HEADER

  append_lines \
    "1. SAINTCON 2020 Badge Firmware (Firmware/Saintcon2020/main.cpp) - Licence not stated" \
    "${UPSTREAM_DIR}/Firmware/Saintcon2020/main.cpp" 1 6

  append_section \
    "2. Emscripten Runtime & C/C++ Standard Library (MIT / University of Illinois NCSA Open Source License)" \
    "${BUILD_TMP}/emscripten-LICENSE.txt"

  append_section \
    "3. musl libc Runtime (linked by Emscripten) - MIT License" \
    "${BUILD_TMP}/musl-COPYRIGHT.txt"
} > "${OUT_FILE}"

echo "[gen-licenses 2020] Wrote ${OUT_FILE} ($(wc -l < "${OUT_FILE}") lines)"
