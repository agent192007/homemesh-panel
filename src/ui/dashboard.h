#pragma once

#include "../panel_model.h"

typedef void (*power_cb_t)(uint8_t entity, bool on);
typedef void (*brightness_cb_t)(uint8_t entity, uint8_t value);

// Main landscape screen: 4 toggle tiles, living-room brightness
// slider, temperature/humidity and connection status.
void dashboard_create(power_cb_t on_power, brightness_cb_t on_brightness);

// Apply a state change. Call from the LVGL (loop) task only.
void dashboard_apply(const PanelEvent &e);
