# HomeMesh Panel

Wall-mounted smart home control panel. ESP32 + 3.2" touchscreen,
talking to Home Assistant over MQTT. Local-first, no cloud.

Academic project, VIT Chennai. Hard deadline: **18 September**.
Carries 70% of the grade. Prioritise a working demo over elegance.

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

## Toolchain

- PlatformIO, `board = esp32dev` (no CYD-specific board definition exists)
- Arduino framework
- **LVGL pinned to 8.3.x.** 9.x changed the display and input driver
  API. Most CYD example code online is 8.3. Do not upgrade.
- TFT_eSPI 2.5.x
- PubSubClient

## Architecture rules

Keep display init and the LVGL touch read callback in their own
files with a clean interface. Hardware specifics must not leak into
UI or MQTT code. This was a deliberate hedge against batch variation
and it stays even though the chips are now known.

```
src/
  main.cpp            entry, LVGL tick + timer handler
  display_driver.*    TFT_eSPI init + LVGL flush callback
  touch_driver.*      GT911 + LVGL indev read callback
  mqtt_client.*       broker, discovery, LWT, reconnect
  ui/                 LVGL screens and widgets
```

## Known traps

- **PubSubClient buffer defaults to 256 bytes.** HA discovery payloads
  exceed this and fail *silently*. `MQTT_MAX_PACKET_SIZE=1024` is set
  in build flags. Never remove it.
- **`lv_timer_handler()` needs calling every ~5ms.** No blocking MQTT
  reconnect loops on the same path or the UI visibly stutters.
- **Touch coordinates need rotating** to match `tft.setRotation()`.
  Taps landing mirrored or on the wrong axis is a rotation mismatch,
  not a broken touch driver.
- Charge-only USB-C cables power the board and backlight but never
  enumerate. If there is no COM port, suspect the cable first.

## MQTT requirements

Not optional — these are what make the demo credible:

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
availability on `homemesh/panel/status`. MQTT runs on its own FreeRTOS
task (core 0) and hands events to the LVGL loop through a queue.
Credentials live in `include/secrets.h` (gitignored).

## Scope

Target 4-6 entities with real bidirectional sync, not many entities
with one-way control.

- 3 lights (at least one with a brightness slider)
- 1 switch
- 1 sensor readout (temp/humidity)
- Connection status indicator

UI is **landscape** (320x240, `PANEL_ROTATION` in `main.cpp`).
Touch mapping was verified correct in portrait with no swap/invert
flags; rotation is handled in `touch_driver.cpp`.

Cut list, in order, if behind schedule: brightness sliders, second
screen, animations. One screen with working bidirectional sync beats
three screens without.

## Conventions

- Block diagrams in the report use plain textbook embedded-systems
  convention: plain rectangles, underscore_named blocks, thin
  connector lines. Not styled cards.
- Leave unknown values blank rather than inventing plausible ones.
