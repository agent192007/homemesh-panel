#include "dashboard.h"

#include <lvgl.h>
#include <stdio.h>

// Layout for 320x240 landscape.
static const lv_coord_t HEADER_H = 32;
static const lv_coord_t GAP = 8;
static const lv_coord_t TILE_W = (320 - 3 * GAP) / 2;
static const lv_coord_t TILE_H = 70;

static const char *NAMES[ENT_COUNT] = {
  "Living Room",
  "Kitchen",
  "Bedroom",
  "Ceiling Fan",
};

#define COL_BG      lv_color_hex(0x101418)
#define COL_TILE    lv_color_hex(0x2A2F36)
#define COL_ON      lv_color_hex(0xF5A623)
#define COL_TEXT    lv_color_white()
#define COL_MUTED   lv_color_hex(0x8A9099)
#define COL_OK      lv_color_hex(0x3DDC84)
#define COL_WARN    lv_color_hex(0xF5A623)
#define COL_BAD     lv_color_hex(0xE5484D)

static power_cb_t power_cb;
static brightness_cb_t brightness_cb;

static lv_obj_t *tiles[ENT_COUNT];
static lv_obj_t *tile_state[ENT_COUNT];
static bool power[ENT_COUNT];
static bool known[ENT_COUNT];

static lv_obj_t *slider;
static lv_obj_t *slider_value;
static lv_obj_t *temp_label;
static lv_obj_t *hum_label;
static lv_obj_t *status_dot;
static lv_obj_t *status_label;

static LinkState link = LinkState::NoWifi;
static bool devices_ok = false;

static bool controls_enabled() {
  return link == LinkState::Online && devices_ok;
}

static void refresh_tile(uint8_t i) {
  if (!known[i]) {
    lv_label_set_text(tile_state[i], "--");
    lv_obj_clear_state(tiles[i], LV_STATE_CHECKED);
    return;
  }
  lv_label_set_text(tile_state[i], power[i] ? "ON" : "OFF");
  if (power[i]) lv_obj_add_state(tiles[i], LV_STATE_CHECKED);
  else lv_obj_clear_state(tiles[i], LV_STATE_CHECKED);
}

static void set_enabled(lv_obj_t *obj, bool en) {
  if (en) lv_obj_clear_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static void refresh_status() {
  bool en = controls_enabled();
  for (uint8_t i = 0; i < ENT_COUNT; i++) set_enabled(tiles[i], en);
  set_enabled(slider, en);

  lv_color_t sensor_col = en ? COL_TEXT : COL_MUTED;
  lv_obj_set_style_text_color(temp_label, sensor_col, 0);
  lv_obj_set_style_text_color(hum_label, sensor_col, 0);

  const char *text;
  lv_color_t col;
  switch (link) {
    case LinkState::NoWifi:   text = "No WiFi";   col = COL_BAD;  break;
    case LinkState::NoBroker: text = "No broker"; col = COL_BAD;  break;
    default:
      if (devices_ok) { text = "Online";      col = COL_OK; }
      else            { text = "Devices off"; col = COL_WARN; }
      break;
  }
  lv_label_set_text(status_label, text);
  lv_obj_set_style_bg_color(status_dot, col, 0);
}

static void set_slider_label(int32_t v) {
  lv_label_set_text_fmt(slider_value, "%d%%", (int)((v * 100 + 127) / 255));
}

static void tile_event_cb(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  if (power_cb) power_cb(i, !power[i]);
}

static void slider_event_cb(lv_event_t *e) {
  int32_t v = lv_slider_get_value(slider);
  set_slider_label(v);
  // Send once on release, not on every drag step.
  if (lv_event_get_code(e) == LV_EVENT_RELEASED && brightness_cb) {
    brightness_cb(ENT_LIVING_LIGHT, (uint8_t)v);
  }
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t col) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  return l;
}

static void create_header(lv_obj_t *scr) {
  lv_obj_t *title = make_label(scr, "HomeMesh", &lv_font_montserrat_20, COL_TEXT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, GAP, 6);

  temp_label = make_label(scr, "--.-\xC2\xB0" "C", &lv_font_montserrat_14, COL_MUTED);
  lv_obj_align(temp_label, LV_ALIGN_TOP_MID, 10, 9);

  hum_label = make_label(scr, "--%", &lv_font_montserrat_14, COL_MUTED);
  lv_obj_align_to(hum_label, temp_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);

  status_label = make_label(scr, "", &lv_font_montserrat_14, COL_MUTED);
  lv_obj_align(status_label, LV_ALIGN_TOP_RIGHT, -GAP, 9);

  status_dot = lv_obj_create(scr);
  lv_obj_set_size(status_dot, 10, 10);
  lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(status_dot, 0, 0);
  lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_CLICKABLE);
}

static void create_tile(lv_obj_t *scr, uint8_t i) {
  lv_obj_t *t = lv_btn_create(scr);
  tiles[i] = t;
  lv_obj_set_size(t, TILE_W, TILE_H);
  lv_obj_set_pos(t, GAP + (i % 2) * (TILE_W + GAP), HEADER_H + GAP + (i / 2) * (TILE_H + GAP));

  lv_obj_set_style_radius(t, 8, 0);
  lv_obj_set_style_shadow_width(t, 0, 0);
  lv_obj_set_style_bg_color(t, COL_TILE, 0);
  lv_obj_set_style_bg_color(t, COL_ON, LV_STATE_CHECKED);
  lv_obj_set_style_text_color(t, COL_TEXT, 0);
  lv_obj_set_style_text_color(t, lv_color_black(), LV_STATE_CHECKED);
  lv_obj_set_style_opa(t, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_add_event_cb(t, tile_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

  lv_obj_t *name = lv_label_create(t);
  lv_label_set_text(name, NAMES[i]);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

  tile_state[i] = lv_label_create(t);
  lv_obj_set_style_text_font(tile_state[i], &lv_font_montserrat_20, 0);
  lv_obj_align(tile_state[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

static void create_slider(lv_obj_t *scr) {
  lv_coord_t y = HEADER_H + GAP + 2 * (TILE_H + GAP) + 6;

  lv_obj_t *label = make_label(scr, "Living", &lv_font_montserrat_14, COL_TEXT);
  lv_obj_set_pos(label, GAP, y + 4);

  slider = lv_slider_create(scr);
  lv_slider_set_range(slider, 1, 255);
  lv_obj_set_size(slider, 180, 12);
  lv_obj_set_pos(slider, 72, y + 7);
  lv_obj_set_style_bg_color(slider, COL_ON, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, COL_ON, LV_PART_KNOB);
  lv_obj_set_style_opa(slider, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_set_ext_click_area(slider, 12);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_RELEASED, NULL);

  slider_value = make_label(scr, "--", &lv_font_montserrat_14, COL_TEXT);
  lv_obj_align(slider_value, LV_ALIGN_TOP_RIGHT, -GAP, y + 4);
}

void dashboard_create(power_cb_t on_power, brightness_cb_t on_brightness) {
  power_cb = on_power;
  brightness_cb = on_brightness;

  lv_obj_t *scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  create_header(scr);
  for (uint8_t i = 0; i < ENT_COUNT; i++) {
    create_tile(scr, i);
    refresh_tile(i);
  }
  create_slider(scr);
  refresh_status();
  lv_obj_align_to(status_dot, status_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

void dashboard_apply(const PanelEvent &e) {
  char buf[16];
  switch (e.type) {
    case EventType::Link:
      link = (LinkState)e.value;
      break;
    case EventType::DevicesAvailable:
      devices_ok = e.value != 0;
      break;
    case EventType::Power:
      if (e.entity < ENT_COUNT) {
        power[e.entity] = e.value != 0;
        known[e.entity] = true;
        refresh_tile(e.entity);
      }
      break;
    case EventType::Brightness:
      // Don't yank the slider out from under a finger.
      if (!lv_slider_is_dragged(slider)) {
        // Clamp to the slider's own range before labelling it, so the
        // knob and the percentage can't disagree - the slider starts
        // at 1, and set_value would silently clamp a reported 0.
        int32_t v = LV_MAX(lv_slider_get_min_value(slider),
                           LV_MIN(lv_slider_get_max_value(slider), e.value));
        lv_slider_set_value(slider, v, LV_ANIM_OFF);
        set_slider_label(v);
      }
      break;
    case EventType::Temperature:
      snprintf(buf, sizeof(buf), "%.1f\xC2\xB0" "C", e.fvalue);
      lv_label_set_text(temp_label, buf);
      lv_obj_align_to(hum_label, temp_label, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
      break;
    case EventType::Humidity:
      snprintf(buf, sizeof(buf), "%.0f%%", e.fvalue);
      lv_label_set_text(hum_label, buf);
      break;
  }
  refresh_status();
  lv_obj_align_to(status_dot, status_label, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}
