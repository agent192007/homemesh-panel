// Icon choice as glyphs rather than names: the same 16 glyphs the panel
// draws, from the masks generated with its icon fonts.
import { useEffect, useRef, useState } from "react";
import { defaultIcon, ICON_NAMES } from "./layout/format";
import { ICON_IMAGES } from "./layout/iconImages";

export function Glyph({ name, size = 18 }: { name: string; size?: number }) {
  const src = ICON_IMAGES[name] ?? ICON_IMAGES.unknown;
  return <span className="glyph" style={{ width: size, height: size, WebkitMaskImage: `url(${src})`, maskImage: `url(${src})` }} />;
}

interface Props { entityId: string; value: string; disabled?: boolean; onChange: (icon: string) => void }

export function IconPicker({ entityId, value, disabled, onChange }: Props) {
  const [open, setOpen] = useState(false);
  const box = useRef<HTMLDivElement>(null);
  const auto = defaultIcon(entityId);
  const shown = value || auto;

  useEffect(() => {
    if (!open) return;
    const close = (e: MouseEvent) => { if (!box.current?.contains(e.target as Node)) setOpen(false); };
    document.addEventListener("mousedown", close);
    return () => document.removeEventListener("mousedown", close);
  }, [open]);

  return (
    <div className="icon-picker" ref={box}>
      <button className="icon-btn" disabled={disabled} title={value ? value : `auto (${auto})`} onClick={() => setOpen((o) => !o)}>
        <Glyph name={shown} />
        <span className={value ? "" : "auto"}>{value || "auto"}</span>
      </button>
      {open && (
        <div className="icon-pop">
          <button className={`icon-cell ${!value ? "sel" : ""}`} title={`Automatic: ${auto}`} onClick={() => { onChange(""); setOpen(false); }}>
            <Glyph name={auto} size={22} /><span>auto</span>
          </button>
          {ICON_NAMES.map((n) => (
            <button key={n} className={`icon-cell ${value === n ? "sel" : ""}`} title={n} onClick={() => { onChange(n); setOpen(false); }}>
              <Glyph name={n} size={22} /><span>{n}</span>
            </button>
          ))}
        </div>
      )}
    </div>
  );
}
