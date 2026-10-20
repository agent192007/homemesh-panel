// The TypeScript layout format must match tools/ha_discover.py byte for
// byte: same layout JSON (what the panel stores) and same ha_layout.h
// (the compiled fallback). Python is the reference; this runs it on the
// same Home Assistant snapshot with its network calls replaced.
//
//   npm test
import { execFileSync } from "node:child_process";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import {
  buildHeader, buildInventory, buildLayout, dumpLayout, selectLayout, THEME_DEFAULTS,
} from "../src/layout/format.ts";

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, "..", "..");
const snap = JSON.parse(readFileSync(resolve(here, "fixtures", "ha_snapshot.json"), "utf8"));
const saved = JSON.parse(readFileSync(resolve(root, "include", "ha_layout.json"), "utf8"));

interface Case {
  name: string; include: string[] | null; favourite: string[]; automations: string[] | null;
  order: string[]; icons: Record<string, string>; theme: Record<string, string> | null;
  names?: Record<string, string>;
  pages?: Record<string, string>;
  sensorPages?: Record<string, string>;
}
const savedIds = saved.entities.map((e: any) => e.id);
const cases: Case[] = [
  { name: "everything controllable", include: null, favourite: ["light.bed_light"], automations: null,
    order: [], icons: {}, theme: null },
  { name: "the committed layout", include: savedIds,
    favourite: saved.entities.filter((e: any) => e.fav).map((e: any) => e.id), automations: [],
    order: savedIds, icons: Object.fromEntries(saved.entities.map((e: any) => [e.id, e.icon])),
    theme: { ...THEME_DEFAULTS, bg: "#000000" } },
  { name: "reversed order, custom icons", include: savedIds, favourite: [], automations: [],
    order: [...savedIds].reverse(), icons: { "switch.ac": "ac" }, theme: null },
  { name: "custom names, with quotes, backslash and blanks", include: savedIds, favourite: [], automations: [],
    order: savedIds, icons: {}, theme: null,
    names: { "light.living_room_rgbww_lights": "Strip", "switch.ac": 'Lamp "Big" \ 2',
             "light.bed_light": "   ", "light.ceiling_lights": "  Ceiling  " } },
  { name: "every automation", include: savedIds.slice(0, 2), favourite: [], automations: null,
    order: [], icons: {}, theme: null },
  { name: "devices with no room placed, a custom page, a moved device", favourite: ["light.office_rgbw_lights"],
    include: [...savedIds, "light.office_rgbw_lights", "light.entrance_color_white_lights", "fan.percentage_full_fan"],
    automations: [], order: [], icons: {}, theme: null,
    pages: { "light.office_rgbw_lights": "Office", "light.entrance_color_white_lights": " Entrance ",
             "switch.ac": "Office", "light.bed_light": "  " } },
  { name: "sensors moved, hidden and placed on a custom page", include: savedIds, favourite: [], automations: [],
    order: [], icons: {}, theme: null,
    pages: { "light.kitchen_lights": "Downstairs" },
    sensorPages: { "sensor.outside_temperature": "Downstairs", "sensor.outside_humidity": "" } },
  { name: "a renamed room page", include: savedIds, favourite: [], automations: [], order: savedIds, icons: {}, theme: null,
    pages: { "light.ceiling_lights": "Lounge", "light.living_room_rgbww_lights": "Lounge" },
    sensorPages: { "sensor.outside_temperature": "Lounge", "sensor.outside_humidity": "Lounge" } },
];

const py = String.raw`
import asyncio, json, sys
sys.path.insert(0, sys.argv[1])
import ha_discover as hd
snap = json.load(open(sys.argv[2], encoding="utf-8"))
cases = json.load(sys.stdin)
class FakeWS:
    async def close(self): pass
async def fake_connect(url, token): return FakeWS()
REPLY = {"get_states": "states", "config/area_registry/list": "areas",
         "config/entity_registry/list": "entity_registry", "config/device_registry/list": "device_registry"}
async def fake_cmd(self, t, **kw): return snap[REPLY[t]]
hd.connect = fake_connect
hd.HA.cmd = fake_cmd
inv = asyncio.run(hd.fetch_inventory("x", "x"))
out = []
for c in cases:
    t = hd.select_layout(inv, include=None if c["include"] is None else set(c["include"]),
                         favourite=set(c["favourite"]),
                         automations=None if c["automations"] is None else set(c["automations"]),
                         order=c["order"], icons=c["icons"], names=c.get("names"),
                         pages=c.get("pages"), sensor_pages=c.get("sensorPages"), log=lambda *a: None)
    out.append({"json": hd.dump_layout(hd.build_layout(*t, theme=c["theme"])), "header": hd.build_header(*t)})
print(json.dumps(out))
`;
const ref = JSON.parse(execFileSync("python", ["-c", py, resolve(root, "tools"), resolve(here, "fixtures", "ha_snapshot.json")],
  { input: JSON.stringify(cases), encoding: "utf8", env: { ...process.env, PYTHONIOENCODING: "utf-8" } }));

const inv = buildInventory(snap.states, snap.areas, snap.entity_registry, snap.device_registry);
let fails = 0;
cases.forEach((c, i) => {
  const t = selectLayout(inv, {
    include: c.include === null ? null : new Set(c.include), favourite: new Set(c.favourite),
    automations: c.automations === null ? null : new Set(c.automations), order: c.order, icons: c.icons,
    names: c.names, pages: c.pages, sensorPages: c.sensorPages,
  });
  const json = dumpLayout(buildLayout(t, c.theme ?? undefined));
  const header = buildHeader(t);
  for (const [what, mine, theirs] of [["layout JSON", json, ref[i].json], ["ha_layout.h", header, ref[i].header]]) {
    const ok = mine === theirs;
    if (!ok) fails++;
    console.log(`  ${ok ? "PASS" : "FAIL"}  ${c.name}: ${what} (${mine.length} B)`);
    if (!ok) {
      const at = [...mine].findIndex((ch, k) => ch !== theirs[k]);
      console.log(`        differs at ${at}: TS ${JSON.stringify(mine.slice(at, at + 60))}`);
      console.log(`                      PY ${JSON.stringify(theirs.slice(at, at + 60))}`);
    }
  }
});
console.log(fails ? `\n${fails} FAILED` : "\nALL PASSED");
process.exit(fails ? 1 : 0);
