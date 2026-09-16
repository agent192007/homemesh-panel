#include "display_driver.h"

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

// Panel pins, driver and colour order are set by build flags in
// platformio.ini (ST7789V, inversion on, BGR).

static TFT_eSPI tft;

// Partial-frame draw buffer. No PSRAM, so a strip of the screen
// rather than a full frame. Sized for the long edge so it works in
// any rotation.
static const uint32_t BUF_PIXELS = 320 * 24;
static lv_color_t draw_buf_mem[BUF_PIXELS];

static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->full, w * h, true);
  tft.endWrite();

  lv_disp_flush_ready(drv);
}

void display_init(uint8_t rotation) {
  tft.init();
  tft.setRotation(rotation);
  tft.fillScreen(TFT_BLACK);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);

  lv_disp_draw_buf_init(&draw_buf, draw_buf_mem, NULL, BUF_PIXELS);

  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = tft.width();
  disp_drv.ver_res = tft.height();
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  Serial.printf("[display] %d x %d, rotation %d\n", tft.width(), tft.height(), rotation);
}
