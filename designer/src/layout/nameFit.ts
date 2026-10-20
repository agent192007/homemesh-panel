// How a tile name will look on the panel.
//
// The cut itself is LVGL's: the simulator lays the name out in a label set
// up exactly like a tile's (sim_fit_label), because LVGL wraps at word
// boundaries, rounds every glyph to whole pixels and applies kerning - and
// a re-implementation that got any of that slightly wrong would show a
// different cut from the panel's. This file only knows the box widths and
// which characters the font has.
//
// Box widths mirror create_device_tile() in src/ui/dashboard.cpp: tiles
// are 148 px wide; the name label is the tile width minus 54 px when a
// chevron shares its row (dimmable lights) or 22 px otherwise, minus
// another 26 px on compact tiles (5-6 on a page), where the icon is inline.
import { NAME_FONT_WIDTHS } from "./fontMetrics.ts";
import type { Kind } from "./format";

const TILE_W = 148;

export function nameBox(kind: Kind, compact: boolean): number {
  return TILE_W - (kind === "Toggle" ? 22 : 54) - (compact ? 26 : 0);
}

// Characters the panel's font cannot draw. LVGL skips them silently, so
// "Café" would show as "Caf".
export function fitName(name: string): { missing: string[] } {
  return { missing: [...new Set([...name].filter((ch) => NAME_FONT_WIDTHS[ch.codePointAt(0)!] === undefined))] };
}
