import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { bridge } from "./bridge";
import { HaClient } from "./ha/haClient";
import {
  buildHeader, buildInventory, buildLayout, dumpLayout, Inventory, Layout, LAYOUT_EVENT, LAYOUT_MAX_JSON,
  MAX_AREAS, MAX_AUTOMATIONS, MAX_ENTITIES, MAX_PER_PAGE, pageOf, selectLayout, sensorPageOf, THEME_DEFAULTS,
} from "./layout/format";
import { presetOf, THEME_PRESETS } from "./layout/themes";
import { Glyph, IconPicker } from "./IconPicker";
import { fitName, nameBox } from "./layout/nameFit";
import { Ev, Link, SimEvent } from "./sim/events";
import { PanelSimulator, PanelSimulatorHandle } from "./sim/PanelSimulator";

type Status = { kind: "idle" | "busy" | "ok" | "error"; text: string };
type Mode = "system" | "dark" | "light";
interface Sel {
  include: Set<string>; favourite: Set<string>; order: string[];
  icons: Record<string, string>; names: Record<string, string>;
  automations: Set<string>;   // scenes, scripts, automations on the Automations screen
  // Page overrides: a page is only a name to the panel. Absent means the
  // device's (or sensor's) HA room; a sensor set to "" is hidden.
  pages: Record<string, string>;
  sensorPages: Record<string, string>;
  extraPages: string[];       // pages made here that have no device yet
  actionColors: Record<string, string>;   // automation -> "#RRGGBB" button colour
}

function selFromLayout(l: Layout): Sel {
  return {
    include: new Set(l.entities.map((e) => e.id)),
    favourite: new Set(l.entities.filter((e) => e.fav).map((e) => e.id)),
    order: l.entities.map((e) => e.id),
    icons: Object.fromEntries(l.entities.map((e) => [e.id, e.icon])),
    // Every saved name and page to start with; the ones that merely
    // repeat what HA says are dropped once the inventory arrives.
    names: Object.fromEntries(l.entities.map((e) => [e.id, e.name])),
    automations: new Set(l.actions.map((a) => a.id)),
    pages: Object.fromEntries(l.entities.map((e) => [e.id, l.areas[e.area]])),
    sensorPages: Object.fromEntries(l.sensors.map((s) => [s.id, l.areas[s.area]])),
    extraPages: [],
    actionColors: Object.fromEntries(l.actions.filter((a) => a.color).map((a) => [a.id, a.color!])),
  };
}

// Once HA's inventory is known: keep only what differs from it, so a
// saved layout comes back as the same choices rather than as pins.
function normalise(s: Sel, inv: Inventory, saved: Layout | null): Sel {
  const friendly = new Map(inv.controllable.map((r) => [r.id, r.name]));
  const room = new Map(inv.controllable.map((r) => [r.id, r.area !== null ? inv.areaName[r.area] : null]));
  const pages = Object.fromEntries(Object.entries(s.pages).filter(([id, p]) => p !== room.get(id)));
  const sensorPages: Record<string, string> = {};
  for (const sn of inv.sensors) {
    const haRoom = sn.area !== null ? inv.areaName[sn.area] : null;
    if (sn.id in s.sensorPages) {
      if (s.sensorPages[sn.id] !== haRoom) sensorPages[sn.id] = s.sensorPages[sn.id];
    } else if (saved && haRoom && saved.areas.includes(haRoom)) {
      // Its room is a page but the saved layout did not show it: hidden.
      sensorPages[sn.id] = "";
    }
  }
  return {
    ...s, pages, sensorPages,
    names: Object.fromEntries(Object.entries(s.names).filter(([id, n]) => n !== friendly.get(id))),
  };
}
interface LiveState { s?: string; brightness?: number | null; rgb?: number[] | null }

const THEME_LABELS: Record<string, string> = {
  bg: "Background", tile: "Tile", on: "On", accent: "Accent", text: "Text",
  muted: "Muted", ok: "Link ok", warn: "Link warn", bad: "Link down",
};

function useDebounced<T>(value: T, ms: number): T {
  const [v, setV] = useState(value);
  useEffect(() => { const t = setTimeout(() => setV(value), ms); return () => clearTimeout(t); }, [value, ms]);
  return v;
}

export default function App() {
  const [url, setUrl] = useState(() => localStorage.getItem("ha_url") ?? "http://localhost:8123");
  const [token, setToken] = useState("");
  const [status, setStatus] = useState<Status>({ kind: "idle", text: "not connected" });
  const [client, setClient] = useState<HaClient | null>(null);
  const [inv, setInv] = useState<Inventory | null>(null);
  const [sel, setSel] = useState<Sel>({ include: new Set(), favourite: new Set(), order: [], icons: {}, names: {},
    automations: new Set(), pages: {}, sensorPages: {}, extraPages: [], actionColors: {} });
  const [newPage, setNewPage] = useState("");
  const [theme, setTheme] = useState<Record<string, string>>({ ...THEME_DEFAULTS });
  const [saved, setSaved] = useState<Layout | null>(null);
  // The layout as last written to the project, for "unsaved changes".
  const [baseline, setBaseline] = useState<string | null>(null);
  const [mode, setMode] = useState<Mode>(() => (localStorage.getItem("app_mode") as Mode) || "system");
  const [project, setProject] = useState<string | null | undefined>(undefined);
  const [live, setLive] = useState(false);
  const [search, setSearch] = useState("");
  const [logs, setLogs] = useState<string[]>([]);
  const [verdict, setVerdict] = useState<{ ok: boolean; reason: string } | null>(null);
  const [toast, setToast] = useState<Status | null>(null);
  const sim = useRef<PanelSimulatorHandle>(null);
  const liveStates = useRef<Map<string, LiveState>>(new Map());
  const designStates = useRef<Map<number, SimEvent[]>>(new Map());
  const savedRef = useRef<Layout | null>(null);

  const log = useCallback((line: string) => setLogs((l) => [...l.slice(-300), line]), []);
  const flash = (s: Status) => { setToast(s); setTimeout(() => setToast(null), 4000); };

  // ---------------------------------------------------------------- start
  useEffect(() => {
    (async () => {
      setProject(await bridge.projectRoot());
      const env = await bridge.loadEnv();
      if (env.url) setUrl(env.url);
      if (env.token) setToken(env.token);
      const raw = await bridge.loadLayout();
      if (raw) {
        const l: Layout = JSON.parse(raw);
        setSaved(l);
        savedRef.current = l;
        setBaseline(dumpLayout(l));
        setSel(selFromLayout(l));
        setTheme({ ...THEME_DEFAULTS, ...(l.theme ?? {}) });
      }
      if (env.url && env.token) connect(env.url, env.token);
    })();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  async function connect(u = url, t = token) {
    client?.close();
    setStatus({ kind: "busy", text: "connecting…" });
    try {
      const c = await HaClient.connect(u, t, () => { setClient(null); setLive(false); setStatus({ kind: "error", text: "disconnected" }); });
      const [states, areas, ents, devs] = await Promise.all([
        c.cmd("get_states"), c.cmd("config/area_registry/list"),
        c.cmd("config/entity_registry/list"), c.cmd("config/device_registry/list"),
      ]);
      const inventory = buildInventory(states, areas, ents, devs);
      // A saved name or page equal to what HA says is not a choice.
      setSel((s) => normalise(s, inventory, savedRef.current));
      localStorage.setItem("ha_url", u);
      setClient(c);
      setInv(inventory);
      setStatus({ kind: "ok", text: `connected · ${inventory.controllable.length} controllable, ${inventory.sensors.length} sensors` });
    } catch (e) {
      setStatus({ kind: "error", text: (e as Error).message });
    }
  }

  // ----------------------------------------------------------- the layout
  const tables = useMemo(() => inv ? selectLayout(inv, {
    include: sel.include, favourite: sel.favourite, automations: sel.automations, order: sel.order,
    icons: sel.icons, names: sel.names, pages: sel.pages, sensorPages: sel.sensorPages,
  }) : null, [inv, sel]);
  const layout: Layout | null = useMemo(() => tables ? buildLayout(tables, theme, sel.actionColors) : saved,
    [tables, theme, saved, sel.actionColors]);
  const json = useMemo(() => (layout ? dumpLayout(layout) : ""), [layout]);
  const simJson = useDebounced(json, 250);
  const bytes = new TextEncoder().encode(json).length;
  const dirty = baseline !== null && json !== "" && json !== baseline;
  const preset = presetOf(theme);
  const friendlyName = useMemo(() => new Map((inv?.controllable ?? []).map((r) => [r.id, r.name])), [inv]);

  // The app's own colour scheme: follows Windows unless chosen.
  useEffect(() => {
    localStorage.setItem("app_mode", mode);
    const mq = window.matchMedia("(prefers-color-scheme: light)");
    const apply = () => { document.documentElement.dataset.theme = mode === "system" ? (mq.matches ? "light" : "dark") : mode; };
    apply();
    mq.addEventListener("change", apply);
    return () => mq.removeEventListener("change", apply);
  }, [mode]);

  // State the network task would deliver after connecting; replayed into
  // every fresh simulator instance.
  const bootEvents = useCallback((): SimEvent[] => {
    if (!layout) return [];
    const ev: SimEvent[] = [{ type: Ev.Link, entity: 0, value: Link.Online }, { type: Ev.DevicesAvailable, entity: 0, value: 1 }];
    layout.entities.forEach((e, i) => {
      if (live) ev.push(...stateEvents(i, liveStates.current.get(e.id)));
      else ev.push(...(designStates.current.get(i) ?? [{ type: Ev.Power, entity: i, value: 0 }]));
    });
    if (live) layout.sensors.forEach((s) => {
      const v = parseFloat(liveStates.current.get(s.id)?.s ?? "");
      if (!Number.isNaN(v)) ev.push({ type: s.hum ? Ev.Humidity : Ev.Temperature, entity: s.area, value: 0, fvalue: v });
    });
    return ev;
  }, [layout, live]);

  // ---------------------------------------------------------------- live
  useEffect(() => {
    if (!live || !client || !layout) return;
    const ids = [...layout.entities.map((e) => e.id), ...layout.sensors.map((s) => s.id)];
    let unsub: (() => void) | null = null;
    client.subscribe("subscribe_entities", { entity_ids: ids }, (event) => {
      const apply = (eid: string, body: any) => {
        if (!body) return;
        const st = { ...(liveStates.current.get(eid) ?? {}) };
        if (body.s !== undefined) st.s = body.s;
        if (body.a && "brightness" in body.a) st.brightness = body.a.brightness;
        // A fan's speed, 0-100 and null while off, as the firmware maps it.
        const pct = eid.startsWith("fan.") && body.a ? body.a.percentage : undefined;
        if (pct > 0) st.brightness = Math.min(255, Math.max(1, Math.round(pct * 255 / 100)));
        if (body.a && "rgb_color" in body.a) st.rgb = body.a.rgb_color;
        liveStates.current.set(eid, st);
        const i = layout.entities.findIndex((e) => e.id === eid);
        const level = pct > 0 ? st.brightness : eid.startsWith("fan.") ? undefined : body.a?.brightness;
        if (i >= 0) for (const e of stateEvents(i, { s: body.s, brightness: level, rgb: body.a?.rgb_color })) sim.current?.post(e);
        layout.sensors.forEach((s) => {
          const v = parseFloat(body.s);
          if (s.id === eid && !Number.isNaN(v)) sim.current?.post({ type: s.hum ? Ev.Humidity : Ev.Temperature, entity: s.area, value: 0, fvalue: v });
        });
      };
      for (const [eid, body] of Object.entries(event.a ?? {})) apply(eid, body);
      for (const [eid, body] of Object.entries(event.c ?? {})) apply(eid, (body as any)["+"]);
    }).then((u) => { unsub = u; }).catch((e) => log(`[designer] live subscribe failed: ${e.message}`));
    return () => { unsub?.(); };
  }, [live, client, simJson]); // eslint-disable-line react-hooks/exhaustive-deps

  // A tap on the simulated panel. Live: the same service calls the
  // firmware makes. Design: echoed straight back, like env:demo.
  const onCommand = useCallback((kind: number, index: number, value: number) => {
    if (!layout) return;
    if (kind === Ev.AutomationRan) {
      const act = layout.actions[index];
      if (live && client && act) {
        const domain = act.id.split(".")[0];
        // As ha_client.cpp: scenes and scripts are turned on, automations triggered.
        client.cmd("call_service", { domain, service: domain === "automation" ? "trigger" : "turn_on", target: { entity_id: act.id } });
      }
      sim.current?.post({ type: Ev.AutomationRan, entity: index, value: 1 });
      return;
    }
    if (live && client) {
      const ent = layout.entities[index];
      if (!ent) return;
      const domain = ent.id.split(".")[0];
      const target = { entity_id: ent.id };
      if (kind === Ev.Power) client.cmd("call_service", { domain, service: value ? "turn_on" : "turn_off", target });
      else if (kind === Ev.Brightness && domain === "fan")
        client.cmd("call_service", { domain, service: "set_percentage", target, service_data: { percentage: Math.max(1, Math.round(value * 100 / 255)) } });
      else if (kind === Ev.Brightness) client.cmd("call_service", { domain, service: "turn_on", target, service_data: { brightness: value } });
      else if (kind === Ev.Color) client.cmd("call_service", { domain, service: "turn_on", target, service_data: { rgb_color: [(value >> 16) & 255, (value >> 8) & 255, value & 255] } });
      return;
    }
    const e = { type: kind, entity: index, value };
    sim.current?.post(e);
    const kept = (designStates.current.get(index) ?? []).filter((x) => x.type !== kind);
    designStates.current.set(index, [...kept, e]);
  }, [layout, live, client]);

  // ------------------------------------------------------------- actions
  // Push also saves: the project files are what the designer opens with
  // next time, and what a reflash falls back to, so they should always
  // hold the layout that is on the panel. Save on its own stays, for
  // keeping work without sending it.
  async function push() {
    if (!client || !layout) return;
    try {
      let saved = "";
      if (tables && project) {
        await bridge.saveLayout(json, buildHeader(tables));
        setBaseline(json);
        setSaved(layout);
        savedRef.current = layout;
        saved = " Saved to the project too.";
      }
      await client.cmd("fire_event", { event_type: LAYOUT_EVENT, event_data: { layout } });
      flash({ kind: "ok", text: `Pushed ${bytes} bytes; the panel restarts into it.${saved}` });
    } catch (e) { flash({ kind: "error", text: (e as Error).message }); }
  }
  async function save() {
    if (!tables || !layout) return;
    try {
      const where = await bridge.saveLayout(json, buildHeader(tables));
      setBaseline(json);
      setSaved(layout);
      savedRef.current = layout;
      flash({ kind: "ok", text: `Wrote ha_layout.json and ha_layout.h in ${where}` });
    } catch (e) { flash({ kind: "error", text: (e as Error).message }); }
  }
  function revert() {
    if (!saved) return;
    const s = selFromLayout(saved);
    setSel(inv ? normalise(s, inv, saved) : s);
    setTheme({ ...THEME_DEFAULTS, ...(saved.theme ?? {}) });
  }

  // Move a room page up or down. Pages follow the order rooms first
  // appear in the entity order, so this moves the room's whole block.
  function moveRoom(areaIndex: number, delta: -1 | 1) {
    if (!layout) return;
    const blocks = layout.areas.map((_, i) => layout.entities.filter((e) => e.area === i).map((e) => e.id));
    const to = areaIndex + delta;
    if (to < 0 || to >= blocks.length) return;
    [blocks[areaIndex], blocks[to]] = [blocks[to], blocks[areaIndex]];
    setSel((s) => ({ ...s, order: blocks.flat() }));
  }

  // --------------------------------------------------------------- pages
  // A page is only a name to the panel: a device's page is its override,
  // else its HA room. These keep the override map minimal, so a device
  // put back in its own room carries no override.
  const haRoom = useMemo(() => new Map([
    ...(inv?.controllable ?? []).map((r) => [r.id, r.area !== null ? inv!.areaName[r.area] : null] as const),
    ...(inv?.sensors ?? []).map((r) => [r.id, r.area !== null ? inv!.areaName[r.area] : null] as const),
  ]), [inv]);
  const haRoomNames = useMemo(() => new Set(Object.values(inv?.areaName ?? {})), [inv]);
  const deviceArea = (id: string) => inv?.controllable.find((r) => r.id === id)?.area ?? null;
  const pageOfDevice = (id: string) => (inv ? pageOf(inv, sel, id, deviceArea(id)) : null);

  function withPage(s: Sel, id: string, page: string): Sel {
    const pages = { ...s.pages };
    if (!page || page === haRoom.get(id)) delete pages[id]; else pages[id] = page;
    // A page made here stays until removed, even once it is emptied.
    return { ...s, pages };
  }
  function setSensorPage(id: string, page: string) {
    setSel((s) => {
      const sensorPages = { ...s.sensorPages };
      const room = haRoom.get(id) ?? null;
      if (page === (room ?? "")) delete sensorPages[id]; else sensorPages[id] = page;
      return { ...s, sensorPages };
    });
  }
  function renamePage(from: string, to: string) {
    to = to.trim();
    if (!to || to === from) return;
    setSel((s) => {
      let n: Sel = { ...s, extraPages: s.extraPages.map((p) => (p === from ? to : p)) };
      for (const r of inv?.controllable ?? []) if (pageOf(inv!, s, r.id, r.area) === from) n = withPage(n, r.id, to);
      const sensorPages = { ...n.sensorPages };
      for (const sn of inv?.sensors ?? []) {
        if (sensorPageOf(inv!, s, sn) !== from) continue;
        if (to === haRoom.get(sn.id)) delete sensorPages[sn.id]; else sensorPages[sn.id] = to;
      }
      return { ...n, sensorPages };
    });
  }
  // Only pages that are not HA rooms can be removed; their devices go back
  // to their own rooms, or off the panel if they have none.
  function removePage(name: string) {
    setSel((s) => ({
      ...s,
      extraPages: s.extraPages.filter((p) => p !== name),
      pages: Object.fromEntries(Object.entries(s.pages).filter(([, p]) => p !== name)),
      sensorPages: Object.fromEntries(Object.entries(s.sensorPages).filter(([, p]) => p !== name)),
    }));
  }
  const pageNameProblem = (name: string, except?: string): string | null => {
    const n = name.trim();
    if (!n) return "Give the page a name";
    if (n === "Home" || n === "Automations") return `"${n}" is already a screen on the panel`;
    if (n !== except && [...(layout?.areas ?? []), ...sel.extraPages].includes(n)) return "There is already a page with that name";
    const missing = fitName(n).missing;
    if (missing.length) return `The panel's font has no ${missing.map((m) => `"${m}"`).join(" ")}`;
    return null;
  };
  function addPage() {
    const n = newPage.trim();
    if (pageNameProblem(n)) return;
    setSel((s) => ({ ...s, extraPages: [...s.extraPages, n] }));
    setNewPage("");
  }
  // Header room for a page title: the title has no width limit on the
  // panel, so a long one runs into the sensor reading (create_header()).
  function titleProblem(name: string, hasSensors: boolean): string | null {
    const w = sim.current?.textWidth(name, 20);
    if (w == null) return null;
    const room = hasSensors ? 282 - (sim.current?.textWidth("88.8°C  88%", 14) ?? 80) : 288;
    if (w <= room) return null;
    return hasSensors ? "Too long for the header: it will run into the temperature" : "Too long for the header: it will run off the screen";
  }

  // -------------------------------------------------------------- picker
  const groups = useMemo(() => {
    if (!inv) return [];
    const by = new Map<string, typeof inv.controllable>();
    for (const r of inv.controllable) {
      if (search && !(r.name + r.id).toLowerCase().includes(search.toLowerCase())) continue;
      const k = r.area ?? "";
      by.set(k, [...(by.get(k) ?? []), r]);
    }
    return [...by.entries()].map(([area, rows]) => ({ area, name: area ? inv.areaName[area] : "(no area)", rows }));
  }, [inv, search]);

  const toggle = (set: Set<string>, id: string) => { const n = new Set(set); n.has(id) ? n.delete(id) : n.add(id); return n; };
  const pages = layout ? [
    { name: "Home", ents: layout.entities.filter((e) => e.fav), area: -1 },
    ...layout.areas.map((a, i) => ({ name: a, ents: layout.entities.filter((e) => e.area === i), area: i })),
    ...sel.extraPages.filter((p) => !layout.areas.includes(p)).map((p) => ({ name: p, ents: [] as Layout["entities"], area: -1 })),
  ] : [];
  const pageCount = pages.length - 1;
  // Every page a device can be put on: the layout's, the ones made here,
  // and every HA room.
  const pageChoices = useMemo(() => [...new Set([...(layout?.areas ?? []), ...sel.extraPages,
    ...[...haRoomNames].sort()])], [layout, sel.extraPages, haRoomNames]);

  // Drag on the Pages card: within a page it reorders (tile position is
  // order within the page); onto another page it moves the device there.
  const dragId = useRef<string | null>(null);
  const dropBefore = (targetId: string) => {
    const from = dragId.current;
    dragId.current = null;
    if (!from || from === targetId || !layout) return;
    const order = layout.entities.map((e) => e.id).filter((id) => id !== from);
    order.splice(order.indexOf(targetId), 0, from);
    const target = pageOfDevice(targetId);
    setSel((s) => {
      const n = target && target !== pageOfDevice(from) ? withPage(s, from, target) : s;
      return { ...n, order };
    });
  };
  const dropOnPage = (page: string) => {
    const from = dragId.current;
    dragId.current = null;
    if (!from || !layout || pageOfDevice(from) === page) return;
    // After the page's last tile, or at the end for an empty page.
    const order = layout.entities.map((e) => e.id).filter((id) => id !== from);
    const last = [...order].reverse().find((id) => pageOfDevice(id) === page);
    order.splice(last ? order.indexOf(last) + 1 : order.length, 0, from);
    setSel((s) => ({ ...withPage(s, from, page), order }));
  };

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">HomeMesh <span>Designer</span></div>
        <input className="url" value={url} onChange={(e) => setUrl(e.target.value)} placeholder="http://localhost:8123" />
        <input className="token" type="password" value={token} onChange={(e) => setToken(e.target.value)} placeholder="long-lived access token" />
        <button onClick={() => connect()}>{client ? "Reconnect" : "Connect"}</button>
        <span className={`status ${status.kind}`}>{status.text}</span>
        <div className="mode" role="group" aria-label="App appearance">
          {(["system", "dark", "light"] as Mode[]).map((m) => (
            <button key={m} className={mode === m ? "active" : ""} onClick={() => setMode(m)}>
              {m === "system" ? "Auto" : m === "dark" ? "Dark" : "Light"}
            </button>
          ))}
        </div>
      </header>

      <main className="cols">
        <section className="col picker">
          <div className="col-head">
            <h2>Entities</h2>
            <input className="search" value={search} onChange={(e) => setSearch(e.target.value)} placeholder="Search" />
          </div>
          {!inv && <p className="hint">Connect to Home Assistant to choose what the panel shows. Until then the simulator runs the saved layout.</p>}
          {groups.map((g) => (
            <div key={g.area} className="group">
              <h3>{g.name}{!g.area && <em className="soft"> · no room in HA — choose a page to place one</em>}</h3>
              {g.rows.map((r) => {
                const on = sel.include.has(r.id);
                const page = pageOfDevice(r.id);
                const needsPage = on && !page;
                return (
                  <div key={r.id} className={`ent ${on ? "on" : ""}`}>
                    <input type="checkbox" checked={on} disabled={!on && sel.include.size >= MAX_ENTITIES}
                      title={!on && sel.include.size >= MAX_ENTITIES ? `The panel holds ${MAX_ENTITIES} devices` : undefined}
                      onChange={() => setSel((s) => ({ ...s, include: toggle(s.include, r.id), order: s.order.includes(r.id) ? s.order : [...s.order, r.id] }))} />
                    <button className={`star ${sel.favourite.has(r.id) ? "fav" : ""}`} title="Show on the Home page"
                      onClick={() => setSel((s) => ({ ...s, favourite: toggle(s.favourite, r.id) }))}>★</button>
                    <div className="ent-name"><div>{r.name}</div><code>{r.id} · <span className={`kind ${r.kind}`}>{r.kind}</span></code></div>
                    <select className={`page-pick ${needsPage ? "needs" : ""} ${sel.pages[r.id] ? "custom" : ""}`} value={page ?? ""}
                      title={sel.pages[r.id] ? `Moved here. Its room in Home Assistant: ${haRoom.get(r.id) ?? "none"}` : "The page this device goes on"}
                      onChange={(e) => { const v = e.target.value; setSel((s) => withPage(s, r.id, v)); }}>
                      {!page && <option value="">Choose page…</option>}
                      {pageChoices.map((p) => <option key={p} value={p}>{p}</option>)}
                    </select>
                    <IconPicker entityId={r.id} value={sel.icons[r.id] ?? ""}
                      onChange={(icon) => setSel((s) => ({ ...s, icons: { ...s.icons, [r.id]: icon } }))} />
                  </div>
                );
              })}
            </div>
          ))}
          {inv && inv.sensors.length > 0 && (
            <div className="group">
              <h3>Header sensors <em className="soft">· one temperature and one humidity per page</em></h3>
              {inv.sensors.map((sn) => {
                const page = sensorPageOf(inv, sel, sn);
                const shown = layout?.sensors.some((x) => x.id === sn.id);
                return (
                  <div key={sn.id} className="ent sensor-row">
                    <div className="ent-name">
                      <div>{sn.name}</div>
                      <code>{sn.id} · {sn.deviceClass}{page && !shown ? ` · not used: ${page} has another ${sn.deviceClass} sensor, or no devices` : ""}</code>
                    </div>
                    <select className={`page-pick ${sn.id in sel.sensorPages ? "custom" : ""}`} value={page ?? ""}
                      onChange={(e) => setSensorPage(sn.id, e.target.value)}>
                      <option value="">Not shown</option>
                      {pageChoices.map((p) => <option key={p} value={p}>{p}</option>)}
                    </select>
                  </div>
                );
              })}
            </div>
          )}
          {inv && (
            <div className="group autos">
              <h3>Automations screen <em className="soft">· swipe up on the panel · {sel.automations.size}/{MAX_AUTOMATIONS}</em></h3>
              {inv.actions.length === 0 && <p className="hint">No scenes, scripts or automations in Home Assistant.</p>}
              {inv.actions.map((a) => {
                const colour = sel.actionColors[a.id];
                return (
                  <div key={a.id} className="ent auto-row">
                    <input type="checkbox" checked={sel.automations.has(a.id)}
                      disabled={!sel.automations.has(a.id) && sel.automations.size >= MAX_AUTOMATIONS}
                      onChange={() => setSel((s) => ({ ...s, automations: toggle(s.automations, a.id) }))} />
                    <div className="ent-name"><div>{a.name}</div><code>{a.id}</code></div>
                    <span className="auto-colour">
                      <input type="color" value={colour ?? "#2A2F36"} title="This automation's button colour on the panel"
                        className={colour ? "set" : ""}
                        onChange={(e) => { const v = e.target.value.toUpperCase(); setSel((s) => ({ ...s, actionColors: { ...s.actionColors, [a.id]: v } })); }} />
                      {colour
                        ? <button className="ghost small" title="Back to a plain button"
                            onClick={() => setSel((s) => { const c = { ...s.actionColors }; delete c[a.id]; return { ...s, actionColors: c }; })}>✕</button>
                        : <span className="soft">plain</span>}
                    </span>
                  </div>
                );
              })}
            </div>
          )}
        </section>

        <section className="col center">
          <div className="sim-head">
            <h2>Panel</h2>
            <span className="sim-note">the firmware's own LVGL UI, running as WebAssembly · drag to swipe pages</span>
            <label className={`live ${live ? "on" : ""}`} title={client ? "" : "connect first"}>
              <input type="checkbox" checked={live} disabled={!client} onChange={(e) => setLive(e.target.checked)} />
              Live states
            </label>
          </div>
          {simJson
            ? <PanelSimulator ref={sim} layoutJson={simJson} bootEvents={bootEvents} onCommand={onCommand}
                onLog={log} onLayoutResult={(ok, reason) => setVerdict({ ok, reason })} />
            : <div className="sim-empty">No layout yet</div>}
          <p className="sim-help">{live
            ? "Live: states come from Home Assistant, and taps really switch the devices."
            : "Design: taps change the simulator only. Turn on Live states to drive real devices."}</p>
          <div className="log">
            <div className="log-head">Panel serial log</div>
            <pre>{logs.join("\n")}</pre>
          </div>
        </section>

        <section className="col side">
          <div className="card">
            <h2>Pages</h2>
            <p className="hint">Drag tiles to reorder, or onto another page to move them; arrows move a whole page. Click a page or device name to rename it — clear a device's name to use Home Assistant's.</p>
            {pages.map((p, pi) => {
              const custom = pi > 0 && !haRoomNames.has(p.name);
              const title = pi > 0 ? titleProblem(p.name, layout?.sensors.some((s) => s.area === p.area) ?? false) : null;
              return (
              <div key={p.name + pi} className="page" onDragOver={(ev) => { if (pi > 0) ev.preventDefault(); }}
                onDrop={(ev) => { if (pi > 0 && ev.target === ev.currentTarget) dropOnPage(p.name); }}>
                <h3 onDragOver={(ev) => { if (pi > 0) ev.preventDefault(); }} onDrop={() => { if (pi > 0) dropOnPage(p.name); }}>
                  {pi === 0
                    ? <span className="page-name">{p.name}</span>
                    : <input key={p.name} className={`page-name page-edit ${custom ? "custom" : ""}`} defaultValue={p.name}
                        title={custom ? "A page made here" : "A room in Home Assistant — rename it for the panel"}
                        onKeyDown={(ev) => { if (ev.key === "Enter") ev.currentTarget.blur(); if (ev.key === "Escape") { ev.currentTarget.value = p.name; ev.currentTarget.blur(); } }}
                        onBlur={(ev) => { const v = ev.currentTarget.value; if (pageNameProblem(v, p.name)) ev.currentTarget.value = p.name; else renamePage(p.name, v); }} />}
                  {pi > 0 && p.area >= 0 && (
                    <span className="room-move">
                      <button title="Move page earlier" disabled={p.area === 0} onClick={() => moveRoom(p.area, -1)}>↑</button>
                      <button title="Move page later" disabled={p.area === (layout?.areas.length ?? 0) - 1} onClick={() => moveRoom(p.area, 1)}>↓</button>
                    </span>
                  )}
                  <span className="count">{p.ents.length}/{MAX_PER_PAGE}</span>
                  {custom && <button className="ghost small remove-page" title="Remove this page; its devices go back to their own rooms" onClick={() => removePage(p.name)}>✕</button>}
                </h3>
                {title && <div className="fit-note page-note">{title}</div>}
                {p.ents.length === 0 && <div className="empty-page">Empty — choose this page for a device on the left, or drag a tile here. Empty pages are not sent to the panel.</div>}
                {p.ents.map((e) => {
                  const ha = friendlyName.get(e.id) ?? e.name;
                  const custom = sel.names[e.id] !== undefined && sel.names[e.id] !== ha;
                  // The tightest place this device appears: Home and its
                  // room can have different tile sizes (5-6 = compact).
                  const compact = (layout?.entities.filter((x) => x.area === e.area).length ?? 0) > 4
                    || (e.fav && (layout?.entities.filter((x) => x.fav).length ?? 0) > 4);
                  const shown = sim.current?.fitLabel(e.name, nameBox(e.kind, compact)) ?? e.name;
                  const missing = fitName(e.name).missing;
                  return (
                    <div key={e.id} className={`tile-row ${pi === 0 ? "fixed" : ""}`} draggable={pi > 0}
                      onDragStart={() => { dragId.current = e.id; }}
                      onDragOver={(ev) => ev.preventDefault()} onDrop={() => dropBefore(e.id)}>
                      <span className="grip">{pi > 0 ? "⋮⋮" : ""}</span>
                      <input className={`name-edit ${custom ? "custom" : ""}`} value={sel.names[e.id] ?? ""} placeholder={ha}
                        title={custom ? `Custom name. Home Assistant calls it "${ha}".` : "Type a shorter name for the panel"}
                        onChange={(ev) => { const v = ev.target.value; setSel((s) => ({ ...s, names: { ...s.names, [e.id]: v } })); }} />
                      <Glyph name={e.icon} />
                      {(shown !== e.name || missing.length > 0) && (
                        <div className="fit-note">
                          {shown !== e.name && <span>Panel shows “{shown}”{compact ? " (compact tile)" : ""}</span>}
                          {missing.length > 0 && <span className="bad-text">Panel's font has no {missing.map((m) => `“${m}”`).join(" ")} — it will be skipped</span>}
                        </div>
                      )}
                    </div>
                  );
                })}
              </div>
              );
            })}
            {inv && (() => {
              const problem = newPage.trim() ? pageNameProblem(newPage) : null;
              const full = pageCount >= MAX_AREAS;
              return (
                <div className="add-page">
                  <input value={newPage} placeholder={full ? `The panel holds ${MAX_AREAS} pages besides Home` : "New page name"}
                    disabled={full} onChange={(e) => setNewPage(e.target.value)}
                    onKeyDown={(e) => { if (e.key === "Enter") addPage(); }} />
                  <button onClick={addPage} disabled={full || !newPage.trim() || !!problem}>Add page</button>
                  {problem && <div className="fit-note bad-text">{problem}</div>}
                </div>
              );
            })()}
          </div>

          <div className="card">
            <h2>Colours</h2>
            <div className="presets">
              {THEME_PRESETS.map((p) => (
                <button key={p.id} className={`preset ${preset?.id === p.id ? "active" : ""}`} title={p.name}
                  onClick={() => setTheme({ ...p.colours })}>
                  <span className="preset-swatch" style={{ background: p.colours.bg }}>
                    <span style={{ background: p.colours.tile }} />
                    <span style={{ background: p.colours.on }} />
                    <span style={{ background: p.colours.accent }} />
                  </span>
                  {p.name}
                </button>
              ))}
              {!preset && <span className="preset custom-tag">Custom</span>}
            </div>
            <div className="swatches">
              {Object.keys(THEME_DEFAULTS).map((k) => (
                <label key={k} className="swatch">
                  <input type="color" value={theme[k]} onChange={(e) => setTheme((t) => ({ ...t, [k]: e.target.value.toUpperCase() }))} />
                  <span>{THEME_LABELS[k]}</span>
                </label>
              ))}
            </div>
          </div>

          <div className="card">
            <h2>Layout</h2>
            <div className="meter"><div style={{ width: `${Math.min(100, bytes / LAYOUT_MAX_JSON * 100)}%` }} className={bytes > LAYOUT_MAX_JSON ? "over" : ""} /></div>
            <div className="facts">
              <span>{bytes} / {LAYOUT_MAX_JSON} bytes</span>
              <span>{layout?.entities.length ?? 0} / {MAX_ENTITIES} entities</span>
              <span>{layout?.areas.length ?? 0} / {MAX_AREAS} pages</span>
            </div>
            {verdict && (verdict.ok
              ? <div className="verdict ok">✓ Accepted by the firmware's validator</div>
              : <div className="verdict bad">✗ The panel would reject this: {verdict.reason}</div>)}
            {tables?.notes.map((n) => <div key={n} className="note">{n}</div>)}
            <div className="project">
              {project
                ? <span title={project}>Project: <code>{project}</code></span>
                : project === null && <span className="warn-text">No project folder found — saving needs one.</span>}
              {project !== undefined && window.homemesh && (
                <button className="ghost small" onClick={async () => {
                  try { if (await bridge.chooseProject()) location.reload(); }
                  catch (e) { flash({ kind: "error", text: (e as Error).message }); }
                }}>{project ? "Change…" : "Choose project folder…"}</button>
              )}
            </div>
            {dirty && <div className="dirty">● Unsaved changes — the project files still hold the previous layout.</div>}
            <div className="actions">
              <button className="ghost" onClick={revert} disabled={!dirty}>Revert</button>
              <button onClick={save} disabled={!tables || !project}>Save to project</button>
              <button className="primary" onClick={push} disabled={!client || !layout || verdict?.ok === false}>Push to panel</button>
            </div>
          </div>
        </section>
      </main>
      {toast && <div className={`toast ${toast.kind}`}>{toast.text}</div>}
    </div>
  );
}

// Power/Brightness/Color events for one entity, as the firmware's
// apply_entity_state() posts them: state optional, null brightness kept.
function stateEvents(i: number, st?: LiveState): SimEvent[] {
  if (!st) return [];
  const ev: SimEvent[] = [];
  // As apply_entity_state() does: "unavailable" greys the tile out
  // instead of reading as off.
  if (st.s !== undefined) {
    const available = st.s !== "unavailable";
    ev.push({ type: Ev.Availability, entity: i, value: available ? 1 : 0 });
    if (available) ev.push({ type: Ev.Power, entity: i, value: st.s === "on" ? 1 : 0 });
  }
  if (st.brightness != null) ev.push({ type: Ev.Brightness, entity: i, value: Math.max(0, Math.min(255, st.brightness)) });
  if (Array.isArray(st.rgb) && st.rgb.length >= 3) ev.push({ type: Ev.Color, entity: i, value: (st.rgb[0] << 16) | (st.rgb[1] << 8) | st.rgb[2] });
  return ev;
}
