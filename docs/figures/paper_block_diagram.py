#!/usr/bin/env python3
"""System block diagram for the IEEE paper (Fig. 1).

Same convention as generate_diagrams.py: plain rectangles,
underscore_named blocks, thin connectors, black on white. Drawn at 300
dpi for a figure spanning both IEEE columns (7.16 in), so text stays
legible in print.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "img" / "paper_block_diagram.png"
OUT.parent.mkdir(exist_ok=True)

W, H = 2150, 980
BLACK, WHITE, GREY = (0, 0, 0), (255, 255, 255), (90, 90, 90)
LINE = 3


def font(size, bold=False):
    try:
        return ImageFont.truetype("C:/Windows/Fonts/" + ("arialbd.ttf" if bold else "arial.ttf"), size)
    except Exception:
        return ImageFont.load_default()


F_GROUP = font(34, True)
F_BOX = font(29, True)
F_SUB = font(24)
F_EDGE = font(24)

img = Image.new("RGB", (W, H), WHITE)
d = ImageDraw.Draw(img)


def tmid(cx, cy, s, f, fill=BLACK):
    l, t, r, b = d.textbbox((0, 0), s, font=f)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), s, font=f, fill=fill)


def box(x, y, w, h, name, sub=None):
    d.rectangle([x, y, x + w, y + h], outline=BLACK, width=LINE, fill=WHITE)
    if sub:
        tmid(x + w / 2, y + h / 2 - 17, name, F_BOX)
        tmid(x + w / 2, y + h / 2 + 19, sub, F_SUB, GREY)
    else:
        tmid(x + w / 2, y + h / 2, name, F_BOX)
    return (x, y, w, h)


def group(x, y, w, h, title):
    step, on = 16, 9
    for px in range(x, x + w, step):
        d.line([(px, y), (min(px + on, x + w), y)], fill=GREY, width=2)
        d.line([(px, y + h), (min(px + on, x + w), y + h)], fill=GREY, width=2)
    for py in range(y, y + h, step):
        d.line([(x, py), (x, min(py + on, y + h))], fill=GREY, width=2)
        d.line([(x + w, py), (x + w, min(py + on, y + h))], fill=GREY, width=2)
    tmid(x + w / 2, y + 30, title, F_GROUP)


def arrow(p, q, both=False):
    d.line([p, q], fill=BLACK, width=LINE)
    for a, b in ([(p, q)] + ([(q, p)] if both else [])):
        (x1, y1), (x2, y2) = a, b
        import math
        ang = math.atan2(y2 - y1, x2 - x1)
        s = 18
        d.polygon([(x2, y2),
                   (x2 - s * math.cos(ang - 0.4), y2 - s * math.sin(ang - 0.4)),
                   (x2 - s * math.cos(ang + 0.4), y2 - s * math.sin(ang + 0.4))], fill=BLACK)


def edge_label(cx, cy, lines):
    gap = 30
    wmax = max(d.textbbox((0, 0), s, font=F_EDGE)[2] for s in lines) + 16
    h = gap * len(lines) + 6
    d.rectangle([cx - wmax / 2, cy - h / 2, cx + wmax / 2, cy + h / 2], fill=WHITE)
    top = cy - (len(lines) - 1) * gap / 2
    for i, s in enumerate(lines):
        tmid(cx, top + i * gap, s, F_EDGE, GREY)


# --- Laptop -----------------------------------------------------------
group(30, 30, 820, 920, "Laptop (Windows)")
designer = box(90, 120, 700, 110, "layout_designer", "entity picker, preview, theme, push")
ha = box(90, 400, 700, 130, "Home_Assistant", "Docker, WebSocket API on :8123")
devices = box(90, 760, 700, 110, "smart_devices", "lights, switches, sensors (any integration)")

arrow((440, 230), (440, 400))
edge_label(440, 315, ["get_states (discovery)", "fire_event homemesh_layout"])
arrow((440, 530), (440, 760), both=True)
edge_label(440, 645, ["integrations"])

# --- Network ----------------------------------------------------------
net = box(905, 395, 330, 160, "wifi_hotspot", "2.4 GHz, WPA2")
arrow((790, 465), (905, 465), both=True)

# --- Panel ------------------------------------------------------------
group(1290, 30, 830, 920, "ESP32 Panel (WROOM-32)")
client = box(1350, 120, 710, 120, "ha_client", "WebSocket: auth, subscribe_entities, call_service")
store = box(1350, 300, 330, 110, "layout_store", "NVS + fallback")
queue = box(1730, 300, 330, 110, "event_queue", "FreeRTOS")
ui = box(1350, 480, 710, 120, "lvgl_dashboard", "LVGL 8.3 pages, tiles, detail panel")
disp = box(1350, 680, 330, 120, "display_driver", "ST7789V, SPI")
touch = box(1730, 680, 330, 120, "touch_driver", "GT911, I2C")
d.rectangle([1350, 840, 2060, 910], outline=GREY, width=2)
tmid(1705, 875, "core 0: network task    |    core 1: LVGL loop", F_SUB, GREY)

# One WebSocket from the hotspot to ha_client, routed up the gutter.
d.line([(1235, 475), (1315, 475), (1315, 180)], fill=BLACK, width=LINE)
arrow((1315, 180), (1350, 180))
arrow((1260, 475), (1235, 475))

arrow((1515, 240), (1515, 300))
arrow((1895, 240), (1895, 300))
arrow((1515, 410), (1515, 480))
arrow((1895, 410), (1895, 480))
arrow((1515, 600), (1515, 680))
arrow((1895, 680), (1895, 600))

img.save(OUT, dpi=(300, 300))
print(f"saved {OUT}")
