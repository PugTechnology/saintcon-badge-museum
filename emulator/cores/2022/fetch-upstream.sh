#!/usr/bin/env bash
# Fetch the SAINTCON 2022 badge firmware UF2 and pinned wokwi/rp2040js source
# into ./upstream/ (git-ignored), transpile rp2040js TypeScript modules to ESM
# JavaScript offline, and verify the staged tree is clean.
#
# Pinned upstream commits (from git log):
#   saintcon2022:
#     repo:    https://github.com/compukidmike/saintcon2022.git
#     commit:  383964e2e0e576dd3c153fc479d37fb429ce2969
#     date:    2022-10-28 11:29:41 -0600
#     message: add firmware fix
#   rp2040js:
#     repo:    https://github.com/wokwi/rp2040js.git
#     commit:  5fbd20508a58b9859758c5cd70b207ba1e0b0150
#     license: MIT
#
# Re-running is a no-op once the pinned commits are staged and verified;
# pass --force to re-extract from scratch.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/saintcon2022.git"
UPSTREAM_COMMIT="383964e2e0e576dd3c153fc479d37fb429ce2969"

RP2040JS_URL="https://github.com/wokwi/rp2040js.git"
RP2040JS_COMMIT="5fbd20508a58b9859758c5cd70b207ba1e0b0150"

DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"
EXPECTED_STAMP="${UPSTREAM_COMMIT}:${RP2040JS_COMMIT}"
TMP_GIT=""

# On any failed exit, remove the staged tree and any temporary directories.
on_exit() {
  local rc=$?
  [[ -n "${TMP_GIT}" ]] && rm -rf "${TMP_GIT}"
  if [[ "${rc}" -ne 0 ]]; then
    rm -rf "${DEST}"
  fi
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
  FORCE=1
fi

verify_staged_tree() {
  set +x
  local UF2="${DEST}/Saintcon2022BadgeFirmwareFix.uf2"
  local RP_DIR="${DEST}/rp2040js"

  if [[ ! -f "${UF2}" ]]; then
    echo "ERROR: Missing ${UF2}" >&2
    exit 1
  fi
  if [[ ! -f "${RP_DIR}/LICENSE" || ! -f "${RP_DIR}/src/index.js" || ! -f "${RP_DIR}/demo/bootrom.js" ]]; then
    echo "ERROR: Missing transpiled rp2040js files in ${RP_DIR}" >&2
    exit 1
  fi

  if find "${DEST}" \( -iname 'readme*' -o -iname '*.md' -o -iname '*.png' -o -iname '*.jpg' -o -iname '*.jpeg' -o -iname '*.hex' -o -iname '*.elf' -o -iname '*.lss' -o -iname '*.srec' -o -iname '*.map' -o -iname '*.pdf' -o -iname '*.zip' -o -iname 'package.json' -o -iname 'package-lock.json' \) | grep -q .; then
    echo "ERROR: Forbidden file staged in 2022 upstream/" >&2
    exit 1
  fi

  # Audit every file in staged upstream/ for flags, HC: codes, Wi-Fi fields,
  # personal contact details, and local host paths.
  python3 - "${DEST}" <<'PY'
import hashlib
import pathlib
import re
import sys

dest = pathlib.Path(sys.argv[1])
uf2 = dest / "Saintcon2022BadgeFirmwareFix.uf2"
uf2_bytes = uf2.read_bytes()
if len(uf2_bytes) != 97792:
    sys.stderr.write(f"ERROR: Unexpected UF2 size {len(uf2_bytes)}\n")
    sys.exit(1)
expected_sha256 = "f5f03f7c4f45abf60de4258b88e8e4055f43bcef8a3ae1a1cc8ecee27fd9c18b"
actual_sha256 = hashlib.sha256(uf2_bytes).hexdigest()
if actual_sha256 != expected_sha256:
    sys.stderr.write(f"ERROR: Unexpected UF2 SHA-256 {actual_sha256}\n")
    sys.exit(1)

email_re = re.compile(rb"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
flag_re = re.compile(rb"(?i)(?:sc22|sc20|saintcon|flag)\{[^}\r\n]+\}")
hc_re = re.compile(rb"HC:[A-Za-z0-9_-]+")
wifi_re = re.compile(rb"(?i)\b(?:wifi_ssid|wifi_pass(?:word)?|wpa_psk)\b")
tel_re = re.compile(rb"TEL[;:][0-9+(). -]{5,}")
win_path_re = re.compile(rb"[A-Za-z]:\\\\Users\\\\")

for p in sorted(dest.rglob("*")):
    if not p.is_file() or p.name == ".fetched_commit":
        continue
    data = p.read_bytes()
    rel = p.relative_to(dest)
    if flag_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected flag pattern detected in {rel}\n")
        sys.exit(1)
    if hc_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected HC: code detected in {rel}\n")
        sys.exit(1)
    if wifi_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected Wi-Fi credential pattern detected in {rel}\n")
        sys.exit(1)
    if tel_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected phone number in {rel}\n")
        sys.exit(1)
    if email_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected email address in {rel}\n")
        sys.exit(1)
    if win_path_re.search(data):
        sys.stderr.write(f"ERROR: Unexpected host path in {rel}\n")
        sys.exit(1)
PY
}

if [[ "${FORCE}" -eq 0 && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${EXPECTED_STAMP}" ]] \
   && [[ -f "${DEST}/Saintcon2022BadgeFirmwareFix.uf2" \
      && -f "${DEST}/rp2040js/LICENSE" \
      && -f "${DEST}/rp2040js/src/index.js" \
      && -f "${DEST}/rp2040js/demo/bootrom.js" ]]; then
  verify_staged_tree
  echo "2022 upstream already at ${UPSTREAM_COMMIT:0:12} + rp2040js@${RP2040JS_COMMIT:0:7} (clean-tree verified)"
  exit 0
fi

echo "==> Staging compukidmike/saintcon2022 @ ${UPSTREAM_COMMIT:0:12} and wokwi/rp2040js @ ${RP2040JS_COMMIT:0:7} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}/rp2040js"

LOCAL_CLONE="${SAINTCON2022_LOCAL_CLONE:-}"
if [[ -z "${LOCAL_CLONE}" ]]; then
  for candidate in \
    "${REPO_ROOT}/upstream/saintcon2022" \
    "${REPO_ROOT}/../saintcon-museum-research/saintcon2022"
  do
    if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
      LOCAL_CLONE="${candidate}"
      break
    fi
  done
fi

if [[ -n "${LOCAL_CLONE}" && -d "${LOCAL_CLONE}/.git" ]] \
   && git -C "${LOCAL_CLONE}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" -- "Saintcon2022BadgeFirmwareFix.uf2" | tar -x -C "${DEST}"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched saintcon2022 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD -- "Saintcon2022BadgeFirmwareFix.uf2" | tar -x -C "${DEST}"
  rm -rf "${TMP_GIT}"
  TMP_GIT=""
fi

RP_CLONE="${RP2040JS_LOCAL_CLONE:-}"
if [[ -z "${RP_CLONE}" ]]; then
  for candidate in \
    "${REPO_ROOT}/upstream/rp2040js" \
    "${REPO_ROOT}/../saintcon-museum-research/rp2040js"
  do
    if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${RP2040JS_COMMIT}^{commit}" 2>/dev/null; then
      RP_CLONE="${candidate}"
      break
    fi
  done
fi

if [[ -n "${RP_CLONE}" && -d "${RP_CLONE}/.git" ]] \
   && git -C "${RP_CLONE}" cat-file -e "${RP2040JS_COMMIT}^{commit}" 2>/dev/null; then
  git -C "${RP_CLONE}" archive "${RP2040JS_COMMIT}" -- "LICENSE" "src" "demo/bootrom.ts" | tar -x -C "${DEST}/rp2040js"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${RP2040JS_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${RP2040JS_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${RP2040JS_COMMIT}" ]]; then
    echo "ERROR: Fetched rp2040js commit does not match ${RP2040JS_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD -- "LICENSE" "src" "demo/bootrom.ts" | tar -x -C "${DEST}/rp2040js"
  rm -rf "${TMP_GIT}"
  TMP_GIT=""
fi

# Transpile rp2040js TypeScript files to ESM JavaScript offline using deno 2.9.7.
REQUIRED_DENO_VERSION="2.9.7"
if ! command -v deno >/dev/null 2>&1; then
  echo "ERROR: deno ${REQUIRED_DENO_VERSION} is required to transpile rp2040js, but deno is not installed." >&2
  exit 1
fi
ACTUAL_DENO_VERSION="$(deno --version | awk 'NR==1 {print $2}')"
if [[ "${ACTUAL_DENO_VERSION}" != "${REQUIRED_DENO_VERSION}" ]]; then
  echo "ERROR: deno ${REQUIRED_DENO_VERSION} is required to transpile rp2040js (found deno ${ACTUAL_DENO_VERSION})." >&2
  exit 1
fi
mapfile -t TS_FILES < <(find "${DEST}/rp2040js/src" "${DEST}/rp2040js/demo" -name '*.ts' | sort)
if [[ "${#TS_FILES[@]}" -eq 0 ]]; then
  echo "ERROR: No TypeScript files found in ${DEST}/rp2040js" >&2
  exit 1
fi
deno bundle --no-remote --no-npm --inline-imports=false --outdir "${DEST}/rp2040js" "${TS_FILES[@]}"

verify_staged_tree

echo "${EXPECTED_STAMP}" > "${STAMP}"
echo "==> Upstream ready at saintcon2022@${UPSTREAM_COMMIT:0:12} + rp2040js@${RP2040JS_COMMIT:0:7}"
