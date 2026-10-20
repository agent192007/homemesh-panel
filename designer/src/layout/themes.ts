// Theme presets for the panel. Each is a full set of the nine colours in
// THEME_DEFAULTS; the layout carries only those that differ from the
// defaults, so "Graphite" (the compiled look) costs nothing to push.
//
// Every preset was rendered on the panel's own UI in the simulator - all
// pages, on and off states, the detail panel - before being listed here.
// Colours are chosen with the panel's 16-bit RGB565 display in mind:
// strong, flat colours survive; subtle near-greys collapse together.
import { THEME_DEFAULTS } from "./format";

export interface ThemePreset { id: string; name: string; light: boolean; colours: Record<string, string> }

export const THEME_PRESETS: ThemePreset[] = [
  { id: "graphite", name: "Graphite", light: false, colours: { ...THEME_DEFAULTS } },
  { id: "midnight", name: "Midnight", light: false, colours: {
    bg: "#0B1220", tile: "#1A2438", on: "#FFB547", accent: "#5EA2FF", text: "#FFFFFF",
    muted: "#8C9AB3", ok: "#3DDC84", warn: "#FFB547", bad: "#FF5C5C" } },
  { id: "forest", name: "Forest", light: false, colours: {
    bg: "#0E1611", tile: "#1E2C23", on: "#F2C14E", accent: "#4FBF85", text: "#F1F5F2",
    muted: "#8FA396", ok: "#4FBF85", warn: "#F2C14E", bad: "#E5674D" } },
  { id: "ember", name: "Ember", light: false, colours: {
    bg: "#16100D", tile: "#2B201A", on: "#FF8A3D", accent: "#F2B880", text: "#FFF6EE",
    muted: "#A3907F", ok: "#6FCF97", warn: "#FF8A3D", bad: "#EB5757" } },
  { id: "contrast", name: "High contrast", light: false, colours: {
    bg: "#000000", tile: "#1C1C1C", on: "#FFD400", accent: "#00B4FF", text: "#FFFFFF",
    muted: "#C8C8C8", ok: "#00E676", warn: "#FFD400", bad: "#FF3B30" } },
  { id: "daylight", name: "Daylight", light: true, colours: {
    bg: "#E9EDF2", tile: "#FFFFFF", on: "#E07B00", accent: "#2F6FEB", text: "#1B1F24",
    muted: "#68717D", ok: "#1E9E5A", warn: "#D97706", bad: "#D93A3A" } },
];

const norm = (t: Record<string, string>) =>
  Object.keys(THEME_DEFAULTS).map((k) => (t[k] ?? THEME_DEFAULTS[k]).toUpperCase()).join();

// Which preset a theme is, or null if it has been customised.
export function presetOf(theme: Record<string, string>): ThemePreset | null {
  const n = norm(theme);
  return THEME_PRESETS.find((p) => norm(p.colours) === n) ?? null;
}
