#pragma once

#include <stdint.h>

// Shared vocabulary between the transport layer and the UI. Neither
// side knows about the other's internals (topics, sockets, widgets) -
// only these types. Swapping MQTT for the HA WebSocket API must not
// change this file.

#define MAX_ENTITIES 12
#define MAX_AUTOMATIONS 8
#define MAX_AREAS 6
// Home page + one per area.
#define MAX_PAGES (1 + MAX_AREAS)
// A favourite appears twice: on Home and on its room page.
#define MAX_TILES (MAX_ENTITIES * 2)
// Devices shown per page, 2 columns x 3 rows. Pages cannot scroll -
// vertical is spent on the Devices/Automations switch - so anything
// past this is not reachable and is dropped at build time.
#define MAX_PER_PAGE 6

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
  Temperature,       // entity = AREA index, fvalue
  Humidity,          // entity = AREA index, fvalue
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
// Areas mirror Home Assistant's own area registry, so the desktop
// authoring tool can populate these straight from HA rather than
// having rooms hand-listed here.
struct EntityDesc {
  const char *name;
  EntityKind kind;
  uint8_t area;      // index into the transport's area table
  bool favourite;    // also appears on the Home page
  // Icon *name* ("light", "fan", "plug"), not a glyph - the UI owns
  // the name-to-glyph mapping, the transport owns which name applies.
  // Last on purpose: the other transports brace-initialise this struct
  // without it, and a trailing member value-initialises to null.
  // Null is valid and draws the unknown glyph.
  const char *icon;
  // A fan whose level is its speed, not brightness. Only labels change;
  // the transport converts HA's percentage to and from 0-255. Trailing
  // for the same reason as icon: brace-initialisers leave it false.
  bool fan;
};

struct AutomationDesc {
  const char *name;
  // Optional button colour (0xRRGGBB) chosen in the designer; without
  // one the button is a plain card. Trailing, so brace-initialisers
  // leave has_color false.
  bool has_color;
  uint32_t color;
};
