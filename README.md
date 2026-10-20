# HomeMesh Panel

A wall-mounted smart home control panel: an ESP32 with a 3.2-inch
touchscreen that controls every device a local Home Assistant server
knows about, over Home Assistant's WebSocket API. No cloud in the path.

VIT Chennai project — Garima Nidhi, Taniska Routray, Bharadhwaj B,
Pranesh S. Faculty guide: Suhasini S.

## How it fits together

- **The panel** (`src/`) — ESP32-WROOM-32, ST7789V display, GT911 touch,
  LVGL 8.3 interface. It holds one WebSocket connection to Home
  Assistant, subscribes to exactly the devices in its layout, and sends
  `call_service` when you tap. Networking and the interface run on
  separate cores.
- **The layout is data.** Which devices appear, on which pages, with
  which names, icons and colours is authored on a PC and pushed to the
  panel through Home Assistant; the panel stores it and restarts into
  it. No reflashing.
- **The designer** (`designer/`) — a desktop app for authoring that
  layout. Its preview is the panel's own interface code compiled to
  WebAssembly (`sim/`), so what you see is what the panel draws; in Live
  mode, tapping the simulated panel switches real devices.
- **The split is deliberate.** The full state of even a small Home
  Assistant install (57 KB for 123 entities, measured) is bigger than the
  panel's graphics memory, so discovery happens on the PC and the panel
  only ever handles small, bounded messages (2.4 KB for its 7 devices).

## Layout of the repository

```
src/          panel firmware (PlatformIO, Arduino framework)
include/      lv_conf.h, the compiled-in fallback layout (ha_layout.h/.json)
designer/     the desktop layout designer (Electron, React, TypeScript)
sim/          builds the firmware's UI to WebAssembly for the designer
tools/        discovery CLI, the older tkinter designer, demo preflight check
docs/         the IEEE paper (docs/paper/), presentation brief, figures
ha/           how the Home Assistant instance is run (Docker)
```

## Building

```
pio run -e ha -t upload -t monitor      # the panel firmware
designer\HomeMesh Designer.cmd          # the designer (or: cd designer && npm start)
```

The panel needs `include/secrets.h` (WiFi and a Home Assistant token;
gitignored). See `designer/README.md` for the designer and `CLAUDE.md`
for the full project notes, hardware details and known traps.
