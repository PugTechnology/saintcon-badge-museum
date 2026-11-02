#!/usr/bin/env bash
# Fetch the SAINTCON 2020 badge firmware source and KiCad PCB layouts into
# ./upstream/ (git-ignored), excluding prebuilt .hex/.elf/.lss/.srec binaries,
# PDFs, README, and KiCad netlist/library-table files that contain host paths,
# and verify the staged tree is clean before any compiler runs.
#
# Pinned upstream HEAD (from git log):
#   repo:    https://github.com/compukidmike/Saintcon2020.git
#   commit:  9b11ef8514be376b9b798ec66248ab2f7a8d1fd3
#   date:    2026-04-24 09:30:47 -0600
#   message: Update README.md
#
# Re-running is a no-op once the pinned commit is staged and verified;
# pass --force to re-extract from scratch.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2020.git"
UPSTREAM_COMMIT="9b11ef8514be376b9b798ec66248ab2f7a8d1fd3"

DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"
TMP_GIT=""
TMP_ZIPS=""

PCB_FILES=(
  "Hardware/Saintcon2020MainBoard/Saintcon2020MainBoard.kicad_pcb"
  "Hardware/Saintcon2020DaughterBoard/Saintcon2020DaughterBoard.kicad_pcb"
  "Hardware/Saintcon2020HorizontalBoard/Saintcon2020HorizontalBoard.kicad_pcb"
  "Hardware/Saintcon2020VerticalBoard/Saintcon2020VerticalBoard.kicad_pcb"
  "Hardware/Saintcon2020MinibadgeHolder/Saintcon2020MinibadgeHolder.kicad_pcb"
  "Hardware/Saintcon2020MinibadgeHolder90/Saintcon2020MinibadgeHolder90.kicad_pcb"
  "Hardware/MinibadgeHoldersPanel/MinibadgeHoldersPanel.kicad_pcb"
)

# On any failed exit, remove the staged tree and any temporary directories.
on_exit() {
  local rc=$?
  [[ -n "${TMP_GIT}" ]] && rm -rf "${TMP_GIT}"
  [[ -n "${TMP_ZIPS}" ]] && rm -rf "${TMP_ZIPS}"
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
  local MAIN_CPP="${DEST}/Firmware/Saintcon2020/main.cpp"

  if [[ ! -f "${MAIN_CPP}" ]]; then
    echo "ERROR: Missing ${MAIN_CPP}" >&2
    exit 1
  fi

  local pcb
  for pcb in "${PCB_FILES[@]}"; do
    if [[ ! -f "${DEST}/${pcb}" ]]; then
      echo "ERROR: Missing staged PCB ${DEST}/${pcb}" >&2
      exit 1
    fi
  done

  if find "${DEST}" \( -iname 'readme*' -o -iname '*.hex' -o -iname '*.elf' -o -iname '*.lss' -o -iname '*.srec' -o -iname '*.map' -o -iname '*.pdf' -o -iname '*.zip' -o -iname '*.net' -o -iname 'fp-lib-table' -o -iname 'sym-lib-table' \) | grep -q .; then
    echo "ERROR: Forbidden file staged in 2020 upstream/" >&2
    exit 1
  fi

  # Audit every file in staged upstream/ for flags, HC: codes, Wi-Fi fields,
  # personal contact details, and local host paths.
  python3 - "${DEST}" <<'PY'
import pathlib
import re
import sys

dest = pathlib.Path(sys.argv[1])

email_re = re.compile(rb"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
flag_re = re.compile(rb"(?i)(?:sc20|saintcon|flag)\{[^}\r\n]+\}")
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
   && [[ -f "${DEST}/Firmware/Saintcon2020/main.cpp" \
      && -f "${DEST}/Hardware/Saintcon2020MainBoard/Saintcon2020MainBoard.kicad_pcb" \
      && -f "${DEST}/Hardware/Saintcon2020DaughterBoard/Saintcon2020DaughterBoard.kicad_pcb" ]]; then
  verify_staged_tree
  echo "2020 upstream already at ${UPSTREAM_COMMIT:0:12} (clean-tree verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2020 @ ${UPSTREAM_COMMIT:0:12} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"

LOCAL_CLONE="${SAINTCON2020_LOCAL_CLONE:-}"
if [[ -z "${LOCAL_CLONE}" ]]; then
  for candidate in \
    "${REPO_ROOT}/upstream/Saintcon2020" \
    "${REPO_ROOT}/../saintcon-museum-research/Saintcon2020"
  do
    if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
      LOCAL_CLONE="${candidate}"
      break
    fi
  done
fi

TMP_ZIPS="$(mktemp -d)"

if [[ -n "${LOCAL_CLONE}" && -d "${LOCAL_CLONE}/.git" ]] \
   && git -C "${LOCAL_CLONE}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" -- "Firmware/Saintcon2020/main.cpp" | tar -x -C "${DEST}"
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" -- "Hardware" | tar -x -C "${TMP_ZIPS}"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched Saintcon2020 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD -- "Firmware/Saintcon2020/main.cpp" | tar -x -C "${DEST}"
  git -C "${TMP_GIT}" archive FETCH_HEAD -- "Hardware" | tar -x -C "${TMP_ZIPS}"
  rm -rf "${TMP_GIT}"
  TMP_GIT=""
fi

python3 - "${TMP_ZIPS}/Hardware" "${DEST}/Hardware" <<'PY'
import pathlib
import sys
import zipfile

zips_dir = pathlib.Path(sys.argv[1])
out_dir = pathlib.Path(sys.argv[2])

expected = [
    ("Saintcon2020MainBoard-KiCad.zip", "Saintcon2020MainBoard/Saintcon2020MainBoard.kicad_pcb"),
    ("Saintcon2020DaughterBoard-KiCad.zip", "Saintcon2020DaughterBoard/Saintcon2020DaughterBoard.kicad_pcb"),
    ("Saintcon2020HorizontalBoard-KiCad.zip", "Saintcon2020HorizontalBoard/Saintcon2020HorizontalBoard.kicad_pcb"),
    ("Saintcon2020VerticalBoard-KiCad.zip", "Saintcon2020VerticalBoard/Saintcon2020VerticalBoard.kicad_pcb"),
    ("Saintcon2020MinibadgeHolder-KiCad.zip", "Saintcon2020MinibadgeHolder/Saintcon2020MinibadgeHolder.kicad_pcb"),
    ("Saintcon2020MinibadgeHolder90-KiCad.zip", "Saintcon2020MinibadgeHolder90/Saintcon2020MinibadgeHolder90.kicad_pcb"),
    ("MinibadgeHoldersPanel-KiCad.zip", "MinibadgeHoldersPanel/MinibadgeHoldersPanel.kicad_pcb"),
]

for zip_name, rel_out in expected:
    zp = zips_dir / zip_name
    if not zp.is_file():
        sys.stderr.write(f"ERROR: Missing expected hardware archive {zp}\n")
        sys.exit(1)
    pcb_name = pathlib.Path(rel_out).name
    with zipfile.ZipFile(zp, "r") as zf:
        matches = [m for m in zf.namelist() if pathlib.Path(m).name == pcb_name]
        if len(matches) != 1:
            sys.stderr.write(f"ERROR: Expected 1 {pcb_name} in {zip_name}, found {matches}\n")
            sys.exit(1)
        data = zf.read(matches[0])
    dst = out_dir / rel_out
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
PY

rm -rf "${TMP_ZIPS}"
TMP_ZIPS=""

verify_staged_tree

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
