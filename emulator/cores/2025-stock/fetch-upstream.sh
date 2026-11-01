#!/usr/bin/env bash
# Fetch the SAINTCON 2025 badge firmware (Firmware/badge/) and its pinned
# managed component sources into ./upstream/ (git-ignored) and blank any
# conference secrets before building.
#
# Re-running is a no-op once the pinned commit is checked out and scrubbed;
# pass --force to re-extract from scratch.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${HERE}/../../.." && pwd)"

UPSTREAM_URL="https://github.com/compukidmike/Saintcon2025.git"
UPSTREAM_COMMIT="360c892981968822fbadfdaa6ff580a55f4396af"
DEST="${HERE}/upstream"
STAMP="${DEST}/.fetched_commit"

FORCE=0
if [[ "${1:-}" == "--force" ]]; then
  FORCE=1
fi

NEEDED_MC=(
  lvgl__lvgl
  johboh__nlohmann-json
  espressif__esp-boost
  espressif__led_strip
  espp__base_component
  espp__base_peripheral
  espp__format
  espp__logger
  espp__ndef
  espp__st25dv
)

scrub_and_verify() {
  # Blank conference Wi-Fi credentials in sdkconfig.defaults.
  # "CXVOQM" XORs its first 5 bytes with badge.c's ssid_key ^ 0xA5 to decode to "MUSEUM".
  local SDKCFG="${DEST}/Firmware/badge/sdkconfig.defaults"
  if [[ ! -f "${SDKCFG}" ]]; then
    echo "ERROR: Missing ${SDKCFG}" >&2
    exit 1
  fi
  sed -i \
    -e 's/^CONFIG_CONFERENCE_WIFI_SSID=.*/CONFIG_CONFERENCE_WIFI_SSID="CXVOQM"/' \
    -e 's/^CONFIG_CONFERENCE_WIFI_PASSWORD=.*/CONFIG_CONFERENCE_WIFI_PASSWORD=""/' \
    "${SDKCFG}"
  grep -qx 'CONFIG_CONFERENCE_WIFI_SSID="CXVOQM"' "${SDKCFG}" || {
    echo "ERROR: CONFIG_CONFERENCE_WIFI_SSID scrub failed in ${SDKCFG}" >&2
    exit 1
  }
  grep -qx 'CONFIG_CONFERENCE_WIFI_PASSWORD=""' "${SDKCFG}" || {
    echo "ERROR: CONFIG_CONFERENCE_WIFI_PASSWORD scrub failed in ${SDKCFG}" >&2
    exit 1
  }

  # Replace hard-coded NFC Wi-Fi credentials in components/badge/ops/nfc.c (both places).
  local NFC_OPS="${DEST}/Firmware/badge/components/badge/ops/nfc.c"
  if [[ ! -f "${NFC_OPS}" ]]; then
    echo "ERROR: Missing ${NFC_OPS}" >&2
    exit 1
  fi
  sed -i \
    -e 's/nfc_write_wifi_config("SAINTCON25", "SAINTCON25")/nfc_write_wifi_config("MUSEUM", "MUSEUM-DEMO")/' \
    -e 's/\.wifi_ssid = "SAINTCON25"/\.wifi_ssid = "MUSEUM"/' \
    -e 's/\.wifi_key  = "SAINTCON25"/\.wifi_key  = "MUSEUM-DEMO"/' \
    "${NFC_OPS}"
  grep -Fq 'nfc_write_wifi_config("MUSEUM", "MUSEUM-DEMO")' "${NFC_OPS}" || {
    echo "ERROR: nfc_write_wifi_config scrub failed in ${NFC_OPS}" >&2
    exit 1
  }
  grep -Fq '.wifi_ssid = "MUSEUM"' "${NFC_OPS}" || {
    echo "ERROR: .wifi_ssid scrub failed in ${NFC_OPS}" >&2
    exit 1
  }
  grep -Fq '.wifi_key  = "MUSEUM-DEMO"' "${NFC_OPS}" || {
    echo "ERROR: .wifi_key scrub failed in ${NFC_OPS}" >&2
    exit 1
  }
  if grep -Fq 'SAINTCON25' "${NFC_OPS}"; then
    echo "ERROR: residual SAINTCON25 found in ${NFC_OPS}" >&2
    exit 1
  fi

  # Remove cryptoauthlib + hardware ATECC608B implementation (replaced by
  # platform/crypto_sim.c and platform/storage_sim.c with a deterministic fake museum identity).
  rm -rf "${DEST}/Firmware/badge/shared_components/esp-cryptoauthlib"
  rm -f  "${DEST}/Firmware/badge/shared_components/secure_element/secure_element.c"
  if [[ -e "${DEST}/Firmware/badge/shared_components/esp-cryptoauthlib" ]] || \
     [[ -e "${DEST}/Firmware/badge/shared_components/secure_element/secure_element.c" ]]; then
    echo "ERROR: Failed to remove hardware secure_element sources" >&2
    exit 1
  fi
}

if [[ "${FORCE}" -eq 0 && -f "${STAMP}" ]] && [[ "$(cat "${STAMP}")" == "${UPSTREAM_COMMIT}" ]] \
   && [[ -d "${DEST}/Firmware/badge" && -d "${DEST}/managed_components/lvgl__lvgl" ]]; then
  MC_DEST="${DEST}/managed_components"
  MC_LOCAL="${REPO_ROOT}/work/stock/managed_components"
  if [[ -d "${MC_LOCAL}/espressif__led_strip" && ! -d "${MC_DEST}/espressif__led_strip" ]]; then
    cp -a "${MC_LOCAL}/espressif__led_strip" "${MC_DEST}/espressif__led_strip"
  fi
  scrub_and_verify
  echo "2025-stock upstream already at ${UPSTREAM_COMMIT:0:12} (scrub verified)"
  exit 0
fi

echo "==> Staging compukidmike/Saintcon2025 (Firmware/badge/) @ ${UPSTREAM_COMMIT:0:12} ..."
rm -rf "${DEST}"
mkdir -p "${DEST}"

LOCAL_CLONE="${REPO_ROOT}/upstream/Saintcon2025"
if [[ -d "${LOCAL_CLONE}/.git" ]] && git -C "${LOCAL_CLONE}" cat-file -e "${UPSTREAM_COMMIT}^{commit}" 2>/dev/null; then
  git -C "${LOCAL_CLONE}" archive "${UPSTREAM_COMMIT}" Firmware/badge | tar -x -C "${DEST}"
else
  TMP_GIT="$(mktemp -d)"
  trap 'rm -rf "${TMP_GIT}"' EXIT
  git -C "${TMP_GIT}" init -q
  git -C "${TMP_GIT}" remote add origin "${UPSTREAM_URL}"
  git -C "${TMP_GIT}" fetch --depth 1 origin "${UPSTREAM_COMMIT}"
  git -C "${TMP_GIT}" archive FETCH_HEAD Firmware/badge | tar -x -C "${DEST}"
  rm -rf "${TMP_GIT}"
  trap - EXIT
fi

scrub_and_verify

# Stage managed components (LVGL 9.3.0, nlohmann-json 3.12.0, esp-boost 0.3.1, led_strip 3.0.1~1,
# espp st25dv/ndef/logger/format/base_component/base_peripheral).
MC_DEST="${DEST}/managed_components"
MC_LOCAL="${REPO_ROOT}/work/stock/managed_components"

mkdir -p "${MC_DEST}"
if [[ -d "${MC_LOCAL}/lvgl__lvgl" ]]; then
  echo "==> Copying managed_components from local work/stock/managed_components ..."
  for comp in "${NEEDED_MC[@]}"; do
    if [[ -d "${MC_LOCAL}/${comp}" ]]; then
      cp -a "${MC_LOCAL}/${comp}" "${MC_DEST}/${comp}"
      find "${MC_DEST}/${comp}" -name ".git" -exec rm -rf {} + 2>/dev/null || true
    fi
  done
else
  echo "==> Fetching pinned managed_components from upstream git tags ..."
  git clone --depth 1 --branch v9.3.0 https://github.com/lvgl/lvgl.git "${MC_DEST}/lvgl__lvgl"
  git clone --depth 1 --branch v3.12.0 https://github.com/nlohmann/json.git "${MC_DEST}/johboh__nlohmann-json"
  TMP_ESPP="$(mktemp -d)"
  git clone --depth 1 --branch v1.0.28 https://github.com/esp-cpp/espp.git "${TMP_ESPP}"
  for sub in base_component base_peripheral format logger ndef st25dv; do
    cp -a "${TMP_ESPP}/components/${sub}" "${MC_DEST}/espp__${sub}"
  done
  if [[ -f "${TMP_ESPP}/LICENSE" ]]; then
    cp -f "${TMP_ESPP}/LICENSE" "${MC_DEST}/espp__base_component/LICENSE"
  fi
  rm -rf "${TMP_ESPP}"
  mkdir -p "${MC_DEST}/espressif__esp-boost"
  git -C "${MC_DEST}/espressif__esp-boost" init -q
  git -C "${MC_DEST}/espressif__esp-boost" remote add origin https://github.com/espressif/esp-boost.git
  # v0.3.1, pinned by the commit recorded in the component's idf_component.yml
  git -C "${MC_DEST}/espressif__esp-boost" fetch --depth 1 origin 392e019e9efe6443c142fd902a93fc875d62f009
  git -C "${MC_DEST}/espressif__esp-boost" checkout -q FETCH_HEAD
  rm -rf "${MC_DEST}/espressif__esp-boost/.git"
  TMP_LED="$(mktemp -d)"
  git -C "${TMP_LED}" init -q
  git -C "${TMP_LED}" remote add origin https://github.com/espressif/idf-extra-components.git
  git -C "${TMP_LED}" fetch --depth 1 origin 69beec7d51591f06dad83f1ed3dd65a3a2e846ce
  git -C "${TMP_LED}" checkout -q FETCH_HEAD
  cp -a "${TMP_LED}/led_strip" "${MC_DEST}/espressif__led_strip"
  rm -rf "${TMP_LED}"
fi

# Apply optional patches in patches/*.patch if any exist.
if [[ -d "${HERE}/patches" ]]; then
  shopt -s nullglob
  for p in "${HERE}/patches/"*.patch; do
    echo "==> Applying patch $(basename "${p}") ..."
    patch -d "${DEST}/Firmware/badge" -p1 < "${p}"
  done
  shopt -u nullglob
fi

echo "${UPSTREAM_COMMIT}" > "${STAMP}"
echo "==> Upstream ready at ${UPSTREAM_COMMIT}"
