#!/usr/bin/env python3
"""Generate the three architecture diagrams for the HomeMesh report.

Plain textbook embedded-systems convention, per CLAUDE.md: plain
rectangles, underscore_named blocks, thin connector lines, black on
white. Deliberately not styled cards.

Fan-outs route through a horizontal bus so no connector ever crosses a
block, which is the thing that makes these diagrams unreadable.
"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

# The architecture diagrams sit beside this script (where the report and the
# documentation build pick them up); img/ holds the paper's figures.
OUT = Path(__file__).resolve().parent

BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
GREY = (105, 105, 105)
LINE = 3


def font(size, bold=False):
    names = ("arialbd.ttf",) if bold else ("arial.ttf",)
    for n in names:
        try:
            return ImageFont.truetype(f"C:/Windows/Fonts/{n}", size)
        except Exception:
            pass
    try:
        return ImageFont.truetype("DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf", size)
    except Exception:
        return ImageFont.load_default()


F_TITLE = font(46, True)
F_BAND = font(30, True)
F_BOX = font(28, True)
F_SUB = font(24)
F_EDGE = font(22)


def tmid(d, cx, cy, s, f, fill=BLACK):
    l, t, r, b = d.textbbox((0, 0), s, font=f)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), s, font=f, fill=fill)


def label(d, cx, cy, lines, f=F_EDGE, fill=GREY):
    """Multi-line label on an opaque plate so it never sits on a line."""
    if isinstance(lines, str):
        lines = [lines]
    gap = 28
    widths = []
    for s in lines:
        l, t, r, b = d.textbbox((0, 0), s, font=f)
        widths.append(r - l)
    w = max(widths) + 18
    h = gap * len(lines) + 8
    d.rectangle([cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2], fill=WHITE)
    top = cy - (len(lines) - 1) * gap / 2
    for i, s in enumerate(lines):
        tmid(d, cx, top + i * gap, s, f, fill)


def box(d, x, y, w, h, lines, dashed=False):
    if dashed:
        step, on = 14, 8
        for px in range(int(x), int(x + w), step):
            d.line([(px, y), (min(px + on, x + w), y)], fill=GREY, width=LINE)
            d.line([(px, y + h), (min(px + on, x + w), y + h)], fill=GREY, width=LINE)
        for py in range(int(y), int(y + h), step):
            d.line([(x, py), (x, min(py + on, y + h))], fill=GREY, width=LINE)
            d.line([(x + w, py), (x + w, min(py + on, y + h))], fill=GREY, width=LINE)
    else:
        d.rectangle([x, y, x + w, y + h], outline=BLACK, width=LINE, fill=WHITE)
    gap = 34
    start = y + h / 2 - (len(lines) - 1) * gap / 2
    for i, ln in enumerate(lines):
        tmid(d, x + w / 2, start + i * gap, ln,
             F_BOX if i == 0 else F_SUB, BLACK if i == 0 else GREY)


def head(d, p1, p2, size=15):
    x1, y1 = p1
    x2, y2 = p2
    a = math.atan2(y2 - y1, x2 - x1)
    d.polygon([(x2, y2),
               (x2 - size * math.cos(a - 0.45), y2 - size * math.sin(a - 0.45)),
               (x2 - size * math.cos(a + 0.45), y2 - size * math.sin(a + 0.45))], fill=BLACK)


def arrow(d, p1, p2, lab=None, both=False, t=0.5, off=(0, 0)):
    d.line([p1, p2], fill=BLACK, width=LINE)
    head(d, p1, p2)
    if both:
        head(d, p2, p1)
    if lab:
        cx = p1[0] + (p2[0] - p1[0]) * t + off[0]
        cy = p1[1] + (p2[1] - p1[1]) * t + off[1]
        label(d, cx, cy, lab)


def fan(d, parent_bottom, child_tops, bus_y, lab=None):
    """Parent down to a horizontal bus, bus across, then down to each child."""
    px, py = parent_bottom
    d.line([(px, py), (px, bus_y)], fill=BLACK, width=LINE)
    xs = [c[0] for c in child_tops] + [px]
    d.line([(min(xs), bus_y), (max(xs), bus_y)], fill=BLACK, width=LINE)
    for cx, cy in child_tops:
        d.line([(cx, bus_y), (cx, cy)], fill=BLACK, width=LINE)
        head(d, (cx, bus_y), (cx, cy))
    if lab:
        label(d, px, bus_y - 26, lab)


# --------------------------------------------------------------------
# 1. OVERALL SYSTEM
# --------------------------------------------------------------------
def overall():
    W, H = 2200, 1240
    im = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(im)
    tmid(d, W / 2, 58, "ARCHITECTURE - OVERALL SYSTEM DIAGRAM", F_TITLE)

    # ---------------- band 1: setup
    d.text((70, 140), "SETUP PATH  -  runs on the laptop, whenever the layout changes",
           font=F_BAND, fill=BLACK)
    y, h, w = 260, 130, 320
    xs = [70, 490, 910, 1330, 1750]
    box(d, xs[0], y, w, h, ["home_assistant", "entity registry"])
    box(d, xs[1], y, w, h, ["homemesh_designer", "desktop app"])
    box(d, xs[2], y, w, h, ["layout_json", "~700 bytes"])
    box(d, xs[3], y, w, h, ["home_assistant", "event bus"])
    box(d, xs[4], y, w, h, ["layout_store", "validate, NVS"])
    for i, lb in enumerate(["get_states", "pick, order, style", "fire_event", "homemesh_layout"]):
        arrow(d, (xs[i] + w, y + h / 2), (xs[i + 1], y + h / 2), [lb], off=(0, -95))

    # ---------------- band 2: runtime
    d.text((70, 580), "RUNTIME PATH  -  continuous, while the panel is powered",
           font=F_BAND, fill=BLACK)
    y2, h2 = 680, 160
    b = [70, 420, 790, 1360, 1800]
    ws = [290, 300, 340, 300, 330]
    box(d, b[0], y2, ws[0], h2, ["smart_devices", "lights, switches,", "fans, sensors"])
    box(d, b[1], y2, ws[1], h2, ["device_integrations", "Zigbee / Z-Wave /", "Wi-Fi / helpers"])
    box(d, b[2], y2, ws[2], h2, ["home_assistant", "websocket_api", "owns device state"])
    box(d, b[3], y2, ws[3], h2, ["ha_client", "on the ESP32"])
    box(d, b[4], y2, ws[4], h2, ["lvgl_ui", "pages, tiles, sliders"])

    mid = y2 + h2 / 2
    arrow(d, (b[0] + ws[0], mid), (b[1], mid), None, both=True)
    arrow(d, (b[1] + ws[1], mid), (b[2], mid), None, both=True)
    arrow(d, (b[2] + ws[2], mid), (b[3], mid), None, both=True)
    arrow(d, (b[3] + ws[3], mid), (b[4], mid), None, both=True)

    # the wireless hop gets the wide gap, so its labels have room
    gx = (b[2] + ws[2] + b[3]) / 2
    label(d, gx, y2 - 50, ["wifi_hotspot   192.168.137.1 : 8123"])
    label(d, gx, y2 + h2 + 46, ["subscribe_entities   (state in)",
                                "call_service   (commands out)"])

    # user
    box(d, b[4], y2 + h2 + 190, ws[4], 120, ["user", "touch input"])
    arrow(d, (b[4] + ws[4] / 2, y2 + h2 + 190), (b[4] + ws[4] / 2, y2 + h2), None, both=True)

    # the stored layout decides what the runtime path subscribes to and draws
    lx = xs[4] + w / 2
    d.line([(lx, y + h), (lx, 470)], fill=BLACK, width=LINE)
    d.line([(b[3] + ws[3] / 2, 470), (b[4] + ws[4] / 2, 470)], fill=BLACK, width=LINE)
    for tx in (b[3] + ws[3] / 2, b[4] + ws[4] / 2):
        d.line([(tx, 470), (tx, y2)], fill=BLACK, width=LINE)
        head(d, (tx, 470), (tx, y2))
    label(d, lx - 310, 470 - 32,
          ["after a restart: which entity_ids, names, icons, colours"])

    im.save(OUT / "arch_overall.png")


# --------------------------------------------------------------------
# 2. HARDWARE
# --------------------------------------------------------------------
def hardware():
    W, H = 2200, 1260
    im = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(im)
    tmid(d, W / 2, 58, "ARCHITECTURE - HARDWARE DIAGRAM", F_TITLE)

    mx, my, mw, mh = 790, 470, 560, 310
    box(d, mx, my, mw, mh, ["esp32_wroom_32", "dual core, 240 MHz",
                            "320 KB usable SRAM", "4 MB flash, no PSRAM"])

    box(d, 1700, 300, 440, 150, ["st7789v_display", "320 x 240 IPS"])
    box(d, 1700, 540, 440, 150, ["gt911_touch_panel", "capacitive, addr 0x5D"])
    box(d, 1700, 780, 440, 150, ["backlight_control", "HIGH = on"])

    arrow(d, (mx + mw, my + 55), (1700, 375),
          ["SPI   27 MHz", "MOSI 13  SCLK 14", "CS 15  DC 2"], t=0.44)
    arrow(d, (mx + mw, my + mh / 2), (1700, 615),
          ["I2C   400 kHz", "SDA 33  SCL 32"], t=0.44)
    arrow(d, (mx + mw, my + mh - 50), (1700, 855), ["GPIO 27"], t=0.44)

    box(d, 120, 440, 440, 150, ["ch340_usb_serial", "programming + console"])
    box(d, 120, 680, 440, 150, ["power_supply_5v", "USB-C"])
    arrow(d, (560, 515), (mx, my + 70), ["UART"], t=0.5, off=(0, -34))
    arrow(d, (560, 755), (mx, my + mh - 60), ["5V / 3V3"], t=0.5, off=(0, -34))

    box(d, 900, 170, 440, 140, ["wifi_radio_2v4ghz", "on-module, 2.4 GHz only"])
    arrow(d, (1120, 310), (mx + mw / 2, my), None, both=True)

    nx, ny, nw, nh = 120, 990, 1420, 210
    d.rectangle([nx, ny, nx + nw, ny + nh], outline=GREY, width=2, fill=WHITE)
    d.text((nx + 26, ny + 24),
           "Board: Probots ESP32-2432S032  (3.2 inch CYD family, capacitive)",
           font=F_BOX, fill=BLACK)
    for i, ln in enumerate([
            "Display and touch controllers vary between production batches of this board.",
            "Both were identified empirically by testing, not taken from the datasheet.",
            "Display needs colour inversion ON and BGR colour order. No CGRAM offsets required.",
    ]):
        d.text((nx + 26, ny + 80 + i * 40), ln, font=F_SUB, fill=GREY)

    im.save(OUT / "arch_hardware.png")


# --------------------------------------------------------------------
# 3. SOFTWARE
# --------------------------------------------------------------------
def software():
    W, H = 2560, 1340
    im = Image.new("RGB", (W, H), WHITE)
    d = ImageDraw.Draw(im)
    tmid(d, W / 2, 58, "ARCHITECTURE - SOFTWARE DIAGRAM", F_TITLE)

    d.text((60, 140), "DRIVERS", font=F_BAND, fill=BLACK)
    d.text((560, 140), "CORE 1  -  user interface", font=F_BAND, fill=BLACK)
    d.text((1720, 140), "CORE 0  -  network", font=F_BAND, fill=BLACK)

    # drivers
    drv = [("lv_tick_timer", "1 ms tick"),
           ("display_flush_cb", "TFT_eSPI -> panel"),
           ("touch_read_cb", "GT911 -> LVGL")]
    for i, (nm, sub) in enumerate(drv):
        box(d, 60, 200 + i * 150, 390, 120, [nm, sub])

    # tasks
    box(d, 560, 290, 440, 140, ["lvgl_task", "lv_timer_handler", "every 5 ms"])
    box(d, 1140, 300, 400, 120, ["panel_event_queue", "FreeRTOS queue"])
    box(d, 1720, 280, 460, 160, ["network_task", "core 0, priority 0", "(1 trips the watchdog)"])

    for i in range(3):
        arrow(d, (450, 260 + i * 150), (560, 330 + i * 22), None)

    arrow(d, (1720, 360), (1540, 360), None)
    arrow(d, (1140, 360), (1000, 360), None)

    # modules
    box(d, 560, 580, 440, 130, ["ui_dashboard", "builds pages from layout"])
    box(d, 1720, 580, 460, 130, ["ha_client", "WebSocket state machine"])
    arrow(d, (780, 430), (780, 580), None)
    arrow(d, (1950, 440), (1950, 580), None)

    # ui children
    ui = [("page_home", "favourites"), ("page_room", "one per area"),
          ("status_bar", "link + sensors")]
    uix = [330, 620, 910]
    for x, (nm, sub) in zip(uix, ui):
        box(d, x, 850, 250, 130, [nm, sub])
    fan(d, (780, 710), [(x + 125, 850) for x in uix], 790)

    # client children
    cl = [("ws_connect_auth", "auth_ok"), ("subscribe_entities", "state in"),
          ("call_service", "commands out"), ("wifi_manager", "join + reconnect")]
    clx = [1399, 1678, 1957, 2236]
    for x, (nm, sub) in zip(clx, cl):
        box(d, x, 850, 265, 130, [nm, sub])
    fan(d, (1950, 710), [(x + 132, 850) for x in clx], 790)

    # data the firmware is driven by
    d.text((60, 1055), "DATA  -  the layout is read at boot; the rest is compiled in",
           font=F_BAND, fill=BLACK)
    dat = [("layout_store", "NVS layout, validated;", "pushed via HA event"),
           ("ha_layout_h", "compiled fallback,", "if NVS is empty"),
           ("panel_theme", "nine colours,", "from the layout"),
           ("panel_model_h", "PanelEvent, LinkState,", "EntityKind"),
           ("secrets_h", "SSID, HA host,", "access token")]
    for i, cols in enumerate(dat):
        box(d, 330 + i * 440, 1110, 400, 150, list(cols), dashed=(i >= 3))

    d.text((60, 1300),
           "network_task -> panel_event_queue -> lvgl_task. The queue keeps blocking network work off the "
           "rendering path.", font=F_SUB, fill=GREY)

    im.save(OUT / "arch_software.png")


overall()
hardware()
software()
print("wrote:")
for p in sorted(OUT.glob("arch_*.png")):
    print("  ", p.name, p.stat().st_size, "bytes")
