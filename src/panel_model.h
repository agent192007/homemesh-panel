#pragma once

#include <stdint.h>

// Shared vocabulary between the MQTT layer and the UI. Neither side
// knows about the other's internals (topics, widgets) - only these.

enum EntityId : uint8_t {
  ENT_LIVING_LIGHT,   // dimmable
  ENT_KITCHEN_LIGHT,
  ENT_BEDROOM_LIGHT,
  ENT_FAN_SWITCH,
  ENT_COUNT
};

enum class LinkState : uint8_t {
  NoWifi,
  NoBroker,
  Online
};

enum class EventType : uint8_t {
  Link,              // value = LinkState
  DevicesAvailable,  // value = 1 online, 0 offline
  Power,             // entity, value = 1 on, 0 off
  Brightness,        // entity, value = 0-255
  Temperature,       // fvalue
  Humidity           // fvalue
};

struct PanelEvent {
  EventType type;
  uint8_t entity;
  int32_t value;
  float fvalue;
};
