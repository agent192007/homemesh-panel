#!/usr/bin/env bash
#
# Regenerate the device icon font and the designer's preview masks.
#
#   bash tools/make_icon_font.sh
#
# You only need this to change WHICH icons exist. Changing which icon an
# entity uses is layout data - pick it in the designer and push it, no
# regeneration and no reflash.
#
# What it produces, all committed so a normal build never needs any of
# the tooling below:
#
#   src/ui/fonts/panel_icons_28.c   LVGL font, 28px, for roomy tiles
#   src/ui/fonts/panel_icons_18.c   LVGL font, 18px, for compact tiles
#   src/ui/panel_icons.h/.cpp       name -> glyph, generated from icons.json
#   tools/icon_masks.py             alpha masks for the designer preview
#   tools/panel_icons.json          the chosen set, for reference
#
# Needs node (for lv_font_conv) and Python with Pillow. Both are
# build-time only - the firmware and the designer need neither.
#
# To add an icon: add a line to WANT in tools/pick_icons.py naming the
# panel's own key and the MDI glyph, rerun this, then add the key to
# ICON_NAMES in tools/ha_discover.py so the designer offers it.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD="$HERE/.iconfont"

mkdir -p "$BUILD"
cd "$BUILD"

if [ ! -d node_modules/@mdi/font ]; then
  echo "==> fetching the Material Design Icons webfont and lv_font_conv"
  npm init -y >/dev/null 2>&1 || true
  npm install --no-audit --no-fund @mdi/font lv_font_conv
fi

echo "==> choosing glyphs"
python "$HERE/pick_icons.py"

RANGE="$(python -c "
import json
icons = json.load(open('icons.json'))
# sorted so the Opts comment lv_font_conv records is stable
print(','.join(sorted('0x' + v['cp'] for v in icons.values())))
")"
TTF="node_modules/@mdi/font/fonts/materialdesignicons-webfont.ttf"

# --no-compress on purpose: a compressed LVGL font is decompressed at
# draw time into a buffer, and buffers are what the fixed 40KB pool
# cannot spare. Sixteen glyphs uncompressed is ~3.4KB of flash.
for SZ in 28 18; do
  echo "==> converting at ${SZ}px"
  ./node_modules/.bin/lv_font_conv \
    --font "$TTF" --range "$RANGE" --size "$SZ" \
    --format lvgl --bpp 4 --no-compress --force-fast-kern-format \
    -o "panel_icons_${SZ}.c"
done

echo "==> installing fonts and emitting the name -> glyph table"
python "$HERE/emit_icon_font.py"

echo "==> baking preview masks for the designer"
python "$HERE/make_icon_masks.py"

echo
echo "Done. Rebuild the firmware to pick up the new fonts:"
echo "    pio run -e ha"
