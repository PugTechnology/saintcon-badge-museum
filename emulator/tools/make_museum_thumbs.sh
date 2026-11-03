#!/usr/bin/env bash
# Builds the small pictures on the museum main page from each year's board.png.
# Crops drop the captions and spare parts in the 2017 and 2020 pictures. Needs ImageMagick 7.
set -euo pipefail
cd "$(dirname "$0")/../web"
mkdir -p museum-art
for y in 2017 2018 2019 2020 2021 2022 2023 2024 2025 2026; do
  crop=()
  case "$y" in
    2017) crop=(-crop 1920x900+0+108 +repage) ;;
    2020) crop=(-crop 1010x1085+40+70 +repage) ;;
  esac
  magick "badges/$y/board.png" "${crop[@]}" -trim +repage -resize '760x760>' \
    -quality 84 -define webp:alpha-quality=90 -strip "museum-art/$y.webp"
done
