// The designer's name preview must match what the panel draws. Expected
// values are what the firmware's UI rendered in the simulator
// (tests/render_png.py); the fit itself comes from LVGL via
// sim_fit_label(), and the box widths from nameBox(), so this checks both.
//
//   node --experimental-strip-types tests/namefit.test.ts
import { readFileSync } from "node:fs";
import createPanelSim from "../src/sim/panel_sim.mjs";
import { fitName, nameBox } from "../src/layout/nameFit.ts";

const sim = await createPanelSim({ print: () => {}, printErr: () => {} });
const layout = readFileSync(new URL("../../include/ha_layout.json", import.meta.url), "utf8").trim();
const n = sim.lengthBytesUTF8(layout) + 1;
const p = sim._malloc(n);
sim.stringToUTF8(layout, p, n);
sim._sim_store_layout(p, sim._malloc(160), 160);
sim._sim_boot();

const fit = (text: string, width: number) => {
  const len = sim.lengthBytesUTF8(text) + 1;
  const t = sim._malloc(len);
  sim.stringToUTF8(text, t, len);
  const out = sim.UTF8ToString(sim._sim_fit_label(t, width));
  sim._free(t);
  return out;
};

const cases: [string, "Toggle" | "RGB", boolean, string][] = [
  ["Bed Light", "RGB", false, "Bed Light"],
  ["Ceiling Lights", "RGB", false, "Ceiling..."],
  ["Living Room RGBWW Lights", "RGB", false, "Living..."],
  ["Decorative Lights", "Toggle", true, "Decorative..."],
  ["Living Room Fan", "Toggle", true, "Living Room..."],
  ["Kitchen Lights", "RGB", true, "Kitchen..."],
  ["Panel Demo Switch", "Toggle", true, "Panel Demo..."],
  ["Strip", "RGB", false, "Strip"],
];
let fails = 0;
for (const [name, kind, compact, want] of cases) {
  const got = fit(name, nameBox(kind, compact));
  const ok = got === want;
  if (!ok) fails++;
  console.log(`  ${ok ? "PASS" : "FAIL"}  ${name} (${kind}${compact ? ", compact" : ""}) -> ${got}${ok ? "" : `, panel shows ${want}`}`);
}
const m = fitName("Café Light ✨").missing;
const okMissing = m.join("") === "é✨";
if (!okMissing) fails++;
console.log(`  ${okMissing ? "PASS" : "FAIL"}  characters the font lacks are reported: ${m.join(" ")}`);
console.log(fails ? `\n${fails} FAILED` : "\nALL PASSED");
process.exit(fails ? 1 : 0);
