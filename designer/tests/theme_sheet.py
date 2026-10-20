"""Render every theme preset on the panel's real UI into one contact sheet.

    python tests/theme_sheet.py <out.png>

For each preset: Home with lights on, a room page with compact tiles, and
the detail panel - the three views where colours meet each other most.
Presets are read from src/layout/themes.ts so the sheet always shows what
the designer offers.
"""
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

here = Path(__file__).resolve().parent
ts = (here.parent / "src" / "layout" / "themes.ts").read_text(encoding="utf-8")
defaults = dict(re.findall(r'(\w+): "(#[0-9A-Fa-f]{6})"',
                           (here.parent / "src" / "layout" / "format.ts").read_text(encoding="utf-8")
                           .split("THEME_DEFAULTS")[1].split("};")[0]))
presets = []
for m in re.finditer(r'name: "([^"]+)", light: (true|false), colours: (\{[^}]*\}|\{ \.\.\.THEME_DEFAULTS \})', ts):
    body = m.group(3)
    cols = dict(defaults) if "THEME_DEFAULTS" in body else dict(re.findall(r'(\w+): "(#[0-9A-Fa-f]{6})"', body))
    presets.append((m.group(1), cols))

base = json.loads((here / "fixtures" / "layout_crowded.json").read_text(encoding="utf-8"))
views = [("on", "0", ""), ("on", "1", ""), ("on", "0", "detail"), ("off", "0", "detail")]
tmp = Path(tempfile.gettempdir())
font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", 22)
sheet = Image.new("RGB", (len(views) * 340 + 20, len(presets) * 290 + 20), (24, 26, 30))
for r, (name, cols) in enumerate(presets):
    layout = dict(base)
    layout["theme"] = {k: v for k, v in cols.items() if v.upper() != defaults[k].upper()}
    lf = tmp / "theme_layout.json"
    lf.write_text(json.dumps(layout, separators=(",", ":")), encoding="utf-8")
    d = ImageDraw.Draw(sheet)
    d.text((20, 20 + r * 290), name, font=font, fill=(230, 230, 230))
    for c, (mode, page, view) in enumerate(views):
        raw = tmp / "theme_frame.rgb565"
        out = subprocess.run(["node", str(here / "render_sim.mjs"), str(lf), str(raw), mode, page, view],
                             capture_output=True, text=True, cwd=here.parent)
        if "REJECTED" in out.stdout:
            sys.exit(f"{name}: {out.stdout}")
        px = struct.unpack(f"<{320 * 240}H", raw.read_bytes())
        img = Image.new("RGB", (320, 240))
        img.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px])
        sheet.paste(img, (20 + c * 340, 55 + r * 290))
    print(f"rendered {name}")
sheet.save(sys.argv[1])
print(f"saved {sys.argv[1]}")
