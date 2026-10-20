# HomeMesh Panel — pre-presentation brief

Read this before presenting. It covers what the system is, how data moves
through it, what it is built on, and what is not finished. Everything here
was verified against the running system on 18 October; figures are
measurements, not estimates. The IEEE paper (`docs/paper/`) is the formal
write-up of the same material.

A summary sits at the end if you only have five minutes.

---

## 1. What it is, in one paragraph

A wall-mounted touch panel that controls the devices in a Home Assistant
installation. An ESP32 microcontroller drives a 3.2-inch capacitive
touchscreen, joins the local Wi-Fi network, and holds a single WebSocket
connection open to Home Assistant. Tapping a tile switches a device; a
slider dims a light; a colour control sets its colour. State flows both
ways, so a change made anywhere else in the house appears on the panel.
Which devices it shows, on which pages, with which icons and colours, is
decided in a desktop app and sent to the panel over the network — no
reflashing. Nothing touches the internet: no cloud account, no vendor app.

---

## 2. Status at a glance

| | |
|---|---|
| Panel driving real Home Assistant | Working, verified 18 Oct |
| Layout | Authored in the desktop designer, pushed over the network, stored on the panel |
| Transport | Home Assistant WebSocket API, `subscribe_entities` |
| Static RAM | 123,024 bytes — about 1.5 KB below the linker's limit |
| Program size | 38.1% of flash (1,198,037 of 3,145,728 bytes) |
| Graphics memory free | 15,652 of 40,960 bytes after the first full draw |
| Largest message the panel handles | 2,403 bytes (initial state of 7 entities) |
| What the panel avoids | 57,671 bytes — the full state dump of 123 entities |
| Firmware | Frozen at tag `v0.4-firmware-freeze` |
| Paper | Drafted, 6 pages IEEE format, due 21 Oct |

---

## 3. The decision the project turns on

The panel originally spoke **MQTT**. It now speaks the **Home Assistant
WebSocket API**. This is the single most important design choice, and it
is worth being able to defend it clearly.

**The problem with MQTT.** MQTT only reaches devices that publish
themselves onto a broker. A Philips Hue bulb, a Z-Wave switch, or any
device added through Home Assistant's ZHA integration lives entirely
inside Home Assistant and has no MQTT topic at all. The panel cannot see
it, let alone control it. The *same physical Zigbee bulb* is reachable if
it was added via Zigbee2MQTT and invisible if it was added via ZHA.

**Why the WebSocket API fixes it.** It reaches every entity uniformly,
whatever integration created it. And one command — `call_service` — is
identical across all of them. Controlling a Zigbee bulb, a Wi-Fi plug and
a software-only helper is the same message with a different target.

**Worth saying out loud:** MQTT was never a rubric requirement, so
moving off it costs nothing. The old MQTT build still exists, tagged.

---

## 4. The constraint that shaped the architecture

Asking Home Assistant for the state of every entity — `get_states` —
returned **57,671 bytes for the 123 entities** of the test installation,
about 469 bytes per entity. A real house with a few hundred devices is
hundreds of kilobytes.

The ESP32-WROOM-32 has no external PSRAM, and its graphics library works
inside a fixed **40 KB** pool. The full state dump of even this small test
system is bigger than that. This is not a coding problem with a clever
solution; it is a wall.

So the work is **split between two machines**, and the split is
load-bearing:

- **The laptop does discovery.** The desktop designer calls `get_states`,
  shows every entity, and you pick the handful the panel should show.
- **The panel never calls `get_states`.** It subscribes to exactly the
  entities in its layout. Its initial state for 7 entities arrived in one
  **2,403-byte** message — 24 times smaller, and it grows with the layout,
  not with the size of the house.

---

## 5. Data flow

### 5.1 Setup path — the layout, from the laptop to the panel

```
   +---------------------+
   |  home_assistant     |   Docker container on the laptop
   +----------+----------+
              |  get_states, area and entity registries (laptop only)
              v
   +---------------------+
   |  layout_designer    |   tools/layout_designer.py
   |  pick, order, icon, |   preview of every page, theme colours
   |  colours, push      |
   +----------+----------+
              |  fire_event "homemesh_layout"  (a 700-byte layout)
              v
   +---------------------+
   |  home_assistant     |   relays the event to subscribers
   +----------+----------+
              |  955-byte event frame, over the panel's own connection
              v
   +---------------------+
   |  esp32_panel        |   validate -> store in NVS -> restart
   +---------------------+
```

The designer filters what it finds: lights, switches, fans and
`input_boolean` helpers as controllable; scenes, scripts and automations
as runnable actions; and sensors **only** if their `device_class` is
temperature or humidity. Entities with no room in Home Assistant are shown
but cannot be placed, because the panel's pages *are* rooms.

Delivering the layout as a Home Assistant event means the panel needs no
web server, and nobody ever needs to know the panel's IP address — both
sides only know where Home Assistant is. The panel validates the whole
layout before storing anything; a bad one is rejected with a reason on the
serial log and the panel keeps running the old one. A layout compiled into
the firmware is the fallback if nothing has ever been pushed.

### 5.2 Runtime path — runs on the panel, continuously

```
   +---------------------+                      +---------------------+
   |  esp32_panel        |                      |  home_assistant     |
   |  lvgl_ui            |<---- state ----------|  websocket_api      |
   |  ha_client          |----- commands ------>|  (owns the devices) |
   +---------------------+                      +---------------------+
            +--------- ws://192.168.137.1:8123 -----------+
                       one connection, held open
```

The whole protocol, worth memorising because it is the cleanest thing in
the project:

```
1.  connect ws://<host>:8123/api/websocket   -> {"type":"auth_required"}
2.  {"type":"auth","access_token":"..."}      -> {"type":"auth_ok"}
3.  {"id":1,"type":"subscribe_entities","entity_ids":[...]}
        -> one snapshot of every listed entity, then only what changes
4.  {"id":2,"type":"subscribe_events","event_type":"homemesh_layout"}
        -> layout pushes from the designer
5.  {"id":N,"type":"call_service","domain":"light","service":"turn_on",
     "target":{"entity_id":"light.bed_light"},"service_data":{"brightness":180}}
```

Message 3 is how state comes *in*, message 5 is how commands go *out*,
message 4 is how the panel gets reconfigured. There is nothing else, and
there is no second connection.

**Measured:** Home Assistant sent the snapshot 11.5 ms after the
subscription, and a command produced its state-change message after a
median of 3.2 ms. Change messages are 144 to 262 bytes. The panel's
round-trip time over the hotspot averaged 40 ms with no loss.

**Availability** is the socket itself: if it drops, the panel greys the
tiles out immediately, and a 15-second heartbeat catches a connection that
dies silently.

### 5.3 Inside the panel

```
   +------------------+                 +------------------+
   |  network_task    |                 |  lvgl_loop       |
   |  core 0, prio 0  |                 |  core 1          |
   |  ha_client.cpp   |--->[ queue ]--->|  dashboard.cpp   |
   |  wifi + socket   |   PanelEvent    |  draws, reads    |
   +------------------+                 +------------------+
          |
     layout_store.cpp  (NVS layout, compiled fallback)
```

The network lives on its own FreeRTOS task on core 0 and hands events to
the interface through a queue. The graphics library needs servicing about
every 5 ms, and a blocking network call on the same thread makes the
interface stutter. Separating them keeps swiping smooth while the network
is busy.

Details that cost real debugging time, worth knowing if asked:

- The network task runs at **priority 0, not 1**; at 1 it starves the idle
  task and trips the watchdog.
- **Nothing large goes on its stack.** A 4 KB JSON buffer declared locally
  overflowed the 12 KB stack — the compiler had inlined it into the
  function that handles every message.
- Every incoming message is **classified on its raw bytes before parsing**,
  and each kind is parsed into a fixed-size buffer that keeps only the
  fields the panel uses. The panel's memory use cannot grow with whatever
  Home Assistant decides to send.
- The panel **reports its own failures**: a graphics-memory failure prints
  the exact file and line and restarts instead of freezing, and Wi-Fi
  failures print the driver's reason code.

### 5.4 The contract between the halves

`src/panel_model.h` is the interface between transport and interface. They
share only `EntityKind` (`Toggle`, `Dimmable`, `RGB`), `LinkState`
(`NoWifi`, `NoBroker`, `Online`) and `PanelEvent` (a small struct copied
through the queue).

**The whole MQTT-to-WebSocket swap did not change this file.** If you are
asked what made the rewrite cheap, this is the answer.

---

## 6. Tech stack

### 6.1 Hardware

| Part | What it is | Note |
|---|---|---|
| Board | Probots ESP32-2432S032 | 3.2-inch "CYD" family, capacitive |
| MCU | ESP32-WROOM-32 | Dual core 240 MHz, **no PSRAM**, 2.4 GHz Wi-Fi only |
| Display driver | **ST7789V** over SPI, 27 MHz | Not the ILI9341 most guides assume |
| Panel | 240×320 IPS, used at 320×240 | Portrait native, rotated to landscape |
| Touch | **GT911** capacitive over I²C | Address 0x5D |
| USB-serial | CH340 | Needs its driver on the host |
| microSD | Slot fitted with a 128 GB card | Not used yet, never tested |

**This board ships with different parts in different batches.** The
display and touch controllers were identified by testing. That is why
display setup and touch reading live in their own files behind a clean
interface.

### 6.2 Firmware

| Layer | Choice | Why |
|---|---|---|
| Build system | PlatformIO, `board = esp32dev` | No board definition exists for this board |
| Framework | Arduino on FreeRTOS | Library support; every example for this board uses it |
| Graphics | **LVGL 8.3.11** (pinned) | 9.x changed the driver API; nearly all example code is 8.3 |
| Display | TFT_eSPI 2.5.43 | |
| WebSocket | links2004/WebSockets 2.7.3 | |
| JSON | ArduinoJson 6.21.6 | Filtered, fixed-size parsing |
| Icons | 16 Material Design glyphs as LVGL fonts | 7.6 KB of flash, no graphics memory |
| Partitions | `huge_app.csv` | 3 MB app partition instead of 1.3 MB |

### 6.3 Off the board

| Piece | What |
|---|---|
| Home Assistant | Official Docker image on the laptop, config at `C:\homeassistant` |
| Entities | Home Assistant's `demo` integration, plus one `input_boolean` helper |
| Layout designer | Python 3, standard-library tkinter + `websockets` — `tools/layout_designer.py` |
| Discovery CLI | `tools/ha_discover.py`, the shared module the designer is built on |
| Preflight check | PowerShell — `tools/preflight.ps1` |
| Network | Windows Mobile Hotspot, WPA2, 2.4 GHz |

**On the demo integration:** it provides lights, switches and sensors with
no broker and no hardware. If asked whether these are "real" devices: they
are simulated inside Home Assistant, but the protocol is identical to a
real bulb and the panel cannot tell the difference.

### 6.4 Source map

```
src/
  main.cpp             entry, LVGL loop, memory and failure reporting
  display_driver.*     TFT_eSPI init + LVGL flush callback
  touch_driver.*       GT911 + LVGL input callback, rotation
  ha_client.*          HA WebSocket: auth, subscribe_entities, call_service
  layout_store.*       the layout: NVS, validation, compiled fallback
  panel_theme.*        the nine theme colours
  mqtt_client.*        legacy MQTT transport
  fake_client.cpp      offline simulated transport, for UI work
  access_point.cpp     a second ESP32 as the demo network (untested fallback)
  ui/dashboard.*       LVGL pages, tiles, detail panel
  ui/panel_icons.*     icon name -> glyph
include/
  ha_layout.h/.json    the compiled-in fallback layout
  secrets.h            credentials, gitignored
tools/
  layout_designer.py   the desktop app
  ha_discover.py       discovery, shared by the app and the CLI
  preflight.ps1        demo-day checks
docs/paper/            the IEEE paper and the script that builds it
```

---

## 7. Network setup

The demo runs on a **laptop hotspot**, never campus Wi-Fi. Campus networks
break it three ways: client isolation blocks the panel from reaching the
laptop, captive portals cannot be traversed by an ESP32, and mDNS is
filtered.

- **The laptop is always `192.168.137.1`.** Windows Mobile Hotspot always
  takes that address, so it is hardcoded.
- **2.4 GHz only — and on this laptop the hotspot setting is not enough.**
  The laptop's Wi-Fi card has a single radio, so the hotspot shares the
  channel of the laptop's own Wi-Fi connection. When that connection was
  on 5 GHz, the hotspot moved there too and the panel could not see it.
  **The card is pinned to 2.4 GHz** (Device Manager → MediaTek Wi-Fi 6
  MT7921 → Advanced → Band Selection → 2.4G Only). Preflight checks it.
- **The hotspot needs a network to share.** If the venue Wi-Fi is
  unusable, connect the laptop to a phone hotspot set to 2.4 GHz first.
- **WPA2, not WPA3.** **Auto-shutoff off.**

**Firewall:** Docker Desktop's own inbound rule covers port 8123, on both
private and public networks. No hand-made rule is needed.

---

## 8. What is actually on the panel

The layout changes whenever someone opens the designer. As of 18 October:

| Page | Devices | Sensors |
|---|---|---|
| Home | Bed Light, Ceiling Lights, Living Room RGBWW Lights (favourites) | — |
| Bedroom | AC (switch), Bed Light | — |
| Kitchen | Kitchen Lights | — |
| Living Room | Ceiling Lights, Living Room RGBWW Lights | Temperature, humidity |

Rubric minimum, all met: four lights with brightness sliders (at least
three required), a switch, a temperature and humidity readout, and a
connection indicator.

**How a tile behaves:** an off device shows a blue icon and `OFF`. When it
turns on, the tile lifts, its icon turns amber inside a tinted disc, and
the reading turns amber — a dimmed light shows its level, such as `72%`,
with a matching amber brightness bar. A chevron opens a detail panel with
on/off, a brightness slider and a colour control.

**Two quirks to have an answer ready for:**

1. *Every light classifies as `RGB`, never `Dimmable`.* Not a bug: `RGB`
   means on/off plus brightness plus colour, so all lights get sliders.
2. *Bedroom and Kitchen show blank sensor slots.* The demo integration has
   only one temperature/humidity pair. A panel showing a made-up
   temperature is worse than one showing none.

Limits, if asked: 12 devices, 6 rooms, 6 devices per page.

---

## 9. What is not finished

- **The layout designer is being rebuilt** as a proper application with a
  real LVGL simulator, so its preview runs the panel's own interface code
  instead of a lookalike. Started 20 October, after the paper's freeze;
  the paper describes the current tkinter designer.
- **MQTT has not been removed.** Unused, but deleting it also deletes a
  working fallback.
- **The microSD card has never been tested.**
- **The connection is unencrypted** and the access token is in the
  firmware — fine on an isolated hotspot, not for a real deployment.
- **A dedicated ESP32 access point** is written but has never been run;
  it would remove the dependence on the laptop's hotspot.

---

## 10. Demo-day runbook

Run this first, from cold:

```
powershell -ExecutionPolicy Bypass -File tools\preflight.ps1
```

It checks the Wi-Fi card is still pinned to 2.4 GHz, the hotspot is up,
the firewall path exists, the Home Assistant container is running and
answering on the hotspot address, and the token is valid with every
device in the layout still present. It names the fix for anything that
fails. Exit code is the number of failures.

**If the panel does not connect**, read its serial log first. It says why:

- `[wifi] not connected: reason 201 (NO_AP_FOUND)` — the panel cannot see
  the network. Almost certainly the hotspot is on 5 GHz; check Band
  Selection, toggle the hotspot.
- Connected to Wi-Fi but no `[ha] authenticated` — Docker or Home
  Assistant is not running.
- A healthy boot ends with `[ha] initial state: 7 entities in one ...-byte
  frame`.

**If something is badly broken**, recovery paths:

- `git reset --hard v0.4-firmware-freeze` — the demo firmware
- `../homemesh_ha_firmware_18oct_full.bin` — one-command restore of the
  whole board (it resets the layout to the compiled-in one; push again)
- The GitHub repository, pushed and current

---

## 11. Questions you are likely to get

**"Why not a Raspberry Pi with a browser?"**
Cost, boot time and power. This runs from cold in a couple of seconds and
draws a fraction of the power, with no operating system to maintain.

**"What happens if Home Assistant goes down?"**
The WebSocket drops, the panel detects it immediately, and the tiles grey
out. It shows a disconnected state rather than stale values that look live.

**"How do you change what is on the panel?"**
In the desktop designer: tick devices, mark favourites, drag to reorder,
pick icons and colours, press Push. The panel restarts with the new
layout in a few seconds. No cable, no reflash.

**"Why does it restart when a layout arrives?"**
Rebuilding a whole screen in place fragments the 40 KB graphics memory,
and running out of it freezes the screen. A restart takes a few seconds
and always works.

**"What was the hardest bug?"**
The panel kept losing its connection at random. Two causes stacked: the
firmware fetched each device's state with a blocking request inside the
connection's own message handler, so every slow reply killed the
connection; and the laptop's single-radio Wi-Fi card moved the hotspot to
5 GHz whenever its own connection did, where the panel cannot see it. The
first was fixed by taking state over the existing connection, the second
by pinning the card to 2.4 GHz — found because the panel now logs *why*
it fails.

**"Why LVGL 8.3 and not 9?"**
Version 9 changed the display and input driver API, and essentially every
worked example for this board is written against 8.3.

---

## 12. Summary

**What it is.** A wall-mounted ESP32 touch panel that controls Home
Assistant devices over the local network, with no cloud in the path.

**What it does.** Room pages and a favourites page, brightness and colour
on every light, temperature and humidity, a connection indicator, and
state in both directions. What it shows is designed on a laptop and sent
to it over the network.

**The key design decision.** The Home Assistant WebSocket API instead of
MQTT, because it reaches every device, with one command shape for all.

**The key constraint.** The full state of even a small installation
(57,671 bytes) is bigger than the panel's graphics memory, so discovery
runs on the laptop and the panel subscribes only to its own devices —
2,403 bytes for seven of them.

**The stack.** ESP32-WROOM-32 with ST7789V and GT911, LVGL 8.3 under
PlatformIO, the network on its own core feeding the interface through a
queue, Home Assistant in Docker on the laptop, a Python desktop designer,
a Windows hotspot pinned to 2.4 GHz.

**The honest framing.** The system is complete and frozen for the demo.
What is in progress — a better designer — improves the authoring tool, not
the panel.
