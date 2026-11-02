#!/usr/bin/env bash
# Fetch the SAINTCON 2017 HITB Minibadge sketch and EAGLE board/schematic into
# ./upstream/ (git-ignored), excluding README files, tutorial slides/zips, and
# logo images, and scrub embedded library author email addresses from the EAGLE
# XML files before any renderer or compiler runs.
#
# Pinned upstream HEAD (from git log):
#   repo:    https://github.com/compukidmike/Saintcon2017.git
#   commit:  d5f1d932035e589253eafbd97a414d78ee9b37e8
#   date:    2017-10-12 10:00:18 -0600
#   message: Update README.md
#
# Re-running is a no-op once the pinned commit is staged and scrub-verified;
# pass --force to re-extract from scratch.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2017.git"
UPSTREAM_COMMIT="d5f1d932035e589253eafbd97a414d78ee9b37e8"

DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"
TMP_GIT=""

ARCHIVE_PATHS=(
  "HITBUTT_Minibadge/HITB_Test.ino"
  "HITBUTT_Minibadge/Base-Rev0.brd"
  "HITBUTT_Minibadge/Base-Rev0.sch"
)

# On any failed exit, remove the staged tree and any temp clone.
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

scrub_and_verify() {
  set +x
  local fresh="${1:-0}"
  local p
  for p in "${ARCHIVE_PATHS[@]}"; do
    if [[ ! -f "${DEST}/${p}" ]]; then
      echo "ERROR: Missing staged file ${DEST}/${p}" >&2
      exit 1
    fi
  done

  if find "${DEST}" \( -iname 'readme*' -o -iname '*.pdf' -o -iname '*.zip' -o -iname '*.png' -o -iname '*.jpg' -o -iname '*.svg' -o -iname '*.ai' \) | grep -q .; then
    echo "ERROR: Forbidden file staged in 2017 upstream/" >&2
    exit 1
  fi

  # Scrub embedded EAGLE library <author> email addresses in Base-Rev0.brd
  # (lines 344, 1425) and Base-Rev0.sch (lines 421, 932, 4335) with
  # fail-if-not-applied check, and audit the staged tree for any remaining
  # personal details, flags, HC: codes, Wi-Fi fields, or host paths.
  python3 - "${DEST}" "${fresh}" <<'PY'
import pathlib
import re
import sys

dest = pathlib.Path(sys.argv[1])
fresh = int(sys.argv[2])

brd_p = dest / "HITBUTT_Minibadge/Base-Rev0.brd"
sch_p = dest / "HITBUTT_Minibadge/Base-Rev0.sch"

author_email_re = re.compile(
    r"(&lt;author&gt;Created by )[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}(&lt;/author&gt;)"
)
REPL = r"\1MUSEUM_REDACTED\2"

for xml_p, expected_count in ((brd_p, 2), (sch_p, 3)):
    txt = xml_p.read_text(encoding="utf-8", errors="surrogateescape")
    new_txt, n = author_email_re.subn(REPL, txt)
    if fresh == 1 and n != expected_count:
        sys.stderr.write(
            f"ERROR: Expected {expected_count} author email scrub(s) in {xml_p.relative_to(dest)}, applied {n}\n"
        )
        sys.exit(1)
    if n > 0:
        xml_p.write_text(new_txt, encoding="utf-8", errors="surrogateescape")
    after = xml_p.read_text(encoding="utf-8", errors="surrogateescape")
    if after.count("&lt;author&gt;Created by MUSEUM_REDACTED&lt;/author&gt;") != expected_count:
        sys.stderr.write(
            f"ERROR: Failed to verify scrubbed author email placeholders in {xml_p.relative_to(dest)}\n"
        )
        sys.exit(1)

email_re = re.compile(rb"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
flag_re = re.compile(rb"(?i)(?:sc17|saintcon|flag)\{[^}\r\n]+\}")
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

if [[ "${FORCE}" -eq 0 && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${UPSTREAM_COMMIT}" ]] \
   && [[ -f "${DEST}/HITBUTT_Minibadge/HITB_Test.ino" \
      && -f "${DEST}/HITBUTT_Minibadge/Base-Rev0.brd" \
      && -f "${DEST}/HITBUTT_Minibadge/Base-Rev0.sch" ]]; then
  scrub_and_verify 0
  echo "2017 upstream already at ${UPSTREAM_COMMIT:0:12} (scrub verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2017 @ ${UPSTREAM_COMMIT:0:12} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"

LOCAL_CLONE="${SAINTCON2017_LOCAL_CLONE:-}"
if [[ -z "${LOCAL_CLONE}" ]]; then
  for candidate in \
    "${REPO_ROOT}/upstream/Saintcon2017" \
    "${REPO_ROOT}/../saintcon-museum-research/Saintcon2017"
  do
    if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
      LOCAL_CLONE="${candidate}"
      break
    fi
  done
fi

if [[ -n "${LOCAL_CLONE}" && -d "${LOCAL_CLONE}/.git" ]] \
   && git -C "${LOCAL_CLONE}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" -- "${ARCHIVE_PATHS[@]}" | tar -x -C "${DEST}"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched Saintcon2017 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD -- "${ARCHIVE_PATHS[@]}" | tar -x -C "${DEST}"
  rm -rf "${TMP_GIT}"
  TMP_GIT=""
fi

scrub_and_verify 1

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
