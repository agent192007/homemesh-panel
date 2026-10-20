# HomeMesh Designer

The desktop app for authoring what the HomeMesh panel shows: which Home
Assistant devices, on which pages, in what order, with which names, icons
and colours — then pushing it to the panel over the network.

Its preview is **not a lookalike**. It is the panel's own firmware UI —
`src/ui/dashboard.cpp`, LVGL 8.3, the icon fonts and `layout_store.cpp` —
compiled to WebAssembly against the firmware's own `lv_conf.h`. Same
pixels, same fonts, same 16-bit colour, same 40 KB graphics memory, and
the same validator: if the simulator rejects a layout, the panel would
too, with the same message.

## Opening it

Double-click **`HomeMesh Designer.cmd`** (first run installs and builds),
or `npm start`. Or build the portable exe — `npm run dist` writes
`release\HomeMesh Designer 0.5.0.exe`, a single file that runs on any
Windows PC. It finds the project folder (for `tools/.env` and
`include/ha_layout.*`) from where it sits, from the folder chosen last
time, or via **Choose project folder…** in the Layout card.

## What it does

- Connects to Home Assistant (`HA_URL` / `HA_TOKEN` from `tools/.env`)
  and lists every entity the panel can drive, grouped by room.
- **Entities**: tick to place, star for the Home page, pick an icon from
  the panel's own glyphs. Scenes, scripts and automations go on the
  Automations screen (swipe up on the panel).
- **Pages**: every device has a page chooser, starting at its Home
  Assistant room. Put it on another page, **add your own pages**, rename
  any page (click its name), or drag a tile onto another page; arrows
  move a whole page. A device with no room in Home Assistant is placed by
  choosing a page for it, and each page's header sensors are chosen the
  same way. The panel only knows pages by name, so this is designer-side:
  no firmware change. A page name too long for the header (it would run
  into the temperature) is flagged, measured with the panel's own font.
- **Rename any device** for the panel — clear the name to use Home
  Assistant's. Each name shows how the panel will cut it ("Ceiling...")
  and warns about characters its font cannot draw (accents, emoji).
- **Colours**: six presets — Graphite (the default), Midnight, Forest,
  Ember, High contrast, Daylight — or edit any of the nine colours.
- The simulated panel updates as you edit. Drag on it to swipe pages
  (up for Automations); tap tiles and the chevron as on the real screen.
- **Live states**: real device states from Home Assistant, and taps
  really switch the devices — the same service calls the firmware makes.
- **Save to project** writes `include/ha_layout.json` and `ha_layout.h`
  (the compiled fallback). **Push to panel** sends the layout through
  Home Assistant; the panel restarts into it. Unsaved changes are flagged
  and can be reverted.
- The app itself follows Windows' light/dark setting, or Auto/Dark/Light
  in the top right.

## Developing

```
npm install
node node_modules/electron/install.js   # npm 11 blocks Electron's own download step
npm run dev                             # hot reload
npm run build && npm start
```

From a VS Code terminal, `ELECTRON_RUN_AS_NODE` is set and makes Electron
run as plain Node; `npm start` and the `.cmd` clear it for you.

## Tests

```
npm test    # layout format == tools/ha_discover.py byte for byte; name cuts == LVGL
node --experimental-strip-types tests/live.test.ts <url> <token>    # live HA round trip
python tests/render_png.py <layout.json> out.png [on] [page] [detail|autos]
python tests/theme_sheet.py sheet.png    # every preset on the real UI
```

`npm test` runs the Python reference on a stored Home Assistant snapshot
(`tests/fixtures/ha_snapshot.json`, attributes trimmed to the three the
format reads) and requires identical layout JSON and `ha_layout.h`, then
checks name truncation against cuts the firmware actually rendered.

## Rebuilding the simulator

Only after changing the firmware's UI or layout code. Needs the
Emscripten SDK at `../../emsdk` (or `$EMSDK`) and PlatformIO's copies of
LVGL and ArduinoJson (`pio run -e ha` once):

```
python ../sim/build.py          # writes src/sim/panel_sim.mjs (committed)
```

Generated sources, rerun if their inputs change:
`scripts/gen_icons.py` (after `tools/make_icon_font.sh`) and
`scripts/gen_font_metrics.py` (after an LVGL font change).

## Layout

```
electron/      main process (window, project folder, three file operations) and preload
src/App.tsx    the window
src/layout/    layout format (ported from tools/ha_discover.py), presets, icons, name fit
src/ha/        Home Assistant WebSocket client
src/sim/       the WebAssembly panel and its React wrapper
tests/         format equivalence, name fit, live round trip, headless renders
```
