# Devices

The ESPHome boards in the demo, besides the panel. Home Assistant talks
to them over ESPHome's native API; the panel controls them through Home
Assistant like any other device.

| Device | Board | What it does | Address |
|---|---|---|---|
| `smart-switch.yaml` | ESP32 + 4-channel relay module + 4× TTP223 touch | Four relays ("Ventilation Fan", Relay 2-4), each also toggled by its own touch pad - works with Home Assistant and Wi-Fi down | 192.168.137.202 |
| `air-monitor.yaml` | ESP32 + MQ-135 + DHT11 | Temperature, humidity, relative air quality, and a "Bad Air" sensor with a threshold set from Home Assistant | 192.168.137.201 |

Fixed addresses, outside the ESP32 access point's leases (.100-.110),
so Home Assistant always finds them. The MQ-135 is not calibrated:
Air Quality is relative to a clean-air baseline you set with the
"Calibrate Clean Air" button after its 5-minute warm-up.

## Set up, once

```powershell
powershell -ExecutionPolicy Bypass -File devices\setup.ps1
```

Creates `devices\.venv` with ESPHome (pinned in `requirements.txt`;
needs Python 3.12-3.14). Then copy `secrets.example.yaml` to
`secrets.yaml` and fill it in. `secrets.yaml` is gitignored: it has the
Wi-Fi password and each device's API key.

## Flash

```powershell
# first time, over USB (the COM port from Device Manager):
powershell -ExecutionPolicy Bypass -File devices\flash.ps1 smart-switch COM5
# after that, over Wi-Fi to its fixed address:
powershell -ExecutionPolicy Bypass -File devices\flash.ps1 smart-switch
```

The first build of each device downloads the ESP32 toolchain (a few
hundred MB). Boards with a CP2102 USB chip need the Silicon Labs CP210x
driver; if a board will not start flashing over USB, hold BOOT while
it connects. After flashing, add it in Home Assistant: Settings >
Devices & services > Add integration > ESPHome, host = its address,
and paste its API key.

## If flashing fails

- **"No more data to read from the serial port" right after "Connecting"**:
  the board is dipping on power as writing starts - the MQ-135's heater
  draws ~150 mA from the same USB. Flash without esptool's stub, which
  is slower but steadier (how the air monitor went on, 23 Oct):

  ```powershell
  devices\.venv\Scripts\python.exe -m esptool --chip esp32 --port COM3 --baud 115200 --no-stub `
      write-flash -z --flash-size detect 0x0 devices\.esphomeuildir-monitoruildirmware.factory.bin
  ```

  Or unplug the MQ-135 while flashing, or use a shorter cable.
- **"MSys/Mingw is not supported"** while installing ESP-IDF: ESPHome
  was started from Git Bash. Use PowerShell (as `flash.ps1` does).

## Notes

- **The ESP32 access point takes 4 devices** (`MAX_CLIENTS` in
  `src/access_point.cpp`): the panel, the laptop and these two. A third
  board needs that raised and the access point reflashed.
- `smart_lamp_api_key` in `secrets.yaml` is unused: there is no smart lamp.
- `air-monitor.yaml` once drove an SG90 servo (owl wings); the board has
  none, so it was removed on 22 Oct and "Drop Threshold" became "Bad Air
  Threshold". If the device was already in Home Assistant, that entity
  comes back under its new name, at the default 140%.
