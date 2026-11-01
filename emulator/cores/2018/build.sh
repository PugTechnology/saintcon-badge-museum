#!/usr/bin/env bash
# Build the SAINTCON 2018 LED Matrix Badge (MicroPython) emulator core:
#   1. Fetch & scrub upstream Badge/Badge Source Code/ + MicroPython v1.24.1
#   2. Generate embedded VFS asset table from scrubbed python_files/ + frozen_modules/
#   3. Native headless CLI (emulator/cores/2018/emu-cli) via host gcc
#   4. WebAssembly core (emulator/web/badges/2018/core.js + core.wasm) via Emscripten 3.1.73 in Docker
#   5. Generate THIRD_PARTY_LICENSES.txt and verify zero original secrets exist in web/ or tracked files
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
EMSDK_IMAGE="${EMSDK_IMAGE:-emscripten/emsdk:3.1.73}"

# Step 1: Stage & scrub upstream before anything is built or bundled.
bash "${SCRIPT_DIR}/fetch-upstream.sh"

cd "${REPO_ROOT}"

BADGE_SRC="emulator/cores/2018/upstream/Badge/Badge Source Code"
BUILD_TMP="emulator/build/core-2018-obj"
JOBS="$(nproc 2>/dev/null || echo 4)"

mkdir -p "emulator/web/badges/2018" "${BUILD_TMP}/native" "${BUILD_TMP}/wasm"

echo "[1/4] Generating scrubbed VFS asset bundle (${BUILD_TMP}/sc18_assets.h)..."
python3 - "${BADGE_SRC}" "${BUILD_TMP}/sc18_assets.h" <<'PYEOF'
import os
import sys

badge_src = sys.argv[1]
out_hdr = sys.argv[2]

entries = []
for subdir in ("frozen_modules", "python_files"):
    root = os.path.join(badge_src, subdir)
    if not os.path.isdir(root):
        raise SystemExit(f"Missing directory: {root}")
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for fn in sorted(filenames):
            if fn.lower().startswith("readme"):
                raise SystemExit(f"Refusing to bundle README file: {fn}")
            if fn.startswith(".") or fn.endswith(".pyc"):
                continue
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace("\\", "/")
            with open(full, "rb") as f:
                data = f.read()
            entries.append((rel, data))

with open(out_hdr, "w", encoding="utf-8") as out:
    out.write("/* Auto-generated from scrubbed SAINTCON 2018 Badge Source Code — DO NOT EDIT */\n")
    out.write("#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\n")
    out.write("typedef struct {\n    const char *rel_path;\n    const uint8_t *data;\n    size_t size;\n} sc18_asset_t;\n\n")
    for idx, (rel, data) in enumerate(entries):
        out.write(f"static const uint8_t k_sc18_asset_data_{idx}[{max(1, len(data))}] = {{\n")
        if len(data) == 0:
            out.write("    0x00\n")
        else:
            for off in range(0, len(data), 16):
                chunk = ", ".join(f"0x{b:02x}" for b in data[off:off + 16])
                out.write(f"    {chunk},\n")
        out.write("};\n\n")
    out.write("static const sc18_asset_t k_sc18_assets[] = {\n")
    for idx, (rel, data) in enumerate(entries):
        out.write(f'    {{ "{rel}", k_sc18_asset_data_{idx}, {len(data)}u }},\n')
    out.write("};\n")
    out.write("static const size_t k_sc18_asset_count = sizeof(k_sc18_assets) / sizeof(k_sc18_assets[0]);\n")

print(f"Bundled {len(entries)} scrubbed files into {out_hdr}")
PYEOF

echo "[2/4] Building 2018 native headless CLI (emulator/cores/2018/emu-cli)..."
make -C emulator/cores/2018 TARGET=native -j"${JOBS}"

echo "[3/4] Building 2018 WebAssembly core (emulator/web/badges/2018/core.js + core.wasm) with ${EMSDK_IMAGE}..."
docker run --rm -i \
    -u "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -e JOBS="${JOBS}" \
    -e BUILD_TMP="${BUILD_TMP}" \
    -v "${REPO_ROOT}:/src" \
    -w /src \
    "${EMSDK_IMAGE}" \
    bash -s <<'DOCKER_EOF'
set -euo pipefail
cp -a /emsdk/upstream/emscripten/cache /tmp/emcache
export EM_CACHE=/tmp/emcache

make -C emulator/cores/2018 TARGET=wasm -j"${JOBS}"

cp -f /emsdk/upstream/emscripten/LICENSE "${BUILD_TMP}/emscripten-LICENSE.txt"
cp -f /emsdk/upstream/emscripten/system/lib/libc/musl/COPYRIGHT "${BUILD_TMP}/musl-COPYRIGHT.txt"
DOCKER_EOF

echo "[4/4] Generating THIRD_PARTY_LICENSES.txt and verifying zero secrets in build outputs..."
bash "${SCRIPT_DIR}/gen-licenses.sh"
bash "${SCRIPT_DIR}/fetch-upstream.sh" --verify-clean

ls -lh "${REPO_ROOT}/emulator/cores/2018/emu-cli" \
       "${REPO_ROOT}/emulator/web/badges/2018/core.js" \
       "${REPO_ROOT}/emulator/web/badges/2018/core.wasm" \
       "${REPO_ROOT}/emulator/web/badges/2018/THIRD_PARTY_LICENSES.txt"
echo "Done."
