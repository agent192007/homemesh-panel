# HomeMesh Panel

Wall-mounted smart home control panel. ESP32 + 3.2" touchscreen,
talking to Home Assistant. Local-first, no cloud.

Academic project, VIT Chennai. Carries 70% of the grade.

**The 70% review is done and went well.** What remains is the final
review and submission. **The paper is due 21 October** (the user's
date, given 18 Oct; earlier notes said the 22nd, then the 20th).
Prioritise an impressive working
demo over elegance — and at this point, prioritise the report over new
capability, because the engineering is essentially where this file says
it should be and the report is not.

## Added 20 Oct — the new designer

**`designer/`: an Electron + React + TypeScript app replacing the
tkinter designer, with a real LVGL simulator.** Working end to end on
20 Oct: connects to HA, picker, favourites, icons, drag reorder, theme,
save, push, and **Live states** — verified in the running app by
tapping the simulated Bed Light tile and watching HA's real light turn
on (brightness 102), then off. See `designer/README.md`.

- The preview is the firmware's own `ui/dashboard.cpp` + LVGL 8.3.11 +
  icon fonts + `layout_store.cpp`, compiled to WebAssembly by
  `sim/build.py` against `include/lv_conf.h`. `sim/` adds only a
  transport, an entry point and two Arduino shims; **nothing in `src/`
  is forked**. Output `designer/src/sim/panel_sim.mjs` is committed, so
  the app builds without Emscripten.
- Applying a layout re-instantiates the module — the simulator's
  restart, mirroring the board. The firmware's validator runs on every
  edit: "Accepted by the firmware's validator" in the app is literal.
- `designer/src/layout/format.ts` ports `tools/ha_discover.py`;
  `npm test` requires byte-identical layout JSON **and** `ha_layout.h`
  against the Python on a stored HA snapshot. Python stays the
  reference — change the format there first.
- The tkinter `tools/layout_designer.py` stays: the paper (due 21 Oct)
  describes it. **It does not know custom names** — saving from it
  writes HA's names back. Use the new designer.
- Second pass, 20 Oct afternoon (user away, asked for it): custom
  names, six theme presets, app dark/light/auto mode, room reordering,
  automations, icon picker showing the panel's glyphs, unsaved-changes
  + Revert, a double-click launcher, and a **portable exe**
  (`npm run dist`, verified: the built exe found the project, read the
  token, connected and booted the simulator).
- **Name hints come from LVGL itself.** `sim_fit_label()` lays a name
  out in an off-screen label set up like a tile's and returns LVGL's
  `LONG_DOT` result. A JS re-implementation got 3 of 8 cuts wrong —
  LVGL wraps at word boundaries, rounds each glyph to whole pixels and
  kerns. `tests/namefit.test.ts` checks against eight cuts the firmware
  rendered. Names with characters the font lacks (it has printable
  ASCII and °) are flagged; LVGL drops those silently.

**Firmware changed after `v0.4-firmware-freeze` — at the user's request
("fix the text overflowing"), 20 Oct — and is flashed** (20 Oct evening,
`pio run -e ha -t upload` on COM6, app only so the pushed layout stayed).
The boot log matched the healthy one: 15,408 B pool free, 8,220 B stack
free, 7 entities in one 2,403-byte frame. Not yet looked at by eye on
the screen: long names and the Daylight theme. Verified before that in
the simulator, which runs this exact `dashboard.cpp`, and by building
all three envs:
- Tile names are one line with "..." (they wrapped into the icon:
  `LONG_DOT` needs a height as well as a width). Compact tiles (5-6 on a
  page) put the icon inline before the name.
- Rendering every preset on the real UI (`designer/tests/theme_sheet.py`)
  found three bugs only visible on a light theme: ON tiles lost their
  names (LVGL's default theme makes checked-button text white), the
  detail panel's close glyph was white on white, and lightening a white
  tile does nothing. Text colours are now explicit, and `shade()`
  darkens light colours instead of lightening them.
- To flash: `../homemesh_ha_firmware_20oct.bin` (app only, keeps the
  pushed layout; SHA-256 `74be98dd…f5ed`) or `_20oct_full.bin` (whole
  board, erases the pushed layout — see Safety net; `836fa814…6017`).
  Check the boot log against the healthy one in Safety net afterwards.
  If anything looks wrong, the 18 Oct images are the known-good ones.

**Pages are names, not HA areas (20 Oct evening, user's request).** The
firmware never cared: a layout's `areas` are strings and entities point
at them by index, so a page can be anything. `select_layout()` gained
`pages=` (entity_id -> page name, overriding its HA area) and
`sensor_pages=` (sensor -> page, `""` hides it; placed sensors win over
ones merely in the area). With neither given the output is unchanged —
the byte-identity tests cover the old cases and three new ones. In the
designer: a page chooser per device and per sensor, add / rename /
remove pages, drag a tile onto another page, and a warning when a page
title would run into the header's temperature (`sim_text_width()`, the
panel's own font metrics; the title label is unbounded on the panel).
Still max 6 pages besides Home. **No firmware change, no reflash.**
Verified in the real app against a scratch project (21 checks: place a
roomless light on a new page, rename a room page and its sensor follows,
save, reload with nothing dirty, remove a page) and on the simulator's
render. The paper and the tkinter tool still describe pages as rooms.

**21 Oct: fan speed, automation colours, demo automations.** At the
user's request. **On the board since 22 Oct** (whole-board flash, md5
verified; that also erased the stored layout, so the panel runs the
compiled `ha_layout.h` until pushed). Booted clean: 15,408 B pool free,
8,188 B stack free, no filter overflow. Built for all envs (env:ha static
RAM 123,200 B, +176), saved as `../homemesh_ha_firmware_21oct.bin` (app
only, SHA-256 `974b4a0d...`).
- **Fans with `supported_features & 1` (SET_SPEED) are `Dimmable`**, in
  both `classify()`s. The firmware reads HA's `percentage` (0-100, null
  when off; null/0 keeps the last level like a light) and sends
  `fan.set_percentage`; the entities filter gained `percentage` in both
  branches (it logs `ENTITIES FILTER OVERFLOWED` if that ever stops
  fitting - check the boot log after flashing). `EntityDesc.fan` makes
  the detail slider say "Speed". **Old firmware + a pushed speed fan =
  brightness sent to a fan, which HA rejects** - flash before pushing.
- **Actions may carry `"color":"#RRGGBB"`**, only when chosen
  (`build_layout(..., action_colors=)` / `buildLayout(t, theme, colors)`;
  the header carries none, like the theme). Lenient in the firmware:
  a bad colour costs the button its colour. Text on it is dark or white
  by brightness. Automation labels now wrap to two lines then "..." -
  they used to have no height, so LONG_DOT never cut.
- HA now has 2 scenes, 2 scripts, 2 automations (in `C:\homeassistant`
  `scenes.yaml` / `scripts.yaml` / `automations.yaml`), made through the
  config API and each run and checked. Welcome Home fires when
  `input_boolean.panel_demo_switch` goes off -> on.
- The test fixture gained fans' `supported_features` and the six
  actions (friendly names only). `tests/slider_probe.mjs` presses the
  detail slider in the simulator and prints the command sent.

**Flashing from the designer was built on 22 Oct and then reverted at
the user's request** (commit `f02f41d`, reverted by the next one): Web
Serial + esptool-js, a whole-board image written by a post-build script.
It worked on the board. To bring it back, revert the revert.

**Presentation state, 22 Oct.** Panel flashed (app only) with the 21 Oct
firmware from a clean tree and pushed the demo layout: the same five
devices plus **four automations in their own colours** (All Off
#E5484D, Goodnight #5B5BD6, Movie Night #8E4EC6, Welcome Home #30A46C),
saved to `include/ha_layout.*`. Watched on serial: push received,
stored, restart, reconnected and logged in. Boot memory with it: 11,860 B
free, largest block 11,176. **Six automations cost too much pool**:
`designer/tests/stress_sim.mjs` (walks every screen in the simulator,
which has the panel's 40 KB pool, and reports the least free) gave 5,020
B with six, 7,316 B with four, 11,760 B with none. Run it after adding
anything to the layout. Preflight passed in full. No theme on the panel.

**22 Oct, later: unavailable devices, push saves, the ESP32 access point.**
- **A device HA reports `unavailable` is drawn offline** (user's
  request): faded tile, the `offline` glyph (MDI lan-disconnect, added
  to the icon font, not offered in the designer's picker), "Offline"
  for its reading, no level bar, not lit, and taps ignored.
  `EventType::Availability` is the **last** enum value so the earlier
  numbers (shared with the simulator's `Ev`) hold. `unknown` still
  reads as off. Only the tile is set DISABLED; the chevron checks
  `avail[]` in `chevron_cb()` instead, because changing every
  chevron's state starts a theme transition on each, out of the pool
  at once: worst case free fell from 7,316 to 3,872 B before that fix.
  Designer Live mode passes it through. Flashed to the panel 22 Oct
  (app only). `tests/render_sim.mjs ... offline` renders it;
  `OFFLINE=1 node tests/stress_sim.mjs` walks it (6,980 B worst, 0
  commands sent for the offline device).
- **Push now also saves to the project** (designer), so the designer
  opens on what the panel has and a reflash falls back to it. The user
  reported the designer "not having the last uploaded layout": pushes
  had never been saved.
- **The demo network is now the ESP32 access point** (`env:ap`, flashed
  22 Oct to a spare CP2102 board on COM3, which needed the CP210x driver
  and BOOT held to flash - no auto-reset into download mode). It picked
  channel 11. The laptop joins `Pranesh_laptop` with 192.168.137.1 set
  by hand and gets internet over phone USB tethering. **Opening COM3
  the normal way resets the AP** (its RTS reaches EN) and drops the
  network; read it with DTR and RTS held low before `open()`. Windows
  will not rejoin a network from a script without Location services.
  `tools/preflight.ps1` now accepts either network.
- **The layout the user pushed on 22 Oct (9 devices, 4 pages) leaves
  6,488 B free at boot, largest block 5,804** - about half the 5-device
  layout's. Run `tests/stress_sim.mjs` on it once it is saved.

**22 Oct, evening: the LVGL pool is 64 KB on the heap.** The user's
9-device layout left 6.5 KB free at boot, and adding a fan made the
designer go blank: the simulator hit `ASSERT ... lv_anim.c:90` (pool
exhausted) and exited - and the board would have restart-looped on that
layout; even the 9-device one crashed in a full screen walk. Now
`LV_MEM_POOL_ALLOC malloc`, `LV_MEM_SIZE 64 KB` in `include/lv_conf.h`:
static RAM fell 123,304 -> 82,248 B, so the heap pays only ~24 KB net.
Worst case free in `tests/stress_sim.mjs`: 25.1 KB (user's layout),
20.3 KB (+fan), 2.1 KB for the largest layout the format allows (12
devices all favourites, 6 pages, 8 automations). **Flashed 22 Oct**
(also saved as `../homemesh_ha_firmware_22oct.bin`, SHA-256
`e0d0ece1...`): `[mem] UI built: ... 31064 free, largest block 30380 |
heap 139004 free, low 138756` with the user's 9-device layout - the pool
figure matches the simulator to the byte. Not yet seen with Wi-Fi and
Home Assistant connected (the access point was unpowered): watch the
heap's `low` once it is.
- The designer no longer goes blank when a layout is too big: the
  simulator boots a new layout and draws a few frames before swapping it
  in, a crash there or later is caught, and the verdict says the panel
  would run out of graphics memory and blocks Push. An accepted layout
  shows its free pool, flagged under 14 KB (walking every screen costs
  6-9 KB more than the first draw).
- `sim/build.py`'s object cache now keys on the project headers too. It
  ignored them, so editing `lv_conf.h` left a 40 KB pool in the
  simulator with no sign of it.

**`devices/`: the user's ESPHome boards** (22 Oct). `smart-switch.yaml`
(4 relays + touch pads, 192.168.137.202) and `air-monitor.yaml` (MQ-135
+ DHT11, .201; its servo removed at the user's request). `setup.ps1`
makes `devices/.venv` with ESPHome 2026.9.1 (Python 3.14 works),
`flash.ps1 <device> [COMx]` flashes by USB or OTA. `secrets.yaml` is
gitignored. Both configs pass `esphome config`; neither compiled or
flashed here. The access point's `MAX_CLIENTS` is 4 - panel, laptop and
these two exactly. The HA log's `switch.smart_switch_* missing`
warnings were taps on those relays while the board was unreachable.

**23 Oct: the air monitor in the status bar; Status tiles.** User's
request, panel flashed and the layout pushed the same day.
- **Layout key `"status"`** (optional, top level): a binary sensor the
  status bar shows as a small green "Good" / red "Bad" block on every
  page, hidden while unknown or offline. `build_layout(status=)` /
  `build_header(status=)` -> `#define HA_STATUS_ENTITY`, emitted only
  when set, so older headers stand; designer: "Status block" chooser
  under Header sensors. Firmware: `g_layout.status`, subscribed with the
  rest, routed as `EventType::HeaderStatus` (1 bad, 0 good, -1 unknown).
- **A page with no temperature/humidity of its own shows the average of
  all pages' sensors**, so the one air monitor fills every header. The
  designer's title-width warning counts the readings and the block.
- **`EntityKind::Status`** (appended last): `binary_sensor`s, a read-only
  tile, green "Good" off / red "Bad" on, no icon, taps ignored. In both
  `classify()`s. The user did not want the air reading as a tile, so the
  layout does not use one; the kind stays.
- The layout now: the air monitor's temperature/humidity on Relay_Module
  (demo "outside" pair hidden), status = its Bad Air. Boot: 30,848 B
  pool free; heap low 135,876 B with Wi-Fi up; worst case in
  `stress_sim.mjs` 24.9 KB.
- `ESPHome`: the air monitor was added to HA through the config-flow API
  (host .201, key from `devices/secrets.yaml`). Its first flash needed
  `esptool --no-stub` (power dip at write start) and PowerShell, not Git
  Bash (ESP-IDF refuses MSYS). See `devices/README.md`.

**Open on 22 Oct: the hotspot link to the panel is lossy.** The user
reported the panel "does not connect back" after a push. Measured: the
laptop's link to its router is clean (0% loss, 7 ms) but pings to the
panel lose 50-75% at -27 to -46 dBm, replies 25-180 ms; on 18 Oct it was
0% loss, 40 ms, same setup. On this link the WebSocket handshake takes
minutes, so after any restart the panel looks dead for a long time; it
does recover by itself (about 3 min after the 22 Oct flash). After the
flash's hard reset the hotspot also dropped it once with `reason 6
(NOT_AUTHED)` - its old association was never ended. Band Selection is
still 2.4G Only. Channel unknown (netsh needs Location services). Not
fixed. Candidate firmware changes, not made: `WiFi.disconnect()` before
`ESP.restart()` on a push, and logging every reconnect (only the first
`[wifi] connected` is printed, so a reconnected panel is silent).

Toolchain facts found on 20 Oct, all on this laptop:
- **Emscripten SDK at `D:\Projects\emsdk`** (outside the repo). This
  emsdk ships `emcc.exe`, not `emcc.bat`; `sim/build.py` calls it by
  absolute path with `EM_CONFIG` set — no `emsdk_env` needed.
- **npm 11 blocks install scripts**, so Electron's binary never
  downloads on `npm install`. Run `node node_modules/electron/install.js`.
- **VS Code sets `ELECTRON_RUN_AS_NODE=1`** in its terminals; Electron
  then runs as plain Node and fails with "does not provide an export
  named 'BrowserWindow'". Unset it to launch.
- Node 26 runs `.ts` directly (`--experimental-strip-types`) but not
  constructor parameter properties — keep shared TS strip-compatible,
  and give relative imports their `.ts` extension in files Node loads.
- A portable exe runs from a temp folder; `PORTABLE_EXECUTABLE_DIR`
  holds where it really sits. The project folder choice is remembered in
  Electron's userData `settings.json`.
- Driving the app headlessly: launch it with
  `--remote-debugging-port=N` and send `Runtime.evaluate` over the
  DevTools WebSocket — how every UI check on 20 Oct was done. Close
  stray instances first, or the port lands on the old one.

## Pick up here (18 Oct)

Three days to the 21st. **The firmware is done — freeze it.** The
paper is drafted; what is left is review and small items. In order:

1. ~~Band Selection check in `tools/preflight.ps1`~~ — done 18 Oct.
   It also reads the entity list from `include/ha_layout.json` now
   (the hand-typed list had gone stale), and an unreachable HA no
   longer reads as "entities missing".
2. **The IEEE paper — drafted 18 Oct, 6 pages.** See "What is left"
   item 4 for the spec and how to rebuild it. Left: the team reads it;
   swap in the teacher's shared template if the user finds it (none
   was on this machine); optionally a photo of the real panel for
   Fig. 4, which is currently the designer's preview.
3. Optional, offered 18 Oct, not started: **screenshots straight from
   the panel.** Stream the LVGL frame buffer over serial from the flush
   callback and rebuild a PNG on the laptop (~15 s per capture at
   115200). Pixel-exact true RGB565, which the designer preview cannot
   show, and far better report figures than phone photos of a 3.2"
   screen. About 45 minutes.
4. Optional: the microSD triage (`pio run -e sdscan -t upload -t
   monitor`) — five minutes, still never run. Do it before the card
   appears in any diagram.
5. Decide whether to keep `src/access_point.cpp` — item 5 below.

Done 18 Oct: the tile look (1d), state over `subscribe_entities`
instead of REST (1e), the network root cause (Deployment network), and
LVGL failures that name themselves (Known traps). The layout push was
proven on hardware on 17 Oct.

Cut: **multi-column tiles** (a 2-wide widget with its controls inline).
Format surgery — `span` in the layout, real placement instead of
`grid_for()`'s arithmetic, and the designer's preview and drag both
have to learn it. A day and a half, four days out, with the report
undone. A **background image** was argued down earlier (320x240 RGB565
is 150 KB and tiles cover most of it), and the gradient that stood in
for it was itself removed — see 1d.

Not worth starting before the deadline: retiring MQTT, microSD work.

## Where the project is going

The panel is being moved off MQTT and onto the **Home Assistant
WebSocket API**, with layout authored by a **desktop companion tool**.
Decided 16 Sep. Rationale, because it is not obvious:

- MQTT only reaches entities that publish themselves onto the broker.
  A Hue bulb, a Z-Wave switch or a ZHA device lives entirely inside HA
  and has no topic at all — the panel cannot see or control it. The
  same physical Zigbee bulb is reachable via Zigbee2MQTT and invisible
  via ZHA. The WebSocket API reaches every entity uniformly.
- `call_service` is identical across every integration, so control
  stops being integration-dependent.

### The PC/ESP32 split (load-bearing — do not collapse it)

`get_states` on a real HA instance returns **hundreds of KB of JSON**.
That will not fit on a WROOM-32 with no PSRAM. This is the same wall
that makes on-device entity discovery infeasible, and it is why the
work is split:

- **The PC** does discovery. It calls `get_states`, shows the entity
  picker, and writes a layout naming 4-8 specific `entity_id`s.
- **The ESP32** never calls `get_states`. It does `subscribe_entities`
  on exactly the entities in its layout, and `call_service` to control
  them. Every message it handles is small and bounded.

Keep it this way. Moving discovery on-device reintroduces a problem
that has no solution on this hardware.

### Firmware WebSocket surface

The whole protocol the panel needs, all on one socket:

```
connect ws://<ha-host>:8123/api/websocket   -> {"type":"auth_required"}
{"type":"auth","access_token":"..."}         -> {"type":"auth_ok"}
{"id":1,"type":"subscribe_entities","entity_ids":[...]}
    -> {"event":{"a":{...}}}   a snapshot of every entity, once
    -> {"event":{"c":{...}}}   then only what changes
{"id":2,"type":"subscribe_events","event_type":"homemesh_layout"}
    -> layout pushes from the desktop tool
{"id":N,"type":"call_service","domain":"light","service":"turn_on",
 "target":{"entity_id":"light.x"},"service_data":{"brightness":180}}
```

`subscribe_entities` replaced `subscribe_trigger` plus one REST GET per
entity on 18 Oct — see "What is left" 1e for why. The panel opens
exactly one TCP connection to HA and nothing else.

Availability is handled by WebSocket disconnect detection; there is no
LWT equivalent.

### Endgame

Desktop tool: entity picker from `get_states` -> drag-and-drop 320x240
canvas -> layout JSON -> pushed to the panel -> stored in NVS ->
LVGL widgets built dynamically at boot. Firmware stays fixed; layout is
data. All of this is written (`tools/layout_designer.py` and
`src/layout_store.*`, 15 Oct) and proven on hardware (17 Oct).
**Do not plan a reflash step** — that would require esptool, a
toolchain and CH340 drivers on the user's machine, and breaks the
"runs on any PC" goal.

## Hardware (confirmed on the actual board — do not re-derive)

Board: **Probots ESP32-2432S032** (3.2" CYD family, capacitive)

This board has batch variation in both controllers. Ours was
identified empirically:

| Part            | What we have | Notes                                    |
|-----------------|--------------|------------------------------------------|
| MCU             | ESP32-WROOM-32 | Classic ESP32. No PSRAM, no LCD_CAM.   |
| Display driver  | **ST7789V**  | NOT ILI9341. Needs inversion + BGR.      |
| Touch           | **GT911**    | I2C, capacitive. Well supported.         |
| USB-serial      | CH340        |                                          |
| Panel           | 240x320 IPS  | Portrait native.                         |
| microSD         | **present, not characterised** | 128GB card fitted 7 Oct. See below. |

A **128GB microSD card** was added to the board on 7 Oct and nothing
uses it yet. Two things must be established before any feature is
designed onto it, and neither should be assumed:

- **Which SPI bus the slot is on.** The TFT is on HSPI
  (SCLK=14 MOSI=13 MISO=12 CS=15). If the slot shares those pins every
  card access has to be fenced against TFT_eSPI's transactions, which
  changes what the card is worth. Community wiring for this board puts
  it on VSPI (18/23/19/5), making the two independent.
- **Whether the card mounts at all.** 128GB is SDXC and ships
  formatted **exFAT**; the Arduino SD library reads FAT16/FAT32 only,
  so a perfectly good card reads as dead hardware. Reformat FAT32 —
  Windows will not do it above 32GB, so use the SD Association
  formatter or guiformat.

`env:sdscan` (`src/sd_card_scan.cpp`) answers both and then repeats the
test with the display live, because mounting on an idle bus proves
nothing about the running panel. **It has not been run yet.**

A 128GB card is wildly oversized for a layout, and layouts go to NVS
anyway — so the card's plausible use is logging or assets, not the
layout path. Decide that *after* the triage, not before.

CGRAM offsets are **not** needed — borders render flush to all four
edges with default settings. Don't add offset defines.

### Display config

Set via build flags in `platformio.ini`, not `User_Setup.h`.
`USER_SETUP_LOADED=1` makes TFT_eSPI ignore its bundled config.

```
TFT_MISO=12  TFT_MOSI=13  TFT_SCLK=14
TFT_CS=15    TFT_DC=2     TFT_RST=-1
TFT_BL=27    (HIGH = on)
SPI_FREQUENCY=27000000
ST7789_DRIVER=1
TFT_INVERSION_ON=1
TFT_RGB_ORDER=TFT_BGR
```

Colour symptoms and what they mean, in case anything regresses:
- Red and blue swapped, green/white/black fine → RGB order wrong
- Everything inverted (red reads cyan) → inversion flag wrong
- Image shifted with garbage stripe → offsets (we do NOT have this)

### Touch config

GT911 over I2C. Record the SDA/SCL pair from the scan in
`src/touch_driver.cpp` — it is the source of truth for pins.

GT911 answers at 0x5D or 0x14.

## Deployment network

The demo runs on a **laptop hotspot**, not campus WiFi. Campus networks
break this three ways: client isolation blocks panel-to-laptop traffic,
captive portals cannot be traversed by an ESP32, and mDNS is filtered.
The hotspot makes the demo independent of the venue.

- **Host IP is fixed at `192.168.137.1`.** Windows Mobile Hotspot runs
  on ICS, which always takes that address. More predictable than a
  router DHCP lease — hardcode it.
- **Hotspot band must be 2.4 GHz — and on this laptop the hotspot's
  own setting is not enough.** ESP32-WROOM-32 is 2.4 GHz only. The
  laptop's card (MediaTek MT7921) has a single radio, so Mobile Hotspot
  shares a channel with the laptop's own WiFi link: when that link sits
  on 5 GHz the hotspot goes with it, whatever its band setting says,
  and the panel cannot see the network at all — it logs
  `[wifi] not connected: reason 201 (NO_AP_FOUND)`. The router moves
  the laptop between bands as it pleases, so for days this looked like
  random "no link" and flaky firmware (found 18 Oct). **Fix, applied
  18 Oct:** Device Manager → Network adapters → MediaTek Wi-Fi 6 MT7921
  → Advanced → **Band Selection → 2.4G Only**, or from an admin
  PowerShell:
  `Set-NetAdapterAdvancedProperty -Name "WiFi" -DisplayName "Band Selection" -DisplayValue "2. 2.4G Only"`.
  Then turn the hotspot off and on. A driver update can reset it —
  hence item 1 of "Pick up here". For diagnosing: `netsh wlan` will not
  report band or channel unless Windows Location services is on, but
  the adapter's link speed is a tell (866.7 Mbps means 5 GHz).
- **Mobile Hotspot will not start without a network to share.** If the
  venue's WiFi is unusable, set the phone's hotspot to 2.4 GHz, join
  the laptop to it, and turn the laptop's hotspot on as usual. (The
  ESP32 access point in "What is left" 5 removes this dependency, but
  has never been run.)
- **WPA2, not WPA3.** WPA3-SAE on WROOM-32 with the Arduino core is
  unreliable.
- **Inbound firewall access to TCP 8123 is required — but a hand-made
  port rule is not.** Docker Desktop installs its own inbound Allow rule
  for `com.docker.backend.exe`, and because the published port is served
  by that process, the rule already covers 8123. Verified 17 Sep: this
  demo works with no port rule of its own. If neither path exists the
  panel joins WiFi fine and then times out on the WebSocket — reads as a
  code bug, isn't one. `tools/preflight.ps1` accepts either and says
  which one is carrying it.
- **Disable hotspot auto-shutoff.** Windows kills it after ~5 min with
  no clients connected.
- Run HA in **Docker Desktop (WSL2 backend)**; published ports bind
  `0.0.0.0` so `192.168.137.1:8123` works. Avoid the VM route —
  bridging onto a virtual ICS adapter is painful and Hyper-V can fight
  with Mobile Hotspot. Pull the image while internet is available.

### The laptop is the whole environment (decided 16 Sep)

Home Assistant, the hotspot, the discovery tool and the build all live
on the **demo laptop**. The wired desktop is not part of the runtime
picture. This was decided deliberately, because the obvious-looking
alternative is a trap:

- Running HA on the desktop for development and moving it to the laptop
  for the demo means HA's address changes from `192.168.100.x` to
  `192.168.137.1`. `HA_HOST` is compiled into `secrets.h`, so that is a
  reflash on demo morning.
- **So the hotspot is the development network too, not just the demo
  network.** The panel sits on `192.168.137.1` from day one and nothing
  about the network changes on the day.
- Flashing is serial, so the panel can be plugged into whichever
  machine is convenient regardless of which network it joins.

`tools/ha_discover.py` and `tools/layout_designer.py` therefore run on
the laptop against `http://localhost:8123`.

### Current infrastructure state (18 Oct)

Home Assistant exists and the panel drives it. First verified end to
end on 17 Sep; as of 18 Oct the whole path is hotspot, auth,
`subscribe_entities`, `subscribe_events` and `call_service`, with the
entity table **and the colours** loaded at runtime from NVS.

- **HA runs in Docker on the laptop.** Config lives at
  `C:\homeassistant`, outside the repo — its `.storage` holds the auth
  tokens and the recorder database. `ha/docker-compose.yml` *documents*
  that running instance rather than creating a second one.
- **The `demo` integration provides the entities**, plus one
  `input_boolean` helper. No broker, no hardware, and being non-MQTT it
  is what demonstrates the new architecture works.
- **Areas are Living Room, Kitchen and Bedroom**, created by HA
  onboarding. Entities are assigned at the entity level, which wins over
  the device's in `ha_discover.py`'s resolution.
- **`include/ha_layout.h` is committed, not gitignored.** It is
  generated, but it is also the only record of the demo layout, and a
  build that needs HA running to succeed is a bad thing to depend on at
  9am. It contains entity_ids and friendly names, no credentials.
- HA URL and long-lived access token: **obtained**. The token lives in
  `tools/.env` and `include/secrets.h`, both gitignored.
- `WIFI_SSID` must be the **hotspot**, not the house network. It pointed
  at the house network on 17 Sep, which is the real reason the panel
  could not reach `192.168.137.1` — not the missing IP it looked like.
- The dev desktop has no wireless interface (`netsh wlan show
  interfaces` reports none), which is why it cannot be the demo machine
  and why the split above resolves toward the laptop.
- The MQTT broker at `192.168.100.9` is no longer in the loop.
- Git identity was never configured on the laptop (only on the desktop).
  History up to 20 Oct is authored `Pranesh S <king191621@gmail.com>`.
  **From 20 Oct evening, at the user's request, new commits are
  `Garima Nidhi <ishima.2026@gmail.com>`** (set repo-locally). Past
  commits were not re-authored. Commit messages carry no AI attribution
  lines - the user had them stripped from the whole history on 20 Oct.

Added since, on 15-16 Oct:

- **`tools/layout_designer.py`** — the desktop authoring application.
- **Layout and theme load from NVS at boot**, falling back to the
  compiled `ha_layout.h` and `panel_theme.cpp`. See "What is left".
- **A push is proven on hardware** (17 Oct): the board came back up
  with `[layout] loaded from NVS: 5 entities, 3 areas, 2 sensors
  (275/2560 arena)`.
- The layout on the board is whatever was last pushed. `env:ha` logs
  which source it booted from — read that rather than assuming.

**The largest frame is now the `subscribe_entities` snapshot: 2536
bytes for 7 entities** (18 Oct) — still the PC/ESP32 split doing its
job, against the hundreds of KB a `get_states` would cost. A layout
push frame is 955 bytes for the 700-byte 5-entity layout (18 Oct; it
was ~890 for an older layout). The WebSockets library drops any frame over
15 KB; see "What is left" 1e.

## Toolchain

- PlatformIO, `board = esp32dev` (no CYD-specific board definition exists)
- Arduino framework
- Desktop tools are Python 3 + stdlib `tkinter`. Only `websockets` is
  a real dependency (`tools/requirements.txt`) - keep it that way, the
  companion tool is supposed to run on a machine with no toolchain.
- **LVGL pinned to 8.3.x.** 9.x changed the display and input driver
  API. Most CYD example code online is 8.3. Do not upgrade.
- TFT_eSPI 2.5.x
- `links2004/arduinoWebSockets` + ArduinoJson — for the HA client.
  `HTTPClient` is no longer used (18 Oct).
- PubSubClient — legacy, removable once the WebSocket path lands
- `pio` is not on PATH in this machine's shells. It lives at
  `%USERPROFILE%\.platformio\penv\Scripts\pio.exe`, and the Python
  beside it has pyserial, for reading the panel's log from a script.

## Architecture rules

Keep display init and the LVGL touch read callback in their own
files with a clean interface. Hardware specifics must not leak into
UI or transport code. This was a deliberate hedge against batch
variation; it also made the MQTT-to-WebSocket swap cheap, and it stays.

```
src/
  main.cpp            entry, LVGL tick + timer handler
  display_driver.*    TFT_eSPI init + LVGL flush callback
  touch_driver.*      GT911 + LVGL indev read callback
  ha_client.*         HA WebSocket: auth, subscribe_entities, call_service
  layout_store.*      the entity table: NVS layout, ha_layout.h fallback
  panel_theme.*       g_theme, the nine colours
  mqtt_client.*       legacy MQTT transport, being retired
  access_point.cpp    env:ap only - a second ESP32 as the demo network
  ui/                 LVGL screens and widgets
```

`panel_model.h` is the contract between transport and UI: `PanelEvent`,
`LinkState`, the entity table. **The transport swap must not change
it** — `ha_client.*` presents the same three-function interface as
`mqtt_client.*` (start / next_event / send_*), so `main.cpp` and
`ui/dashboard.cpp` barely change.

## Known traps

- **`get_states` is too large for the ESP32.** See the PC/ESP32 split
  above. Never call it from firmware.
- **`lv_timer_handler()` needs calling every ~5ms.** No blocking
  network reconnect loops on the same path or the UI visibly stutters.
  Network lives on its own FreeRTOS task (core 0) and hands events to
  the LVGL loop through a queue.
- **Network task priority must be 0, not 1.** At 1 it starves IDLE0 and
  trips the task watchdog. See the comment in `mqtt_client.cpp`.
- **Nothing large goes on the net task's stack.** It has 12 KB and the
  WebSockets call chain is already ~20 frames deep when a frame
  reaches `handle_text`. A 4 KB `StaticJsonDocument` local there cost
  a day: GCC inlined the function into `handle_text`, so the 4 KB was
  reserved on *every* frame, and it panicked inside an unrelated
  `Serial.printf` during startup with no layout in sight. Big
  documents live at file scope — see `big_doc` and `text_buf` in
  `ha_client.cpp` and `parse_doc` in `layout_store.cpp`. The panel
  logs its stack high-water mark after auth (8272 bytes free on
  18 Oct); watch that number rather than waiting for the canary.
- **Touch coordinates need rotating** to match `tft.setRotation()`.
  Taps landing mirrored or on the wrong axis is a rotation mismatch,
  not a broken touch driver.
- **`LV_MEM_SIZE` is a fixed pool - 64 KB from the heap since 22 Oct
  (it was 40 KB in `.bss`; the notes below are from then).**
  Built widgets come out of it, and so do the temporary buffers drawing
  takes. A failed allocation used to be a silent freeze — LVGL's
  default assert handler is `while(1)` — and since 18 Oct
  `lv_assert_hit()` in `main.cpp` prints `[lvgl] ASSERT FAILED at
  <file>:<line>` with the pool's state, then restarts. The panel logs
  its headroom at boot (`[mem] UI built: ...`): ~15 KB free, largest
  block ~15 KB, with a 5-entity layout. `max_used` counts requested
  bytes only, without block headers, so read the free figures. To grow
  the pool, allocate it from the heap (`LV_MEM_POOL_ALLOC`), not
  `.bss` — see the next trap.
- **`env:ha` has ~1.5 KB of static RAM to spare.** The linker's
  `dram0_0_seg` is the real limit, not the 320 KB PlatformIO reports —
  it said 37.5% used while ~1.5 KB short of overflow. A 48 KB LVGL pool
  failed to link on 18 Oct, and so did a single new 2 KB static buffer,
  which is why `ha_client.cpp` has one shared `text_buf`. Any new
  static buffer in `env:ha` has to be paid for. The heap has ~160 KB
  free; more room comes from there.
- **LVGL states do not reach children.** `lv_obj_add_state()` changes
  one object; a child styled for `LV_STATE_CHECKED` never sees it
  unless the state is set on the child too. The icon shipped with dead
  CHECKED styling for exactly this reason. `refresh_entity()` now sets
  it on the tile, icon, reading and bar explicitly.
- **Gradients band on this panel.** RGB565 has 32 levels of red and
  blue, so a subtle ramp on a dark colour is a few visible stripes,
  each faintly tinted because green steps on different rows. The
  designer preview draws 24-bit colour and cannot show it. Keep fills
  flat, or turn on `LV_DITHER_GRADIENT`.
- **WiFi failures are logged with the driver's reason code** —
  `[wifi] not connected: reason N (NAME)`. 201 / NO_AP_FOUND means the
  panel cannot see the SSID at all; on this laptop that is almost
  certainly the hotspot on 5 GHz (see Deployment network).
- **"Ghost" tiles were the link dropping, not memory.** Tiles fade to
  40% opacity while the link is down; with the reconnect loop they
  cycled, and lingered between pages. Gone once the link was stable
  (18 Oct). The pool had ~15 KB to spare throughout.
- Charge-only USB-C cables power the board and backlight but never
  enumerate. If there is no COM port, suspect the cable first.
- **PubSubClient buffer defaults to 256 bytes** (legacy path only). HA
  discovery payloads exceed this and fail *silently*;
  `MQTT_MAX_PACKET_SIZE=1024` is set in build flags.

## Legacy MQTT implementation

**MQTT is NOT a rubric requirement** (confirmed 16 Sep). Retained here
as documented history and as a working fallback. Once the WebSocket
path lands, the broker and the simulator can be dropped entirely — HA
**helpers** (`input_boolean`, `input_number`, template lights) replace
the simulated entities, need no broker, and being non-MQTT they are
also what demonstrates that the new architecture works.

What the MQTT path does today:

- HA MQTT Discovery so entities appear without manual YAML
- **Retained** state messages so the panel is correct after reboot
- **LWT** on an availability topic so HA shows offline, not stale
- Entities greyed out on screen when the broker is unreachable

### Topic contract

The entities are simulated by `mqtt_simulated_devices/ha_placeholder_entities.py`,
which owns their HA discovery. The panel is a **controller**: it
subscribes to retained `homemesh/sim/<slug>/state` topics and publishes
to `homemesh/sim/<slug>/set`. Device availability is `homemesh/sim/status`.
The panel's own discovery/LWT is a WiFi-signal diagnostic sensor with
availability on `homemesh/panel/status`.

Credentials live in `include/secrets.h` (gitignored).

## Scope

The entity list is **authored, not fixed** — pick it in
`tools/layout_designer.py` and push it. `include/ha_layout.json` is
whatever was last written; `include/ha_layout.h` is the same selection
as the compiled fallback. Bounded at `MAX_ENTITIES` (12), six areas,
six tiles per page.

**Do not quote an entity count as a fact about the project** — it
changes whenever someone opens the designer. What has to stay true is
the rubric minimum below.

Rubric minimum, all met:

- 3 lights (at least one with a brightness slider)
- 1 switch
- 1 sensor readout (temp/humidity)
- Connection status indicator

Prefer 4-6 entities with real bidirectional sync over many entities
with one-way control.

Two things about the `demo` integration worth not rediscovering:

- **Every demo light reports `RGB`, never `Dimmable`.** That is fine —
  `RGB` is on/off plus brightness plus colour, and the slider is gated
  on `kind != Toggle`, so all four lights get one.
- **There is only one temperature/humidity pair**, and both sensors are
  in Living Room. Bedroom and Kitchen render blank sensor slots.
  Deliberate: template sensors would mean inventing plausible room
  temperatures, and blank beats invented.

UI is **landscape** (320x240, `PANEL_ROTATION` in `main.cpp`).
Touch mapping was verified correct in portrait with no swap/invert
flags; rotation is handled in `touch_driver.cpp`.

Cut list, in order, if behind schedule: drag-and-drop authoring (fall
back to a checkbox picker and a fixed 2x2 grid), brightness sliders,
second screen, animations. **Nothing has been cut as of 18 Oct** — the
first item on that list shipped instead. If something has to give in
the last days, give up the microSD work and retiring MQTT, not
anything on this list.

## What is left

Accurate as of 18 Oct. Ordered by what unblocks the most.

### 1. Layout as data, not a header — done, proven on hardware 17 Oct

Both paths are confirmed on the board. With nothing stored it falls
back to the compiled `ha_layout.h` (16 Oct). A push from the designer
arrived, validated, was stored, and the board came back up with
`[layout] loaded from NVS: 5 entities, 3 areas, 2 sensors (275/2560
arena)` (17 Oct).

How it works, so it is not re-derived:

- `src/layout_store.*` owns the entity table. `layout_boot()` reads a
  layout from NVS and falls back to the compiled `ha_layout.h` if
  there is nothing stored, nothing parseable, or nothing that
  validates. **The header stays in the build deliberately** — a board
  never pushed to still has to come up demoable, and a build whose UI
  depends on NVS having been written is a bad thing to discover at 9am.
- Delivery rides the WebSocket the panel already holds. The desktop
  tool fires a custom **`homemesh_layout`** HA event; the panel is
  subscribed to it. No HTTP server on the ESP32, and neither end ever
  needs the panel's IP address — both only have to know where HA is.
  `LAYOUT_EVENT` in `layout_store.h` and `ha_discover.py` must match.
  Verified 16 Oct on the HA side: `subscribe_events` succeeds,
  `fire_event` returns success, and the event reaches a subscriber as
  an ~890-byte frame.
- **A push is recognised from the raw frame, before parsing.** It used
  to be decided from the filtered document, and that failed silently:
  the hot-path filter is a fixed-size document, and ArduinoJson drops
  keys that do not fit **without an error**. Adding one key to an
  already-full 256-byte filter made every push vanish with nothing on
  the console. Do not move this check back into the parsed document.
- There is **no acknowledgement** back to the desktop tool. "Pushed N
  bytes" means HA accepted the event, not that the panel took it. The
  serial log is the only confirmation.
- Applying a layout **restarts the board**. Tearing down a screen's
  worth of LVGL objects to build another is where the fixed 40 KB pool
  fragments, and OOM there is a hang, not a clean error. A reboot is a
  few seconds and is deterministic. This is a choice, not a shortcut.
- Strings live in a fixed **2560-byte arena**, not the heap, so a
  layout cannot grow into territory LVGL needs. A 5-entity layout used
  275 bytes on 18 Oct; a maxed-out layout would use 2320. Over that it
  is rejected whole, with a reason.
- Validation runs **before** anything is stored and never touches the
  live table, so a rejected push leaves the panel running on the
  layout it has. Every rejection names its cause on serial — a push
  that silently does nothing is the worst version of this feature.

The trap this had to work around, which is load-bearing: control
messages (auth, command results) go through a small filter into a
fixed **768-byte** document, and that is what keeps the hot path
bounded. A 1002-byte layout does not fit it, and neither does the
~2.5 KB state snapshot. So both are recognised from the raw frame and
parsed on their own paths into the shared `big_doc`, paid only when
one arrives — **do not widen the shared filter to make either fit.**

`layout_store.cpp` is excluded from `env:panel` and `env:demo`: it
needs ArduinoJson, which those paths do not carry.

### 1b. Colours as data — done 16 Oct

Styling was nine `#define`s in `ui/dashboard.cpp`. A pushed layout can
now carry an optional `theme` object and the designer has swatches for
all nine. Worth knowing:

- All 34 colour uses go through those nine macros, so they kept their
  names and now read `g_theme`. The rest of `dashboard.cpp` was
  untouched — keep it that way.
- `g_theme` lives in **`src/panel_theme.cpp`**, not the layout store,
  because `ui/dashboard.cpp` needs it in every environment and the
  store is only built into `env:ha`. `env:demo` and `env:panel` keep
  the compiled colours, which is correct — they cannot receive a theme.
- A missing colour means *keep the default*, so a partial theme is
  valid and no colour is ever undefined. `theme_defaults()` in
  `panel_theme.cpp` and `THEME_DEFAULTS` in `ha_discover.py` must
  agree.
- The tool sends only colours that differ, so an unstyled layout has
  no `theme` key at all.

### 1c. Icons per entity — done 17 Oct

A device-type glyph in what was dead space in the middle of each tile.
16 Material Design Icons glyphs (light, ceiling, lamp, strip, fan, ac,
toggle, plug, temp, humidity, tv, speaker, lock, blinds, home, unknown)
compiled into **LVGL fonts** at 28 px and 18 px,
`src/ui/fonts/panel_icons_*.c`. A glyph draws like text straight from
flash, so the icons cost ~7.6 KB of flash and nothing from the 40 KB
pool; decoded images would have come out of it.

- The icon is layout data. Each entity carries an icon *name*, picked
  in the designer (defaulting from the domain) and pushed like
  everything else. `icon_glyph(name)` in `ui/panel_icons.cpp` maps a
  name to a glyph; which name applies is decided in `layout_store.cpp`.
- `EntityDesc.icon` is the struct's **last** member on purpose, so the
  MQTT and fake transports' brace-initialisers still compile.
- To change *which* icons exist: `bash tools/make_icon_font.sh` (needs
  node and Pillow, build-time only). It regenerates the fonts, the name
  table and the designer's preview masks, reproducibly. Changing which
  icon an entity uses needs none of that — pick it and push.

### 1d. Tile look — done 18 Oct

The answer to "the UI looks plain". In `ui/dashboard.cpp`:

- Cards have a 10 px radius and a 1 px lit top edge (`CARD_EDGE`) on a
  **flat** fill. They had vertical gradients for a day, which banded
  badly on the RGB565 panel (see Known traps) — removed, with a test
  that fails if one comes back.
- **ON lifts the card instead of flooding it amber.** The tile goes a
  shade lighter (`CARD_LIFT`), the icon turns amber in a tinted disc,
  and the reading and brightness bar turn amber. Six solid amber tiles
  on one page was noise. The detail panel's single big button still
  floods, where loud is right.
- A lit dimmable reads its level, `72%`, not `ON`. Not `ON 72%`: at
  Montserrat 20 that is 84 px, and the bar leaves the reading 64 px on
  a roomy tile. `100%` is 51 px.
- HA reports brightness as null for an off light, so the bar keeps the
  last level — the level it comes back at — drawn muted. In white it
  read as "on".
- The designer's preview mirrors `CARD_EDGE` and `CARD_RADIUS` by name,
  and the contract test checks they agree.
- Not yet seen on hardware: the 60 px compact tiles a 5-6 tile page
  uses. Every page of the current layout has 3 tiles or fewer.

### 1e. State over the WebSocket — done 18 Oct

The panel used to fetch initial state with one REST GET per entity,
made from **inside the WebSocket callback**. Over the hotspot those
GETs timed out (`-11`, `-1`), and while they blocked — up to 21 s —
the heartbeat could not run, so the library dropped the socket,
reconnected and did it all again: the "no link" loop, with every tile
reading `--`. The timeouts themselves turned out to be the hotspot
moving to 5 GHz (see Deployment network), but blocking in the callback
is what turned each flaky GET into a dead connection.

Now one `subscribe_entities` replaces `subscribe_trigger` and the GETs.
HA sends a snapshot of every subscribed entity, then only what changes,
on the socket the panel already holds. `HTTPClient` is gone (34 KB of
flash). All of this was measured against the real HA (2026.9) before
the firmware was written:

- Snapshot: `{"event":{"a":{"light.x":{"s":"on","a":{...}}}}}`, 2536
  bytes for 7 entities. Change:
  `{"event":{"c":{"light.x":{"+":{"s":"off","a":{...}}}}}}`.
- **A change can arrive with no `"s"`** — brightness moving while the
  light stays on. `apply_entity_state()` treats state as optional.
- Frames are routed on the raw bytes (`"event":{"a":` and
  `"event":{"c":`) to `handle_entities_event()`, and parsed with
  ArduinoJson's `"*"` wildcard filter into `big_doc`. A check of the
  firmware's own routing strings against every frame type HA sends
  passed 11 of 11.
- The subscription is built in `text_buf` and prints
  `SUBSCRIPTION TRUNCATED` rather than silently dropping ids, as the
  old 1 KB stack buffer did.
- **Ceiling:** the WebSockets library drops any frame over 15 KB and
  closes the connection. A 12-entity layout with demo-sized attributes
  is ~5 KB, but an entity with an enormous attribute (a WLED light with
  a hundred effects) could push a snapshot over.

### 2. Desktop tool GUI — done 15 Oct (superseded 20 Oct by `designer/`)

`tools/layout_designer.py`. Entity picker grouped by area, Home-page
favourites, a 320x240 preview at 2x, drag to reorder, writes both
outputs. stdlib tkinter only — a pip install of a GUI framework would
undercut the "runs on any PC" goal. Discovery still needs `websockets`,
same as the CLI.

`ha_discover.py` is now a shared module (`fetch_inventory`,
`select_layout`, `build_header`, `build_layout`) with the CLI as one
caller and the app as the other, so the layout format is defined once.
If you change it, change it there. Regression test: the app's write
path must reproduce the committed `ha_layout.h` byte for byte.

Two deliberate limits, both forced by the firmware rather than by time:

- **Drag edits order, not position.** `grid_for()` in
  `ui/dashboard.cpp` derives a tile's position from its index within
  its page, so order is the whole of what the panel can honour. A
  free-form canvas would author layouts the panel silently ignores.
- **Home page order is derived, not editable.** `build_pages()` walks
  the entity list once, pushing each favourite onto page 0 and each
  entity onto its room page, so the two orders are one traversal.
  Reordering a room reorders Home.

Was first on the cut list; nothing was cut to get it.

The preview is a tkinter redraw of the panel, **not LVGL**: same
geometry and colours, but Windows fonts drawn smaller, 24-bit colour
(so it cannot show banding), and every tile drawn off. Judge looks on
the board.

### 3. Retire MQTT

Now genuinely unblocked: the `demo` integration and the `input_boolean`
helper replace the simulated entities, so nothing needs the broker.

- Remove `src/mqtt_client.*`, `mqtt_simulated_devices/`, the
  PubSubClient dependency, `MQTT_MAX_PACKET_SIZE`, `env:panel`, and the
  `MQTT_*` defines in `secrets.h`.
- Keep `v0.1-working-demo` as the historical fallback — deleting the
  code removes a rollback path, so do this after the layout work is
  stable, not before.

Cleanup rather than new capability, but it shrinks what can break.

### 4. Report and demo material — the paper is drafted

**What the faculty asked for (given 18 Oct):** an IEEE conference
format paper, **6 pages, as a Word document**: abstract, keywords,
introduction, methodology (with a block diagram), results and
discussion, conclusion, references — **15 to 18 recent papers or
journal articles**. All authors named, **Pranesh S last**. Faculty
guide: **Suhasini S** (in the Acknowledgment, not the author list —
the user asked for their own name last). Due **21 Oct**.

**`docs/paper/HomeMesh_IEEE_Paper.docx`** — drafted 18 Oct, exactly 6
pages, ~4,080 words, 5 figures, 4 tables, 2 listings, 17 references.

- Built by `python docs/paper/build_paper.py` (python-docx). Do not
  hand-edit the .docx and then rebuild — the script overwrites it.
  Change the text in the script.
- `powershell -ExecutionPolicy Bypass -File docs\paper\check_pages.ps1`
  has **Microsoft Word** open it, report the page count and export a
  PDF beside it. Word is the only reliable judge of layout; check after
  every edit, since the limit is 6.
- Layout follows IEEE's A4 conference template from its published spec
  (Times, 24 pt title, two columns, `Abstract—`/`Index Terms—`,
  small-caps Roman headings, 8 pt references, balanced last page).
  IEEE's site blocks scripted downloads of the real template, and the
  teacher's shared format was not on this machine.
- **Every reference was verified against Crossref** (the DOI registry)
  — authors, venue, year, pages, DOI. Never add one that has not been;
  a fabricated citation in a submitted paper is serious. Citations are
  numbered by first use automatically, and the build fails if any
  reference goes uncited.
- **Every number in it was measured on 18 Oct**, not estimated:
  `get_states` 57,671 B for 123 entities; snapshot 2,403 B for 7;
  changes 144-262 B; a 700 B layout arrives as a 955 B event; snapshot
  11.5 ms after subscribing; service call to state change 3.2 ms median
  (server side, 10 trials); panel RTT 40 ms, 0% loss; firmware figures
  from the boot log and the build report.
- Figures: `docs/figures/paper_block_diagram.py` (Fig. 1) and
  `docs/figures/paper_figures.py` (sequence, message sizes) draw from
  code; `docs/paper/fig_designer.png` is a real screenshot of the
  connected designer; `fig_ui_pages.png` is the designer's preview,
  captioned as such.
- Author block carries register numbers: Garima Nidhi 24BCE1782,
  Taniska Routray 24BCE1578, Bharadhwaj 24BCE1959, Pranesh S 24BCE5138.
  Affiliation "School of Computer Science and Engineering, VIT,
  Chennai" was assumed from the BCE register numbers — confirm.

**`docs/HomeMesh_Panel_Project_Documentation.docx` — rewritten 20 Oct,
current.** The handover document a teammate builds the **slides** from
(the user's stated use). 34 pages: sections 1-16 in course order (with
a literature survey, the designer, the field case study), Appendix A a
20-slide plan naming the figure file for each slide plus anticipated
questions, Appendix B the team-only list of what must not be claimed
and the review-morning checklist.

- Built by **`python docs/build_documentation.py`** (python-docx), which
  then has Word fill the contents page, save, and export the PDF beside
  it. Do not hand-edit the .docx — the next build overwrites it. Every
  page was checked as rendered by Word on 20 Oct.
- References are pulled from `build_paper.py`'s `REFS` at build time
  (the build fails if the two lists diverge), plus technical docs.
- The device table is read from `include/ha_layout.json` at build time.
- Its Appendix B says the board runs the 18 Oct firmware and the new
  designer's push has not been watched arriving on the board. **Update
  both lines and rebuild once either changes.**
- Figures it uses, all regenerable: `docs/figures/arch_*.png`
  (`generate_diagrams.py`, updated 20 Oct to the pushed-layout design
  and `subscribe_entities`; it writes beside itself),
  `docs/figures/img/ui_tile_detail.png` and `theme_presets.png`
  (`designer/tests/render_png.py`, `theme_sheet.py`),
  `docs/figures/img/designer_app.png` (a DevTools screenshot of a
  separate designer instance), and the paper's figures.
- The hardware diagram still leaves the microSD card out, deliberately:
  the triage has not been run (see Hardware).

`docs/PROJECT_BRIEF.md` (18 Oct) is secondary now; it still describes
the tkinter designer in its body.

**Team roles for the review (given 21 Oct).** The user assigned them so
each member owns an area to present and answer for: **Pranesh S** team
lead, hardware and firmware architecture; **Garima Nidhi** UI/UX lead,
panel interface and the designer; **Taniska Routray** Home Assistant,
protocol and networking (assigned by Claude to fill the gap); **Bharadhwaj
B** testing, research and documentation (likewise). They live in
`ROLES`, `PRESENTS` and `ANSWERS` in `docs/build_documentation.py` (title
page, Appendix A: who presents each slide and takes each question) and
in the README's Team table. Change them there if the user does.

### 5. ESP32 access point — written 18 Oct, never run

`src/access_point.cpp`, built as `env:ap`: a second ESP32 as the demo's
own 2.4 GHz network, so the demo stops depending on the hotspot having
a network to share or on the venue's channel. Built as a **drop-in**:
the same SSID and password (read from the panel's own `secrets.h`), the
same `192.168.137.0/24` with the AP at `.254` and DHCP from `.100` to
`.110`, and the laptop takes `192.168.137.1` by hand — so the panel
needs no reflash. It picks the quietest of channels 1, 6 and 11 at
boot. The laptop setup is in the file's header.

Shelved once 2.4G Only fixed the hotspot, and kept as a documented
fallback pending the user's call. Two rules if it is ever used:
**never run it and the Windows hotspot at once** (one name, and the
panel joins whichever is louder), and testing it takes the laptop off
the internet — tether a phone over USB first, or the session running
on the laptop goes offline.

### Deliberate gaps, not TODOs

- Bedroom and Kitchen have no sensor readouts. See Scope.
- The Office and Entrance lights and three generic fans exist in HA but
  have no area. They are spares held in reserve, not omissions. Since
  20 Oct the designer can place them by giving them a page (below);
  `ha_discover.py` without `pages=` still drops them.

## Safety net

- Git repo: `https://github.com/agent192007/homemesh-panel` (private).
  Pushed and current as of 18 Oct.
- Tag **`v0.4-firmware-freeze`** is current, 18 Oct: icons, the tile
  look, state over `subscribe_entities`, and LVGL failures that name
  themselves. This is the demo firmware. Rollback is
  `git reset --hard v0.4-firmware-freeze`.
- Tag **`v0.3-runtime-layout`**: the panel reads its entity table from
  NVS, falling back to the compiled `ha_layout.h`. Boots clean on the
  board, 15 Oct, but still fetches state over REST.
- Tag **`v0.2-ha-websocket-demo`** is the last build with the entity
  table compiled in. Use it if the runtime layout path misbehaves —
  it is the same demo, minus the push.
- Tag **`v0.1-working-demo`** is the older static MQTT build.
- Working binaries saved **outside** the project — `.pio` is
  gitignored, so the repo cannot rebuild them if the toolchain
  misbehaves. This is the real demo insurance. Both 18 Oct images were
  built from a clean tree at the commit `v0.4-firmware-freeze` tags,
  and byte-checked against the build.
  - **`../homemesh_ha_firmware_20oct.bin` / `_20oct_full.bin`** — the
    20 Oct firmware with the name and light-theme fixes, built from
    commit `ece7867`. **On the board since 20 Oct**, healthy boot log
    — see "Added 20 Oct". Flash the same way as below, substituting the
    file name.
  - **`../homemesh_ha_firmware_18oct_full.bin`** (last hardware-verified) —
    bootloader, partition table and app in one image, so it restores a
    board whatever was flashed to it last (`env:panel` and `env:demo`
    use a different partition table). From the project folder:

    ```
    %USERPROFILE%\.platformio\penv\Scripts\python.exe %USERPROFILE%\.platformio\packages\tool-esptoolpy\esptool.py --chip esp32 --port COM7 --baud 921600 write_flash 0x0 ..\homemesh_ha_firmware_18oct_full.bin
    ```

    **It erases the pushed layout.** `merge_bin` pads the gaps between
    parts with 0xFF, and NVS (`0x9000`-`0xDFFF`) is one of them, so the
    panel comes back on the compiled `ha_layout.h` — which matched the
    pushed layout on 18 Oct. Push again from the designer if it has
    changed since. SHA-256 `befe1d08…94ccbe`.
  - `../homemesh_ha_firmware_18oct.bin` — the same app alone. Write it
    at `0x10000` onto a board already on the `env:ha` partition table;
    that leaves NVS, and so the pushed layout, alone. SHA-256
    `70053bc8…0f13b`.
  - `../homemesh_ha_firmware_15oct.bin` and
    `../homemesh_ha_firmware_17sep.bin` are app-only and predate
    17-18 Oct: state over REST, so only as good as the network, and no
    icons.
  - The `../homemesh_demo_firmware_12sep.bin` this file used to name was
    **missing** when checked on 17 Sep, and nothing in the repo could
    reconstruct it. If the MQTT fallback is wanted, rebuild `env:panel`
    from `v0.1-working-demo`. Check the binary exists; do not assume.
- **`tools/preflight.ps1`** checks the things that do not survive a
  reboot — hotspot, firewall path, container, HA reachable on the
  hotspot address, token valid, every layout entity still present. Run
  it from cold before demoing. Exits with the number of failures. Since
  18 Oct it also checks the WiFi card is pinned to 2.4G Only, and takes
  the entity list from `include/ha_layout.json`.
- A healthy boot on the panel's serial log, 18 Oct — compare against
  this before suspecting anything else:

  ```
  [layout] loaded from NVS: 5 entities, 3 areas, 2 sensors (275/2560 arena)
  [wifi] connecting to '<WIFI_SSID>'
  [mem] UI built: LVGL pool peak 13026 of 40960 (31%), now 15652 free, largest block 14968 | heap 161028 free, low 158632
  [wifi] connected, IP 192.168.137.225, RSSI -35 dBm, modem sleep off
  [ha] connecting to ws://192.168.137.1:8123/api/websocket
  [ha] websocket open, waiting for auth_required
  [ha] authenticated
  [ha] subscribed to 7 entities
  [ha] listening for homemesh_layout
  [ha] net task stack free: 8272 bytes
  [ha] initial state: 7 entities in one 2475-byte frame
  ```
- The WebSocket work landed on `feat/ui-paged-navigation`, not
  `feat/ha-websocket` as originally planned, and is merged. **`main` is
  the demoable branch** — it has the paged UI, the discovery tool, the
  WebSocket transport and the layout.

## Conventions

- Block diagrams in the report use plain textbook embedded-systems
  convention: plain rectangles, underscore_named blocks, thin
  connector lines. Not styled cards.
- Leave unknown values blank rather than inventing plausible ones.
