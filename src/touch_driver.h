#pragma once

#include <stdint.h>

// Initialises the touch controller and registers it as an LVGL
// pointer device. Call after display_init(), with the same rotation.
// Returns false if the controller did not respond.
bool touch_init(uint8_t rotation);
