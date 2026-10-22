// Headless check of the simulator: boot the panel's real UI with a layout,
// apply state, navigate, and write the frame buffer as raw RGB565 (the
// caller converts it to PNG).
//
//   node tests/render_sim.mjs <layout.json> <out.rgb565> [on] [page] [detail]
//
// "on" turns every dimmable light on at 72%; page N swipes left N times
// from Home (0); "detail" taps the first tile's chevron to open the
// detail panel. Mode "offline" is "on" with the Home page's first device
// reported unavailable by Home Assistant.
import { readFileSync, writeFileSync } from "node:fs";
import createPanelSim from "../src/sim/panel_sim.mjs";

const [layoutPath, outPath, mode, pageArg, view] = process.argv.slice(2);
const page = Number(pageArg ?? 0);
const layoutJson = readFileSync(layoutPath, "utf8").trim();
const layout = JSON.parse(layoutJson);
const log = [];
const sim = await createPanelSim({ print: (s) => log.push(s), printErr: (s) => log.push("ERR " + s) });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const errBuf = sim._malloc(160);
const n = sim.lengthBytesUTF8(layoutJson) + 1;
const jsonPtr = sim._malloc(n);
sim.stringToUTF8(layoutJson, jsonPtr, n);
if (!sim._sim_store_layout(jsonPtr, errBuf, 160)) {
  console.log("REJECTED: " + sim.UTF8ToString(errBuf));
  process.exit(2);
}
sim._sim_boot();
const EV = { Link: 0, DevicesAvailable: 1, Power: 2, Brightness: 3, Color: 4, Temperature: 5, Humidity: 6, Availability: 8 };
const lit = mode === "on" || mode === "offline";
sim._sim_event(EV.Link, 0, 2, 0);
sim._sim_event(EV.DevicesAvailable, 0, 1, 0);
layout.entities.forEach((e, i) => {
  sim._sim_event(EV.Power, i, lit && e.kind !== "Toggle" ? 1 : 0, 0);
  if (e.kind !== "Toggle") sim._sim_event(EV.Brightness, i, 184, 0);
  if (e.kind === "RGB") sim._sim_event(EV.Color, i, 0xffa452, 0);
});
if (mode === "offline") {
  const first = Math.max(0, layout.entities.findIndex((e) => e.fav));
  sim._sim_event(EV.Availability, first, 0, 0);
}
layout.sensors.forEach((s) => sim._sim_event(s.hum ? EV.Humidity : EV.Temperature, s.area, 0, s.hum ? 54 : 15.6));

// LVGL redraws on a 30 ms period in real time, so steps need real time.
const run = async (ms) => { for (let t = 0; t < ms; t += 20) { sim._sim_step(); await sleep(20); } };
const tap = async (x, y) => { sim._sim_pointer(x, y, 1); await run(80); sim._sim_pointer(x, y, 0); await run(200); };
await run(200);
for (let p = 0; p < page; p++) {
  sim._sim_pointer(280, 150, 1); await run(40);
  for (let x = 260; x >= 60; x -= 40) { sim._sim_pointer(x, 150, 1); await run(20); }
  sim._sim_pointer(60, 150, 0); await run(200);
}
// First tile's chevron: tile at (8, 36), 148 wide; chevron top-right.
if (view === "detail") await tap(136, 52);
if (view === "autos" || view === "autos2") {
  // An upward swipe, to the Automations screen.
  sim._sim_pointer(160, 210, 1); await run(40);
  for (let y = 180; y >= 40; y -= 35) { sim._sim_pointer(160, y, 1); await run(20); }
  sim._sim_pointer(160, 40, 0); await run(200);
}
if (view === "autos2") {
  // ...and left to its second page (automations 5-8).
  sim._sim_pointer(280, 150, 1); await run(40);
  for (let x = 260; x >= 60; x -= 40) { sim._sim_pointer(x, 150, 1); await run(20); }
  sim._sim_pointer(60, 150, 0); await run(400);
}
await run(300);
const fb = new Uint16Array(sim.HEAPU16.buffer, sim._sim_framebuffer(), 320 * 240);
writeFileSync(outPath, Buffer.from(fb.buffer, fb.byteOffset, fb.byteLength));
console.log(log.join("\n"));
