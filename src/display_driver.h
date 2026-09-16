#pragma once

#include <stdint.h>

// Initialises the panel and registers it with LVGL.
// Call lv_init() first. rotation follows TFT_eSPI (0-3, 0 = portrait).
void display_init(uint8_t rotation);
