#include "dashboard.h"

#include <lvgl.h>
#include <stdio.h>

#include "../transport.h"

// ------------------------------------------------------------
//  Layout: 320x240 landscape.
//
//  A fixed header lives on lv_layer_top() so it is drawn once and
//  stays put across both screens. Each screen owns an lv_tileview
//  below it, scrolling horizontally only - which leaves vertical
//  drags free to surface as gestures and swap the screens.
// ------------------------------------------------------------
static const lv_coord_t HEADER_H = 28;
static const lv_coord_t BODY_H = 240 - HEADER_H;
static const lv_coord_t GAP = 8;

#define COL_BG      lv_color_hex(0x101418)
#define COL_TILE    lv_color_hex(0x2A2F36)
#define COL_ON      lv_color_hex(0xF5A623)
#define COL_ACCENT  lv_color_hex(0x4C8DFF)
#define COL_TEXT    lv_color_white()
#define COL_MUTED   lv_color_hex(0x8A9099)
#define COL_OK      lv_color_hex(0x3DDC84)
#define COL_WARN    lv_color_hex(0xF5A623)
#define COL_BAD     lv_color_hex(0xE5484D)

struct EntityUI {
  lv_obj_t *ov_tile;   // overview mini tile
  lv_obj_t *ov_state;  // overview state label
  lv_obj_t *btn;       // per-page toggle button
  lv_obj_t *btn_label;
  lv_obj_t *slider;    // NULL for Toggle
  lv_obj_t *pct;       // NULL for Toggle
  lv_obj_t *wheel;     // NULL unless RGB
  lv_obj_t *swatch;    // NULL unless RGB
};

static EntityUI ui[MAX_ENTITIES];
static lv_obj_t *auto_btn[MAX_AUTOMATIONS];

static bool power[MAX_ENTITIES];
static uint8_t bright[MAX_ENTITIES];
static int32_t color[MAX_ENTITIES];
static bool known[MAX_ENTITIES];

static uint8_t ent_n;
static uint8_t auto_n;

static lv_obj_t *scr_devices;
static lv_obj_t *scr_autos;
// Tracked rather than read back from lv_scr_act(): the title must
// update from the screen we are switching *to*, not the one LVGL
// still considers active.
static lv_obj_t *cur_screen;
static lv_obj_t *tv_devices;
static lv_obj_t *tv_autos;

static lv_obj_t *title_label;
static lv_obj_t *sensor_label;
static lv_obj_t *status_dot;

static LinkState link = LinkState::NoWifi;
static bool devices_ok = false;
static float last_temp = 0, last_hum = 0;
static bool have_temp = false, have_hum = false;

static bool controls_enabled() {
  return link == LinkState::Online && devices_ok;
}

// ------------------------------------------------------------
//  Small helpers
// ------------------------------------------------------------
static lv_obj_t *make_label(lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t col) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, col, 0);
  return l;
}

static void set_enabled(lv_obj_t *obj, bool en) {
  if (!obj) return;
  if (en) lv_obj_clear_state(obj, LV_STATE_DISABLED);
  else lv_obj_add_state(obj, LV_STATE_DISABLED);
}

static void style_card(lv_obj_t *o) {
  lv_obj_set_style_radius(o, 8, 0);
  lv_obj_set_style_shadow_width(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_bg_color(o, COL_TILE, 0);
  lv_obj_set_style_opa(o, LV_OPA_40, LV_STATE_DISABLED);
}

static int pct_of(uint8_t v) { return (v * 100 + 127) / 255; }

// ------------------------------------------------------------
//  Event callbacks
// ------------------------------------------------------------
static void toggle_cb(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  transport_send_power(i, !power[i]);
}

static void slider_cb(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  int32_t v = lv_slider_get_value(ui[i].slider);
  lv_label_set_text_fmt(ui[i].pct, "%d%%", pct_of((uint8_t)v));
  if (lv_event_get_code(e) == LV_EVENT_RELEASED)
    transport_send_brightness(i, (uint8_t)v);
}

static void wheel_cb(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  lv_color_t c = lv_colorwheel_get_rgb(ui[i].wheel);
  lv_obj_set_style_bg_color(ui[i].swatch, c, 0);
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
    uint32_t v = lv_color_to32(c);
    transport_send_color(i, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
  }
}

static void automation_cb(lv_event_t *e) {
  transport_run_automation((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

// Clear the brief "ran" highlight.
static void unflash_cb(lv_timer_t *t) {
  lv_obj_clear_state((lv_obj_t *)t->user_data, LV_STATE_CHECKED);
  lv_timer_del(t);
}

static void update_title();

static void tile_changed_cb(lv_event_t *e) {
  (void)e;
  update_title();
}

// Vertical swipe swaps Devices <-> Automations. Horizontal is consumed
// by the tileview, so only LV_DIR_TOP/BOTTOM reach here.
static void gesture_cb(lv_event_t *e) {
  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  lv_obj_t *cur = (lv_obj_t *)lv_event_get_user_data(e);

  if (d == LV_DIR_TOP && cur == scr_devices && auto_n) {
    cur_screen = scr_autos;
    lv_scr_load(scr_autos);
    update_title();
  } else if (d == LV_DIR_BOTTOM && cur == scr_autos) {
    cur_screen = scr_devices;
    lv_scr_load(scr_devices);
    update_title();
  }
}

// ------------------------------------------------------------
//  Refresh
// ------------------------------------------------------------
static void refresh_entity(uint8_t i) {
  const char *state = !known[i] ? "--" : (power[i] ? "ON" : "OFF");
  lv_label_set_text(ui[i].ov_state, state);
  lv_label_set_text(ui[i].btn_label, state);

  if (known[i] && power[i]) {
    lv_obj_add_state(ui[i].ov_tile, LV_STATE_CHECKED);
    lv_obj_add_state(ui[i].btn, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(ui[i].ov_tile, LV_STATE_CHECKED);
    lv_obj_clear_state(ui[i].btn, LV_STATE_CHECKED);
  }
}

static void update_title() {
  const char *t = "Devices";
  if (cur_screen == scr_autos) {
    t = "Automations";
  } else {
    lv_obj_t *act = lv_tileview_get_tile_act(tv_devices);
    // Tile 0 is the overview; tiles 1..n map to entities.
    for (uint8_t i = 0; i < ent_n; i++) {
      if (act == lv_obj_get_parent(ui[i].btn)) {
        t = transport_entity(i)->name;
        break;
      }
    }
  }
  lv_label_set_text(title_label, t);
}

static void refresh_status() {
  bool en = controls_enabled();
  for (uint8_t i = 0; i < ent_n; i++) {
    set_enabled(ui[i].ov_tile, en);
    set_enabled(ui[i].btn, en);
    set_enabled(ui[i].slider, en);
    set_enabled(ui[i].wheel, en);
  }
  for (uint8_t i = 0; i < auto_n; i++) set_enabled(auto_btn[i], en);

  lv_color_t col;
  switch (link) {
    case LinkState::NoWifi:   col = COL_BAD;  break;
    case LinkState::NoBroker: col = COL_BAD;  break;
    default:                  col = devices_ok ? COL_OK : COL_WARN; break;
  }
  lv_obj_set_style_bg_color(status_dot, col, 0);

  char buf[24];
  if (!en) {
    const char *t = link == LinkState::NoWifi   ? "No WiFi"
                  : link == LinkState::NoBroker ? "No link"
                                                : "Devices off";
    lv_label_set_text(sensor_label, t);
    lv_obj_set_style_text_color(sensor_label, col, 0);
  } else if (have_temp || have_hum) {
    snprintf(buf, sizeof(buf), "%.1f\xC2\xB0" "C  %.0f%%", last_temp, last_hum);
    lv_label_set_text(sensor_label, buf);
    lv_obj_set_style_text_color(sensor_label, COL_MUTED, 0);
  } else {
    lv_label_set_text(sensor_label, "");
  }
  lv_obj_align_to(sensor_label, status_dot, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

// ------------------------------------------------------------
//  Construction
// ------------------------------------------------------------
static void create_header() {
  // lv_layer_top() is created with lv_obj_create(NULL), i.e. it is a
  // *screen* - LVGL forces it back to full display size on every
  // layout pass. Sizing it here does nothing, and giving it an opaque
  // background paints over the whole panel. It must stay transparent;
  // the bar is a child of it.
  lv_obj_t *top = lv_layer_top();
  lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
  lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *bar = lv_obj_create(top);
  lv_obj_remove_style_all(bar);
  lv_obj_set_size(bar, 320, HEADER_H);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, COL_BG, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  title_label = make_label(bar, "Devices", &lv_font_montserrat_20, COL_TEXT);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, GAP, 0);

  status_dot = lv_obj_create(bar);
  lv_obj_set_size(status_dot, 10, 10);
  lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(status_dot, 0, 0);
  lv_obj_clear_flag(status_dot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(status_dot, LV_ALIGN_RIGHT_MID, -GAP, 0);

  sensor_label = make_label(bar, "", &lv_font_montserrat_14, COL_MUTED);
  lv_obj_align_to(sensor_label, status_dot, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

// Overview: every entity as a mini tile, tap to toggle.
static void create_overview(lv_obj_t *tile) {
  lv_coord_t w = (320 - 3 * GAP) / 2;
  lv_coord_t h = (BODY_H - 3 * GAP) / 2;

  for (uint8_t i = 0; i < ent_n && i < 4; i++) {
    lv_obj_t *t = lv_btn_create(tile);
    style_card(t);
    lv_obj_set_size(t, w, h);
    lv_obj_set_pos(t, GAP + (i % 2) * (w + GAP), GAP + (i / 2) * (h + GAP));
    lv_obj_set_style_bg_color(t, COL_ON, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(t, COL_TEXT, 0);
    lv_obj_set_style_text_color(t, lv_color_black(), LV_STATE_CHECKED);
    lv_obj_add_event_cb(t, toggle_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

    lv_obj_t *n = lv_label_create(t);
    lv_label_set_text(n, transport_entity(i)->name);
    lv_obj_set_width(n, w - 20);
    lv_label_set_long_mode(n, LV_LABEL_LONG_DOT);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *s = lv_label_create(t);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_20, 0);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    ui[i].ov_tile = t;
    ui[i].ov_state = s;
  }
}

// One page per entity, built from its kind.
static void create_entity_page(lv_obj_t *tile, uint8_t i) {
  const EntityDesc *d = transport_entity(i);

  lv_obj_t *name = make_label(tile, d->name, &lv_font_montserrat_20, COL_TEXT);
  lv_obj_set_pos(name, GAP + 4, 6);

  lv_obj_t *b = lv_btn_create(tile);
  style_card(b);
  lv_obj_set_size(b, 96, 40);
  lv_obj_set_pos(b, 320 - 96 - GAP, 4);
  lv_obj_set_style_bg_color(b, COL_ON, LV_STATE_CHECKED);
  lv_obj_set_style_text_color(b, COL_TEXT, 0);
  lv_obj_set_style_text_color(b, lv_color_black(), LV_STATE_CHECKED);
  lv_obj_add_event_cb(b, toggle_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
  ui[i].btn = b;
  ui[i].btn_label = lv_label_create(b);
  lv_obj_set_style_text_font(ui[i].btn_label, &lv_font_montserrat_20, 0);
  lv_obj_center(ui[i].btn_label);

  if (d->kind == EntityKind::RGB) {
    lv_obj_t *w = lv_colorwheel_create(tile, true);
    lv_obj_set_size(w, 104, 104);
    lv_obj_set_pos(w, GAP + 6, 56);
    lv_obj_add_event_cb(w, wheel_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    lv_obj_add_event_cb(w, wheel_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)i);
    ui[i].wheel = w;

    lv_obj_t *sw = lv_obj_create(tile);
    style_card(sw);
    lv_obj_set_size(sw, 150, 30);
    lv_obj_set_pos(sw, 150, 56);
    lv_obj_clear_flag(sw, LV_OBJ_FLAG_CLICKABLE);
    ui[i].swatch = sw;
  }

  if (d->kind != EntityKind::Toggle) {
    lv_coord_t sx = (d->kind == EntityKind::RGB) ? 150 : GAP + 4;
    lv_coord_t sy = (d->kind == EntityKind::RGB) ? 112 : 110;
    lv_coord_t sw_ = (d->kind == EntityKind::RGB) ? 150 : 320 - 2 * (GAP + 4);

    lv_obj_t *cap = make_label(tile, "Brightness", &lv_font_montserrat_14, COL_MUTED);
    lv_obj_set_pos(cap, sx, sy);

    ui[i].pct = make_label(tile, "--", &lv_font_montserrat_14, COL_TEXT);
    lv_obj_set_pos(ui[i].pct, sx + sw_ - 40, sy);

    lv_obj_t *s = lv_slider_create(tile);
    lv_slider_set_range(s, 1, 255);
    lv_obj_set_size(s, sw_, 14);
    lv_obj_set_pos(s, sx, sy + 24);
    lv_obj_set_style_bg_color(s, COL_ON, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, COL_ON, LV_PART_KNOB);
    lv_obj_set_style_opa(s, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_ext_click_area(s, 14);
    lv_obj_add_event_cb(s, slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(uintptr_t)i);
    lv_obj_add_event_cb(s, slider_cb, LV_EVENT_RELEASED, (void *)(uintptr_t)i);
    ui[i].slider = s;
  }
}

static void create_automation_pages() {
  lv_coord_t w = (320 - 3 * GAP) / 2;
  lv_coord_t h = (BODY_H - 3 * GAP) / 2;
  uint8_t pages = (auto_n + 3) / 4;

  for (uint8_t p = 0; p < pages; p++) {
    lv_obj_t *tile = lv_tileview_add_tile(tv_autos, p, 0, LV_DIR_HOR);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);

    for (uint8_t k = 0; k < 4; k++) {
      uint8_t idx = p * 4 + k;
      if (idx >= auto_n) break;

      lv_obj_t *b = lv_btn_create(tile);
      style_card(b);
      lv_obj_set_size(b, w, h);
      lv_obj_set_pos(b, GAP + (k % 2) * (w + GAP), GAP + (k / 2) * (h + GAP));
      lv_obj_set_style_bg_color(b, COL_ACCENT, LV_STATE_CHECKED);
      lv_obj_add_event_cb(b, automation_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)idx);

      lv_obj_t *l = lv_label_create(b);
      lv_label_set_text(l, transport_automation(idx)->name);
      lv_obj_set_width(l, w - 20);
      lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
      lv_obj_center(l);

      auto_btn[idx] = b;
    }
  }
}

static lv_obj_t *make_screen(lv_obj_t **tv_out) {
  lv_obj_t *scr = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr, COL_BG, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *tv = lv_tileview_create(scr);
  lv_obj_set_pos(tv, 0, HEADER_H);
  lv_obj_set_size(tv, 320, BODY_H);
  lv_obj_set_style_bg_color(tv, COL_BG, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  // No paging animation. The tileview is the scroller, so these go on
  // it, not on the tiles. anim_time 0 makes the snap-to-page instant;
  // elastic is the rubber-band overshoot at the ends and momentum the
  // fling glide - both read as animation once the snap is instant.
  lv_obj_set_style_anim_time(tv, 0, 0);
  lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  lv_obj_add_event_cb(tv, tile_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

  *tv_out = tv;
  return scr;
}

void dashboard_create() {
  ent_n = transport_entity_count();
  if (ent_n > MAX_ENTITIES) ent_n = MAX_ENTITIES;
  auto_n = transport_automation_count();
  if (auto_n > MAX_AUTOMATIONS) auto_n = MAX_AUTOMATIONS;

  create_header();

  scr_devices = make_screen(&tv_devices);
  scr_autos = make_screen(&tv_autos);

  lv_obj_t *ov = lv_tileview_add_tile(tv_devices, 0, 0, LV_DIR_HOR);
  lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
  create_overview(ov);

  for (uint8_t i = 0; i < ent_n; i++) {
    lv_obj_t *tile = lv_tileview_add_tile(tv_devices, i + 1, 0, LV_DIR_HOR);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    create_entity_page(tile, i);
  }

  create_automation_pages();

  lv_obj_add_event_cb(scr_devices, gesture_cb, LV_EVENT_GESTURE, scr_devices);
  lv_obj_add_event_cb(scr_autos, gesture_cb, LV_EVENT_GESTURE, scr_autos);

  cur_screen = scr_devices;
  lv_scr_load(scr_devices);

  for (uint8_t i = 0; i < ent_n; i++) refresh_entity(i);
  refresh_status();
  update_title();
}

// ------------------------------------------------------------
//  State in
// ------------------------------------------------------------
void dashboard_apply(const PanelEvent &e) {
  switch (e.type) {
    case EventType::Link:
      link = (LinkState)e.value;
      break;

    case EventType::DevicesAvailable:
      devices_ok = e.value != 0;
      break;

    case EventType::Power:
      if (e.entity < ent_n) {
        power[e.entity] = e.value != 0;
        known[e.entity] = true;
        refresh_entity(e.entity);
      }
      break;

    case EventType::Brightness:
      if (e.entity < ent_n && ui[e.entity].slider) {
        bright[e.entity] = (uint8_t)e.value;
        // Don't yank the slider out from under a finger.
        if (!lv_obj_has_state(ui[e.entity].slider, LV_STATE_PRESSED)) {
          int32_t v = LV_MAX(lv_slider_get_min_value(ui[e.entity].slider),
                             LV_MIN(lv_slider_get_max_value(ui[e.entity].slider),
                                    e.value));
          lv_slider_set_value(ui[e.entity].slider, v, LV_ANIM_OFF);
          lv_label_set_text_fmt(ui[e.entity].pct, "%d%%", pct_of((uint8_t)v));
        }
      }
      break;

    case EventType::Color:
      if (e.entity < ent_n && ui[e.entity].wheel) {
        color[e.entity] = e.value;
        if (!lv_obj_has_state(ui[e.entity].wheel, LV_STATE_PRESSED)) {
          lv_color_t c = lv_color_make(rgb_r(e.value), rgb_g(e.value),
                                       rgb_b(e.value));
          lv_colorwheel_set_rgb(ui[e.entity].wheel, c);
          lv_obj_set_style_bg_color(ui[e.entity].swatch, c, 0);
        }
      }
      break;

    case EventType::Temperature:
      last_temp = e.fvalue;
      have_temp = true;
      break;

    case EventType::Humidity:
      last_hum = e.fvalue;
      have_hum = true;
      break;

    case EventType::AutomationRan:
      if (e.entity < auto_n && auto_btn[e.entity]) {
        lv_obj_add_state(auto_btn[e.entity], LV_STATE_CHECKED);
        lv_timer_t *t = lv_timer_create(unflash_cb, 500, auto_btn[e.entity]);
        lv_timer_set_repeat_count(t, 1);
      }
      break;
  }
  refresh_status();
}
