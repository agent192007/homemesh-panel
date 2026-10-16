"""Install the generated fonts and emit the name -> glyph mapping.

The icon names are the panel's own ("light", "fan", "plug"), not MDI's,
because they travel in the layout JSON and have to outlive whichever
icon set they were cut from.
"""
import json, pathlib, shutil

TOOLS = pathlib.Path(__file__).resolve().parent
HERE = TOOLS / ".iconfont"      # where make_icon_font.sh works
REPO = TOOLS.parent
FONTS = REPO / "src" / "ui" / "fonts"
FONTS.mkdir(parents=True, exist_ok=True)

icons = json.loads((HERE / "icons.json").read_text(encoding="utf-8"))

for sz in (28, 18):
    shutil.copy(HERE / f"panel_icons_{sz}.c", FONTS / f"panel_icons_{sz}.c")
    print("installed", FONTS / f"panel_icons_{sz}.c")


def utf8_escaped(cp_hex):
    raw = chr(int(cp_hex, 16)).encode("utf-8")
    return "".join("\\x%02X" % b for b in raw)


names = list(icons)
h = ['#pragma once', '',
     '#include <lvgl.h>', '',
     '// ------------------------------------------------------------',
     '//  Device icons.',
     '//',
     '//  A 16-glyph subset of Material Design Icons, converted to two',
     '//  LVGL fonts by tools/make_icon_font.sh. They are font glyphs,',
     '//  not images: they draw like text, straight out of flash, and',
     '//  cost nothing from the 40KB LVGL pool. That is the whole',
     '//  reason icons are a font here and not PNGs on the SD card.',
     '//',
     '//  Flash cost: ~3.4KB of bitmap for both sizes together.',
     '//',
     '//  The names below are the panel\'s own, not MDI\'s. They travel',
     '//  in the layout JSON, so they have to outlive whichever icon',
     '//  set they happened to be cut from.',
     '// ------------------------------------------------------------',
     '',
     'LV_FONT_DECLARE(panel_icons_28);',
     'LV_FONT_DECLARE(panel_icons_18);', '']

for key, v in icons.items():
    h.append('#define ICON_%-9s "%s"   // mdi:%s  U+%s'
             % (key.upper(), utf8_escaped(v["cp"]), v["mdi"], v["cp"]))

h += ['',
      '#define ICON_COUNT %d' % len(names),
      '',
      '// Icon name from the layout -> glyph. Unknown names fall back to',
      '// ICON_UNKNOWN rather than drawing nothing, so a typo in a pushed',
      '// layout is visible on the panel instead of silently blank.',
      '//',
      '// Deriving an icon from an entity_id is NOT here - that lives in',
      '// layout_store.cpp, because it is about entity_ids. This file',
      '// only knows how to turn a name into something drawable.',
      'const char *icon_glyph(const char *name);',
      '']
(REPO / "src" / "ui" / "panel_icons.h").write_text("\n".join(h), encoding="utf-8")
print("wrote src/ui/panel_icons.h")

c = ['#include "panel_icons.h"', '',
     '#include <string.h>', '',
     '// Flat table rather than a map: 16 entries, looked up once per',
     '// tile at build time, never in the draw path.',
     'struct IconEntry { const char *name; const char *glyph; };', '',
     'static const IconEntry ICONS[] = {']
for key in names:
    c.append('  { "%s", ICON_%s },' % (key, key.upper()))
c += ['};', '',
      'const char *icon_glyph(const char *name) {',
      '  if (name && *name) {',
      '    for (unsigned i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); i++) {',
      '      if (strcmp(ICONS[i].name, name) == 0) return ICONS[i].glyph;',
      '    }',
      '  }',
      '  return ICON_UNKNOWN;',
      '}', '']
(REPO / "src" / "ui" / "panel_icons.cpp").write_text("\n".join(c), encoding="utf-8")
print("wrote src/ui/panel_icons.cpp")

# The Python half needs the same names.
py = {k: {"mdi": v["mdi"], "cp": v["cp"]} for k, v in icons.items()}
(TOOLS / "panel_icons.json").write_text(
    json.dumps(py, indent=2) + "\n", encoding="utf-8")
print("wrote tools/panel_icons.json")
