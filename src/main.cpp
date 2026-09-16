#include <Arduino.h>
#include <lvgl.h>

#include "display_driver.h"
#include "touch_driver.h"
#include "transport.h"
#include "ui/dashboard.h"

// 1 = landscape (320x240). 0 = native portrait, 3 = landscape
// flipped 180. Display and touch must use the same value.
static const uint8_t PANEL_ROTATION = 1;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\nHomeMesh Panel starting");

  lv_init();  // LVGL tick comes from millis() via lv_conf.h
  display_init(PANEL_ROTATION);
  touch_init(PANEL_ROTATION);

  // Transport first: the UI reads its entity table to build pages.
  transport_start();
  dashboard_create();
}

void loop() {
  PanelEvent e;
  while (transport_next_event(&e)) dashboard_apply(e);

  lv_timer_handler();
  delay(5);
}
