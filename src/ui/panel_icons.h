#pragma once

#include <lvgl.h>

// ------------------------------------------------------------
//  Device icons.
//
//  A 16-glyph subset of Material Design Icons, converted to two
//  LVGL fonts by tools/make_icon_font.sh. They are font glyphs,
//  not images: they draw like text, straight out of flash, and
//  cost nothing from the 40KB LVGL pool. That is the whole
//  reason icons are a font here and not PNGs on the SD card.
//
//  Flash cost: ~3.4KB of bitmap for both sizes together.
//
//  The names below are the panel's own, not MDI's. They travel
//  in the layout JSON, so they have to outlive whichever icon
//  set they happened to be cut from.
// ------------------------------------------------------------

LV_FONT_DECLARE(panel_icons_28);
LV_FONT_DECLARE(panel_icons_18);

#define ICON_LIGHT     "\xF3\xB0\x8C\xB5"   // mdi:lightbulb  U+F0335
#define ICON_CEILING   "\xF3\xB0\x9D\xA9"   // mdi:ceiling-light  U+F0769
#define ICON_LAMP      "\xF3\xB0\x9A\xB5"   // mdi:lamp  U+F06B5
#define ICON_STRIP     "\xF3\xB1\x81\x91"   // mdi:led-strip-variant  U+F1051
#define ICON_FAN       "\xF3\xB0\x88\x90"   // mdi:fan  U+F0210
#define ICON_AC        "\xF3\xB0\x80\x9B"   // mdi:air-conditioner  U+F001B
#define ICON_TOGGLE    "\xF3\xB0\x94\xA1"   // mdi:toggle-switch  U+F0521
#define ICON_PLUG      "\xF3\xB0\x9A\xA5"   // mdi:power-plug  U+F06A5
#define ICON_TEMP      "\xF3\xB0\x94\x8F"   // mdi:thermometer  U+F050F
#define ICON_HUMIDITY  "\xF3\xB0\x96\x8E"   // mdi:water-percent  U+F058E
#define ICON_TV        "\xF3\xB0\x94\x82"   // mdi:television  U+F0502
#define ICON_SPEAKER   "\xF3\xB0\x93\x83"   // mdi:speaker  U+F04C3
#define ICON_LOCK      "\xF3\xB0\x8C\xBE"   // mdi:lock  U+F033E
#define ICON_BLINDS    "\xF3\xB0\x82\xAC"   // mdi:blinds  U+F00AC
#define ICON_HOME      "\xF3\xB0\x8B\x9C"   // mdi:home  U+F02DC
#define ICON_UNKNOWN   "\xF3\xB0\x8B\x97"   // mdi:help-circle  U+F02D7

#define ICON_COUNT 16

// Icon name from the layout -> glyph. Unknown names fall back to
// ICON_UNKNOWN rather than drawing nothing, so a typo in a pushed
// layout is visible on the panel instead of silently blank.
const char *icon_glyph(const char *name);
