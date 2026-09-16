#pragma once

#include <stdint.h>

// Shared vocabulary between the transport layer and the UI. Neither
// side knows about the other's internals (topics, sockets, widgets) -
// only these types. Swapping MQTT for the HA WebSocket API must not
// change this file.

#define MAX_ENTITIES 12
#define MAX_AUTOMATIONS 8

// What controls an entity gets. Drives which widgets its page builds,
// so adding a capability is a change here plus one page builder.
enum class EntityKind : uint8_t {
  Toggle,     // on/off only
  Dimmable,   // on/off + brightness
  RGB,        // on/off + brightness + colour
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
  Color,             // entity, value = packed 0xRRGGBB
  Temperature,       // fvalue
  Humidity,          // fvalue
  AutomationRan,     // entity = automation index (feedback flash)
};

struct PanelEvent {
  EventType type;
  uint8_t entity;
  int32_t value;
  float fvalue;
};

// Colour travels packed in PanelEvent::value so the queue element
// stays small - it is copied by value into a FreeRTOS queue.
static inline int32_t pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((int32_t)r << 16) | ((int32_t)g << 8) | b;
}
static inline uint8_t rgb_r(int32_t v) { return (uint8_t)(v >> 16); }
static inline uint8_t rgb_g(int32_t v) { return (uint8_t)(v >> 8); }
static inline uint8_t rgb_b(int32_t v) { return (uint8_t)v; }

// Descriptors are supplied by the transport. Today they are static
// tables; once layouts are authored on the PC and stored in NVS the
// transport will populate them at boot and nothing above has to change.
struct EntityDesc {
  const char *name;
  EntityKind kind;
};

struct AutomationDesc {
  const char *name;
};
