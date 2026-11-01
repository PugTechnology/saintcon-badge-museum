#!/usr/bin/env bash
# Fetch the SAINTCON 2024 wrist badge firmware (Firmware/) and its pinned
# managed component sources into ./upstream/ (git-ignored) and scrub all
# conference/debug Wi-Fi credentials before any compiler runs.
#
# Re-running is a no-op once the pinned commit is staged and scrub-verified;
# pass --force to re-extract from scratch.
set -euo pipefail
set +x

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2024.git"
UPSTREAM_COMMIT="5e9412c837334379d7f6cadf68327fc0771f636d"

# Managed components pinned by version (from Firmware/dependencies.lock) AND commit SHA.
LVGL_URL="https://github.com/lvgl/lvgl.git"
LVGL_VERSION="v9.2.0"
LVGL_COMMIT="aa7446344c6ec7631112ef031983ef24077e24d5"

NLOHMANN_URL="https://github.com/nlohmann/json.git"
NLOHMANN_VERSION="v3.11.3"
NLOHMANN_COMMIT="9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03"

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
  local SDKCFG="${DEST}/Firmware/sdkconfig.defaults"
  local MAIN_C="${DEST}/Firmware/main/main.c"

  if [[ ! -f "${SDKCFG}" ]]; then
    echo "ERROR: Missing ${SDKCFG}" >&2
    exit 1
  fi
  if [[ ! -f "${MAIN_C}" ]]; then
    echo "ERROR: Missing ${MAIN_C}" >&2
    exit 1
  fi

  local -a raw_unquoted=()
  local -a raw_quoted=()
  local val
  for val in \
    "$(sed -nE 's/^CONFIG_CONFERENCE_WIFI_SSID="([^"]*)"$/\1/p' "${SDKCFG}")" \
    "$(sed -nE 's/^CONFIG_CONFERENCE_WIFI_PASSWORD="([^"]*)"$/\1/p' "${SDKCFG}")" \
    "$(sed -nE 's/^[[:space:]]*\/\/[[:space:]]*\.ssid[[:space:]]*=[[:space:]]*"([^"]*)",[[:space:]]*$/\1/p' "${MAIN_C}")"
  do
    if [[ -n "${val}" && "${val}" != "MUSEUM" ]]; then
      raw_unquoted+=("${val}")
      raw_quoted+=("\"${val}\"")
    fi
  done
  val="$(sed -nE 's/^[[:space:]]*\/\/[[:space:]]*\.password[[:space:]]*=[[:space:]]*"([^"]*)",[[:space:]]*$/\1/p' "${MAIN_C}")"
  if [[ -n "${val}" ]]; then
    raw_quoted+=("\"${val}\"")
  fi
  val=""

  if [[ "${fresh}" -eq 1 && "${#raw_quoted[@]}" -ne 4 ]]; then
    raw_unquoted=()
    raw_quoted=()
    echo "ERROR: Expected 4 credential fields to scrub in fresh upstream tree" >&2
    exit 1
  fi

  # 1. Scrub conference Wi-Fi SSID and password in Firmware/sdkconfig.defaults.
  sed -E -i \
    -e 's/^CONFIG_CONFERENCE_WIFI_SSID="[^"]*"$/CONFIG_CONFERENCE_WIFI_SSID="MUSEUM"/' \
    -e 's/^CONFIG_CONFERENCE_WIFI_PASSWORD="[^"]*"$/CONFIG_CONFERENCE_WIFI_PASSWORD=""/' \
    "${SDKCFG}"

  grep -qx 'CONFIG_CONFERENCE_WIFI_SSID="MUSEUM"' "${SDKCFG}" || {
    raw_unquoted=(); raw_quoted=()
    echo "ERROR: CONFIG_CONFERENCE_WIFI_SSID scrub failed in ${SDKCFG}" >&2
    exit 1
  }
  grep -qx 'CONFIG_CONFERENCE_WIFI_PASSWORD=""' "${SDKCFG}" || {
    raw_unquoted=(); raw_quoted=()
    echo "ERROR: CONFIG_CONFERENCE_WIFI_PASSWORD scrub failed in ${SDKCFG}" >&2
    exit 1
  }

  # 2. Scrub commented debug Wi-Fi credentials in Firmware/main/main.c.
  sed -E -i \
    -e 's/^([[:space:]]*\/\/[[:space:]]*\.ssid[[:space:]]*=[[:space:]]*)"[^"]*"(,[[:space:]]*)$/\1"MUSEUM"\2/' \
    -e 's/^([[:space:]]*\/\/[[:space:]]*\.password[[:space:]]*=[[:space:]]*)"[^"]*"(,[[:space:]]*)$/\1""\2/' \
    "${MAIN_C}"

  grep -Eq '^[[:space:]]*//[[:space:]]*\.ssid[[:space:]]*=[[:space:]]*"MUSEUM",[[:space:]]*$' "${MAIN_C}" || {
    raw_unquoted=(); raw_quoted=()
    echo "ERROR: main.c debug .ssid scrub failed in ${MAIN_C}" >&2
    exit 1
  }
  grep -Eq '^[[:space:]]*//[[:space:]]*\.password[[:space:]]*=[[:space:]]*"",[[:space:]]*$' "${MAIN_C}" || {
    raw_unquoted=(); raw_quoted=()
    echo "ERROR: main.c debug .password scrub failed in ${MAIN_C}" >&2
    exit 1
  }

  # 3. Confirm zero occurrences of any captured original secret remain anywhere in staged Firmware/.
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

patch_and_verify_lvgl() {
  # Backport LVGL 9.3.0 fix for empty child transform/simple layers in LVGL 9.2.0
  # (src/draw/lv_draw.c:183): when a rotated/layered widget produces 0 draw tasks
  # (e.g. status_label == "" during initial SCREEN_MAIN render; see Firmware's own
  # note at statusbar.c:127), skipping lv_draw_dispatch_layer() when
  # layer->draw_task_head == NULL leaves the parent LV_DRAW_TASK_TYPE_LAYER task
  # stuck in LV_DRAW_TASK_STATE_WAITING forever.
  local LV_DRAW_C="${DEST}/managed_components/lvgl__lvgl/src/draw/lv_draw.c"
  if [[ ! -f "${LV_DRAW_C}" ]]; then
    echo "ERROR: Missing ${LV_DRAW_C}" >&2
    exit 1
  fi
  python3 - "${LV_DRAW_C}" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
s = p.read_text()
orig = "if(layer->draw_task_head && lv_draw_dispatch_layer(disp, layer))"
fixed = "if((layer->draw_task_head || (layer->parent && layer->all_tasks_added)) && lv_draw_dispatch_layer(disp, layer))"
orig_else = "    /*Assign draw tasks to the draw_units*/\n    else {"
fixed_else = "    /*Assign draw tasks to the draw_units*/\n    else if(layer->draw_task_head) {"
changed = False
if orig in s:
    s = s.replace(orig, fixed, 1)
    changed = True
if orig_else in s:
    s = s.replace(orig_else, fixed_else, 1)
    changed = True
if fixed not in s or fixed_else not in s:
    sys.stderr.write(f"ERROR: Failed to verify LVGL 9.2.0 empty-layer fix in {p}\n")
    sys.exit(1)
if changed:
    p.write_text(s)
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

if [[ "${FORCE}" -eq 0 && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${UPSTREAM_COMMIT}" ]] \
   && [[ -d "${DEST}/Firmware" && -d "${DEST}/managed_components/lvgl__lvgl" && -d "${DEST}/managed_components/johboh__nlohmann-json" ]]; then
  scrub_and_verify 0
  patch_and_verify_lvgl
  echo "2024 upstream already at ${UPSTREAM_COMMIT:0:12} (scrub verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2024 (Firmware/) @ ${UPSTREAM_COMMIT:0:12} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"

LOCAL_CLONE=""
for candidate in \
  "${REPO_ROOT}/upstream/Saintcon2024" \
  "${HOME}/Projects/saintcon-museum-research/Saintcon2024"
do
  if [[ -d "${candidate}/.git" ]] && git -C "${candidate}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
    LOCAL_CLONE="${candidate}"
    break
  fi
done

if [[ -n "${LOCAL_CLONE}" ]]; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" Firmware | tar -x -C "${DEST}"
else
  TMP_GIT="$(mktemp -d)"
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch -q --depth 1 origin "${UPSTREAM_COMMIT}"
  if [[ "$(git -C "${TMP_GIT}" rev-parse FETCH_HEAD)" != "${UPSTREAM_COMMIT}" ]]; then
    echo "ERROR: Fetched Saintcon2024 commit does not match ${UPSTREAM_COMMIT}" >&2
    exit 1
  fi
  git -C "${TMP_GIT}" archive FETCH_HEAD Firmware | tar -x -C "${DEST}"
  rm -rf "${TMP_GIT}"
fi

scrub_and_verify 1

MC_DEST="${DEST}/managed_components"
mkdir -p "${MC_DEST}"

fetch_pinned_git "${LVGL_URL}" "${LVGL_VERSION}" "${LVGL_COMMIT}" "${MC_DEST}/lvgl__lvgl"
fetch_pinned_git "${NLOHMANN_URL}" "${NLOHMANN_VERSION}" "${NLOHMANN_COMMIT}" "${MC_DEST}/johboh__nlohmann-json"
patch_and_verify_lvgl

# Apply optional patches in patches/*.patch if any exist.
if [[ -d "${HERE}/patches" ]]; then
  shopt -s nullglob
  for p in "${HERE}/patches/"*.patch; do
    echo "==> Applying patch $(basename "${p}") ..."
    patch -d "${DEST}/Firmware" -p1 < "${p}"
  done
  shopt -u nullglob
fi

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
