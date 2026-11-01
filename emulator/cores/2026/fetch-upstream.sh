#!/usr/bin/env bash
# Fetch the SAINTCON 2026 badge firmware (MIT, Professor Plum) at the pinned commit into
# emulator/cores/2026/upstream/ (git-ignored: we do not republish the author's repo, we fetch it).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="${SCRIPT_DIR}/upstream"
REPO_URL="https://github.com/Professor-plum/SAINTCON-BADGE-2026.git"
COMMIT="2168f62e23151fcc26a8860a7b7bf076642e2bbc"

if [[ -f "${DEST}/firmware/game.c" && -f "${DEST}/LICENSE" ]]; then
    exit 0
fi
TMP="$(mktemp -d)"
trap 'rm -rf "${TMP}"' EXIT
git clone --quiet --filter=blob:none --no-checkout "${REPO_URL}" "${TMP}/repo"
git -C "${TMP}/repo" checkout --quiet "${COMMIT}"
rm -rf "${DEST}" && mkdir -p "${DEST}"
cp -r "${TMP}/repo/firmware" "${TMP}/repo/LICENSE" "${DEST}/"
echo "fetched SAINTCON-BADGE-2026 @ ${COMMIT:0:7} into ${DEST}"
