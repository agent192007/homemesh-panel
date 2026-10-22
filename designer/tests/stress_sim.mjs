// Walk every screen of a layout in the simulator (same 40 KB LVGL pool as
// the panel) and report the least free memory seen, and any LVGL assert.
//   node tests/stress_sim.mjs <layout.json>
import { readFileSync } from "node:fs";
import createPanelSim from "../src/sim/panel_sim.mjs";
const out = [];
const sim = await createPanelSim({ print: (s) => out.push(s), printErr: (s) => out.push(s) });
const json = readFileSync(process.argv[2], "utf8");
const n = sim.lengthBytesUTF8(json) + 1, p = sim._malloc(n), err = sim._malloc(160);
sim.stringToUTF8(json, p, n); sim._sim_store_layout(p, err, 160); sim._free(p);
const L = JSON.parse(json);
sim._sim_boot();
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let min = Infinity;
const run = async (ms) => { for (let t = 0; t < ms; t += 20) { sim._sim_step(); min = Math.min(min, sim._sim_pool_free()); await sleep(20); } };
const tap = async (x, y) => { sim._sim_pointer(x, y, 1); await run(80); sim._sim_pointer(x, y, 0); await run(250); };
const swipe = async (x0, y0, x1, y1) => { sim._sim_pointer(x0, y0, 1); await run(40);
  for (let k = 1; k <= 6; k++) { sim._sim_pointer(Math.round(x0 + (x1 - x0) * k / 6), Math.round(y0 + (y1 - y0) * k / 6), 1); await run(20); }
  sim._sim_pointer(x1, y1, 0); await run(400); };
sim._sim_event(0, 0, 2, 0); sim._sim_event(1, 0, 1, 0);
L.entities.forEach((_, i) => { sim._sim_event(2, i, 1, 0); sim._sim_event(3, i, 180, 0); });
await run(400);
for (let round = 0; round < 2; round++) {
  for (let pg = 0; pg <= L.areas.length; pg++) { await tap(136, 52); await tap(296, 24); await swipe(280, 150, 40, 150); }
  for (let pg = 0; pg <= L.areas.length; pg++) await swipe(40, 150, 280, 150);
  await swipe(160, 210, 160, 30); await swipe(280, 150, 40, 150); await tap(80, 80); await swipe(160, 40, 160, 220);
  sim._sim_event(0, 0, 1, 0); await run(600); sim._sim_event(0, 0, 2, 0); await run(600);    // link down, faded tiles, back
}
console.log(`least free in the LVGL pool: ${min} bytes`);
const bad = out.filter((l) => /ASSERT|abort|out of memory/i.test(l));
console.log(bad.length ? bad.join("\n") : "no asserts");
