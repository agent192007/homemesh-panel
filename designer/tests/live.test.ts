// Live-mode plumbing against a real Home Assistant: the app's HaClient,
// subscribe_entities, and a service call round trip - the path the
// designer's "Live states" switch uses.
//
//   node --experimental-strip-types tests/live.test.ts <url> <token>
//
// Toggles switch.ac twice, so it ends where it started.
import { HaClient } from "../src/ha/haClient.ts";

const [url, token] = process.argv.slice(2);
let fails = 0;
const check = (name: string, ok: boolean, detail = "") => {
  console.log(`  ${ok ? "PASS" : "FAIL"}  ${name}${ok ? "" : "  " + detail}`);
  if (!ok) fails++;
};

const c = await HaClient.connect(url, token, () => {});
check("connects and authenticates", true);

const events: any[] = [];
const unsub = await c.subscribe("subscribe_entities", { entity_ids: ["switch.ac", "light.bed_light"] }, (e) => events.push(e));
await new Promise((r) => setTimeout(r, 500));
const snap = events.find((e) => e.a);
check("snapshot arrives with both entities", !!snap && "switch.ac" in snap.a && "light.bed_light" in snap.a,
  JSON.stringify(Object.keys(snap?.a ?? {})));
const before = snap?.a["switch.ac"].s;

for (const _ of [1, 2]) {
  const t0 = performance.now();
  const n = events.length;
  await c.cmd("call_service", { domain: "switch", service: "toggle", target: { entity_id: "switch.ac" } });
  while (events.length === n && performance.now() - t0 < 3000) await new Promise((r) => setTimeout(r, 10));
  const ch = events.slice(n).find((e) => e.c?.["switch.ac"]);
  check(`toggle produces a change event (${(performance.now() - t0).toFixed(1)} ms)`, !!ch);
}
const after = events.filter((e) => e.c?.["switch.ac"]?.["+"]?.s).at(-1)?.c["switch.ac"]["+"].s;
check("switch.ac is back where it started", after === before, `${before} -> ${after}`);

unsub();
await new Promise((r) => setTimeout(r, 200));
const n = events.length;
await c.cmd("call_service", { domain: "switch", service: "toggle", target: { entity_id: "switch.ac" } });
await c.cmd("call_service", { domain: "switch", service: "toggle", target: { entity_id: "switch.ac" } });
await new Promise((r) => setTimeout(r, 400));
check("no events after unsubscribing", events.length === n, `${events.length - n} arrived`);
c.close();
console.log(fails ? `\n${fails} FAILED` : "\nALL PASSED");
process.exit(fails ? 1 : 0);
