#include "panel_icons.h"

#include <string.h>

// Flat table rather than a map: 16 entries, looked up once per
// tile at build time, never in the draw path.
struct IconEntry { const char *name; const char *glyph; };

static const IconEntry ICONS[] = {
  { "light", ICON_LIGHT },
  { "ceiling", ICON_CEILING },
  { "lamp", ICON_LAMP },
  { "strip", ICON_STRIP },
  { "fan", ICON_FAN },
  { "ac", ICON_AC },
  { "toggle", ICON_TOGGLE },
  { "plug", ICON_PLUG },
  { "temp", ICON_TEMP },
  { "humidity", ICON_HUMIDITY },
  { "tv", ICON_TV },
  { "speaker", ICON_SPEAKER },
  { "lock", ICON_LOCK },
  { "blinds", ICON_BLINDS },
  { "home", ICON_HOME },
  { "unknown", ICON_UNKNOWN },
  { "offline", ICON_OFFLINE },
};

const char *icon_glyph(const char *name) {
  if (name && *name) {
    for (unsigned i = 0; i < sizeof(ICONS) / sizeof(ICONS[0]); i++) {
      if (strcmp(ICONS[i].name, name) == 0) return ICONS[i].glyph;
    }
  }
  return ICON_UNKNOWN;
}
