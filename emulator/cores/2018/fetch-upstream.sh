#!/usr/bin/env bash
# Fetch the SAINTCON 2018 LED matrix badge MicroPython source
# ("Badge/Badge Source Code/") and the pinned MicroPython release into
# ./upstream/ (git-ignored), scrub all secrets/flags/conference Wi-Fi values
# before anything is built or served, and verify zero original secret values
# exist in staged upstream/, emulator/web/, or git-tracked files.
#
# Usage:
#   ./fetch-upstream.sh            # stage & verify (idempotent once stamped)
#   ./fetch-upstream.sh --force    # re-extract and scrub from scratch
#   ./fetch-upstream.sh --verify-clean  # confirm zero original secrets in upstream/, web/, or tracked files
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

# compukidmike/Saintcon2018 pinned HEAD commit:
#   commit fee2837d4607ee8a0360e99709894fc5fc608948
#   Date:  2022-11-13 21:23:01 -0700
#   Msg:   "Merge pull request #5 from jrwagz/update-flashing-readme"
UPSTREAM_URL="https://github.com/compukidmike/Saintcon2018.git"
UPSTREAM_COMMIT="fee2837d4607ee8a0360e99709894fc5fc608948"

# MicroPython WebAssembly + core VM pinned by release tag AND commit SHA.
MPY_URL="https://github.com/micropython/micropython.git"
MPY_VERSION="v1.24.1"
MPY_COMMIT="ecfdd5d6f9be971852003c2049600dc7b3e2a838"

DEST="${HERE}/upstream"
BADGE_SRC="${DEST}/Badge/Badge Source Code"
STAMP="${DEST}/.fetched_commit"
TMP_GIT=""

# On any failed exit, remove staged Badge/ (which could hold unscrubbed secrets) and marker.
on_exit() {
  local rc=$?
  [[ -n "${TMP_GIT}" ]] && rm -rf "${TMP_GIT}"
  if [[ "${rc}" -ne 0 ]]; then
    rm -rf "${DEST}/Badge" "${STAMP}"
  fi
}
trap on_exit EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
trap 'exit 129' HUP

MODE="stage"
if [[ "${1:-}" == "--force" ]]; then
  MODE="force"
elif [[ "${1:-}" == "--verify-clean" ]]; then
  MODE="verify-clean"
elif [[ -n "${1:-}" ]]; then
  echo "ERROR: Unknown argument: $1" >&2
  exit 1
fi

# Sets SRC_GIT (not printed: a $(...) subshell would hide TMP_GIT from the EXIT trap and leak the temp clone).
SRC_GIT=""
find_or_fetch_upstream_git() {
  local candidate
  for candidate in \
    "${REPO_ROOT}/upstream/Saintcon2018" \
    "${HOME}/Projects/saintcon-museum-research/Saintcon2018"
  do
    if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
      SRC_GIT="${candidate}"
      return 0
    fi
  done

  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched Saintcon2018 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  SRC_GIT="${TMP_GIT}"
}

# Populate raw_secrets array from upstream git commit objects in memory (never echoed or written to disk).
declare -a raw_secrets=()
load_original_secrets_from_git() {
  set +x
  local git_dir="$1"
  raw_secrets=()

  local hcid_blob idx1_blob parrot_blob cfg_blob
  hcid_blob="$(git -C "${git_dir}" show "${UPSTREAM_COMMIT}:Badge/Badge Source Code/frozen_modules/hcid.py")"
  idx1_blob="$(git -C "${git_dir}" show "${UPSTREAM_COMMIT}:Badge/Badge Source Code/python_files/www/index_1.html")"
  parrot_blob="$(git -C "${git_dir}" show "${UPSTREAM_COMMIT}:Badge/Badge Source Code/python_files/www/partyparrot.html")"
  cfg_blob="$(git -C "${git_dir}" show "${UPSTREAM_COMMIT}:Badge/Badge Source Code/python_files/configure.py")"

  local orig_salt orig_wifi_pw orig_hcid_flag orig_idx_flag orig_parrot_flag orig_ssid
  orig_salt="$(printf '%s\n' "${hcid_blob}" | sed -nE 's/^salt[[:space:]]*=[[:space:]]*"([^"]+)"[[:space:]]*$/\1/p')"
  orig_wifi_pw="$(printf '%s\n' "${hcid_blob}" | awk '/^def getBadgeWifiPassword/{f=1;next} f && /return "/{match($0, /"[^"]+"/); print substr($0, RSTART+1, RLENGTH-2); exit}')"
  orig_hcid_flag="$(printf '%s\n' "${hcid_blob}" | awk '/^def flag/{f=1;next} f && /return "/{match($0, /"[^"]+"/); print substr($0, RSTART+1, RLENGTH-2); exit}')"
  orig_idx_flag="$(printf '%s\n' "${idx1_blob}" | sed -nE 's/.*(flag\{[^}]+\}).*/\1/p')"
  orig_parrot_flag="$(printf '%s\n' "${parrot_blob}" | sed -nE 's/.*(flag\{[^}]+\}).*/\1/p')"
  orig_ssid="$(printf '%s\n' "${cfg_blob}" | sed -nE 's/^default_ssid[[:space:]]*=[[:space:]]*"([^"]+)"[[:space:]]*$/\1/p')"

  hcid_blob=""; idx1_blob=""; parrot_blob=""; cfg_blob=""

  if [[ -z "${orig_salt}" || -z "${orig_wifi_pw}" || -z "${orig_hcid_flag}" || -z "${orig_idx_flag}" || -z "${orig_parrot_flag}" || -z "${orig_ssid}" ]]; then
    orig_salt=""; orig_wifi_pw=""; orig_hcid_flag=""; orig_idx_flag=""; orig_parrot_flag=""; orig_ssid=""
    echo "ERROR: Failed to locate all 6 expected secret/credential fields in upstream commit" >&2
    exit 1
  fi

  raw_secrets=(
    "${orig_salt}"
    "${orig_wifi_pw}"
    "${orig_hcid_flag}"
    "${orig_idx_flag}"
    "${orig_parrot_flag}"
    "${orig_ssid}"
    "${orig_hcid_flag#flag\{}"
    "${orig_idx_flag#flag\{}"
    "${orig_parrot_flag#flag\{}"
  )
  # Strip trailing '}' on the three inner flag bodies.
  raw_secrets[6]="${raw_secrets[6]%\}}"
  raw_secrets[7]="${raw_secrets[7]%\}}"
  raw_secrets[8]="${raw_secrets[8]%\}}"

  orig_salt=""; orig_wifi_pw=""; orig_hcid_flag=""; orig_idx_flag=""; orig_parrot_flag=""; orig_ssid=""
}

verify_no_secrets_anywhere() {
  set +x
  local s
  if [[ "${#raw_secrets[@]}" -lt 9 ]]; then
    echo "ERROR: Secret verification array not initialized" >&2
    exit 1
  fi

  for s in "${raw_secrets[@]}"; do
    if [[ -z "${s}" ]]; then
      raw_secrets=()
      echo "ERROR: Empty secret entry in verification array" >&2
      exit 1
    fi
    if [[ -d "${DEST}/Badge" ]] && grep -R -a -F -q -- "${s}" "${DEST}/Badge"; then
      s=""
      raw_secrets=()
      echo "ERROR: Residual secret detected in staged upstream Badge/; removing staged tree" >&2
      rm -rf "${DEST}/Badge" "${STAMP}"
      exit 1
    fi
    if [[ -d "${REPO_ROOT}/emulator/web" ]] && grep -R -a -F -q -- "${s}" "${REPO_ROOT}/emulator/web"; then
      s=""
      raw_secrets=()
      echo "ERROR: Residual secret detected in emulator/web/!" >&2
      exit 1
    fi
    if git -C "${REPO_ROOT}" ls-files -z | xargs -0 grep -a -F -q -- "${s}" 2>/dev/null; then
      s=""
      raw_secrets=()
      echo "ERROR: Residual secret detected in git-tracked files!" >&2
      exit 1
    fi
  done
  s=""
}

scrub_and_verify() {
  set +x
  local HCID_PY="${BADGE_SRC}/frozen_modules/hcid.py"
  local IDX1_HTML="${BADGE_SRC}/python_files/www/index_1.html"
  local PARROT_HTML="${BADGE_SRC}/python_files/www/partyparrot.html"
  local CFG_PY="${BADGE_SRC}/python_files/configure.py"

  for f in "${HCID_PY}" "${IDX1_HTML}" "${PARROT_HTML}" "${CFG_PY}"; do
    if [[ ! -f "${f}" ]]; then
      echo "ERROR: Missing expected upstream file ${f}" >&2
      exit 1
    fi
  done

  # Ensure no README files were staged anywhere under Badge/.
  if [[ -n "$(find "${DEST}/Badge" -iname 'readme*' -print -quit)" ]]; then
    echo "ERROR: Unexpected README file found in staged Badge/" >&2
    exit 1
  fi

  # 1. Scrub frozen_modules/hcid.py: salt, getBadgeWifiPassword(), and flag().
  python3 - "${HCID_PY}" <<'PY'
import pathlib, re, sys

p = pathlib.Path(sys.argv[1])
s = p.read_text()

s, n1 = re.subn(
    r'^salt\s*=\s*"[^"]*"[ \t]*$',
    'salt = "MUSEUM_FAKE_SALT_2018_00"',
    s, count=1, flags=re.M,
)
s, n2 = re.subn(
    r'(^def getBadgeWifiPassword\(\):\s*\n\s*return\s+)"[^"]*"',
    r'\1"MUSEUMWIFI"',
    s, count=1, flags=re.M,
)
s, n3 = re.subn(
    r'(^def flag\(\):\s*\n\s*return\s+)"[^"]*"',
    r'\1"flag{MUSEUM_PLACEHOLDER_NOT_A_REAL_FLAG}"',
    s, count=1, flags=re.M,
)
if (n1, n2, n3) != (1, 1, 1):
    sys.stderr.write("ERROR: hcid.py scrub pattern count mismatch\n")
    sys.exit(1)
p.write_text(s)
PY

  grep -qx 'salt = "MUSEUM_FAKE_SALT_2018_00"' "${HCID_PY}" || {
    echo "ERROR: salt scrub verification failed in hcid.py" >&2
    exit 1
  }
  grep -Fq 'return "MUSEUMWIFI"' "${HCID_PY}" || {
    echo "ERROR: getBadgeWifiPassword scrub verification failed in hcid.py" >&2
    exit 1
  }
  grep -Fq 'return "flag{MUSEUM_PLACEHOLDER_NOT_A_REAL_FLAG}"' "${HCID_PY}" || {
    echo "ERROR: flag() scrub verification failed in hcid.py" >&2
    exit 1
  }

  # 2. Scrub challenge flags in python_files/www/index_1.html and partyparrot.html.
  sed -E -i 's/flag\{[^}]+\}/flag{MUSEUM_HTML_PLACEHOLDER_1}/g' "${IDX1_HTML}"
  grep -Fq 'flag{MUSEUM_HTML_PLACEHOLDER_1}' "${IDX1_HTML}" || {
    echo "ERROR: index_1.html flag scrub verification failed" >&2
    exit 1
  }

  sed -E -i 's/flag\{[^}]+\}/flag{MUSEUM_HTML_PLACEHOLDER_2}/g' "${PARROT_HTML}"
  grep -Fq 'flag{MUSEUM_HTML_PLACEHOLDER_2}' "${PARROT_HTML}" || {
    echo "ERROR: partyparrot.html flag scrub verification failed" >&2
    exit 1
  }

  # 3. Scrub conference default_ssid in python_files/configure.py.
  sed -E -i 's/^default_ssid[[:space:]]*=[[:space:]]*"[^"]*"[[:space:]]*$/default_ssid = "MUSEUM"/' "${CFG_PY}"
  grep -qx 'default_ssid = "MUSEUM"' "${CFG_PY}" || {
    echo "ERROR: configure.py default_ssid scrub verification failed" >&2
    exit 1
  }

  # 4. Ensure no non-placeholder flag{...} pattern exists anywhere in staged Badge/.
  if grep -R -a -Eo 'flag\{[^}]*\}' "${DEST}/Badge" \
     | grep -Ev 'flag\{(MUSEUM_PLACEHOLDER_NOT_A_REAL_FLAG|MUSEUM_HTML_PLACEHOLDER_1|MUSEUM_HTML_PLACEHOLDER_2)\}$' >/dev/null; then
    echo "ERROR: Unscrubbed flag{...} pattern found in staged Badge/" >&2
    exit 1
  fi
}

patch_and_verify_upstream() {
  local CFG_PY="${BADGE_SRC}/python_files/configure.py"
  local HC_PY="${BADGE_SRC}/python_files/hc.py"

  # 1. configure.py:60 in git HEAD lacks a newline before `f = open( web_dir + 'index_2.html', 'r')`
  #    (present in flashed Saintcon2018.bin at 0x220913).
  python3 - "${CFG_PY}" "${HC_PY}" <<'PY'
import ast, pathlib, sys

cfg_p = pathlib.Path(sys.argv[1])
cfg = cfg_p.read_text()
broken = ")f = open( web_dir + 'index_2.html', 'r')"
fixed = ")\n  f = open( web_dir + 'index_2.html', 'r')"
if broken in cfg:
    cfg = cfg.replace(broken, fixed, 1)
    cfg_p.write_text(cfg)
if fixed not in cfg:
    sys.stderr.write("ERROR: Failed to verify configure.py:60 newline fix\n")
    sys.exit(1)
ast.parse(cfg, filename=str(cfg_p))

# 2. hc.py:5 in git HEAD calls HCID.getHCID() without importing hcid as HCID
#    (present in flashed Saintcon2018.bin at 0x22201e).
hc_p = pathlib.Path(sys.argv[2])
hc = hc_p.read_text()
imp = "import hcid as HCID\n"
if imp not in hc:
    target = "import ujson\n"
    if target not in hc:
        sys.stderr.write("ERROR: Unexpected hc.py header\n")
        sys.exit(1)
    hc = hc.replace(target, target + imp, 1)
    hc_p.write_text(hc)
if imp not in hc:
    sys.stderr.write("ERROR: Failed to verify hc.py import fix\n")
    sys.exit(1)
ast.parse(hc, filename=str(hc_p))
PY
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

if [[ "${MODE}" == "verify-clean" ]]; then
  find_or_fetch_upstream_git
  load_original_secrets_from_git "${SRC_GIT}"
  verify_no_secrets_anywhere
  raw_secrets=()
  echo "2018 secret-free verification passed (staged upstream/, emulator/web/, and git-tracked files)."
  exit 0
fi

if [[ "${MODE}" == "stage" && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${UPSTREAM_COMMIT}" ]] \
   && [[ -d "${BADGE_SRC}/python_files" && -d "${BADGE_SRC}/frozen_modules" && -f "${DEST}/micropython/LICENSE" ]]; then
  find_or_fetch_upstream_git
  load_original_secrets_from_git "${SRC_GIT}"
  scrub_and_verify
  patch_and_verify_upstream
  verify_no_secrets_anywhere
  raw_secrets=()
  echo "2018 upstream already at ${UPSTREAM_COMMIT:0:12} (scrub & clean-tree verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2018 (Badge/Badge Source Code/) @ ${UPSTREAM_COMMIT:0:12} ..."
mkdir -p "${DEST}"
rm -rf "${DEST}/Badge" "${STAMP}"

find_or_fetch_upstream_git
load_original_secrets_from_git "${SRC_GIT}"

git -C "${SRC_GIT}" archive "${UPSTREAM_COMMIT}" "Badge/Badge Source Code" | tar -x -C "${DEST}"

scrub_and_verify
patch_and_verify_upstream
verify_no_secrets_anywhere
raw_secrets=()

if [[ "${MODE}" == "force" || ! -f "${DEST}/micropython/LICENSE" || ! -d "${DEST}/micropython/ports/webassembly" ]]; then
  fetch_pinned_git "${MPY_URL}" "${MPY_VERSION}" "${MPY_COMMIT}" "${DEST}/micropython"
fi

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
