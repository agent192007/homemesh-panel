#!/usr/bin/env python3
"""
HomeMesh: read the entity and area registries out of Home Assistant and
emit the panel's layout.

This is the PC half of the split described in CLAUDE.md. get_states on a
real instance is hundreds of KB, which will not fit on a WROOM-32 with no
PSRAM - so discovery happens here, and the firmware only ever subscribes
to the handful of entity_ids this writes out.

Two consumers share this module, and that is the point of its shape:

    ha_discover.py      flags-only CLI (this file's main())
    layout_designer.py  the desktop authoring application

Both call fetch_inventory() / select_layout() / build_header() /
build_layout(), so the output format is defined exactly once. Changing
how a layout is authored must not change what a layout is.

Usage:
    python tools/ha_discover.py --list
    python tools/ha_discover.py --favourite light.bed_light,switch.ac
    python tools/ha_discover.py --include light.bed_light,light.ceiling_lights

Credentials come from tools/.env (HA_URL, HA_TOKEN) or the environment,
and --url / --token override both. Create the token in Home Assistant
under your user profile -> Security -> Long-lived access tokens.
"""

import argparse
import asyncio
import json
import os
import sys
from pathlib import Path

try:
    import websockets
except ImportError:
    sys.exit("websockets not installed. Run: pip install -r tools/requirements.txt")

HERE = Path(__file__).resolve().parent
HEADER_OUT = HERE.parent / "include" / "ha_layout.h"
LAYOUT_OUT = HERE.parent / "include" / "ha_layout.json"

# Domains the panel can actually control, mapped to how they are driven.
TOGGLE_DOMAINS = {"switch", "fan", "input_boolean"}
# FanEntityFeature.SET_SPEED. A fan with it gets a speed slider: it is
# classed Dimmable, and the firmware maps its "percentage" to the level.
FAN_SET_SPEED = 1
LIGHT_DOMAIN = "light"
# Read-only yes/no sensors, shown as a Status tile: green "Good" when off,
# red "Bad" when on (the air monitor's Bad Air, a leak, a door left open).
STATUS_DOMAINS = {"binary_sensor"}
# Domains that behave as "run this", i.e. the Automations screen.
ACTION_DOMAINS = {"scene", "script", "automation"}
# Sensors the header bar can show, keyed by HA's own device_class.
# Anything else is ignored - the panel has two sensor slots per room.
SENSOR_CLASSES = {"temperature", "humidity"}

# Colour support flags as reported in a light's supported_color_modes.
RGB_MODES = {"hs", "rgb", "rgbw", "rgbww", "xy"}
DIM_MODES = {"brightness", "color_temp"} | RGB_MODES

# Icon names the firmware knows, in src/ui/panel_icons.h. These are the
# panel's own names, not Material Design Icons' - they travel in the
# layout JSON and have to outlive whichever icon set they were cut from.
ICON_NAMES = [
    "light", "ceiling", "lamp", "strip", "fan", "ac", "toggle", "plug",
    "temp", "humidity", "tv", "speaker", "lock", "blinds", "home", "unknown",
]

# What an entity gets when nobody picks for it. Mirrors DOMAIN_ICONS in
# src/layout_store.cpp - change one, change the other, or the designer
# preview and the panel will disagree.
DOMAIN_ICONS = {
    "light": "light",
    "fan": "fan",
    "switch": "plug",
    "input_boolean": "toggle",
    "media_player": "tv",
    "lock": "lock",
    "cover": "blinds",
}


def default_icon(entity_id):
    return DOMAIN_ICONS.get(entity_id.split(".", 1)[0], "unknown")


MAX_AREAS = 6
MAX_ENTITIES = 12
MAX_AUTOMATIONS = 8
MAX_PER_PAGE = 6

# The layout format version the firmware checks. Bump only on a
# breaking change - the panel should refuse a version it does not know
# rather than guess at fields.
LAYOUT_VERSION = 1

# The custom HA event a pushed layout travels on. Must match
# LAYOUT_EVENT in src/layout_store.h.
#
# The panel subscribes to this on the WebSocket it already holds, so
# pushing a layout needs no HTTP server on the ESP32 and - the part
# that actually matters on a hotspot - no way of discovering the
# panel's IP address. Both ends only have to know where HA is.
LAYOUT_EVENT = "homemesh_layout"

# The panel's colours. Must match theme_defaults() in
# src/panel_theme.cpp - these are the compiled look, and a layout that
# names none of them produces exactly that.
#
# Order is the order they are offered in the designer, roughly most to
# least worth changing.
THEME_DEFAULTS = {
    "bg":     "#101418",   # screen background
    "tile":   "#2A2F36",   # device tile face
    "on":     "#F5A623",   # an entity that is on
    "accent": "#4C8DFF",   # sliders and brightness bars
    "text":   "#FFFFFF",   # primary label text
    "muted":  "#8A9099",   # secondary text
    "ok":     "#3DDC84",   # status dot: connected
    "warn":   "#F5A623",   # status dot: WiFi but no Home Assistant
    "bad":    "#E5484D",   # status dot: no WiFi
}


def load_env():
    env = {}
    f = HERE / ".env"
    if f.exists():
        for line in f.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if line and not line.startswith("#") and "=" in line:
                k, v = line.split("=", 1)
                env[k.strip()] = v.strip().strip('"').strip("'")
    return env


def resolve_credentials(url=None, token=None):
    """Explicit argument, then tools/.env, then the environment."""
    env = load_env()
    url = url or env.get("HA_URL") or os.environ.get("HA_URL")
    token = token or env.get("HA_TOKEN") or os.environ.get("HA_TOKEN")
    return url, token


class HA:
    def __init__(self, ws):
        self.ws = ws
        self._id = 0

    async def cmd(self, type_, **kw):
        self._id += 1
        await self.ws.send(json.dumps({"id": self._id, "type": type_, **kw}))
        while True:
            msg = json.loads(await self.ws.recv())
            if msg.get("id") == self._id and msg.get("type") == "result":
                if not msg.get("success", False):
                    raise RuntimeError(f"{type_} failed: {msg.get('error')}")
                return msg["result"]


async def connect(url, token):
    ws_url = url.replace("https://", "wss://").replace("http://", "ws://")
    ws_url = ws_url.rstrip("/") + "/api/websocket"
    ws = await websockets.connect(ws_url, max_size=16 * 1024 * 1024)

    hello = json.loads(await ws.recv())
    if hello.get("type") != "auth_required":
        raise RuntimeError(f"unexpected greeting: {hello}")
    await ws.send(json.dumps({"type": "auth", "access_token": token}))
    reply = json.loads(await ws.recv())
    if reply.get("type") != "auth_ok":
        raise RuntimeError(f"auth failed: {reply}")
    return ws


def classify(state):
    """Return EntityKind name, or None if the panel cannot drive it."""
    domain = state["entity_id"].split(".", 1)[0]
    if domain == "fan" and int(state.get("attributes", {}).get("supported_features") or 0) & FAN_SET_SPEED:
        return "Dimmable"
    if domain in STATUS_DOMAINS:
        return "Status"
    if domain in TOGGLE_DOMAINS:
        return "Toggle"
    if domain == LIGHT_DOMAIN:
        modes = set(state.get("attributes", {}).get("supported_color_modes") or [])
        if modes & RGB_MODES:
            return "RGB"
        if modes & DIM_MODES:
            return "Dimmable"
        return "Toggle"
    return None


class Inventory:
    """Everything the panel could possibly show, already filtered and
    sorted. Keyed by area id rather than area name, so a selection
    survives a room being renamed in HA."""

    def __init__(self, controllable, actions, sensors, area_name):
        self.controllable = controllable  # (eid, name, kind, area_id)
        self.actions = actions            # (eid, name)
        self.sensors = sensors            # (eid, name, device_class, area_id)
        self.area_name = area_name        # area_id -> display name

    def area_of(self, area_id):
        return self.area_name.get(area_id, "(no area)")


async def fetch_inventory(url, token):
    ws = await connect(url, token)
    ha = HA(ws)
    try:
        states = await ha.cmd("get_states")
        areas = await ha.cmd("config/area_registry/list")
        ent_reg = await ha.cmd("config/entity_registry/list")
        dev_reg = await ha.cmd("config/device_registry/list")
    finally:
        await ws.close()

    area_name = {a["area_id"]: a["name"] for a in areas}
    dev_area = {d["id"]: d.get("area_id") for d in dev_reg}
    # Entity-level area wins over the device's, matching HA's own
    # resolution order.
    ent_area = {}
    for e in ent_reg:
        a = e.get("area_id") or dev_area.get(e.get("device_id"))
        if a:
            ent_area[e["entity_id"]] = a

    controllable, actions, sensors = [], [], []
    for s in states:
        eid = s["entity_id"]
        domain = eid.split(".", 1)[0]
        attrs = s.get("attributes", {})
        name = attrs.get("friendly_name") or eid
        if domain in ACTION_DOMAINS:
            actions.append((eid, name))
            continue
        if domain == "sensor":
            dc = attrs.get("device_class")
            if dc in SENSOR_CLASSES:
                sensors.append((eid, name, dc, ent_area.get(eid)))
            continue
        kind = classify(s)
        if kind:
            controllable.append((eid, name, kind, ent_area.get(eid)))

    controllable.sort(key=lambda r: (area_name.get(r[3], "~"), r[1]))
    actions.sort(key=lambda r: r[1])
    sensors.sort(key=lambda r: (area_name.get(r[3], "~"), r[2], r[1]))

    return Inventory(controllable, actions, sensors, area_name)


def select_layout(inv, include=None, favourite=(), automations=None,
                  order=None, icons=None, names=None, pages=None,
                  sensor_pages=None, log=print):
    """Turn a selection into the four tables a layout is made of.

    include     entity_ids to place, or None for everything controllable
    favourite   entity_ids that also appear on the Home page
    automations scene/script/automation entity_ids, or None for all
    order       entity_ids in the order they should occupy tile slots.
                A tile's position is purely its order within its page -
                see grid_for() in ui/dashboard.cpp - so this is what the
                authoring tool's drag-and-drop actually edits.

    icons       entity_id -> icon name, overriding the domain default
    names       entity_id -> name to show on the panel, overriding HA's
                friendly name. Blank or whitespace means "use HA's".
    pages       entity_id -> the page it goes on, overriding its HA area.
                A page is only a name to the panel, so this is how a
                device with no area in HA gets placed, and how pages
                that are not HA areas are made. Blank means "its area".
    sensor_pages
                entity_id -> page for a temperature/humidity sensor,
                overriding its HA area. "" hides the sensor. Sensors
                placed here win over ones that are only in the area.

    Returns (area_names, rows, acts, sensor_rows) where rows are
    (eid, name, kind, area_index, favourite, icon).
    """
    icons = icons or {}
    names = names or {}
    pages = pages or {}
    sensor_pages = sensor_pages or {}

    def page_of(eid, area):
        p = (pages.get(eid) or "").strip()
        if p:
            return p
        return inv.area_name.get(area) if area is not None else None
    favourite = set(favourite)
    chosen = [r for r in inv.controllable if include is None or r[0] in include]

    if include:
        missing = set(include) - {r[0] for r in chosen}
        if missing:
            raise ValueError("Not found or not controllable: "
                             + ", ".join(sorted(missing)))

    if order:
        # Anything unranked sorts after everything ranked, keeping its
        # existing relative order.
        rank = {eid: i for i, eid in enumerate(order)}
        chosen.sort(key=lambda r: rank.get(r[0], len(rank)))

    # Pages that survive, by name, in first-seen order. Entities with no
    # page - no area in HA and none given - are dropped rather than
    # invented into one.
    used_areas, dropped = [], []
    for eid, name, kind, area in chosen:
        page = page_of(eid, area)
        if page is None:
            dropped.append(eid)
        elif page not in used_areas:
            used_areas.append(page)
    if dropped:
        log(f"note: {len(dropped)} entity(ies) have no area in HA and were skipped:")
        for d in dropped:
            log(f"      {d}")
        log("      assign an area in HA, or give them a page.")
    used_areas = used_areas[:MAX_AREAS]

    rows, per_area = [], {}
    for eid, name, kind, area in chosen:
        page = page_of(eid, area)
        if page not in used_areas:
            continue
        idx = used_areas.index(page)
        per_area.setdefault(idx, 0)
        if per_area[idx] >= MAX_PER_PAGE:
            log(f"note: {page} page is full, skipping {eid}")
            continue
        per_area[idx] += 1
        rows.append((eid, (names.get(eid) or "").strip() or name, kind, idx,
                     eid in favourite, icons.get(eid) or default_icon(eid)))
        if len(rows) >= MAX_ENTITIES:
            break

    n_fav = sum(1 for r in rows if r[4])
    if n_fav > MAX_PER_PAGE:
        log(f"note: {n_fav} favourites but the Home page holds "
            f"{MAX_PER_PAGE}; the extras will not be shown.")

    acts = [a for a in inv.actions
            if automations is None or a[0] in automations][:MAX_AUTOMATIONS]

    # One temperature and one humidity sensor per area, chosen
    # automatically - the header has exactly two slots per room and
    # nothing to disambiguate a second candidate with. Areas with no
    # sensor are left out entirely; the panel renders those slots
    # blank rather than showing a made-up reading.
    def sensor_page(s):
        if s[0] in sensor_pages:
            return (sensor_pages[s[0]] or "").strip() or None
        return inv.area_name.get(s[3]) if s[3] is not None else None

    sensor_rows = []
    for idx, page in enumerate(used_areas):
        for dc in ("temperature", "humidity"):
            fits = [s for s in inv.sensors if s[2] == dc and sensor_page(s) == page]
            match = next((s for s in fits if s[0] in sensor_pages), None) or (fits[0] if fits else None)
            if match:
                sensor_rows.append((match[0], idx, dc == "humidity"))
        if not any(r[1] == idx for r in sensor_rows):
            log(f"note: {page} has no temperature or humidity sensor")

    return list(used_areas), rows, acts, sensor_rows


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def build_header(area_names, rows, acts, sensor_rows, status=None):
    """The compile-time path: include/ha_layout.h.

    Still the mechanism today. Once the panel stores a layout in NVS
    this becomes the fallback the firmware boots from when NVS is
    empty, which is why it is generated from the same tables as the
    JSON rather than by a separate code path.
    """
    out = ["#pragma once", "",
           "// Generated by tools/ha_discover.py - do not edit by hand.",
           "// Regenerate after changing areas or entity selection in HA.",
           "",
           '#include "../src/panel_model.h"', "",
           "// Entities carry an icon name; layout_store.cpp checks this.",
           "#define HA_LAYOUT_HAS_ICONS 1",
           "",
           f"#define HA_AREA_COUNT {len(area_names)}",
           "static const char *HA_AREAS[] = {"]
    for name in area_names:
        out.append(f"  {c_str(name)},")
    out += ["};", "",
            "struct HaEntityCfg {",
            "  const char *id;",
            "  const char *name;",
            "  EntityKind kind;",
            "  uint8_t area;",
            "  bool favourite;",
            "  const char *icon;",
            "};", "",
            f"#define HA_ENTITY_COUNT {len(rows)}",
            "static const HaEntityCfg HA_ENTITIES[] = {"]
    for eid, name, kind, area, fav, icon in rows:
        out.append(f"  {{ {c_str(eid)}, {c_str(name)}, EntityKind::{kind}, "
                   f"{area}, {'true' if fav else 'false'}, {c_str(icon)} }},")
    out += ["};", "",
            "struct HaActionCfg {",
            "  const char *id;",
            "  const char *name;",
            "};", "",
            f"#define HA_ACTION_COUNT {len(acts)}",
            "static const HaActionCfg HA_ACTIONS[] = {"]
    for eid, name in acts:
        out.append(f"  {{ {c_str(eid)}, {c_str(name)} }},")
    if not acts:
        # An empty initialiser is not valid C++; the count is what the
        # firmware iterates on, so a placeholder row is harmless.
        out.append('  { "", "" },')
    out += ["};", "",
            "// Header-bar sensors, one temperature and one humidity per area.",
            "struct HaSensorCfg {",
            "  const char *id;",
            "  uint8_t area;",
            "  bool humidity;   // false = temperature",
            "};", "",
            f"#define HA_SENSOR_COUNT {len(sensor_rows)}",
            "static const HaSensorCfg HA_SENSORS[] = {"]
    for eid, area, is_hum in sensor_rows:
        out.append(f"  {{ {c_str(eid)}, {area}, {'true' if is_hum else 'false'} }},")
    if not sensor_rows:
        out.append('  { "", 0, false },')
    out += ["};", ""]
    if status:
        # The binary sensor the status bar shows as a green "Good" / red
        # "Bad" block. Only emitted when chosen, so older headers stand.
        out += [f"#define HA_STATUS_ENTITY {c_str(status)}", ""]
    return "\n".join(out)


def build_layout(area_names, rows, acts, sensor_rows, theme=None, action_colors=None, status=None):
    """The delivery format: what gets pushed to the panel and stored.

    Deliberately terse - short keys, no whitespace when serialised -
    because the ESP32 parses this alongside a 40KB LVGL heap. The
    nine-entity demo layout comes to well under a kilobyte, against
    the hundreds of KB a get_states would cost.

    Order is significant: a page lays its tiles out in list order.
    """
    out = {
        "v": LAYOUT_VERSION,
        "areas": list(area_names),
        "entities": [
            {"id": eid, "name": name, "kind": kind, "area": area,
             "fav": fav, "icon": icon}
            for eid, name, kind, area, fav, icon in rows
        ],
        # "color" only where one was chosen: "#RRGGBB", drawn as that
        # automation's button. The compiled header carries none.
        "actions": [dict({"id": eid, "name": name},
                         **({"color": action_colors[eid].upper()} if (action_colors or {}).get(eid) else {}))
                    for eid, name in acts],
        "sensors": [
            {"id": eid, "area": area, "hum": is_hum}
            for eid, area, is_hum in sensor_rows
        ],
    }
    # Optional: a binary sensor the status bar shows as a green "Good" /
    # red "Bad" block on every page (the air monitor's Bad Air).
    if status:
        out["status"] = status

    # Only colours that differ from the compiled defaults are sent.
    # The firmware treats a missing colour as "keep the default", so
    # an unstyled layout carries no theme object at all and costs
    # nothing - and a theme that happens to match the defaults reads
    # as what it is, rather than as a redundant copy of them.
    if theme:
        changed = {k: v for k, v in theme.items()
                   if k in THEME_DEFAULTS
                   and v.upper() != THEME_DEFAULTS[k].upper()}
        if changed:
            out["theme"] = changed

    return out


def dump_layout(layout):
    """Compact JSON - the exact bytes the panel will receive."""
    return json.dumps(layout, separators=(",", ":"), ensure_ascii=False)


async def push_layout(url, token, layout):
    """Send a layout to the panel, via Home Assistant.

    Fires LAYOUT_EVENT with the layout as event data. Any panel
    subscribed to that event stores it in NVS and restarts into it,
    which is what replaces the regenerate/rebuild/reflash cycle.

    Returns the payload size in bytes. Note that this reaches *every*
    panel listening, which is the right behaviour for one wall panel
    and worth knowing before there are two.
    """
    payload = dump_layout(layout)
    if len(payload) > 3072:
        raise ValueError(f"layout is {len(payload)} bytes; the panel accepts 3072")

    ws = await connect(url, token)
    ha = HA(ws)
    try:
        await ha.cmd("fire_event", event_type=LAYOUT_EVENT,
                     event_data={"layout": layout})
    finally:
        await ws.close()
    return len(payload)


def write_outputs(area_names, rows, acts, sensor_rows, theme=None, log=print):
    # The header carries no theme: colours are a runtime concern now,
    # and ha_layout.h is the fallback for a board that has never been
    # pushed to - which by definition has the compiled colours.
    HEADER_OUT.write_text(build_header(area_names, rows, acts, sensor_rows),
                          encoding="utf-8")
    payload = dump_layout(build_layout(area_names, rows, acts, sensor_rows, theme))
    LAYOUT_OUT.write_text(payload + "\n", encoding="utf-8")
    log(f"Wrote {HEADER_OUT}")
    log(f"Wrote {LAYOUT_OUT}  ({len(payload)} bytes)")
    log(f"  {len(area_names)} areas, {len(rows)} entities "
        f"({sum(1 for r in rows if r[4])} favourites), {len(acts)} actions, "
        f"{len(sensor_rows)} sensors")


async def run(args):
    url, token = resolve_credentials(args.url, args.token)
    if not url or not token:
        sys.exit("Set HA_URL and HA_TOKEN in tools/.env, or pass --url/--token.")

    inv = await fetch_inventory(url, token)

    if args.list:
        print(f"\n{len(inv.controllable)} controllable entities:\n")
        for eid, name, kind, area in inv.controllable:
            print(f"  {kind:<9} {inv.area_of(area):<16} {name:<26} {eid}")
        print(f"\n{len(inv.actions)} runnable (scenes/scripts/automations):\n")
        for eid, name in inv.actions:
            print(f"  {name:<26} {eid}")
        print(f"\n{len(inv.sensors)} temperature/humidity sensors:\n")
        for eid, name, dc, area in inv.sensors:
            print(f"  {dc:<12} {inv.area_of(area):<16} {name:<26} {eid}")
        print("\nPick with --include, mark Home-page entries with --favourite.")
        print("Sensors are picked up automatically for whichever areas you use.")
        print("Or author it visually: python tools/layout_designer.py\n")
        return

    def csv(s):
        return set(x.strip() for x in s.split(",") if x.strip()) if s else None

    try:
        tables = select_layout(inv,
                               include=csv(args.include),
                               favourite=csv(args.favourite) or (),
                               automations=csv(args.automations))
    except ValueError as e:
        sys.exit(str(e))

    print()
    write_outputs(*tables)

    if args.push:
        area_names, rows, acts, sensor_rows = tables
        layout = build_layout(area_names, rows, acts, sensor_rows)
        try:
            n = await push_layout(url, token, layout)
        except Exception as e:
            sys.exit(f"push failed: {type(e).__name__}: {e}")
        print()
        print(f"Pushed {n} bytes to the panel.")
        print("It stores the layout in NVS and restarts into it; give it "
              "a few seconds.")


def main():
    ap = argparse.ArgumentParser(description="Generate the panel layout from Home Assistant.")
    ap.add_argument("--url", help="e.g. http://192.168.100.4:8123")
    ap.add_argument("--token", help="long-lived access token")
    ap.add_argument("--list", action="store_true", help="show what HA has and exit")
    ap.add_argument("--include", help="comma-separated entity_ids to put on the panel")
    ap.add_argument("--favourite", default="", help="comma-separated entity_ids for the Home page")
    ap.add_argument("--automations", help="comma-separated scene/script/automation entity_ids")
    ap.add_argument("--push", action="store_true",
                    help="also send the layout to the panel over HA (no reflash)")
    asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    main()
