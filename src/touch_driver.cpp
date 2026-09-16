#include "touch_driver.h"

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>

// ------------------------------------------------------------
//  GT911 capacitive touch, polled over I2C.
//
//  Pins confirmed by src/i2c_touch_scan.cpp on our board:
//    SDA = 33, SCL = 32, GT911 answered at 0x5D.
//  This file is the source of truth for touch pins.
//
//  INT and RST are not used: the scan found the chip without a
//  reset sequence, and driving INT during reset can change the
//  GT911's I2C address. We poll instead of using the interrupt.
// ------------------------------------------------------------
static const int TOUCH_SDA = 33;
static const int TOUCH_SCL = 32;
static const uint8_t GT911_ADDR_PRIMARY = 0x5D;
static const uint8_t GT911_ADDR_ALT = 0x14;

// Native panel size (portrait, rotation 0).
static const int16_t PANEL_W = 240;
static const int16_t PANEL_H = 320;

// Orientation fix-ups between the GT911's axes and the panel's
// portrait axes. Tune these with the test screen if taps land in
// the wrong place at rotation 0.
#define TOUCH_SWAP_XY 0
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 0

// GT911 registers
static const uint16_t REG_PRODUCT_ID = 0x8140;
static const uint16_t REG_X_RES = 0x8048;
static const uint16_t REG_STATUS = 0x814E;
static const uint16_t REG_POINT1 = 0x8150;

static uint8_t gt_addr = GT911_ADDR_PRIMARY;
static uint8_t disp_rotation = 0;
static uint16_t res_x = PANEL_W;
static uint16_t res_y = PANEL_H;

static int16_t last_x = 0;
static int16_t last_y = 0;
static bool last_pressed = false;

static lv_indev_drv_t indev_drv;

static bool gt_read(uint16_t reg, uint8_t *buf, uint8_t len) {
  Wire.beginTransmission(gt_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint16_t)gt_addr, (size_t)len, true) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static void gt_write_byte(uint16_t reg, uint8_t val) {
  Wire.beginTransmission(gt_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(val);
  Wire.endTransmission();
}

// Raw GT911 coordinates -> panel portrait coordinates -> rotated
// screen coordinates matching tft.setRotation().
static void map_point(uint16_t rx, uint16_t ry, int16_t *out_x, int16_t *out_y) {
  int32_t px, py;
#if TOUCH_SWAP_XY
  px = (int32_t)ry * PANEL_W / res_y;
  py = (int32_t)rx * PANEL_H / res_x;
#else
  px = (int32_t)rx * PANEL_W / res_x;
  py = (int32_t)ry * PANEL_H / res_y;
#endif
#if TOUCH_INVERT_X
  px = PANEL_W - 1 - px;
#endif
#if TOUCH_INVERT_Y
  py = PANEL_H - 1 - py;
#endif
  px = constrain(px, 0, PANEL_W - 1);
  py = constrain(py, 0, PANEL_H - 1);

  switch (disp_rotation) {
    case 1: *out_x = py;               *out_y = PANEL_W - 1 - px; break;
    case 2: *out_x = PANEL_W - 1 - px; *out_y = PANEL_H - 1 - py; break;
    case 3: *out_x = PANEL_H - 1 - py; *out_y = px;               break;
    default: *out_x = px;              *out_y = py;               break;
  }
}

static void read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  uint8_t status;
  if (gt_read(REG_STATUS, &status, 1) && (status & 0x80)) {
    uint8_t points = status & 0x0F;
    if (points > 0) {
      uint8_t p[4];
      if (gt_read(REG_POINT1, p, 4)) {
        map_point(p[0] | (p[1] << 8), p[2] | (p[3] << 8), &last_x, &last_y);
        last_pressed = true;
      }
    } else {
      last_pressed = false;
    }
    gt_write_byte(REG_STATUS, 0);  // tell the GT911 we consumed the buffer
  }
  // If the buffer was not ready, report the previous state unchanged.

  data->point.x = last_x;
  data->point.y = last_y;
  data->state = last_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
}

static bool probe(uint8_t addr) {
  gt_addr = addr;
  uint8_t id[4];
  if (!gt_read(REG_PRODUCT_ID, id, 4)) return false;
  Serial.printf("[touch] GT911 at 0x%02X, product id '%c%c%c%c'\n",
                addr, id[0], id[1], id[2], id[3] ? id[3] : ' ');
  return true;
}

bool touch_init(uint8_t rotation) {
  disp_rotation = rotation;
  Wire.begin(TOUCH_SDA, TOUCH_SCL, 400000);

  if (!probe(GT911_ADDR_PRIMARY) && !probe(GT911_ADDR_ALT)) {
    Serial.println("[touch] GT911 not found on SDA=33 SCL=32");
    return false;
  }

  uint8_t r[4];
  if (gt_read(REG_X_RES, r, 4)) {
    uint16_t x = r[0] | (r[1] << 8);
    uint16_t y = r[2] | (r[3] << 8);
    if (x > 0 && y > 0) {
      res_x = x;
      res_y = y;
    }
  }
  Serial.printf("[touch] reported resolution %u x %u\n", res_x, res_y);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = read_cb;
  lv_indev_drv_register(&indev_drv);
  return true;
}
