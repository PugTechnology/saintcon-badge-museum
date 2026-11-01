#!/usr/bin/env bash
# Fetch the SAINTCON 2023 "Incidents" badge firmware (Firmware/Source/) and its
# pinned ArduinoJson library into ./upstream/ (git-ignored) and scrub all
# conference Wi-Fi credentials before any compiler runs.
#
# Re-running is a no-op once the pinned commit is staged and scrub-verified;
# pass --force to re-extract from scratch.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2023.git"
UPSTREAM_COMMIT="6f5f3407bb57000e0131fc317ad907f27bda7234"

# Library dependency pinned by version (from Firmware/Source/platformio.ini) AND commit SHA.
ARDUINOJSON_URL="https://github.com/bblanchon/ArduinoJson.git"
ARDUINOJSON_VERSION="v6.21.3"
ARDUINOJSON_COMMIT="4b00783345484b4fd4a4f89f842809ef3eee1681"

DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"
TMP_GIT=""

# On any failed exit, remove the staged Firmware/ (it may hold unscrubbed secrets) and any temp clone.
on_exit() {
  local rc=$?
  [[ -n "${TMP_GIT}" ]] && rm -rf "${TMP_GIT}"
  if [[ "${rc}" -ne 0 ]]; then
    rm -rf "${DEST}/Firmware" "${STAMP}"
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

scrub_and_verify() {
  set +x
  local fresh="${1:-0}"
  local BADGE_CPP="${DEST}/Firmware/Source/src/badge.cpp"

  if [[ ! -f "${BADGE_CPP}" ]]; then
    echo "ERROR: Missing ${BADGE_CPP}" >&2
    exit 1
  fi

  local -a raw_unquoted=()
  local -a raw_quoted=()
  local ssid_val pass_val
  ssid_val="$(sed -nE 's/^[[:space:]]*if[[:space:]]*\([[:space:]]*WifiConnect\([[:space:]]*"([^"]*)"[[:space:]]*,[[:space:]]*"[^"]*"[[:space:]]*\)\)[[:space:]]*$/\1/p' "${BADGE_CPP}")"
  pass_val="$(sed -nE 's/^[[:space:]]*if[[:space:]]*\([[:space:]]*WifiConnect\([[:space:]]*"[^"]*"[[:space:]]*,[[:space:]]*"([^"]*)"[[:space:]]*\)\)[[:space:]]*$/\1/p' "${BADGE_CPP}")"

  if [[ -n "${ssid_val}" && "${ssid_val}" != "MUSEUM" ]]; then
    raw_unquoted+=("${ssid_val}")
    raw_quoted+=("\"${ssid_val}\"")
  fi
  if [[ -n "${pass_val}" ]]; then
    raw_unquoted+=("${pass_val}")
    raw_quoted+=("\"${pass_val}\"")
  fi
  ssid_val=""
  pass_val=""

  if [[ "${fresh}" -eq 1 && "${#raw_quoted[@]}" -ne 2 ]]; then
    raw_unquoted=()
    raw_quoted=()
    echo "ERROR: Expected 2 Wi-Fi credential fields to scrub in fresh upstream tree" >&2
    exit 1
  fi

  # 1. Scrub conference Wi-Fi SSID and password in Firmware/Source/src/badge.cpp.
  sed -E -i \
    -e 's/^([[:space:]]*if[[:space:]]*\([[:space:]]*WifiConnect\()[[:space:]]*"[^"]*"[[:space:]]*,[[:space:]]*"[^"]*"([[:space:]]*\)\)[[:space:]]*)$/\1"MUSEUM", ""\2/' \
    "${BADGE_CPP}"

  grep -Eq '^[[:space:]]*if[[:space:]]*\([[:space:]]*WifiConnect\("MUSEUM", ""\)\)[[:space:]]*$' "${BADGE_CPP}" || {
    raw_unquoted=(); raw_quoted=()
    echo "ERROR: WifiConnect credential scrub failed in ${BADGE_CPP}" >&2
    exit 1
  }

  # 2. Confirm zero occurrences of any captured original secret remain anywhere in staged Firmware/.
  local val
  for val in "${raw_unquoted[@]}" "${raw_quoted[@]}"; do
    if grep -R -a -F -q -- "${val}" "${DEST}/Firmware"; then
      val=""
      raw_unquoted=(); raw_quoted=()
      echo "ERROR: Residual secret detected in staged Firmware/ after scrubbing; removing it" >&2
      rm -rf "${DEST}/Firmware" "${STAMP}"
      exit 1
    fi
  done
  val=""
  raw_unquoted=()
  raw_quoted=()
}

fetch_pinned_git() {
  local url="$1"
  local ver="$2"
  local sha="$3"
  local out="$4"
  echo "==> Fetching $(basename "${out}") (${ver} @ ${sha:0:12}) ..."
  rm -rf "${out}"
  mkdir -p "${out}"
  git -C "${out}" init -q
  git -C "${out}" remote add origin "${url}"
  git -C "${out}" fetch -q --depth 1 origin "${sha}"
  git -C "${out}" checkout -q FETCH_HEAD
  local actual
  actual="$(git -C "${out}" rev-parse HEAD)"
  if [[ "${actual}" != "${sha}" ]]; then
    echo "ERROR: Commit mismatch for ${out}: expected ${sha}, got ${actual}" >&2
    exit 1
  fi
  rm -rf "${out}/.git"
}

if [[ "${FORCE}" -eq 0 && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${UPSTREAM_COMMIT}" ]] \
   && [[ -d "${DEST}/Firmware/Source" && -d "${DEST}/lib_deps/ArduinoJson" ]]; then
  scrub_and_verify 0
  echo "2023 upstream already at ${UPSTREAM_COMMIT:0:12} (scrub verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2023 (Firmware/Source/) @ ${UPSTREAM_COMMIT:0:12} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"

LOCAL_CLONE=""
for candidate in \
  "${REPO_ROOT}/upstream/Saintcon2023" \
  "${HOME}/Projects/saintcon-museum-research/Saintcon2023"
do
  if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
    LOCAL_CLONE="${candidate}"
    break
  fi
done

if [[ -n "${LOCAL_CLONE}" ]]; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" Firmware/Source | tar -x -C "${DEST}"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched Saintcon2023 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD Firmware/Source | tar -x -C "${DEST}"
  rm -rf "${TMP_GIT}"
fi

scrub_and_verify 1

LIB_DEST="${DEST}/lib_deps"
mkdir -p "${LIB_DEST}"
fetch_pinned_git "${ARDUINOJSON_URL}" "${ARDUINOJSON_VERSION}" "${ARDUINOJSON_COMMIT}" "${LIB_DEST}/ArduinoJson"

# Apply optional patches in patches/*.patch if any exist.
if [[ -d "${HERE}/patches" ]]; then
  shopt -s nullglob
  for p in "${HERE}/patches/"*.patch; do
    echo "==> Applying patch $(basename "${p}") ..."
    patch -d "${DEST}/Firmware/Source" -p1 < "${p}"
  done
  shopt -u nullglob
fi

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
