#!/usr/bin/env python3
"""Fig. 3 (message sequence) and Fig. 4 (message sizes) for the paper.

Same plain convention as the block diagram. Both are drawn at 300 dpi
for one IEEE column (3.5 in), so text is sized to survive that.
The numbers in Fig. 4 are the measured values reported in Table I.
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent / "img"
OUT.mkdir(exist_ok=True)
BLACK, WHITE, GREY, LIGHT = (0, 0, 0), (255, 255, 255), (90, 90, 90), (200, 200, 200)


def font(size, bold=False):
    try:
        return ImageFont.truetype("C:/Windows/Fonts/" + ("arialbd.ttf" if bold else "arial.ttf"), size)
    except Exception:
        return ImageFont.load_default()


def tmid(d, cx, cy, s, f, fill=BLACK):
    l, t, r, b = d.textbbox((0, 0), s, font=f)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), s, font=f, fill=fill)


def head(d, x1, y1, x2, y2, s=16):
    ang = math.atan2(y2 - y1, x2 - x1)
    d.polygon([(x2, y2),
               (x2 - s * math.cos(ang - 0.4), y2 - s * math.sin(ang - 0.4)),
               (x2 - s * math.cos(ang + 0.4), y2 - s * math.sin(ang + 0.4))], fill=BLACK)


# ---------------------------------------------------------------- Fig. 3
def sequence():
    W, H = 1050, 1250
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    fb, f, fs = font(27, True), font(23), font(21)
    lanes = {"designer": 150, "ha": 525, "panel": 900}
    names = {"designer": "layout_designer", "ha": "Home_Assistant", "panel": "ESP32 panel"}
    for k, x in lanes.items():
        d.rectangle([x - 135, 20, x + 135, 80], outline=BLACK, width=3)
        tmid(d, x, 50, names[k], fb)
        for y in range(80, H - 20, 18):
            d.line([(x, y), (x, y + 9)], fill=GREY, width=2)

    y = [120]

    def msg(a, b, text, note=None, dashed=False):
        y[0] += 70
        x1, x2 = lanes[a], lanes[b]
        if dashed:
            step = 1 if x2 > x1 else -1
            for xx in range(x1, x2, 22 * step):
                end = xx + 12 * step
                end = min(end, x2) if step > 0 else max(end, x2)
                d.line([(xx, y[0]), (end, y[0])], fill=BLACK, width=3)
        else:
            d.line([(x1, y[0]), (x2, y[0])], fill=BLACK, width=3)
        head(d, x1, y[0], x2, y[0])
        tmid(d, (x1 + x2) / 2, y[0] - 20, text, f)
        if note:
            y[0] += 8
            tmid(d, (x1 + x2) / 2, y[0] + 18, note, fs, GREY)
            y[0] += 22

    def band(text):
        y[0] += 52
        d.rectangle([30, y[0] - 20, W - 30, y[0] + 20], fill=(238, 238, 238))
        tmid(d, W / 2, y[0], text, fs, GREY)

    band("start-up: Wi-Fi joined, WebSocket opened")
    msg("ha", "panel", "auth_required")
    msg("panel", "ha", "auth (access token)")
    msg("ha", "panel", "auth_ok")
    msg("panel", "ha", "subscribe_entities [7 ids]")
    msg("panel", "ha", "subscribe_events (layout)")
    msg("ha", "panel", "event a: snapshot", "2,403 B, all 7 entities", dashed=True)
    band("user taps a tile")
    msg("panel", "ha", "call_service light.turn_on")
    msg("ha", "panel", "event c: change", "144-262 B", dashed=True)
    band("layout edited on the PC")
    msg("designer", "ha", "fire_event homemesh_layout", "700 B layout")
    msg("ha", "panel", "event: layout (955 B)", "validate, store in NVS, restart", dashed=True)
    out = OUT / "paper_sequence.png"
    img.crop((0, 0, W, y[0] + 50)).save(out, dpi=(300, 300))
    print(f"saved {out}")


# ---------------------------------------------------------------- Fig. 4
def sizes():
    W, H = 1050, 560
    img = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(img)
    f, fs, fb = font(23), font(20), font(23, True)
    data = [("get_states, 123 entities", 57671),
            ("panel snapshot, 7 entities", 2403),
            ("layout delivery", 955),
            ("change: light off", 262),
            ("change: brightness", 144)]
    x0, x1 = 360, 1000
    lo, hi = math.log10(100), math.log10(100000)

    def xp(v):
        return x0 + (math.log10(v) - lo) / (hi - lo) * (x1 - x0)

    top, row = 40, 80
    for tick in (100, 1000, 10000, 100000):
        x = xp(tick)
        d.line([(x, top - 10), (x, top + row * len(data))], fill=LIGHT, width=2)
        label = {100: "100 B", 1000: "1 KB", 10000: "10 KB", 100000: "100 KB"}[tick]
        tmid(d, x, top + row * len(data) + 26, label, fs, GREY)
    for i, (name, v) in enumerate(data):
        yc = top + i * row + row / 2
        l, t, r, b = d.textbbox((0, 0), name, font=f)
        d.text((x0 - 16 - (r - l), yc - (b - t) / 2 - t), name, font=f, fill=BLACK)
        fill = (60, 60, 60) if i == 0 else (150, 150, 150)
        d.rectangle([x0, yc - 22, xp(v), yc + 22], fill=fill)
        if i == 0:
            tmid(d, xp(v) - 80, yc, f"{v:,} B", fb, WHITE)
        else:
            tmid(d, xp(v) + 70, yc, f"{v:,} B", f)
    d.line([(x0, top - 10), (x0, top + row * len(data))], fill=BLACK, width=3)
    tmid(d, (x0 + x1) / 2, top + row * len(data) + 66, "message size (log scale)", fs, GREY)
    out = OUT / "paper_message_sizes.png"
    img.crop((0, 0, W, top + row * len(data) + 90)).save(out, dpi=(300, 300))
    print(f"saved {out}")


sequence()
sizes()
