// The panel, running: the firmware's own LVGL UI compiled to WebAssembly.
//
// A new layout is applied the way the board applies one - validated by
// the firmware's layout_store_json(), then a restart (a fresh module
// instance). A rejected layout leaves the running instance alone and
// reports the firmware's own reason, again exactly as on the board.
import { forwardRef, useEffect, useImperativeHandle, useRef, useState } from "react";
import createPanelSim from "./panel_sim.mjs";
import type { PanelSimModule } from "./panel_sim.mjs";
import type { SimEvent } from "./events";

const W = 320, H = 240;

export interface PanelSimulatorHandle {
  post(e: SimEvent): void;
  // LVGL's own one-line LONG_DOT cut of `text` in `width` px, or null
  // before the simulator is up. See sim_fit_label().
  fitLabel(text: string, width: number): string | null;
  // Pixel width of one line in the panel's 14 or 20 px font, or null
  // before the simulator is up. See sim_text_width().
  textWidth(text: string, px: 14 | 20): number | null;
}

interface Props {
  layoutJson: string;
  scale?: number;
  // Events replayed into every fresh instance after boot - the state
  // the network task would deliver right after connecting.
  bootEvents: () => SimEvent[];
  onCommand: (kind: number, index: number, value: number) => void;
  onLog: (line: string) => void;
  onLayoutResult: (ok: boolean, reason: string) => void;
}

export const PanelSimulator = forwardRef<PanelSimulatorHandle, Props>(function PanelSimulator(
  { layoutJson, scale = 2, bootEvents, onCommand, onLog, onLayoutResult }, ref) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const sim = useRef<PanelSimModule | null>(null);
  const cb = useRef({ onCommand, onLog, onLayoutResult, bootEvents });
  cb.current = { onCommand, onLog, onLayoutResult, bootEvents };
  const [booting, setBooting] = useState(true);

  useImperativeHandle(ref, () => ({
    post(e) { sim.current?._sim_event(e.type, e.entity, e.value, e.fvalue ?? 0); },
    fitLabel(text, width) {
      const m = sim.current;
      if (!m) return null;
      const n = m.lengthBytesUTF8(text) + 1;
      const p = m._malloc(n);
      m.stringToUTF8(text, p, n);
      const out = m.UTF8ToString(m._sim_fit_label(p, width));
      m._free(p);
      return out;
    },
    textWidth(text, px) {
      const m = sim.current;
      if (!m) return null;
      const n = m.lengthBytesUTF8(text) + 1;
      const p = m._malloc(n);
      m.stringToUTF8(text, p, n);
      const w = m._sim_text_width(p, px);
      m._free(p);
      return w;
    },
  }), []);

  // Apply a layout: validate in a fresh instance, swap only if accepted.
  useEffect(() => {
    let cancelled = false;
    (async () => {
      const m = await createPanelSim({
        print: (s) => cb.current.onLog(s),
        printErr: (s) => cb.current.onLog(s),
        onCommand: (k, i, v) => cb.current.onCommand(k, i, v),
      });
      if (cancelled) return;
      const n = m.lengthBytesUTF8(layoutJson) + 1;
      const json = m._malloc(n);
      const err = m._malloc(160);
      m.stringToUTF8(layoutJson, json, n);
      const ok = m._sim_store_layout(json, err, 160) === 1;
      const reason = ok ? "" : m.UTF8ToString(err);
      m._free(json);
      m._free(err);
      cb.current.onLayoutResult(ok, reason);
      if (!ok) { setBooting(false); return; }   // keep the old instance running
      m._sim_boot();
      for (const e of cb.current.bootEvents()) m._sim_event(e.type, e.entity, e.value, e.fvalue ?? 0);
      sim.current = m;
      setBooting(false);
    })();
    return () => { cancelled = true; };
  }, [layoutJson]);

  // The board's loop(): step LVGL, repaint when it flushed.
  useEffect(() => {
    const ctx = canvas.current!.getContext("2d")!;
    const img = ctx.createImageData(W, H);
    let raf = 0;
    const frame = () => {
      const m = sim.current;
      if (m && m._sim_step()) {
        const fb = new Uint16Array(m.HEAPU16.buffer, m._sim_framebuffer(), W * H);
        const d = img.data;
        for (let i = 0, j = 0; i < fb.length; i++, j += 4) {
          const v = fb[i];
          // RGB565 -> RGB888, as the panel's 16-bit colour really is.
          d[j] = ((v >> 11) & 31) * 255 / 31;
          d[j + 1] = ((v >> 5) & 63) * 255 / 63;
          d[j + 2] = (v & 31) * 255 / 31;
          d[j + 3] = 255;
        }
        ctx.putImageData(img, 0, 0);
      }
      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, []);

  // Mouse as touch, in panel coordinates.
  const point = (e: React.PointerEvent, down: boolean) => {
    const r = canvas.current!.getBoundingClientRect();
    const x = Math.max(0, Math.min(W - 1, Math.round((e.clientX - r.left) / r.width * W)));
    const y = Math.max(0, Math.min(H - 1, Math.round((e.clientY - r.top) / r.height * H)));
    sim.current?._sim_pointer(x, y, down ? 1 : 0);
  };

  return (
    <div className="sim-frame" style={{ width: W * scale + 24, height: H * scale + 24 }}>
      <canvas
        ref={canvas}
        width={W}
        height={H}
        style={{ width: W * scale, height: H * scale }}
        onPointerDown={(e) => { e.currentTarget.setPointerCapture(e.pointerId); point(e, true); }}
        onPointerMove={(e) => point(e, e.buttons === 1)}
        onPointerUp={(e) => point(e, false)}
      />
      {booting && <div className="sim-boot">starting panel…</div>}
    </div>
  );
});
