"""Find the MDI codepoints for the glyphs the panel needs."""
import re, json, pathlib

css = pathlib.Path("node_modules/@mdi/font/css/materialdesignicons.css").read_text(encoding="utf-8")
cp = dict(re.findall(r'\.mdi-([a-z0-9-]+)::before\s*\{\s*content:\s*"\\([0-9A-Fa-f]+)"', css))
print("glyphs in this MDI build:", len(cp))

# Named for what they mean on the panel, not for the MDI glyph, because
# the layout JSON carries these names and they outlive the icon set.
WANT = [
    ("light",     "lightbulb",         "light, generic"),
    ("ceiling",   "ceiling-light",     "ceiling light"),
    ("lamp",      "lamp",              "bed / table lamp"),
    ("strip",     "led-strip-variant", "RGB strip"),
    ("fan",       "fan",               "fan"),
    ("ac",        "air-conditioner",   "air conditioner"),
    ("toggle",    "toggle-switch",     "input_boolean helper"),
    ("plug",      "power-plug",        "switch / socket"),
    ("temp",      "thermometer",       "temperature"),
    ("humidity",  "water-percent",     "humidity"),
    ("tv",        "television",        "media player"),
    ("speaker",   "speaker",           "speaker"),
    ("lock",      "lock",              "lock"),
    ("blinds",    "blinds",            "cover / blind"),
    ("home",      "home",              "home page"),
    ("unknown",   "help-circle",       "fallback"),
]

missing = [m for _, m, _ in WANT if m not in cp]
if missing:
    raise SystemExit("not in this MDI build: " + ", ".join(missing))

out = {}
for key, mdi, note in WANT:
    out[key] = {"mdi": mdi, "cp": cp[mdi].upper()}
    print(f"  {key:<10} {mdi:<18} U+{cp[mdi].upper():<6} {note}")

pathlib.Path("icons.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
codes = sorted(int(v["cp"], 16) for v in out.values())
print("\ncount:", len(codes))
print("span: U+%X .. U+%X" % (codes[0], codes[-1]))
print("range arg:", ",".join("0x%X" % c for c in codes))
