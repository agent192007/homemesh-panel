#pragma once

#include <stdint.h>

// ------------------------------------------------------------
//  The panel's colours, as data.
//
//  These were #defines in ui/dashboard.cpp, so restyling the panel
//  meant a rebuild and a reflash - the same problem the entity table
//  had. A pushed layout may carry an optional "theme" object, and
//  these are what it fills.
//
//  Plain 0xRRGGBB rather than lv_color_t deliberately: this header is
//  included by the transport and the layout store, and neither should
//  have to pull in LVGL to talk about a colour. ui/dashboard.cpp does
//  the one conversion, at the point of use.
//
//  g_theme always holds a complete theme. A layout with no "theme"
//  object, or one naming only some of the colours, leaves the rest at
//  the compiled defaults - so a partial theme is a valid theme and
//  there is no state where a colour is undefined.
// ------------------------------------------------------------

struct PanelTheme {
  uint32_t bg;      // screen background
  uint32_t tile;    // device tile face
  uint32_t on;      // an entity that is on, and the warning colour
  uint32_t accent;  // sliders, brightness bars
  uint32_t text;    // primary label text
  uint32_t muted;   // secondary text, blank sensor slots
  uint32_t ok;      // status dot: connected
  uint32_t warn;    // status dot: WiFi but no Home Assistant
  uint32_t bad;     // status dot: no WiFi
};

extern PanelTheme g_theme;

// The compiled look. Also the fallback for anything a pushed theme
// does not name.
void theme_defaults(PanelTheme *t);

// Number of fields, for tools that iterate them.
#define PANEL_THEME_FIELDS 9
