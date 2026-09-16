#pragma once

#include "panel_model.h"

// Legacy fixed entity table. The new contract (panel_model.h) has no
// entity enum - entities are a runtime table supplied by the transport.
// Kept local to the MQTT path, which is being retired.
enum LegacyEntityId : uint8_t {
  ENT_LIVING_LIGHT,   // dimmable
  ENT_KITCHEN_LIGHT,
  ENT_BEDROOM_LIGHT,
  ENT_FAN_SWITCH,
  ENT_COUNT
};

// Starts WiFi + MQTT on a separate FreeRTOS task (core 0), so
// connecting and reconnecting never block lv_timer_handler().
void mqtt_start();

// Call from loop(). Returns true and fills *e while events are
// pending. All LVGL work stays on the loop task this way.
bool mqtt_next_event(PanelEvent *e);

// Queue a command for the network task. Safe to call from UI code.
void mqtt_send_power(uint8_t entity, bool on);
void mqtt_send_brightness(uint8_t entity, uint8_t value);
