#pragma once

#include "panel_model.h"

// The interface every transport implements. main.cpp and ui/ include
// only this - never a concrete transport header. Implementations:
//
//   fake_client.cpp   env:demo   - simulated, no network at all
//   mqtt_client.cpp   env:panel  - legacy MQTT, being retired
//   ha_client.cpp     (planned)  - HA WebSocket API
//
// Networking runs on its own FreeRTOS task; events reach the LVGL loop
// through a queue, so nothing here may block.

void transport_start();

// Call from loop(). Returns true and fills *e while events are pending.
bool transport_next_event(PanelEvent *e);

// Commands. Safe to call from UI callbacks; they only enqueue.
void transport_send_power(uint8_t entity, bool on);
void transport_send_brightness(uint8_t entity, uint8_t value);
void transport_send_color(uint8_t entity, uint8_t r, uint8_t g, uint8_t b);
void transport_run_automation(uint8_t index);

// The entity/automation tables the UI builds pages from. Valid after
// transport_start() returns.
uint8_t transport_entity_count();
const EntityDesc *transport_entity(uint8_t i);
uint8_t transport_automation_count();
const AutomationDesc *transport_automation(uint8_t i);
