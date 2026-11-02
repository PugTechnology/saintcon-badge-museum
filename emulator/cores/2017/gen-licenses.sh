#!/usr/bin/env bash
# Deterministically regenerate emulator/web/badges/2017/THIRD_PARTY_LICENSES.txt
# from the pinned, scrubbed SAINTCON 2017 upstream staging tree.
#
# Always runs fetch-upstream.sh first so the staged tree is guaranteed to be
# present and scrubbed before reading.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"
UPSTREAM="${HERE}/upstream"
BUILD_TMP="${REPO_ROOT}/emulator/build/core-2017-obj"
CACHE_2020="${REPO_ROOT}/emulator/build/core-2020-obj"
OUT="${REPO_ROOT}/emulator/web/badges/2017/THIRD_PARTY_LICENSES.txt"

mkdir -p "${BUILD_TMP}" "$(dirname "${OUT}")"

"${HERE}/fetch-upstream.sh"

# Cache Emscripten and musl license texts from emsdk container or existing build cache
if [[ -f "/emsdk/upstream/emscripten/LICENSE" ]]; then
  cp -f "/emsdk/upstream/emscripten/LICENSE" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "/emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT" "${BUILD_TMP}/musl-COPYRIGHT.txt"
elif [[ -f "${CACHE_2020}/emscripten-LICENSE.txt" && -f "${CACHE_2020}/musl-COPYRIGHT.txt" ]]; then
  cp -f "${CACHE_2020}/emscripten-LICENSE.txt" "${BUILD_TMP}/emscripten-LICENSE.txt"
  cp -f "${CACHE_2020}/musl-COPYRIGHT.txt" "${BUILD_TMP}/musl-COPYRIGHT.txt"
elif [[ ! -f "${BUILD_TMP}/emscripten-LICENSE.txt" || ! -f "${BUILD_TMP}/musl-COPYRIGHT.txt" ]]; then
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "${BUILD_TMP}:/out" \
    emscripten/emsdk:3.1.73 \
    bash -c 'cp -f /emsdk/upstream/emscripten/LICENSE /out/emscripten-LICENSE.txt && cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT /out/musl-COPYRIGHT.txt'
fi

COMMIT="$(cat "${UPSTREAM}/.fetched_commit")"

python3 - "${UPSTREAM}" "${OUT}" "${COMMIT}" "${BUILD_TMP}" <<'PY'
import pathlib
import sys

upstream = pathlib.Path(sys.argv[1])
out_path = pathlib.Path(sys.argv[2])
commit = sys.argv[3].strip()
build_tmp = pathlib.Path(sys.argv[4])

ino_path = upstream / "HITBUTT_Minibadge/HITB_Test.ino"
ino_lines = ino_path.read_text(encoding="utf-8", errors="replace").splitlines()
emscripten_lic = (build_tmp / "emscripten-LICENSE.txt").read_text(encoding="utf-8").strip()
musl_lic = (build_tmp / "musl-COPYRIGHT.txt").read_text(encoding="utf-8").strip()

sections = []
sections.append(
    """================================================================================
SAINTCON 2017 — HITB Minibadge 2017 ("Original Firmware" / Stock Mode)
Third-Party Credits, Upstream Provenance, and Licence Notice
================================================================================

1. UPSTREAM REPOSITORY & PINNED COMMIT
--------------------------------------------------------------------------------
- Repository:  https://github.com/compukidmike/Saintcon2017
- Commit:      """
    + commit
    + """
- Commit Date: 2017-10-12 10:00:18 -0600
- Summary:     Update README.md
- Scope Note:  The upstream repository states "These are the files for the
               stuff I worked on for Saintcon 2017". It contains the
               "HITB Minibadge 2017" (`HITBUTT_Minibadge/HITB_Test.ino`,
               `Base-Rev0.brd`, `Base-Rev0.sch`) and `MinibadgeTutorial/`.
               It is a minibadge, not the main 2017 conference badge; the
               main 2017 badge firmware is not in this repository.

2. AUTHOR & BOARD CREDITS (VERIFIED FROM SOURCE & SILKSCREEN)
--------------------------------------------------------------------------------
- Repository:
    The repository `compukidmike/Saintcon2017` is compukidmike's
    (https://github.com/compukidmike).
- Handles on the Board's Back Silkscreen (`HITBUTT_Minibadge/Base-Rev0.brd:215-222`,
  layer 22 `bPlace` under `@HITBUTT 2017`, listed in the order of the board file):
    - compukidmike — named on the board
    - d4rkm4tter — named on the board
    - kampf — named on the board
    - D3M0S — named on the board
    - Heinrich — named on the board
    - dumby — named on the board
    - RuShan — named on the board
- The embedded EAGLE library entries in `Base-Rev0.brd` and `Base-Rev0.sch`
  carry their own author lines.

3. LICENCE STATUS
--------------------------------------------------------------------------------
- Licence not stated.
- No `LICENSE` or `COPYING` file is present in `compukidmike/Saintcon2017`
  at commit `"""
    + commit
    + """`, and no SPDX or licence header appears in
  `HITBUTT_Minibadge/HITB_Test.ino`, `Base-Rev0.brd`, or `Base-Rev0.sch`.

4. EXTERNAL ARDUINO LIBRARIES (WRITTEN TO BE USED WITH; REPLACED BY SHIMS)
--------------------------------------------------------------------------------
`HITBUTT_Minibadge/README.md` and `HITBUTT_Minibadge/HITB_Test.ino:1-2` state
that the sketch was written to be used with:
  - `APA102` by pololu (`#include <APA102.h>`)
  - `ADCTouch` by martin2250 (`#include <ADCTouch.h>`)

Neither third-party library is shipped, copied, or linked into this emulator.
Our own shims in `emulator/cores/2017/platform/` (`Arduino.h`,
`APA102.h`, `ADCTouch.h`, `emu_2017.cpp`) replace them so `HITB_Test.ino`
compiles unchanged.

5. STAGED UPSTREAM FILES & SCRUB AUDIT
--------------------------------------------------------------------------------
- `HITBUTT_Minibadge/HITB_Test.ino` ("""
    + str(len(ino_lines))
    + """ lines, compiled unchanged)
- `HITBUTT_Minibadge/Base-Rev0.brd` (EAGLE 7.7.0 XML board layout; embedded
  library `<author>` email addresses at lines 344 and 1425 scrubbed by
  `fetch-upstream.sh` to `MUSEUM_REDACTED`)
- `HITBUTT_Minibadge/Base-Rev0.sch` (EAGLE 7.7.0 XML schematic; embedded
  library `<author>` email addresses at lines 421, 932, and 4335 scrubbed by
  `fetch-upstream.sh` to `MUSEUM_REDACTED`)

================================================================================
6. Emscripten Runtime & C/C++ Standard Library (MIT / University of Illinois NCSA Open Source License)
================================================================================

"""
    + emscripten_lic
    + """

================================================================================
7. musl libc Runtime (linked by Emscripten) - MIT License
================================================================================

"""
    + musl_lic
    + "\n"
)

out_path.write_text("\n".join(sections), encoding="utf-8")
print(f"[gen-licenses 2017] Wrote {out_path} ({len(out_path.read_text().splitlines())} lines)")
PY
