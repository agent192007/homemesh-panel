#pragma once

// Copy to secrets.h (gitignored) and fill in.

#define WIFI_SSID ""
#define WIFI_PASSWORD ""

// Broker IP address. Use the IP, not homeassistant.local -
// the ESP32 does not resolve mDNS names reliably.
#define MQTT_HOST ""
#define MQTT_PORT 1883
#define MQTT_USER ""
#define MQTT_PASSWORD ""

// ---- Home Assistant WebSocket API (env:ha) ----
// Host running Home Assistant. Use the IP, not homeassistant.local -
// the ESP32 does not resolve mDNS names reliably.
#define HA_HOST ""
#define HA_PORT 8123

// Long-lived access token: HA -> your user (bottom left) -> Security
// -> Long-lived access tokens -> Create.
#define HA_TOKEN ""
