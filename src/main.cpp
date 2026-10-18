#include <Arduino.h>
#include <lvgl.h>

#include "display_driver.h"
#include "touch_driver.h"
#include "transport.h"
#include "ui/dashboard.h"

// 1 = landscape (320x240). 0 = native portrait, 3 = landscape
// flipped 180. Display and touch must use the same value.
static const uint8_t PANEL_ROTATION = 1;

// Wired in as LV_ASSERT_HANDLER by lv_conf.h, replacing LVGL's silent
// while(1). Runs on the LVGL thread - the only one that calls LVGL.
extern "C" void lv_assert_hit(const char *file, int line) {
  const char *base = file;
  for (const char *p = file; *p; p++)
    if (*p == '/' || *p == '\\') base = p + 1;
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  Serial.printf("\n[lvgl] ASSERT FAILED at %s:%d - pool %u of %u bytes free, "
                "largest block %u, peak use %u. Restarting.\n",
                base, line, (unsigned)m.free_size, (unsigned)m.total_size,
                (unsigned)m.free_biggest_size, (unsigned)m.max_used);
  Serial.flush();
  delay(2000);
  ESP.restart();
}

// The pool's peak (max_used) is the number that matters: mask buffers
// are allocated while drawing and freed straight after, so the
// widgets alone undercount what a redraw needs. Note it counts
// requested bytes only - block headers are not in it - so the "free"
// figures are the honest measure of headroom. Logged once
// the UI is built, then again only when the peak climbs.
static uint32_t peak_logged;

static void log_memory(const char *when) {
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  peak_logged = m.max_used;
  Serial.printf("[mem] %s: LVGL pool peak %u of %u (%u%%), now %u free, "
                "largest block %u | heap %u free, low %u\n",
                when, (unsigned)m.max_used, (unsigned)m.total_size,
                (unsigned)(m.max_used * 100 / m.total_size),
                (unsigned)m.free_size, (unsigned)m.free_biggest_size,
                (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap());
}

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
  lv_timer_handler();   // draw once, so the peak includes a full redraw
  log_memory("UI built");
}

void loop() {
  PanelEvent e;
  while (transport_next_event(&e)) dashboard_apply(e);

  lv_timer_handler();

  static uint32_t last_mem_check;
  if (millis() - last_mem_check > 2000) {
    last_mem_check = millis();
    lv_mem_monitor_t m;
    lv_mem_monitor(&m);
    if (m.max_used > peak_logged) log_memory("new peak");
  }

  delay(5);
}
