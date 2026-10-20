# HomeMesh Designer

The desktop app for authoring what the HomeMesh panel shows: which Home
Assistant devices, on which pages, in what order, with which icons and
colours — then pushing it to the panel over the network.

Its preview is **not a lookalike**. It is the panel's own firmware UI —
`src/ui/dashboard.cpp`, LVGL 8.3, the icon fonts and `layout_store.cpp` —
compiled to WebAssembly against the firmware's own `lv_conf.h`. Same
pixels, same fonts, same 16-bit colour, same 40 KB graphics memory, and
the same validator: if the simulator rejects a layout, the panel would
too, with the same message.

## What it does

- Connects to Home Assistant (reads `HA_URL` / `HA_TOKEN` from
  `tools/.env` automatically) and lists every entity the panel can drive,
  grouped by room.
- Pick entities, mark Home-page favourites, choose icons, drag tiles to
  reorder within a room, set the nine theme colours.
- The simulated panel updates as you edit. Drag on it to swipe pages; tap
  tiles and the chevron as on the real screen.
- **Live states**: the simulator shows Home Assistant's real device
  states, and taps really switch the devices — the same service calls
  the firmware makes.
- **Save to project** writes `include/ha_layout.json` and `ha_layout.h`
  (the compiled fallback). **Push to panel** sends the layout to the
  panel through Home Assistant; it restarts into it.
- The panel's serial log, from the simulated firmware, appears under the
  preview.

## Running it

```
cd designer
npm install
node node_modules/electron/install.js   # npm 11 blocks Electron's own download step
npm run build
npx electron .
```

`npm run dev` runs it with hot reload. From a VS Code terminal, clear
`ELECTRON_RUN_AS_NODE` first (VS Code sets it, and it makes Electron run
as plain Node): `set ELECTRON_RUN_AS_NODE=` in cmd, or
`env -u ELECTRON_RUN_AS_NODE npx electron .` in bash.

## Tests

```
npm test                                   # TS layout format == tools/ha_discover.py, byte for byte
node --experimental-strip-types tests/live.test.ts <url> <token>   # live HA round trip
node tests/render_sim.mjs ../include/ha_layout.json out.rgb565 on  # headless simulator frame
```

`npm test` runs the Python reference on a stored Home Assistant snapshot
(`tests/fixtures/ha_snapshot.json`, attributes trimmed to the three the
format reads) and requires identical layout JSON and `ha_layout.h`.

## Rebuilding the simulator

Only needed after changing the firmware's UI or layout code. Needs the
Emscripten SDK at `../../emsdk` (or `$EMSDK`) and PlatformIO's copies of
LVGL and ArduinoJson (`pio run -e ha` once):

```
python ../sim/build.py          # writes src/sim/panel_sim.mjs (committed)
```

See `../sim/` for the transport, entry point and the two small Arduino
shims that let the firmware's code run in the browser unmodified.

## Layout

```
electron/      main process (window, three file operations) and preload
src/App.tsx    the window
src/layout/    the layout format, ported from tools/ha_discover.py
src/ha/        Home Assistant WebSocket client
src/sim/       the WebAssembly panel and its React wrapper
tests/         format equivalence, live round trip, headless render
```
