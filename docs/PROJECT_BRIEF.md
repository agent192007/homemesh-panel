# HomeMesh Panel — pre-presentation brief

Read this before presenting. It covers what the system is, how data moves
through it, what it is built on, and what is not finished. Everything here
was verified against the running system on 17 September; figures are
measurements, not estimates.

A summary sits at the end if you only have five minutes.

---

## 1. What it is, in one paragraph

A wall-mounted touch panel that controls the devices in a Home Assistant
installation. An ESP32 microcontroller drives a 3.2-inch capacitive
touchscreen, joins the local Wi-Fi network, and holds a single WebSocket
connection open to Home Assistant. Tapping a tile switches a device; a
slider dims a light; a colour wheel sets its colour. State flows both
ways, so a change made anywhere else in the house appears on the panel.
Nothing touches the internet — no cloud account, no vendor app, no
external service in the path.

---

## 2. Status at a glance

| | |
|---|---|
| Panel driving real Home Assistant | Working, flashed, verified 17 Sep |
| Devices on the panel | 9, across 3 room pages |
| Transport | Home Assistant WebSocket API |
| Memory used | 32.5% of RAM (106,520 of 327,680 bytes) |
| Program size | 38.4% of flash (1,207,613 of 3,145,728 bytes) |
| Largest network message handled | 1,608 bytes |
| Biggest thing outstanding | Layout is still compiled in, not sent over the air |

---

## 3. The decision the project turns on

The panel originally spoke **MQTT**. It now speaks the **Home Assistant
WebSocket API**. This is the single most important design choice, and it
is worth being able to defend it clearly.

**The problem with MQTT.** MQTT only reaches devices that publish
themselves onto a broker. A Philips Hue bulb, a Z-Wave switch, or any
device added through Home Assistant's ZHA integration lives entirely
inside Home Assistant and has no MQTT topic at all. The panel cannot see
it, let alone control it. The perverse part: the *same physical Zigbee
bulb* is reachable if it was added via Zigbee2MQTT and invisible if it
was added via ZHA. Identical hardware, opposite outcome, decided by
something the user never thinks about.

**Why the WebSocket API fixes it.** It reaches every entity uniformly,
whatever integration created it. And one command — `call_service` — is
identical across all of them. Controlling a Zigbee bulb, a Wi-Fi plug and
a software-only helper is the same message with a different target.
Control stops being integration-dependent.

**Worth saying out loud:** MQTT was never a rubric requirement, so
removing it costs nothing. The old MQTT build still exists, tagged, as a
fallback.

---

## 4. The constraint that shaped the architecture

Asking Home Assistant for the state of every entity — the `get_states`
call — returns **hundreds of kilobytes of JSON** on a real installation.

The ESP32-WROOM-32 has 320 KB of RAM total, no external PSRAM, and most
of that RAM is already committed to the display buffer, the graphics
library and the Wi-Fi stack. That response cannot be received, let alone
parsed. This is not a coding problem with a clever solution; it is a wall.

So the work is **split between two machines**, and this split is
load-bearing:

- **The laptop does discovery.** It has the memory. It calls
  `get_states`, shows every entity, and writes down the handful chosen.
- **The panel never calls `get_states`.** It subscribes to exactly the
  entities in its layout and sends commands to them. Every message it
  handles is small and bounded by design.

The 1,608-byte measurement is the evidence this works — that is the
largest single frame the panel ever has to deal with, measured against
the real instance.

---

## 5. Data flow

### 5.1 Setup path — runs on the laptop, once

```
   +---------------------+
   |  home_assistant     |   Docker container on the laptop
   |  (entity registry)  |
   +----------+----------+
              |
              |  get_states, area_registry, entity_registry
              |  (hundreds of KB — laptop only)
              v
   +---------------------+
   |  ha_discover_py     |   tools/ha_discover.py
   |  (discovery tool)   |   filters to controllable domains
   +----------+----------+
              |
              |  chosen entity_ids + areas + favourites
              v
   +---------------------+
   |  ha_layout_h        |   include/ha_layout.h
   |  (generated header) |   9 entity_ids, 3 areas, 2 sensors
   +----------+----------+
              |
              |  compiled in at build time
              v
   +---------------------+
   |  firmware_bin       |   PlatformIO, env:ha
   |  (flashed over USB) |
   +---------------------+
```

The discovery tool filters what it finds. It keeps lights, switches, fans
and `input_boolean` helpers as controllable; scenes, scripts and
automations as runnable actions; and sensors **only** if their
`device_class` is `temperature` or `humidity`. Everything else — and
there is a lot of it — is dropped before you ever see the list.

It also classifies each light by what it supports: `Toggle`, `Dimmable`,
or `RGB`. That classification decides which widgets get built.

### 5.2 Runtime path — runs on the panel, continuously

```
   +---------------------+                      +---------------------+
   |  esp32_panel        |                      |  home_assistant     |
   |                     |                      |                     |
   |  lvgl_ui            |<---- state events ---|  websocket_api      |
   |  ha_client          |----- commands ------>|  (owns the devices) |
   +---------------------+                      +---------------------+
            |                                             |
            +--------- ws://192.168.137.1:8123 -----------+
                       single connection, held open
```

The entire protocol is **four messages**. This is worth memorising,
because it is the cleanest thing in the project:

```
1.  connect ws://<host>:8123/api/websocket
       server -> {"type":"auth_required"}

2.  {"type":"auth","access_token":"..."}
       server -> {"type":"auth_ok"}

3.  {"id":1,"type":"subscribe_trigger",
     "trigger":{"platform":"state","entity_id":[ ...the 9... ]}}
       server -> a state event whenever any of them changes

4.  {"id":2,"type":"call_service","domain":"light","service":"turn_on",
     "target":{"entity_id":"light.bed_light"},
     "service_data":{"brightness":180}}
```

Message 3 is how state comes *in*. Message 4 is how commands go *out*.
There is nothing else.

**Availability** is handled by detecting the WebSocket disconnecting.
MQTT had a "last will and testament" mechanism for this; the WebSocket
API has no equivalent and does not need one — if the socket drops, the
panel knows immediately and greys the tiles out.

### 5.3 Inside the panel

```
   +------------------+        +------------------+
   |  network_task    |        |  lvgl_task       |
   |  core 0, prio 0  |        |  core 1, main    |
   |                  |        |                  |
   |  ha_client.cpp   |--->[ queue ]--->| dashboard.cpp |
   |  wifi + socket   |  PanelEvent     |  draws, reads |
   +------------------+                 +------------------+
```

The network lives on its **own FreeRTOS task on core 0**, and hands
events to the interface through a queue. This matters: the graphics
library needs its handler called roughly every 5 milliseconds, and a
blocking network reconnect on the same thread makes the whole interface
visibly stutter. Separating them is why swiping between pages stays
smooth while the network is busy.

Two details that cost real debugging time and are worth knowing if asked:

- The network task runs at **priority 0, not 1**. At priority 1 it
  starves the idle task on that core and trips the watchdog.
- Touch coordinates are **rotated in software** to match the screen
  orientation. Taps landing mirrored is a rotation mismatch, not a
  broken touch driver.

### 5.4 The contract between the halves

`src/panel_model.h` is the interface between transport and interface, and
neither side knows anything about the other. The transport knows nothing
about widgets; the interface knows nothing about sockets. They share only:

- `EntityKind` — `Toggle`, `Dimmable`, `RGB`. What controls a device gets.
- `LinkState` — `NoWifi`, `NoBroker`, `Online`.
- `PanelEvent` — a small struct copied by value through the queue, with an
  event type (`Power`, `Brightness`, `Color`, `Temperature`, `Humidity`,
  `Link`, …), an entity index and a value.

**The whole MQTT-to-WebSocket swap did not change this file.** That is
the payoff from separating them in the first place — `ha_client.cpp`
presents the same three-function shape as `mqtt_client.cpp` did
(start / next_event / send), so `main.cpp` and `dashboard.cpp` barely
changed. If you are asked what made the rewrite cheap, this is the answer.

---

## 6. Tech stack

### 6.1 Hardware

| Part | What it is | Note |
|---|---|---|
| Board | Probots ESP32-2432S032 | 3.2-inch "CYD" family, capacitive |
| MCU | ESP32-WROOM-32 | Dual core, 320 KB RAM, **no PSRAM** |
| Display driver | **ST7789V** | Not the ILI9341 most guides assume |
| Panel | 240×320 IPS, used at 320×240 | Portrait native, rotated to landscape |
| Touch | **GT911** capacitive over I²C | Answers at address 0x5D or 0x14 |
| USB-serial | CH340 | Needs its driver on the host |

**This board ships with different parts in different batches.** The
display driver and touch controller were identified empirically, by
testing, not read off a datasheet. That is why the project keeps display
initialisation and the touch read callback in their own files behind a
clean interface — it was a deliberate hedge against exactly this. It also
happens to be what made the transport swap cheap.

Display configuration is set through build flags in `platformio.ini`
rather than TFT_eSPI's bundled `User_Setup.h`; `USER_SETUP_LOADED=1` tells
the library to ignore its own config. The panel needs colour inversion on
and BGR colour order.

### 6.2 Firmware

| Layer | Choice | Why |
|---|---|---|
| Build system | PlatformIO, `board = esp32dev` | No board definition exists for this board |
| Framework | Arduino | Library support, and every example for this board uses it |
| Graphics | **LVGL 8.3.x** (pinned) | 9.x changed the driver API; nearly all example code is 8.3 |
| Display | TFT_eSPI 2.5.43 | |
| WebSocket | links2004/WebSockets 2.4.1 | |
| JSON | ArduinoJson 6.21.5 | |
| Partitions | `huge_app.csv` | 3 MB app partition instead of 1.3 MB |

The partition choice is worth understanding. With the default layout, the
graphics library plus display driver plus Wi-Fi plus WebSocket plus JSON
lands at about **92%** of the 1.3 MB application partition — too tight to
work in. `huge_app.csv` gives 3 MB and drops it to around 38%, leaving
real headroom for the runtime-layout work still to come. It costs the
over-the-air update slot, which this project does not use.

### 6.3 Off the board

| Piece | What |
|---|---|
| Home Assistant | Official Docker image, on the laptop, config at `C:\homeassistant` |
| Entities | Home Assistant's `demo` integration, plus one `input_boolean` helper |
| Discovery tool | Python 3, `websockets` library — `tools/ha_discover.py` |
| Preflight check | PowerShell — `tools/preflight.ps1` |
| Network | Windows Mobile Hotspot, WPA2, 2.4 GHz |

**On the demo integration:** it provides lights, switches and sensors with
no broker and no hardware. Being non-MQTT is exactly the point — driving
these from the panel is what proves the new architecture works. If someone
asks whether these are "real" devices, the honest answer is that they are
simulated inside Home Assistant, but the panel cannot tell the difference
and neither could a real bulb: the protocol is identical.

### 6.4 Source map

```
src/
  main.cpp             entry, LVGL tick and timer handler
  display_driver.*     TFT_eSPI init + LVGL flush callback
  touch_driver.*       GT911 + LVGL input callback, rotation
  ha_client.*          HA WebSocket: auth, subscribe, call_service
  mqtt_client.*        legacy MQTT transport, being retired
  fake_client.cpp      offline simulated transport, for UI work
  transport.h          the shape all three share
  panel_model.h        the contract between transport and UI
  ui/dashboard.*       LVGL pages, tiles, sliders, colour wheel
include/
  ha_layout.h          generated — the 9 entities
  secrets.h            credentials, gitignored
tools/
  ha_discover.py       the PC half of the split
  preflight.ps1        demo-day checks
```

Three transports implement the same interface: `ha_client` (current),
`mqtt_client` (legacy), and `fake_client` (simulated, for working on the
interface with no network at all). PlatformIO picks one per build
environment. `env:ha` is the one that matters.

---

## 7. Network setup

The demo runs on a **laptop hotspot**, never campus Wi-Fi. Campus networks
break it three ways: client isolation blocks the panel from reaching the
laptop, captive portals cannot be traversed by an ESP32, and mDNS is
filtered. The hotspot makes the demo independent of the venue entirely.

- **The laptop is always `192.168.137.1`.** Windows Mobile Hotspot runs on
  Internet Connection Sharing, which always takes that address. More
  predictable than a DHCP lease, so it is hardcoded.
- **2.4 GHz only.** The ESP32-WROOM-32 has no 5 GHz radio. Windows 11
  often defaults to 5 GHz, and the panel then never sees the network at
  all — which looks exactly like broken firmware.
- **WPA2, not WPA3.**
- **Hotspot auto-shutoff must be off**, or Windows kills it after about
  five minutes with nothing connected.

The hotspot is also the *development* network, not just the demo network.
That was deliberate: if Home Assistant lived on a different network during
development, its address would change on demo morning, and the address is
compiled into the firmware — meaning a reflash at 9am. Instead nothing
about the network changes on the day.

**Firewall:** Docker Desktop installs its own inbound rule for
`com.docker.backend.exe`, and because the published port is served by that
process, it already covers port 8123. No hand-made port rule was needed.

---

## 8. What is actually on the panel

Three room pages plus a home page of favourites.

| Room | Devices | Sensors |
|---|---|---|
| Bedroom | Bed Light (RGB), AC (switch), Ceiling Fan | — |
| Kitchen | Kitchen Lights (RGB), Panel Demo Switch | — |
| Living Room | Ceiling Lights (RGB), Living Room RGBWW Lights (RGB), Decorative Lights, Living Room Fan | Temperature, humidity |

The four lights are the home-page favourites, so a favourite appears
twice: once on Home and once on its room page.

**Two quirks to have an answer ready for:**

1. *Every light classifies as `RGB`, never `Dimmable`.* That is not a bug.
   `RGB` means on/off **plus brightness plus colour**, and the brightness
   slider is enabled for anything that is not a plain toggle. So all four
   lights get sliders. The rubric asked for at least one; there are four.

2. *Bedroom and Kitchen show blank sensor slots.* The demo integration
   provides only **one** temperature/humidity pair, and both are assigned
   to Living Room. Rather than invent plausible room temperatures with
   template sensors, the other pages are left blank. If challenged, the
   position is that a panel showing a made-up temperature is worse than
   one showing none.

Limits, if asked how far it scales: 16 entities, 6 areas, 6 devices per
page. The screen cannot scroll, so anything past six on a page would be
unreachable and is dropped at build time.

---

## 9. What is not finished

Be straightforward about this. It is a checkpoint, not a submission.

**1. The layout is still compiled in — this is the real gap.**
Today, changing which devices appear means regenerating the header,
rebuilding, and **reflashing over USB**. The intended design is the
opposite: the panel is *sent* its layout over the network, stores it in
non-volatile memory, and builds its screen from that at boot, with the
firmware never changing. Discovery is done and proven; delivery has not
started. If asked what happens if this does not land: the panel works
exactly as demonstrated, it just cannot be reconfigured without a computer.

**2. The desktop tool has no interface yet.** `ha_discover.py` works but
is driven by command-line flags. The intended version is an entity picker
plus a drag-and-drop canvas the size of the screen. This is first on the
cut list if time runs short — the fallback is a checkbox list and a fixed
2×2 grid.

**3. MQTT has not been removed.** The broker, the simulator and
`mqtt_client.cpp` are still in the tree. They are now genuinely unused,
but deleting them also deletes a working fallback, so it waits until the
layout work is stable.

**4. Report and final demo material.**

---

## 10. Demo-day runbook

Run this first, from cold:

```
powershell -ExecutionPolicy Bypass -File tools\preflight.ps1
```

It checks the five things that do not survive a reboot and names the fix
for whichever fails: hotspot up, firewall path present, Home Assistant
container running, Home Assistant answering on the hotspot address, and
the token valid with all nine devices still present. Exit code is the
number of failures.

**If the panel does not connect**, the order to check is:

1. Is the hotspot on, and on 2.4 GHz? This is the most common cause and
   it looks exactly like a firmware bug.
2. Is the Docker container running? Docker Desktop does not always start
   itself.
3. Has the network name in the firmware drifted from the hotspot name?

**If something is badly broken**, there are three independent recovery
paths:

- `git reset --hard v0.2-ha-websocket-demo` — the known-good WebSocket build
- The GitHub repository, pushed and current
- `../homemesh_ha_firmware_17sep.bin` — a flashable binary saved outside
  the project, because the build directory is not in version control

---

## 11. Questions you are likely to get

**"Why not a Raspberry Pi with a browser?"**
Cost, boot time and power. This runs from cold in a couple of seconds and
draws a fraction of the power. A Pi would also mean maintaining an
operating system on a wall panel.

**"What happens if Home Assistant goes down?"**
The WebSocket drops, the panel detects it immediately, and the tiles grey
out. It shows a disconnected state rather than stale values that look live.

**"Are these real devices?"**
They are simulated inside Home Assistant by its demo integration. The
protocol is identical to a real bulb — the panel cannot tell the
difference. Swapping in real hardware changes nothing in the firmware.

**"Why is the device list fixed?"**
It is compiled in today. Sending it over the network and storing it on the
panel is the next stage, and it is the main remaining work.

**"Why LVGL 8.3 and not 9?"**
Version 9 changed the display and input driver API, and essentially every
worked example for this board is written against 8.3.

**"How did you know which display driver it has?"**
By testing. This board ships with different controllers in different
batches, so it was identified empirically — which is why the hardware
setup lives behind a clean interface.

---

## 12. Summary

**What it is.** A wall-mounted ESP32 touch panel that controls Home
Assistant devices over the local network, with no cloud in the path.

**What it does today.** Nine devices across three room pages, with
brightness and colour on every light, temperature and humidity readings,
and a connection indicator. State runs both ways — a change made anywhere
else appears on the panel. It is flashed and working against a real Home
Assistant instance.

**The key design decision.** Moving off MQTT onto the Home Assistant
WebSocket API, because MQTT only reaches devices that publish to a broker
and most real smart-home devices do not. The WebSocket API reaches every
entity uniformly, with one command shape for all of them.

**The key constraint.** Asking Home Assistant for every entity returns
hundreds of kilobytes; the chip has 320 KB of RAM and no PSRAM. So
discovery runs on the laptop once, and the panel only ever subscribes to
the nine devices it was told about. The largest message it handles is
1,608 bytes — measured, not estimated.

**The stack.** ESP32-WROOM-32 with an ST7789V display and GT911 touch,
running LVGL 8.3 and TFT_eSPI under PlatformIO; a WebSocket client on a
separate core feeding the interface through a queue; Home Assistant in
Docker on the laptop; a Python discovery tool; a Windows hotspot at a
fixed address.

**What is left.** One real piece of engineering — sending the layout to
the panel over the network and storing it there, so reconfiguring it no
longer means reflashing. Then the desktop authoring interface, removing
the old MQTT code, and the report.

**The honest framing.** The architecture is proven end to end and the
hard constraint is solved. What remains is carrying the same design one
step further, not discovering whether it works.
