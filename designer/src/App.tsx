import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import { bridge } from "./bridge";
import { HaClient } from "./ha/haClient";
import {
  buildHeader, buildInventory, buildLayout, dumpLayout, Inventory, Layout,
  LAYOUT_EVENT, LAYOUT_MAX_JSON, MAX_AUTOMATIONS, MAX_ENTITIES, MAX_PER_PAGE, selectLayout, THEME_DEFAULTS,
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
}

function selFromLayout(l: Layout): Sel {
  return {
    include: new Set(l.entities.map((e) => e.id)),
    favourite: new Set(l.entities.filter((e) => e.fav).map((e) => e.id)),
    order: l.entities.map((e) => e.id),
    icons: Object.fromEntries(l.entities.map((e) => [e.id, e.icon])),
    // Every saved name to start with; the ones that merely repeat HA's
    // friendly name are dropped once the inventory arrives.
    names: Object.fromEntries(l.entities.map((e) => [e.id, e.name])),
    automations: new Set(l.actions.map((a) => a.id)),
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
  const [sel, setSel] = useState<Sel>({ include: new Set(), favourite: new Set(), order: [], icons: {}, names: {}, automations: new Set() });
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
      // A saved name equal to HA's friendly name is not a custom name.
      const friendly = new Map(inventory.controllable.map((r) => [r.id, r.name]));
      setSel((s) => ({ ...s, names: Object.fromEntries(Object.entries(s.names).filter(([id, n]) => n !== friendly.get(id))) }));
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
    icons: sel.icons, names: sel.names,
  }) : null, [inv, sel]);
  const layout: Layout | null = useMemo(() => tables ? buildLayout(tables, theme) : saved, [tables, theme, saved]);
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
        if (body.a && "rgb_color" in body.a) st.rgb = body.a.rgb_color;
        liveStates.current.set(eid, st);
        const i = layout.entities.findIndex((e) => e.id === eid);
        if (i >= 0) for (const e of stateEvents(i, { s: body.s, brightness: body.a?.brightness, rgb: body.a?.rgb_color })) sim.current?.post(e);
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
  async function push() {
    if (!client || !layout) return;
    try {
      await client.cmd("fire_event", { event_type: LAYOUT_EVENT, event_data: { layout } });
      flash({ kind: "ok", text: `Pushed ${bytes} bytes. The panel restarts into it; its serial log confirms.` });
    } catch (e) { flash({ kind: "error", text: (e as Error).message }); }
  }
  async function save() {
    if (!tables || !layout) return;
    try {
      const where = await bridge.saveLayout(json, buildHeader(tables));
      setBaseline(json);
      setSaved(layout);
      flash({ kind: "ok", text: `Wrote ha_layout.json and ha_layout.h in ${where}` });
    } catch (e) { flash({ kind: "error", text: (e as Error).message }); }
  }
  function revert() {
    if (!saved) return;
    const s = selFromLayout(saved);
    setSel({ ...s, names: Object.fromEntries(Object.entries(s.names).filter(([id, n]) => n !== friendlyName.get(id))) });
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
  const pages = layout ? [{ name: "Home", ents: layout.entities.filter((e) => e.fav) }, ...layout.areas.map((a, i) => ({ name: a, ents: layout.entities.filter((e) => e.area === i) }))] : [];

  // Drag within a room page: tile position is order within the page.
  const dragId = useRef<string | null>(null);
  const dropBefore = (targetId: string) => {
    const from = dragId.current;
    dragId.current = null;
    if (!from || from === targetId || !layout) return;
    const area = (id: string) => layout.entities.find((e) => e.id === id)?.area;
    if (area(from) !== area(targetId)) return;
    const order = layout.entities.map((e) => e.id).filter((id) => id !== from);
    order.splice(order.indexOf(targetId), 0, from);
    setSel((s) => ({ ...s, order }));
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
              <h3>{g.name}{!g.area && <em> · no room in HA, cannot be placed</em>}</h3>
              {g.rows.map((r) => {
                const on = sel.include.has(r.id);
                return (
                  <div key={r.id} className={`ent ${on ? "on" : ""} ${g.area ? "" : "disabled"}`}>
                    <input type="checkbox" checked={on} disabled={!g.area}
                      onChange={() => setSel((s) => ({ ...s, include: toggle(s.include, r.id), order: s.order.includes(r.id) ? s.order : [...s.order, r.id] }))} />
                    <button className={`star ${sel.favourite.has(r.id) ? "fav" : ""}`} title="Show on the Home page" disabled={!g.area}
                      onClick={() => setSel((s) => ({ ...s, favourite: toggle(s.favourite, r.id) }))}>★</button>
                    <div className="ent-name"><div>{r.name}</div><code>{r.id}</code></div>
                    <span className={`kind ${r.kind}`}>{r.kind}</span>
                    <IconPicker entityId={r.id} value={sel.icons[r.id] ?? ""} disabled={!g.area}
                      onChange={(icon) => setSel((s) => ({ ...s, icons: { ...s.icons, [r.id]: icon } }))} />
                  </div>
                );
              })}
            </div>
          ))}
          {inv && (
            <div className="group autos">
              <h3>Automations screen <em className="soft">· swipe up on the panel · {sel.automations.size}/{MAX_AUTOMATIONS}</em></h3>
              {inv.actions.length === 0 && <p className="hint">No scenes, scripts or automations in Home Assistant.</p>}
              {inv.actions.map((a) => (
                <label key={a.id} className="ent auto-row">
                  <input type="checkbox" checked={sel.automations.has(a.id)}
                    disabled={!sel.automations.has(a.id) && sel.automations.size >= MAX_AUTOMATIONS}
                    onChange={() => setSel((s) => ({ ...s, automations: toggle(s.automations, a.id) }))} />
                  <div className="ent-name"><div>{a.name}</div><code>{a.id}</code></div>
                </label>
              ))}
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
            <p className="hint">Drag tiles to reorder within a room; arrows move a whole room. Edit a name to change what the panel shows — clear it to use Home Assistant's.</p>
            {pages.map((p, pi) => (
              <div key={p.name + pi} className="page">
                <h3>
                  <span className="page-name">{p.name}</span>
                  {pi > 0 && (
                    <span className="room-move">
                      <button title="Move room earlier" disabled={pi === 1} onClick={() => moveRoom(pi - 1, -1)}>↑</button>
                      <button title="Move room later" disabled={pi === pages.length - 1} onClick={() => moveRoom(pi - 1, 1)}>↓</button>
                    </span>
                  )}
                  <span className="count">{p.ents.length}/{MAX_PER_PAGE}</span>
                </h3>
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
            ))}
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
              <span>{layout?.areas.length ?? 0} rooms</span>
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
  if (st.s !== undefined) ev.push({ type: Ev.Power, entity: i, value: st.s === "on" ? 1 : 0 });
  if (st.brightness != null) ev.push({ type: Ev.Brightness, entity: i, value: Math.max(0, Math.min(255, st.brightness)) });
  if (Array.isArray(st.rgb) && st.rgb.length >= 3) ev.push({ type: Ev.Color, entity: i, value: (st.rgb[0] << 16) | (st.rgb[1] << 8) | st.rgb[2] });
  return ev;
}
