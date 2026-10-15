# HomeMesh Panel

Wall-mounted smart home control panel. ESP32 + 3.2" touchscreen,
talking to Home Assistant. Local-first, no cloud.

Academic project, VIT Chennai. Carries 70% of the grade.

**The 70% review is done and went well.** What remains is the final
review and submission, **20 October**. Prioritise an impressive working
demo over elegance — and at this point, prioritise the report over new
capability, because the engineering is essentially where this file says
it should be and the report is not.

## Pick up here (end of 16 Oct)

Four days left. In order:

1. **Prove a layout push on the board.** This is the only part of the
   architecture never confirmed on hardware. HA's side is verified;
   the panel went offline before the fix could be flashed. Flash
   `env:ha`, push from the designer, and look for
   `[layout] push received` then `[layout] loaded from NVS`.
2. **Run the microSD triage** (`pio run -e sdscan -t upload -t monitor`).
   Five minutes, never run. It decides whether the 128GB card is a
   feature or just hardware that happens to be fitted — and the
   diagrams should not be redrawn until that is known.
3. **Update the report material.** `docs/` describes a version of this
   project that no longer exists. See "What is left" item 4.

Not blocking, and arguably not worth starting: retiring MQTT.

The last session ended with the board physically disconnected —
`getportnames` returned nothing. Replug before assuming anything is
broken.

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
- **The ESP32** never calls `get_states`. It does `subscribe_trigger`
  on exactly the entities in its layout, and `call_service` to control
  them. Every message it handles is small and bounded.

Keep it this way. Moving discovery on-device reintroduces a problem
that has no solution on this hardware.

### Firmware WebSocket surface

The whole protocol the panel needs is four messages:

```
connect ws://<ha-host>:8123/api/websocket   -> {"type":"auth_required"}
{"type":"auth","access_token":"..."}         -> {"type":"auth_ok"}
{"id":1,"type":"subscribe_trigger",
 "trigger":{"platform":"state","entity_id":[...]}}  -> state events
{"id":2,"type":"call_service","domain":"light","service":"turn_on",
 "target":{"entity_id":"light.x"},"service_data":{"brightness":180}}
```

Availability is handled by WebSocket disconnect detection; there is no
LWT equivalent.

### Endgame

Desktop tool: entity picker from `get_states` -> drag-and-drop 320x240
canvas -> layout JSON -> pushed to the panel -> stored in NVS ->
LVGL widgets built dynamically at boot. Firmware stays fixed; layout is
data. All of this is now written (`tools/layout_designer.py` and
`src/layout_store.*`, 15 Oct) but not yet proven on hardware — see
"What is left". **Do not plan a reflash step** — that would require esptool, a
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
- **Hotspot band must be 2.4 GHz.** ESP32-WROOM-32 is 2.4 GHz only.
  Windows 11 often defaults to 5 GHz, and the ESP32 then never sees the
  SSID. Looks exactly like broken firmware.
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

### Current infrastructure state (16 Oct)

Home Assistant exists and the panel drives it. Verified end to end on
17 Sep: hotspot, auth, `subscribe_trigger`, `call_service`, flashed and
working on the board. Still true on 16 Oct, now with the entity table
**and the colours** loaded at runtime instead of compiled in.

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
  It is now set repo-locally to `Pranesh S <king191621@gmail.com>`, the
  identity the whole history uses.

Added since, on 15-16 Oct:

- **`tools/layout_designer.py`** — the desktop authoring application.
- **Layout and theme load from NVS at boot**, falling back to the
  compiled `ha_layout.h` and `panel_theme.cpp`. See "What is left".
- **The push itself is not yet confirmed on hardware.** HA's half is
  verified; the panel's half has never completed a push end to end.
  Do not describe it as working until it has.
- The layout on the board is whatever was last pushed. `env:ha` logs
  which source it booted from — read that rather than assuming.

**Largest WebSocket frame observed: 1608 bytes** — the PC/ESP32 split
doing its job, against the hundreds of KB a `get_states` would cost.
A layout push frame is ~890 bytes, comfortably inside the same budget.

## Toolchain

- PlatformIO, `board = esp32dev` (no CYD-specific board definition exists)
- Arduino framework
- Desktop tools are Python 3 + stdlib `tkinter`. Only `websockets` is
  a real dependency (`tools/requirements.txt`) - keep it that way, the
  companion tool is supposed to run on a machine with no toolchain.
- **LVGL pinned to 8.3.x.** 9.x changed the display and input driver
  API. Most CYD example code online is 8.3. Do not upgrade.
- TFT_eSPI 2.5.x
- `links2004/arduinoWebSockets` + ArduinoJson — for the HA client
- PubSubClient — legacy, removable once the WebSocket path lands

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
  ha_client.*         HA WebSocket: auth, subscribe_trigger, call_service
  mqtt_client.*       legacy MQTT transport, being retired
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
  documents live at file scope — see `layout_doc` in `ha_client.cpp`
  and `parse_doc` in `layout_store.cpp`. The panel logs its stack
  high-water mark after auth; watch that number rather than waiting
  for the canary.
- **Touch coordinates need rotating** to match `tft.setRotation()`.
  Taps landing mirrored or on the wrong axis is a rotation mismatch,
  not a broken touch driver.
- **`LV_MEM_SIZE` is a fixed 40 KB pool.** Dynamically built widgets
  come out of it. LVGL OOM presents as a hang or blank screen, not a
  clean error.
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
second screen, animations. **Nothing has been cut as of 16 Oct** — the
first item on that list shipped instead. If something has to give in
the last four days, give up the microSD work and retiring MQTT, not
anything on this list.

## What is left

Accurate as of 16 Oct. Ordered by what unblocks the most.

### 1. Layout as data, not a header — flashed, push unconfirmed

Every environment builds, and the board **boots on it** (16 Oct): it
reads NVS, finds nothing stored, falls back to the compiled
`ha_layout.h`, and runs. That fallback path is confirmed.

**What is not confirmed is a push completing.** No layout has ever
reached the board end to end. HA's half is verified — `subscribe_events`
succeeds, `fire_event` returns success, the event reaches a subscriber
as an ~890-byte frame — but the panel went offline before the fix for
the silent-drop bug below could be flashed. Until someone sees
`[layout] push received` followed by `[layout] loaded from NVS`, say
so plainly rather than implying it works.

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
  layout cannot grow into territory LVGL needs. The demo layout uses
  403 bytes; a maxed-out layout would use 2320. Over that it is
  rejected whole, with a reason.
- Validation runs **before** anything is stored and never touches the
  live table, so a rejected push leaves the panel running on the
  layout it has. Every rejection names its cause on serial — a push
  that silently does nothing is the worst version of this feature.

The trap this had to work around, which is load-bearing: `handle_text`
parses every frame through a filter into a **768-byte** document, and
that is what keeps the hot path bounded. A 1002-byte layout does not
fit it. So a layout push gets its own wider parse on its own path,
paid only when a push arrives — **do not widen the shared filter to
make the layout fit it.**

`layout_store.cpp` is excluded from `env:panel` and `env:demo`: it
needs ArduinoJson, which those paths do not carry.

Still true until it is proven on hardware: `ha_layout.h` is the
mechanism that is known to work, and reflashing is the update path
that is known to work.

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

### 2. Desktop tool GUI — done 15 Oct

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

### 4. Report and demo material — the real gap now

**This is the thing being graded on the 20th, and it is the least
current part of the project.** `docs/` was written on 17 Sep and
describes a version of this project that no longer exists.

What exists today:

- `docs/PROJECT_BRIEF.md` — the pre-presentation briefing.
- `docs/HomeMesh_Panel_Project_Documentation.docx` — the handover
  document written for a teammate building slides. Structurally
  validated but **never visually opened** — LibreOffice and pandoc are
  both absent from this machine. Open it once before relying on it.
- `docs/figures/arch_overall.png`, `arch_hardware.png`,
  `arch_software.png`, generated by `generate_diagrams.py`, in the
  plain-rectangle convention at the bottom of this file.

What is stale in all of them:

- No mention of **`tools/layout_designer.py`**; they describe discovery
  as flags-only.
- No mention of the **runtime layout path** — they describe the entity
  table as a compiled header, which was the thing the project was
  criticised for being.
- No mention of **theming**.
- No mention of the **microSD card**, which is physically on the board.
- The title page still has blanks: team members, register numbers,
  faculty guide, submission date.

The three diagrams are regenerated by editing
`docs/figures/generate_diagrams.py` and rerunning it — do not hand-edit
the PNGs. It needs Pillow.

### Deliberate gaps, not TODOs

- Bedroom and Kitchen have no sensor readouts. See Scope.
- The Office and Entrance lights and three generic fans exist in HA but
  have no area, so `ha_discover.py` drops them. They are spares held in
  reserve, not omissions.

## Safety net

- Git repo: `https://github.com/agent192007/homemesh-panel` (private).
  **12 commits ahead of `origin/main` as of the end of 16 Oct — the
  runtime layout, theming and the desktop app are local only.** Push
  before relying on the remote as a backup.
- Tag **`v0.3-runtime-layout`** is current: the panel reads its entity
  table from NVS, falling back to the compiled `ha_layout.h`. Boots
  clean on the board, 15 Oct. Rollback is
  `git reset --hard v0.3-runtime-layout`.
- Tag **`v0.2-ha-websocket-demo`** is the last build with the entity
  table compiled in. Use it if the runtime layout path misbehaves —
  it is the same demo, minus the push.
- Tag **`v0.1-working-demo`** is the older static MQTT build.
- Working binaries saved **outside** the project at
  `../homemesh_ha_firmware_15oct.bin` (current) and
  `../homemesh_ha_firmware_17sep.bin` — `.pio` is gitignored, so the
  repo cannot rebuild it if the toolchain misbehaves. This is the real
  demo insurance.
  - The `../homemesh_demo_firmware_12sep.bin` this file used to name was
    **missing** when checked on 17 Sep, and nothing in the repo could
    reconstruct it. If the MQTT fallback is wanted, rebuild `env:panel`
    from `v0.1-working-demo`. Check the binary exists; do not assume.
- **`tools/preflight.ps1`** checks the things that do not survive a
  reboot — hotspot, firewall path, container, HA reachable on the
  hotspot address, token valid, every layout entity still present. Run
  it from cold before demoing. Exits with the number of failures.
- The WebSocket work landed on `feat/ui-paged-navigation`, not
  `feat/ha-websocket` as originally planned, and is merged. **`main` is
  the demoable branch** — it has the paged UI, the discovery tool, the
  WebSocket transport and the layout.

## Conventions

- Block diagrams in the report use plain textbook embedded-systems
  convention: plain rectangles, underscore_named blocks, thin
  connector lines. Not styled cards.
- Leave unknown values blank rather than inventing plausible ones.
