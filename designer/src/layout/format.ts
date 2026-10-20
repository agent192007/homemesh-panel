// The layout format, ported from tools/ha_discover.py.
//
// ha_discover.py stays the reference: tests/format.test.ts runs both on
// the same inventory and requires byte-identical layout JSON and
// ha_layout.h. Two details matter for that:
//   - sorting compares strings by code unit (<, >), as Python's sort does,
//     never localeCompare - a different order moves tiles on the panel;
//   - JSON is compact with keys in insertion order, as dump_layout()
//     writes it, because those are the exact bytes the panel stores.

export const LAYOUT_VERSION = 1;
export const LAYOUT_EVENT = "homemesh_layout";
export const LAYOUT_MAX_JSON = 3072; // src/layout_store.h
export const MAX_AREAS = 6;
export const MAX_ENTITIES = 12;
export const MAX_AUTOMATIONS = 8;
export const MAX_PER_PAGE = 6;

const TOGGLE_DOMAINS = new Set(["switch", "fan", "input_boolean"]);
const ACTION_DOMAINS = new Set(["scene", "script", "automation"]);
const SENSOR_CLASSES = new Set(["temperature", "humidity"]);
const RGB_MODES = new Set(["hs", "rgb", "rgbw", "rgbww", "xy"]);
const DIM_MODES = new Set(["brightness", "color_temp", ...RGB_MODES]);

export const ICON_NAMES = [
  "light", "ceiling", "lamp", "strip", "fan", "ac", "toggle", "plug",
  "temp", "humidity", "tv", "speaker", "lock", "blinds", "home", "unknown",
] as const;

// Mirrors DOMAIN_ICONS in ha_discover.py and src/layout_store.cpp.
const DOMAIN_ICONS: Record<string, string> = {
  light: "light", fan: "fan", switch: "plug", input_boolean: "toggle",
  media_player: "tv", lock: "lock", cover: "blinds",
};

// Must match theme_defaults() in src/panel_theme.cpp. Order is the
// order the designer offers them.
export const THEME_DEFAULTS: Record<string, string> = {
  bg: "#101418", tile: "#2A2F36", on: "#F5A623", accent: "#4C8DFF",
  text: "#FFFFFF", muted: "#8A9099", ok: "#3DDC84", warn: "#F5A623", bad: "#E5484D",
};

export type Kind = "Toggle" | "Dimmable" | "RGB";

export interface HaState {
  entity_id: string;
  state: string;
  attributes: Record<string, any>;
}

export interface Controllable { id: string; name: string; kind: Kind; area: string | null }
export interface Action { id: string; name: string }
export interface Sensor { id: string; name: string; deviceClass: string; area: string | null }

export interface Inventory {
  controllable: Controllable[];
  actions: Action[];
  sensors: Sensor[];
  areaName: Record<string, string>;
}

// (eid, name, kind, area_index, favourite, icon) - the Python row shape.
export type Row = [string, string, Kind, number, boolean, string];
export type SensorRow = [string, number, boolean];
export interface Tables {
  areaNames: string[];
  rows: Row[];
  acts: [string, string][];
  sensorRows: SensorRow[];
  notes: string[];
}

export interface Layout {
  v: number;
  areas: string[];
  entities: { id: string; name: string; kind: Kind; area: number; fav: boolean; icon: string }[];
  actions: { id: string; name: string }[];
  sensors: { id: string; area: number; hum: boolean }[];
  theme?: Record<string, string>;
}

export function defaultIcon(entityId: string): string {
  return DOMAIN_ICONS[entityId.split(".", 1)[0]] ?? "unknown";
}

export function classify(s: HaState): Kind | null {
  const domain = s.entity_id.split(".", 1)[0];
  if (TOGGLE_DOMAINS.has(domain)) return "Toggle";
  if (domain === "light") {
    const modes: string[] = s.attributes?.supported_color_modes ?? [];
    if (modes.some((m) => RGB_MODES.has(m))) return "RGB";
    if (modes.some((m) => DIM_MODES.has(m))) return "Dimmable";
    return "Toggle";
  }
  return null;
}

// Python's tuple ordering on strings: code-unit comparison.
function cmp(a: (string | number)[], b: (string | number)[]): number {
  for (let i = 0; i < a.length; i++) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

export function buildInventory(
  states: HaState[],
  areas: { area_id: string; name: string }[],
  entReg: { entity_id: string; area_id?: string | null; device_id?: string | null }[],
  devReg: { id: string; area_id?: string | null }[],
): Inventory {
  const areaName: Record<string, string> = {};
  for (const a of areas) areaName[a.area_id] = a.name;
  const devArea: Record<string, string | null | undefined> = {};
  for (const d of devReg) devArea[d.id] = d.area_id;
  // Entity-level area wins over the device's, as in HA itself.
  const entArea: Record<string, string> = {};
  for (const e of entReg) {
    const a = e.area_id || (e.device_id ? devArea[e.device_id] : undefined);
    if (a) entArea[e.entity_id] = a;
  }

  const controllable: Controllable[] = [], actions: Action[] = [], sensors: Sensor[] = [];
  for (const s of states) {
    const eid = s.entity_id;
    const domain = eid.split(".", 1)[0];
    const attrs = s.attributes ?? {};
    const name: string = attrs.friendly_name || eid;
    if (ACTION_DOMAINS.has(domain)) { actions.push({ id: eid, name }); continue; }
    if (domain === "sensor") {
      const dc = attrs.device_class;
      if (SENSOR_CLASSES.has(dc)) sensors.push({ id: eid, name, deviceClass: dc, area: entArea[eid] ?? null });
      continue;
    }
    const kind = classify(s);
    if (kind) controllable.push({ id: eid, name, kind, area: entArea[eid] ?? null });
  }
  const an = (a: string | null) => (a !== null && a in areaName ? areaName[a] : "~");
  controllable.sort((x, y) => cmp([an(x.area), x.name], [an(y.area), y.name]));
  actions.sort((x, y) => cmp([x.name], [y.name]));
  sensors.sort((x, y) => cmp([an(x.area), x.deviceClass, x.name], [an(y.area), y.deviceClass, y.name]));
  return { controllable, actions, sensors, areaName };
}

export interface Selection {
  include: Set<string> | null;   // null = everything controllable
  favourite: Set<string>;
  automations: Set<string> | null;
  order: string[];
  icons: Record<string, string>;
  // entity_id -> name shown on the panel; blank means HA's friendly name.
  names?: Record<string, string>;
  // entity_id -> page name, overriding its HA area; blank means its area.
  // A page is only a name to the panel, so this places devices with no
  // area and makes pages that are not HA areas.
  pages?: Record<string, string>;
  // sensor entity_id -> page; "" hides it. Placed sensors win over ones
  // that are only in the page's area.
  sensorPages?: Record<string, string>;
}

// The page a device goes on: its override, else its HA area's name.
export function pageOf(inv: Inventory, sel: Pick<Selection, "pages">, id: string, area: string | null): string | null {
  const p = (sel.pages?.[id] ?? "").trim();
  if (p) return p;
  return area !== null && area in inv.areaName ? inv.areaName[area] : null;
}

export function sensorPageOf(inv: Inventory, sel: Pick<Selection, "sensorPages">, s: Sensor): string | null {
  if (sel.sensorPages && s.id in sel.sensorPages) return sel.sensorPages[s.id].trim() || null;
  return s.area !== null && s.area in inv.areaName ? inv.areaName[s.area] : null;
}

// Port of select_layout(). Notes are what the Python logs.
export function selectLayout(inv: Inventory, sel: Selection): Tables {
  const notes: string[] = [];
  let chosen = inv.controllable.filter((r) => sel.include === null || sel.include.has(r.id));
  if (sel.order.length) {
    const rank = new Map(sel.order.map((eid, i) => [eid, i] as const));
    // Stable: unranked keep their relative order after the ranked ones.
    chosen = [...chosen].sort((a, b) => (rank.get(a.id) ?? rank.size) - (rank.get(b.id) ?? rank.size));
  }
  // Pages by name, in first-seen order.
  const usedAreas: string[] = [];
  const dropped: string[] = [];
  for (const r of chosen) {
    const page = pageOf(inv, sel, r.id, r.area);
    if (page === null) dropped.push(r.id);
    else if (!usedAreas.includes(page)) usedAreas.push(page);
  }
  if (dropped.length) notes.push(`${dropped.length} selected device(s) have no room in HA — choose a page for them`);
  if (usedAreas.length > MAX_AREAS) notes.push(`the panel holds ${MAX_AREAS} pages besides Home; ${usedAreas.slice(MAX_AREAS).join(", ")} will not appear`);
  const areas = usedAreas.slice(0, MAX_AREAS);

  const rows: Row[] = [];
  const perArea = new Map<number, number>();
  for (const r of chosen) {
    const page = pageOf(inv, sel, r.id, r.area);
    if (page === null || !areas.includes(page)) continue;
    const idx = areas.indexOf(page);
    const n = perArea.get(idx) ?? 0;
    if (n >= MAX_PER_PAGE) { notes.push(`${page} page is full, skipping ${r.id}`); continue; }
    perArea.set(idx, n + 1);
    const name = (sel.names?.[r.id] ?? "").trim() || r.name;
    rows.push([r.id, name, r.kind, idx, sel.favourite.has(r.id), sel.icons[r.id] || defaultIcon(r.id)]);
    if (rows.length >= MAX_ENTITIES) break;
  }
  // Python stops here silently; the designer says so. Notes are not part
  // of the output, so the tables stay identical to the reference.
  const placeable = chosen.filter((r) => { const p = pageOf(inv, sel, r.id, r.area); return p !== null && areas.includes(p); }).length;
  if (placeable > MAX_ENTITIES) notes.push(`the panel holds ${MAX_ENTITIES} devices; ${placeable - MAX_ENTITIES} more are selected and will not appear`);
  const nFav = rows.filter((r) => r[4]).length;
  if (nFav > MAX_PER_PAGE) notes.push(`${nFav} favourites but the Home page holds ${MAX_PER_PAGE}`);

  const acts = inv.actions
    .filter((a) => sel.automations === null || sel.automations.has(a.id))
    .slice(0, MAX_AUTOMATIONS)
    .map((a) => [a.id, a.name] as [string, string]);

  const sensorRows: SensorRow[] = [];
  areas.forEach((page, idx) => {
    for (const dc of ["temperature", "humidity"]) {
      const fits = inv.sensors.filter((s) => s.deviceClass === dc && sensorPageOf(inv, sel, s) === page);
      const m = fits.find((s) => sel.sensorPages !== undefined && s.id in sel.sensorPages) ?? fits[0];
      if (m) sensorRows.push([m.id, idx, dc === "humidity"]);
    }
  });
  return { areaNames: [...areas], rows, acts, sensorRows, notes };
}

export function buildLayout(t: Tables, theme?: Record<string, string>): Layout {
  const out: Layout = {
    v: LAYOUT_VERSION,
    areas: [...t.areaNames],
    entities: t.rows.map(([id, name, kind, area, fav, icon]) => ({ id, name, kind, area, fav, icon })),
    actions: t.acts.map(([id, name]) => ({ id, name })),
    sensors: t.sensorRows.map(([id, area, hum]) => ({ id, area, hum })),
  };
  if (theme) {
    const changed: Record<string, string> = {};
    for (const [k, v] of Object.entries(theme)) {
      if (k in THEME_DEFAULTS && v.toUpperCase() !== THEME_DEFAULTS[k].toUpperCase()) changed[k] = v;
    }
    if (Object.keys(changed).length) out.theme = changed;
  }
  return out;
}

// The exact bytes the panel receives and stores.
export function dumpLayout(layout: Layout): string {
  return JSON.stringify(layout);
}

function cStr(s: string): string {
  return '"' + s.replace(/\\/g, "\\\\").replace(/"/g, '\\"') + '"';
}

// Port of build_header(): include/ha_layout.h, the compiled fallback.
export function buildHeader(t: Tables): string {
  const out = ["#pragma once", "",
    "// Generated by tools/ha_discover.py - do not edit by hand.",
    "// Regenerate after changing areas or entity selection in HA.", "",
    '#include "../src/panel_model.h"', "",
    "// Entities carry an icon name; layout_store.cpp checks this.",
    "#define HA_LAYOUT_HAS_ICONS 1", "",
    `#define HA_AREA_COUNT ${t.areaNames.length}`,
    "static const char *HA_AREAS[] = {"];
  for (const n of t.areaNames) out.push(`  ${cStr(n)},`);
  out.push("};", "", "struct HaEntityCfg {", "  const char *id;", "  const char *name;",
    "  EntityKind kind;", "  uint8_t area;", "  bool favourite;", "  const char *icon;", "};", "",
    `#define HA_ENTITY_COUNT ${t.rows.length}`, "static const HaEntityCfg HA_ENTITIES[] = {");
  for (const [eid, name, kind, area, fav, icon] of t.rows)
    out.push(`  { ${cStr(eid)}, ${cStr(name)}, EntityKind::${kind}, ${area}, ${fav ? "true" : "false"}, ${cStr(icon)} },`);
  out.push("};", "", "struct HaActionCfg {", "  const char *id;", "  const char *name;", "};", "",
    `#define HA_ACTION_COUNT ${t.acts.length}`, "static const HaActionCfg HA_ACTIONS[] = {");
  for (const [eid, name] of t.acts) out.push(`  { ${cStr(eid)}, ${cStr(name)} },`);
  if (!t.acts.length) out.push('  { "", "" },');
  out.push("};", "", "// Header-bar sensors, one temperature and one humidity per area.",
    "struct HaSensorCfg {", "  const char *id;", "  uint8_t area;", "  bool humidity;   // false = temperature",
    "};", "", `#define HA_SENSOR_COUNT ${t.sensorRows.length}`, "static const HaSensorCfg HA_SENSORS[] = {");
  for (const [eid, area, hum] of t.sensorRows) out.push(`  { ${cStr(eid)}, ${area}, ${hum ? "true" : "false"} },`);
  if (!t.sensorRows.length) out.push('  { "", 0, false },');
  out.push("};", "");
  return out.join("\n");
}
