// Headless check of the simulator: boot the panel's real UI with a layout,
// optionally apply state, swipe, and write the frame buffer as raw RGB565.
//
//   node tests/render_sim.mjs <layout.json> <out.rgb565> [on]
//
// "on" turns every light on at 72% before the frame is captured, to show
// the ON-state styling. The raw file is converted to PNG by the caller.
import { readFileSync, writeFileSync } from "node:fs";
import createPanelSim from "../src/sim/panel_sim.mjs";

const [layoutPath, outPath, mode] = process.argv.slice(2);
const layoutJson = readFileSync(layoutPath, "utf8");
const layout = JSON.parse(layoutJson);
const log = [];
const sim = await createPanelSim({ print: (s) => log.push(s), printErr: (s) => log.push("ERR " + s) });

const errBuf = sim._malloc(160);
const n = sim.lengthBytesUTF8(layoutJson) + 1;
const jsonPtr = sim._malloc(n);
sim.stringToUTF8(layoutJson, jsonPtr, n);
const ok = sim._sim_store_layout(jsonPtr, errBuf, 160);
if (!ok) log.push("store rejected: " + sim.UTF8ToString(errBuf));

sim._sim_boot();
// Link up, devices available, as the network task posts after auth_ok.
const EV = { Link: 0, DevicesAvailable: 1, Power: 2, Brightness: 3, Color: 4, Temperature: 5, Humidity: 6 };
sim._sim_event(EV.Link, 0, 2, 0);
sim._sim_event(EV.DevicesAvailable, 0, 1, 0);
layout.entities.forEach((e, i) => {
  const on = mode === "on" && e.kind !== "Toggle";
  sim._sim_event(EV.Power, i, on ? 1 : 0, 0);
  if (e.kind !== "Toggle") sim._sim_event(EV.Brightness, i, 184, 0);
});
layout.sensors.forEach((s) => sim._sim_event(s.hum ? EV.Humidity : EV.Temperature, s.area, 0, s.hum ? 54 : 15.6));

// LVGL redraws on a 30 ms period (LV_DISP_DEF_REFR_PERIOD), measured in
// real time, so steps need real time between them to produce a frame.
for (let i = 0; i < 12; i++) {
  sim._sim_step();
  await new Promise((r) => setTimeout(r, 40));
}
const fb = new Uint16Array(sim.HEAPU16.buffer, sim._sim_framebuffer(), 320 * 240);
writeFileSync(outPath, Buffer.from(fb.buffer, fb.byteOffset, fb.byteLength));
console.log(log.join("\n"));
console.log(`layout from NVS: ${sim._sim_layout_from_nvs()}, LVGL pool free: ${sim._sim_pool_free()} bytes`);
