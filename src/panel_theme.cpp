#include "panel_theme.h"

// Defined here rather than in layout_store.cpp because ui/dashboard.cpp
// needs it in every environment, and the layout store is only built
// into env:ha - the simulated and legacy MQTT paths would not link.
//
// The values are the ones that were #defined in ui/dashboard.cpp.
// env:ha may overwrite them from a pushed layout; the other
// environments simply keep them.

void theme_defaults(PanelTheme *t) {
  t->bg     = 0x101418;
  t->tile   = 0x2A2F36;
  t->on     = 0xF5A623;
  t->accent = 0x4C8DFF;
  t->text   = 0xFFFFFF;
  t->muted  = 0x8A9099;
  t->ok     = 0x3DDC84;
  t->warn   = 0xF5A623;
  t->bad    = 0xE5484D;
}

PanelTheme g_theme = {
  0x101418, 0x2A2F36, 0xF5A623, 0x4C8DFF,
  0xFFFFFF, 0x8A9099, 0x3DDC84, 0xF5A623, 0xE5484D,
};
