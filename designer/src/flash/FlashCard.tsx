// "Panel firmware" card: flash a display with the project's firmware,
// watch it boot over the same cable, and send it this layout once it is
// listening. The serial log is the only proof a panel took a layout
// (there is no acknowledgement), so it is shown rather than summarised.
import { useEffect, useRef, useState } from "react";
import { bridge, type FirmwareInfo } from "../bridge";
import { flashPanel, watchBoot, type FlashProgress } from "./flasher";

interface Props {
  canPush: boolean;                 // connected to HA, layout accepted
  pushLayout: () => Promise<void>;  // fire the layout event
}

type Phase = "idle" | "flashing" | "booting" | "done" | "failed";

export function FlashCard({ canPush, pushLayout }: Props) {
  const [info, setInfo] = useState<FirmwareInfo | null>(null);
  const [phase, setPhase] = useState<Phase>("idle");
  const [progress, setProgress] = useState<FlashProgress | null>(null);
  const [log, setLog] = useState<string[]>([]);
  const [result, setResult] = useState("");
  const [sendAfter, setSendAfter] = useState(true);
  const logRef = useRef<HTMLPreElement>(null);
  const supported = typeof navigator !== "undefined" && "serial" in navigator && !!window.homemesh;

  useEffect(() => { bridge.firmwareInfo().then(setInfo).catch((e) => setInfo({ error: String(e) })); }, [phase === "idle"]);
  useEffect(() => { logRef.current?.scrollTo(0, logRef.current.scrollHeight); }, [log]);

  const lines = useRef<string[]>([]);
  const add = (line: string) => { lines.current = [...lines.current.slice(-300), line]; setLog(lines.current); };

  async function run() {
    if (!info || info.error) return;
    if (!confirm("Flash this panel?\n\nIt writes the whole board: any layout stored on it is erased, and it "
      + "starts on the layout compiled into the firmware" + (sendAfter && canPush ? " - then this designer sends it the current layout." : ".")
      + "\n\nKeep the cable in until it finishes.")) return;
    lines.current = []; setLog([]); setResult(""); setProgress(null);
    let port: SerialPort;
    try {
      port = await navigator.serial.requestPort({});
    } catch {
      setPhase("failed"); setResult("No serial port found. Plug the panel in with a data cable (a charge-only cable powers it but never shows a port).");
      return;
    }
    const where = await bridge.lastSerialPort();
    try {
      setPhase("flashing");
      add(`Flashing ${info.bytes?.toLocaleString()} bytes to ${where || "the panel"}…`);
      const image = await bridge.loadFirmware();
      const chip = await flashPanel(port, image, info.md5, setProgress, (s) => { if (s.trim()) add(s.trimEnd()); });
      add(`✓ Written and verified on ${chip}. Restarting it…`);

      setPhase("booting");
      const push = sendAfter && canPush;
      let pushed = false, restarted = false, outcome = "";
      await watchBoot(port, (line) => {
        add(line);
        if (line.includes("[ha] listening for homemesh_layout") && push && !pushed) {
          pushed = true;
          add("→ Sending this layout…");
          pushLayout().catch((e) => add(`✗ Could not send the layout: ${(e as Error).message}`));
        }
        if (line.includes("[layout] stored") && line.includes("restarting")) restarted = true;
        if (line.includes("push REJECTED")) { outcome = "The panel rejected the layout: " + line.split("REJECTED:")[1]; return true; }
        if (line.includes("[ha] initial state") && (!push || restarted)) {
          outcome = push ? "Flashed, and running the layout from this designer." : "Flashed, connected and running.";
          return true;
        }
        if (/ASSERT FAILED|Guru Meditation|panic/i.test(line)) { outcome = "The panel crashed while starting — see the log."; return true; }
      }, 240_000);

      if (outcome.startsWith("Flashed")) { setPhase("done"); setResult(outcome); }
      else {
        setPhase("failed");
        setResult(outcome || describeStall(lines.current));
      }
    } catch (e) {
      setPhase("failed");
      setResult(flashError((e as Error).message));
    }
  }

  const busy = phase === "flashing" || phase === "booting";
  return (
    <div className="card">
      <h2>Panel firmware</h2>
      <p className="hint">Set up a new display, or update one: flash it over USB, watch it start, and send it this layout. No other software needed.</p>
      {!supported && <div className="note">Flashing needs the desktop app.</div>}
      {info?.error && <div className="note">{info.error}</div>}
      {info && !info.error && (
        <div className="fw-facts">
          <span>Built {info.built}{info.commit ? ` · ${info.commit}` : ""}</span>
          <span>Joins Wi-Fi <b>{info.wifi || "?"}</b> · Home Assistant at <b>{info.ha_host || "?"}</b></span>
        </div>
      )}
      <label className="fw-opt">
        <input type="checkbox" checked={sendAfter} onChange={(e) => setSendAfter(e.target.checked)} disabled={busy} />
        Then send it this layout{!canPush && <span className="soft"> (connect to Home Assistant first)</span>}
      </label>
      {progress && busy && (
        <div className="fw-progress">
          <div className="meter"><div style={{ width: `${Math.round(progress.fraction * 100)}%` }} /></div>
          <span>{phase === "booting" ? "Starting up…" : { connecting: "Connecting…", writing: `Writing ${Math.round(progress.fraction * 100)}%`,
            verifying: "Verifying…", restarting: "Restarting…" }[progress.stage]}</span>
        </div>
      )}
      {result && <div className={`verdict ${phase === "done" ? "ok" : "bad"}`}>{phase === "done" ? "✓ " : "✗ "}{result}</div>}
      {log.length > 0 && <pre ref={logRef} className="fw-log">{log.join("\n")}</pre>}
      <div className="actions">
        <span className="soft fw-warn">Erases the panel's stored layout.</span>
        <button className="primary" onClick={run} disabled={!supported || !info || !!info.error || busy}>
          {busy ? "Working…" : "Flash a panel…"}
        </button>
      </div>
    </div>
  );
}

// Where a panel stopped, in words, from the last thing it said.
function describeStall(log: string[]): string {
  const last = [...log].reverse();
  const wifi = last.find((l) => l.includes("[wifi] not connected") || l.includes("[wifi] still not connected"));
  if (wifi && !log.some((l) => l.includes("[wifi] connected")))
    return `It never joined the Wi-Fi (${wifi.replace(/^.*\[wifi\] (still )?not connected: /, "")}). Reason 201 means it cannot see the network: is the hotspot on and on 2.4 GHz?`;
  if (wifi && log.some((l) => l.includes("[wifi] connected")))
    return `It joined the Wi-Fi, then the hotspot dropped it (${wifi.replace(/^.*\[wifi\] (still )?not connected: /, "")}). `
      + "It keeps retrying on its own; on a poor link that can take minutes. Ping it from the laptop to check the link.";
  if (log.some((l) => l.includes("[wifi] connected")) && !log.some((l) => l.includes("[ha] authenticated")))
    return "It joined the Wi-Fi but did not reach Home Assistant. Is Home Assistant running, and is the token in the firmware still valid?";
  return "The panel did not finish starting within four minutes — see the log.";
}

function flashError(msg: string): string {
  if (/Failed to connect|Timed out|timeout/i.test(msg))
    return "Could not talk to the board's bootloader. Unplug and replug it and try again; if it still fails, hold BOOT while it connects.";
  if (/busy|Failed to open|access denied/i.test(msg))
    return "The serial port is in use — close any serial monitor (PlatformIO, Arduino) and try again.";
  return msg;
}
