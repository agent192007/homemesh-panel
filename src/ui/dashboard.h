#pragma once

#include "../panel_model.h"

// Two screens, swapped by a vertical swipe:
//
//   Devices      overview page, then one page per entity
//   Automations  pages of four
//
// Horizontal swipe pages within a screen (lv_tileview). Commands go
// straight to transport_*; the UI never learns what the transport is.
void dashboard_create();

// Apply a state change. Call from the LVGL (loop) task only.
void dashboard_apply(const PanelEvent &e);
