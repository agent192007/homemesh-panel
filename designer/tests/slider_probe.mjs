// Probe: open the first tile's detail panel and press its slider at x,
// printing the commands the UI sends. node tests/slider_probe.mjs <layout> <x> [moveTo]
import { readFileSync } from "node:fs";
import createPanelSim from "../src/sim/panel_sim.mjs";
const [layoutPath, xArg, moveArg] = process.argv.slice(2);
const sim = await createPanelSim({ print: () => {}, printErr: () => {} });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const json = readFileSync(layoutPath, "utf8");
const n = sim.lengthBytesUTF8(json) + 1, p = sim._malloc(n);
const err = sim._malloc(160); sim.stringToUTF8(json, p, n); sim._sim_store_layout(p, err, 160); sim._free(p);
const cmds = [];
sim.onCommand = (k, i, v) => cmds.push([k, i, v]);
sim._sim_boot();
sim._sim_event(0, 0, 2, 0); sim._sim_event(1, 0, 1, 0); sim._sim_event(2, 0, 1, 0); sim._sim_event(3, 0, 84, 0);   // online, devices ok, on, level 84
const run = async (ms) => { for (let t = 0; t < ms; t += 20) { sim._sim_step(); await sleep(20); } };
await run(300);
sim._sim_pointer(136, 52, 1); await run(80); sim._sim_pointer(136, 52, 0); await run(300);
const x = +xArg, to = moveArg ? +moveArg : x;
sim._sim_pointer(x, 156, 1); await run(100);
if (to !== x) for (let k = 1; k <= 6; k++) { sim._sim_pointer(Math.round(x + (to - x) * k / 6), 156, 1); await run(30); }
sim._sim_pointer(to, 156, 0); await run(300);
console.log(JSON.stringify(cmds));
