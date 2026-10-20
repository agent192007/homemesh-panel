"""Render simulator frames to PNG: python tests/render_png.py <layout> <out.png> [on] [page]

Runs tests/render_sim.mjs and converts its RGB565 output, at 2x.
"""
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

here = Path(__file__).resolve().parent
layout, out, *rest = sys.argv[1:]
raw = Path(tempfile.gettempdir()) / "homemesh_sim_frame.rgb565"
r = subprocess.run(["node", str(here / "render_sim.mjs"), layout, str(raw), *rest],
                   capture_output=True, text=True, cwd=here.parent)
sys.stdout.write(r.stdout + r.stderr)
if r.returncode:
    sys.exit(r.returncode)
px = struct.unpack(f"<{320 * 240}H", raw.read_bytes())
img = Image.new("RGB", (320, 240))
img.putdata([(((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31) for v in px])
img.resize((640, 480), Image.NEAREST).save(out)
os.remove(raw)
