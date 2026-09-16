# HomeMesh Panel

Wall-mounted smart home control panel. ESP32 + 3.2" touchscreen,
talking to Home Assistant. Local-first, no cloud.

Academic project, VIT Chennai. Carries 70% of the grade.
**18 September is a checkpoint: ~70% complete, not a final submission.**
Final deadline is ~3 weeks out (early October, not yet fixed).
Prioritise an impressive working demo over elegance.

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
data. **Do not plan a reflash step** — that would require esptool, a
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
- **Windows Firewall inbound rule for TCP 8123 is required.** Without
  it the panel joins WiFi fine and then times out on the WebSocket —
  reads as a code bug, isn't one.
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

`tools/ha_discover.py` therefore runs on the laptop against
`http://localhost:8123`.

### Current infrastructure state (16 Sep)

- **No Home Assistant instance exists yet.** Standing HA up on the
  laptop is step zero and is a bigger task than the WebSocket spike.
- The dev desktop has no wireless interface (`netsh wlan show
  interfaces` reports none), which is why it cannot be the demo machine
  and why the split above resolves toward the laptop.
- The existing MQTT broker is at `192.168.100.9` — a separate machine,
  with no HA attached. Everything to date has been panel to broker to
  Python simulator, with HA never in the loop.
- HA host URL and long-lived access token: not yet obtained.
- **`include/ha_layout.h` is committed, not gitignored.** It is
  generated, but it is also the only record of the demo layout, and a
  build that needs HA running to succeed is a bad thing to depend on at
  9am. It contains entity_ids and friendly names, no credentials.

## Toolchain

- PlatformIO, `board = esp32dev` (no CYD-specific board definition exists)
- Arduino framework
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

Entities are currently hardcoded (3 lights + 1 switch + temp/humidity).
Under the new architecture they become a runtime table driven by the
layout, bounded at ~16 entries.

- 3 lights (at least one with a brightness slider)
- 1 switch
- 1 sensor readout (temp/humidity)
- Connection status indicator

Prefer 4-6 entities with real bidirectional sync over many entities
with one-way control.

UI is **landscape** (320x240, `PANEL_ROTATION` in `main.cpp`).
Touch mapping was verified correct in portrait with no swap/invert
flags; rotation is handled in `touch_driver.cpp`.

Cut list, in order, if behind schedule: drag-and-drop authoring (fall
back to a checkbox picker and a fixed 2x2 grid), brightness sliders,
second screen, animations. Nothing has been cut as of 16 Sep.

## Safety net

- Git repo: `https://github.com/agent192007/homemesh-panel` (private).
- Tag **`v0.1-working-demo`** is the last known-good static MQTT build.
  Rollback is `git reset --hard v0.1-working-demo`.
- Working binary saved **outside** the project at
  `../homemesh_demo_firmware_12sep.bin` — `.pio` is gitignored, so the
  repo cannot rebuild it if the toolchain misbehaves. This is the real
  demo insurance.
- Do the WebSocket work on `feat/ha-websocket`; `main` stays demoable.

## Conventions

- Block diagrams in the report use plain textbook embedded-systems
  convention: plain rectangles, underscore_named blocks, thin
  connector lines. Not styled cards.
- Leave unknown values blank rather than inventing plausible ones.
