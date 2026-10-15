#pragma once

#include <stddef.h>
#include <stdint.h>

#include "panel_model.h"

// ------------------------------------------------------------
//  The runtime layout table.
//
//  This is the "layout as data" half of the endgame in CLAUDE.md.
//  Which entities the panel shows used to be HA_ENTITIES[] in
//  include/ha_layout.h - const, in flash, changeable only by a
//  regenerate/rebuild/reflash cycle. Now it is a table in RAM,
//  populated at boot from whichever source is available:
//
//    1. NVS, if a layout has ever been pushed to this board
//    2. the compiled ha_layout.h, otherwise
//
//  The header stays in the build on purpose. A board that has never
//  been pushed to still has to come up with a working demo on it, and
//  a build whose UI depends on NVS having been written is a bad thing
//  to discover at 9am.
//
//  Strings live in one fixed arena rather than the heap. The LVGL
//  pool is a fixed 40KB and OOM there presents as a hang, so the
//  layout deliberately cannot grow into it - if a layout does not fit
//  the arena it is rejected whole, with a reason.
// ------------------------------------------------------------

struct LayoutEntity {
  const char *id;      // entity_id, for subscribe_trigger / call_service
  const char *name;    // friendly name, for the tile
  EntityKind kind;
  uint8_t area;        // index into areas[]
  bool favourite;      // also appears on the Home page
};

struct LayoutAction {
  const char *id;
  const char *name;
};

struct LayoutSensor {
  const char *id;
  uint8_t area;
  bool humidity;       // false = temperature
};

struct Layout {
  const char *areas[MAX_AREAS];
  LayoutEntity ents[MAX_ENTITIES];
  LayoutAction acts[MAX_AUTOMATIONS];
  LayoutSensor sens[MAX_ENTITIES];
  uint8_t area_n;
  uint8_t ent_n;
  uint8_t act_n;
  uint8_t sens_n;
  bool from_nvs;       // false = running off the compiled fallback
};

extern Layout g_layout;

// The largest layout accepted, in bytes of JSON. NVS caps a string
// entry at 4000; staying under that leaves room for NVS's own
// overhead. The nine-entity demo layout is ~1000 bytes.
static const size_t LAYOUT_MAX_JSON = 3072;

// The HA event a pushed layout arrives on. The panel subscribes to
// this on the WebSocket it already holds, which is why the desktop
// tool never needs to discover the panel's IP address.
#define LAYOUT_EVENT "homemesh_layout"

// Populate g_layout. NVS first, compiled header on any failure.
// Call once, before anything reads the table.
void layout_boot();

// Validate a pushed layout and persist it to NVS. Does NOT touch
// g_layout - the board restarts to pick it up, so there is never a
// half-swapped table driving live LVGL widgets.
//
// Returns false and fills err on rejection, which is the common case
// worth reporting: a layout too big for the arena, an area index out
// of range, an unknown format version.
bool layout_store_json(const char *json, size_t len, char *err, size_t err_n);

// Drop the stored layout; the next boot falls back to ha_layout.h.
void layout_forget();
