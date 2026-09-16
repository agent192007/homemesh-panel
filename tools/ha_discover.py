#!/usr/bin/env python3
"""
HomeMesh: read the entity and area registries out of Home Assistant and
emit the panel's layout header.

This is the PC half of the split described in CLAUDE.md. get_states on a
real instance is hundreds of KB, which will not fit on a WROOM-32 with no
PSRAM - so discovery happens here, and the firmware only ever subscribes
to the handful of entity_ids this writes into include/ha_layout.h.

It is also the seed of the desktop authoring tool: the picker and the
drag-and-drop canvas replace the --include/--favourite flags later, but
the output format stays the same.

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

# Domains the panel can actually control, mapped to how they are driven.
TOGGLE_DOMAINS = {"switch", "fan", "input_boolean"}
LIGHT_DOMAIN = "light"
# Domains that behave as "run this", i.e. the Automations screen.
ACTION_DOMAINS = {"scene", "script", "automation"}
# Sensors the header bar can show, keyed by HA's own device_class.
# Anything else is ignored - the panel has two sensor slots per room.
SENSOR_CLASSES = {"temperature", "humidity"}

# Colour support flags as reported in a light's supported_color_modes.
RGB_MODES = {"hs", "rgb", "rgbw", "rgbww", "xy"}
DIM_MODES = {"brightness", "color_temp"} | RGB_MODES

MAX_AREAS = 6
MAX_ENTITIES = 12
MAX_AUTOMATIONS = 8
MAX_PER_PAGE = 6


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


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


async def run(args):
    env = load_env()
    url = args.url or env.get("HA_URL") or os.environ.get("HA_URL")
    token = args.token or env.get("HA_TOKEN") or os.environ.get("HA_TOKEN")
    if not url or not token:
        sys.exit("Set HA_URL and HA_TOKEN in tools/.env, or pass --url/--token.")

    ws = await connect(url, token)
    ha = HA(ws)

    states = await ha.cmd("get_states")
    areas = await ha.cmd("config/area_registry/list")
    ent_reg = await ha.cmd("config/entity_registry/list")
    dev_reg = await ha.cmd("config/device_registry/list")
    await ws.close()

    area_name = {a["area_id"]: a["name"] for a in areas}
    dev_area = {d["id"]: d.get("area_id") for d in dev_reg}
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

    if args.list:
        print(f"\n{len(controllable)} controllable entities:\n")
        for eid, name, kind, area in controllable:
            print(f"  {kind:<9} {area_name.get(area, '(no area)'):<16} {name:<26} {eid}")
        print(f"\n{len(actions)} runnable (scenes/scripts/automations):\n")
        for eid, name in actions:
            print(f"  {name:<26} {eid}")
        print(f"\n{len(sensors)} temperature/humidity sensors:\n")
        for eid, name, dc, area in sensors:
            print(f"  {dc:<12} {area_name.get(area, '(no area)'):<16} {name:<26} {eid}")
        print("\nPick with --include, mark Home-page entries with --favourite.")
        print("Sensors are picked up automatically for whichever areas you use.\n")
        return

    include = set(x.strip() for x in args.include.split(",") if x.strip()) if args.include else None
    favourite = set(x.strip() for x in args.favourite.split(",") if x.strip())
    run_ids = set(x.strip() for x in args.automations.split(",") if x.strip()) if args.automations else None

    chosen = [r for r in controllable if include is None or r[0] in include]
    if include:
        missing = include - {r[0] for r in chosen}
        if missing:
            sys.exit(f"Not found or not controllable: {', '.join(sorted(missing))}")

    # Areas that survive, in first-seen order. Entities with no area are
    # dropped rather than invented into one.
    used_areas, dropped = [], []
    for eid, name, kind, area in chosen:
        if area is None:
            dropped.append(eid)
        elif area not in used_areas:
            used_areas.append(area)
    if dropped:
        print(f"note: {len(dropped)} entity(ies) have no area in HA and were skipped:")
        for d in dropped:
            print(f"      {d}")
        print("      assign an area in HA, or they cannot be placed on a room page.")
    used_areas = used_areas[:MAX_AREAS]

    rows, per_area = [], {}
    for eid, name, kind, area in chosen:
        if area not in used_areas:
            continue
        idx = used_areas.index(area)
        per_area.setdefault(idx, 0)
        if per_area[idx] >= MAX_PER_PAGE:
            print(f"note: {area_name[area]} page is full, skipping {eid}")
            continue
        per_area[idx] += 1
        rows.append((eid, name, kind, idx, eid in favourite))
        if len(rows) >= MAX_ENTITIES:
            break

    acts = [a for a in actions if run_ids is None or a[0] in run_ids][:MAX_AUTOMATIONS]

    # One temperature and one humidity sensor per area, chosen
    # automatically - the header has exactly two slots per room and
    # nothing to disambiguate a second candidate with. Areas with no
    # sensor are left out entirely; the panel renders those slots
    # blank rather than showing a made-up reading.
    sensor_rows = []
    for idx, area in enumerate(used_areas):
        for dc in ("temperature", "humidity"):
            match = next((s for s in sensors if s[3] == area and s[2] == dc), None)
            if match:
                sensor_rows.append((match[0], idx, dc == "humidity"))
        if not any(r[1] == idx for r in sensor_rows):
            print(f"note: {area_name[area]} has no temperature or humidity sensor")

    out = ["#pragma once", "",
           "// Generated by tools/ha_discover.py - do not edit by hand.",
           "// Regenerate after changing areas or entity selection in HA.",
           "",
           '#include "../src/panel_model.h"', "",
           f"#define HA_AREA_COUNT {len(used_areas)}",
           "static const char *HA_AREAS[] = {"]
    for a in used_areas:
        out.append(f"  {c_str(area_name[a])},")
    out += ["};", "",
            "struct HaEntityCfg {",
            "  const char *id;",
            "  const char *name;",
            "  EntityKind kind;",
            "  uint8_t area;",
            "  bool favourite;",
            "};", "",
            f"#define HA_ENTITY_COUNT {len(rows)}",
            "static const HaEntityCfg HA_ENTITIES[] = {"]
    for eid, name, kind, area, fav in rows:
        out.append(f"  {{ {c_str(eid)}, {c_str(name)}, EntityKind::{kind}, "
                   f"{area}, {'true' if fav else 'false'} }},")
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

    HEADER_OUT.write_text("\n".join(out), encoding="utf-8")
    print(f"\nWrote {HEADER_OUT}")
    print(f"  {len(used_areas)} areas, {len(rows)} entities "
          f"({sum(1 for r in rows if r[4])} favourites), {len(acts)} actions, "
          f"{len(sensor_rows)} sensors")


def main():
    ap = argparse.ArgumentParser(description="Generate the panel layout from Home Assistant.")
    ap.add_argument("--url", help="e.g. http://192.168.100.4:8123")
    ap.add_argument("--token", help="long-lived access token")
    ap.add_argument("--list", action="store_true", help="show what HA has and exit")
    ap.add_argument("--include", help="comma-separated entity_ids to put on the panel")
    ap.add_argument("--favourite", default="", help="comma-separated entity_ids for the Home page")
    ap.add_argument("--automations", help="comma-separated scene/script/automation entity_ids")
    asyncio.run(run(ap.parse_args()))


if __name__ == "__main__":
    main()
