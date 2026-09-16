#include "dashboard.h"

#include <lvgl.h>
#include <stdio.h>

#include "../transport.h"

// ------------------------------------------------------------
//  Layout: 320x240 landscape.
//
//  Devices screen pages are *rooms*: page 0 is Home (favourites),
//  then one page per area. Automations are a second screen, global,
//  reached by a vertical swipe.
//
//  A fixed header lives on lv_layer_top(); so does the device detail
//  panel, which therefore covers the header and swallows gestures
//  while it is open.
//
//  Pages cannot scroll - vertical is spent on the screen switch - so
//  a page holds at most MAX_PER_PAGE tiles in a 2-column grid.
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

// One rendered tile. A favourite has two of these - one on Home, one
// on its room page - so refreshes walk the list rather than indexing
// by entity.
struct TileUI {
  uint8_t entity;
  lv_obj_t *tile;
  lv_obj_t *state;
  lv_obj_t *bar;  // mini brightness bar, NULL for Toggle
};

static TileUI tiles[MAX_TILES];
static uint8_t tile_n;

static lv_obj_t *auto_btn[MAX_AUTOMATIONS];

static bool power[MAX_ENTITIES];
static uint8_t bright[MAX_ENTITIES];
static int32_t color[MAX_ENTITIES];
static bool known[MAX_ENTITIES];

static float area_temp[MAX_AREAS], area_hum[MAX_AREAS];
static bool area_temp_ok[MAX_AREAS], area_hum_ok[MAX_AREAS];

static uint8_t ent_n, auto_n, area_n;

// Page composition. Page 0 = Home (favourites), page k = area k-1.
static uint8_t page_ent[MAX_PAGES][MAX_PER_PAGE];
static uint8_t page_ent_n[MAX_PAGES];

static lv_obj_t *scr_devices;
static lv_obj_t *scr_autos;
static lv_obj_t *cur_screen;
static lv_obj_t *tv_devices;
static lv_obj_t *tv_autos;

static uint8_t dev_page, dev_pages;
static uint8_t auto_page, auto_pages;
static bool input_captured;

static lv_obj_t *title_label;
static lv_obj_t *sensor_label;
static lv_obj_t *status_dot;

// Single reused detail panel - one colour wheel for the whole UI
// rather than one per RGB device.
static lv_obj_t *dp_panel;
static lv_obj_t *dp_title;
static lv_obj_t *dp_btn, *dp_btn_label;
static lv_obj_t *dp_wheel, *dp_slider, *dp_pct, *dp_cap;
static uint8_t dp_entity;
static bool dp_open;

static LinkState link = LinkState::NoWifi;
static bool devices_ok = false;

static bool controls_enabled() {
  return link == LinkState::Online && devices_ok;
}

static void refresh_header();

// ------------------------------------------------------------
//  Helpers
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

static bool has_extras(const EntityDesc *d) {
  return d->kind != EntityKind::Toggle;
}

// ------------------------------------------------------------
//  Detail panel
// ------------------------------------------------------------
static void refresh_detail() {
  if (!dp_open) return;
  const EntityDesc *d = transport_entity(dp_entity);

  lv_label_set_text(dp_title, d->name);
  lv_label_set_text(dp_btn_label,
                    !known[dp_entity] ? "--" : (power[dp_entity] ? "ON" : "OFF"));
  if (known[dp_entity] && power[dp_entity]) lv_obj_add_state(dp_btn, LV_STATE_CHECKED);
  else lv_obj_clear_state(dp_btn, LV_STATE_CHECKED);

  if (!lv_obj_has_state(dp_slider, LV_STATE_PRESSED)) {
    int32_t v = LV_MAX(1, LV_MIN(255, (int32_t)bright[dp_entity]));
    lv_slider_set_value(dp_slider, v, LV_ANIM_OFF);
    lv_label_set_text_fmt(dp_pct, "%d%%", pct_of((uint8_t)v));
  }

  if (d->kind == EntityKind::RGB && !lv_obj_has_state(dp_wheel, LV_STATE_PRESSED)) {
    lv_colorwheel_set_rgb(dp_wheel, lv_color_make(rgb_r(color[dp_entity]),
                                                  rgb_g(color[dp_entity]),
                                                  rgb_b(color[dp_entity])));
  }
}

static void open_detail(uint8_t entity) {
  const EntityDesc *d = transport_entity(entity);
  dp_entity = entity;
  dp_open = true;

  // Two layouts: wheel on the left with a short slider beside it, or
  // no wheel and a full-width slider.
  if (d->kind == EntityKind::RGB) {
    lv_obj_clear_flag(dp_wheel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(dp_cap, 150, 104);
    lv_obj_set_pos(dp_pct, 274, 104);
    lv_obj_set_pos(dp_slider, 150, 128);
    lv_obj_set_width(dp_slider, 156);
  } else {
    lv_obj_add_flag(dp_wheel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(dp_cap, 20, 122);
    lv_obj_set_pos(dp_pct, 264, 122);
    lv_obj_set_pos(dp_slider, 20, 148);
    lv_obj_set_width(dp_slider, 280);
  }

  refresh_detail();
  lv_obj_clear_flag(dp_panel, LV_OBJ_FLAG_HIDDEN);
}

static void close_detail_cb(lv_event_t *e) {
  (void)e;
  dp_open = false;
  lv_obj_add_flag(dp_panel, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------------
//  Event callbacks
// ------------------------------------------------------------
static void toggle_cb(lv_event_t *e) {
  uint8_t i = (uint8_t)(uintptr_t)lv_event_get_user_data(e);
  transport_send_power(i, !power[i]);
}

static void dp_toggle_cb(lv_event_t *e) {
  (void)e;
  transport_send_power(dp_entity, !power[dp_entity]);
}

static void chevron_cb(lv_event_t *e) {
  open_detail((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void dp_slider_cb(lv_event_t *e) {
  int32_t v = lv_slider_get_value(dp_slider);
  lv_label_set_text_fmt(dp_pct, "%d%%", pct_of((uint8_t)v));
  if (lv_event_get_code(e) == LV_EVENT_RELEASED)
    transport_send_brightness(dp_entity, (uint8_t)v);
}

static void dp_wheel_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
  uint32_t v = lv_color_to32(lv_colorwheel_get_rgb(dp_wheel));
  transport_send_color(dp_entity, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static void automation_cb(lv_event_t *e) {
  transport_run_automation((uint8_t)(uintptr_t)lv_event_get_user_data(e));
}

static void unflash_cb(lv_timer_t *t) {
  lv_obj_clear_state((lv_obj_t *)t->user_data, LV_STATE_CHECKED);
  lv_timer_del(t);
}

static void capture_cb(lv_event_t *e) {
  input_captured = (lv_event_get_code(e) == LV_EVENT_PRESSED);
}

static void add_capture(lv_obj_t *o) {
  lv_obj_add_event_cb(o, capture_cb, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(o, capture_cb, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(o, capture_cb, LV_EVENT_PRESS_LOST, NULL);
}

static void goto_page(lv_obj_t *tv, uint8_t page) {
  lv_obj_set_tile_id(tv, page, 0, LV_ANIM_OFF);
  refresh_header();
}

static void gesture_cb(lv_event_t *e) {
  if (input_captured || dp_open) return;

  lv_dir_t d = lv_indev_get_gesture_dir(lv_indev_get_act());
  lv_obj_t *cur = (lv_obj_t *)lv_event_get_user_data(e);

  if (cur == scr_devices) {
    if (d == LV_DIR_LEFT && dev_page + 1 < dev_pages) {
      goto_page(tv_devices, ++dev_page);
    } else if (d == LV_DIR_RIGHT && dev_page > 0) {
      goto_page(tv_devices, --dev_page);
    } else if (d == LV_DIR_TOP && auto_n) {
      cur_screen = scr_autos;
      lv_scr_load(scr_autos);
      refresh_header();
    }
  } else if (cur == scr_autos) {
    if (d == LV_DIR_LEFT && auto_page + 1 < auto_pages) {
      goto_page(tv_autos, ++auto_page);
    } else if (d == LV_DIR_RIGHT && auto_page > 0) {
      goto_page(tv_autos, --auto_page);
    } else if (d == LV_DIR_BOTTOM) {
      cur_screen = scr_devices;
      lv_scr_load(scr_devices);
      refresh_header();
    }
  }
}

// ------------------------------------------------------------
//  Refresh
// ------------------------------------------------------------
static void refresh_entity(uint8_t i) {
  const char *state = !known[i] ? "--" : (power[i] ? "ON" : "OFF");

  for (uint8_t t = 0; t < tile_n; t++) {
    if (tiles[t].entity != i) continue;
    lv_label_set_text(tiles[t].state, state);
    if (known[i] && power[i]) lv_obj_add_state(tiles[t].tile, LV_STATE_CHECKED);
    else lv_obj_clear_state(tiles[t].tile, LV_STATE_CHECKED);
    if (tiles[t].bar) lv_bar_set_value(tiles[t].bar, bright[i], LV_ANIM_OFF);
  }

  if (dp_open && dp_entity == i) refresh_detail();
}

static void refresh_header() {
  const char *t;
  if (cur_screen == scr_autos) t = "Automations";
  else if (dev_page == 0) t = "Home";
  else t = transport_area(dev_page - 1);
  lv_label_set_text(title_label, t);

  bool en = controls_enabled();
  for (uint8_t i = 0; i < tile_n; i++) set_enabled(tiles[i].tile, en);
  for (uint8_t i = 0; i < auto_n; i++) set_enabled(auto_btn[i], en);
  set_enabled(dp_btn, en);
  set_enabled(dp_slider, en);
  set_enabled(dp_wheel, en);

  lv_color_t col;
  switch (link) {
    case LinkState::NoWifi:
    case LinkState::NoBroker: col = COL_BAD; break;
    default: col = devices_ok ? COL_OK : COL_WARN; break;
  }
  lv_obj_set_style_bg_color(status_dot, col, 0);

  if (!en) {
    const char *s = link == LinkState::NoWifi   ? "No WiFi"
                  : link == LinkState::NoBroker ? "No link"
                                                : "Devices off";
    lv_label_set_text(sensor_label, s);
    lv_obj_set_style_text_color(sensor_label, col, 0);
  } else {
    // Room pages show that room's sensors; Home and Automations show
    // the average across every room that has reported.
    float t_sum = 0, h_sum = 0;
    uint8_t t_cnt = 0, h_cnt = 0;
    if (cur_screen == scr_devices && dev_page > 0) {
      uint8_t a = dev_page - 1;
      if (area_temp_ok[a]) { t_sum = area_temp[a]; t_cnt = 1; }
      if (area_hum_ok[a])  { h_sum = area_hum[a];  h_cnt = 1; }
    } else {
      for (uint8_t a = 0; a < area_n; a++) {
        if (area_temp_ok[a]) { t_sum += area_temp[a]; t_cnt++; }
        if (area_hum_ok[a])  { h_sum += area_hum[a];  h_cnt++; }
      }
    }
    char buf[24];
    if (t_cnt && h_cnt)
      snprintf(buf, sizeof(buf), "%.1f\xC2\xB0" "C  %.0f%%",
               t_sum / t_cnt, h_sum / h_cnt);
    else if (t_cnt)
      snprintf(buf, sizeof(buf), "%.1f\xC2\xB0" "C", t_sum / t_cnt);
    else
      buf[0] = '\0';
    lv_label_set_text(sensor_label, buf);
    lv_obj_set_style_text_color(sensor_label, COL_MUTED, 0);
  }
  lv_obj_align_to(sensor_label, status_dot, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

// ------------------------------------------------------------
//  Construction
// ------------------------------------------------------------
static void create_header() {
  // lv_layer_top() is a screen - LVGL forces it to full display size
  // every layout pass, so it must stay transparent and the bar is a
  // child of it.
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

  title_label = make_label(bar, "Home", &lv_font_montserrat_20, COL_TEXT);
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

static void create_detail() {
  lv_obj_t *p = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(p);
  lv_obj_set_size(p, 320, 240);
  lv_obj_set_pos(p, 0, 0);
  lv_obj_set_style_bg_color(p, COL_BG, 0);
  lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
  lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);  // swallow taps beneath
  lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
  dp_panel = p;

  dp_title = make_label(p, "", &lv_font_montserrat_20, COL_TEXT);
  lv_obj_set_pos(dp_title, 14, 8);

  lv_obj_t *close = lv_btn_create(p);
  style_card(close);
  lv_obj_set_size(close, 44, 34);
  lv_obj_set_pos(close, 320 - 44 - GAP, 6);
  lv_obj_add_event_cb(close, close_detail_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *cl = lv_label_create(close);
  lv_label_set_text(cl, LV_SYMBOL_CLOSE);
  lv_obj_center(cl);

  dp_btn = lv_btn_create(p);
  style_card(dp_btn);
  lv_obj_set_size(dp_btn, 92, 38);
  lv_obj_set_pos(dp_btn, 170, 50);
  lv_obj_set_style_bg_color(dp_btn, COL_ON, LV_STATE_CHECKED);
  lv_obj_set_style_text_color(dp_btn, COL_TEXT, 0);
  lv_obj_set_style_text_color(dp_btn, lv_color_black(), LV_STATE_CHECKED);
  lv_obj_add_event_cb(dp_btn, dp_toggle_cb, LV_EVENT_CLICKED, NULL);
  dp_btn_label = lv_label_create(dp_btn);
  lv_obj_set_style_text_font(dp_btn_label, &lv_font_montserrat_20, 0);
  lv_obj_center(dp_btn_label);

  dp_wheel = lv_colorwheel_create(p, true);
  lv_obj_set_size(dp_wheel, 104, 104);
  lv_obj_set_pos(dp_wheel, 16, 52);
  lv_obj_add_event_cb(dp_wheel, dp_wheel_cb, LV_EVENT_RELEASED, NULL);
  add_capture(dp_wheel);

  dp_cap = make_label(p, "Brightness", &lv_font_montserrat_14, COL_MUTED);
  dp_pct = make_label(p, "--", &lv_font_montserrat_14, COL_TEXT);

  dp_slider = lv_slider_create(p);
  lv_slider_set_range(dp_slider, 1, 255);
  lv_obj_set_height(dp_slider, 16);
  lv_obj_set_style_bg_color(dp_slider, COL_ON, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(dp_slider, COL_ON, LV_PART_KNOB);
  lv_obj_set_style_opa(dp_slider, LV_OPA_40, LV_STATE_DISABLED);
  lv_obj_set_ext_click_area(dp_slider, 14);
  lv_obj_add_event_cb(dp_slider, dp_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(dp_slider, dp_slider_cb, LV_EVENT_RELEASED, NULL);
  add_capture(dp_slider);
}

static void create_device_tile(lv_obj_t *parent, uint8_t i,
                               lv_coord_t x, lv_coord_t y,
                               lv_coord_t w, lv_coord_t h) {
  const EntityDesc *d = transport_entity(i);
  bool roomy = h >= 80;

  lv_obj_t *t = lv_btn_create(parent);
  style_card(t);
  lv_obj_set_size(t, w, h);
  lv_obj_set_pos(t, x, y);
  lv_obj_set_style_bg_color(t, COL_ON, LV_STATE_CHECKED);
  lv_obj_set_style_text_color(t, COL_TEXT, 0);
  lv_obj_set_style_text_color(t, lv_color_black(), LV_STATE_CHECKED);
  lv_obj_add_event_cb(t, toggle_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);

  lv_obj_t *name = lv_label_create(t);
  lv_label_set_text(name, d->name);
  lv_obj_set_width(name, w - (has_extras(d) ? 54 : 22));
  lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t *st = lv_label_create(t);
  lv_obj_set_style_text_font(st, roomy ? &lv_font_montserrat_20
                                       : &lv_font_montserrat_14, 0);
  lv_obj_align(st, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  lv_obj_t *bar = NULL;
  if (has_extras(d)) {
    // Chevron opens the detail panel. A click here does not reach the
    // tile's own CLICKED handler: LVGL only bubbles events when
    // LV_OBJ_FLAG_EVENT_BUBBLE is set, and it is not.
    lv_obj_t *chev = lv_btn_create(t);
    lv_obj_set_size(chev, 30, 30);
    lv_obj_set_style_radius(chev, 6, 0);
    lv_obj_set_style_shadow_width(chev, 0, 0);
    lv_obj_set_style_border_width(chev, 0, 0);
    lv_obj_set_style_bg_color(chev, COL_BG, 0);
    lv_obj_set_style_bg_opa(chev, LV_OPA_50, 0);
    lv_obj_align(chev, LV_ALIGN_TOP_RIGHT, 6, -6);
    lv_obj_add_event_cb(chev, chevron_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    lv_obj_t *cv = lv_label_create(chev);
    lv_label_set_text(cv, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(cv, COL_TEXT, 0);
    lv_obj_center(cv);

    bar = lv_bar_create(t);
    lv_bar_set_range(bar, 0, 255);
    lv_obj_set_size(bar, w - 90, 6);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_RIGHT, 0, -6);
    lv_obj_set_style_bg_color(bar, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_TEXT, LV_PART_INDICATOR);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  }

  tiles[tile_n].entity = i;
  tiles[tile_n].tile = t;
  tiles[tile_n].state = st;
  tiles[tile_n].bar = bar;
  tile_n++;
}

// Grid geometry for n tiles: always 2 columns, 2 or 3 rows.
static void grid_for(uint8_t n, lv_coord_t *w, lv_coord_t *h, uint8_t *rows) {
  uint8_t r = (n + 1) / 2;
  if (r < 2) r = 2;
  if (r > 3) r = 3;
  *rows = r;
  *w = (320 - 3 * GAP) / 2;
  *h = (BODY_H - (r + 1) * GAP) / r;
}

static void build_pages() {
  // Page 0: favourites. Pages 1..area_n: one per area.
  for (uint8_t i = 0; i < ent_n; i++) {
    const EntityDesc *d = transport_entity(i);
    if (d->favourite && page_ent_n[0] < MAX_PER_PAGE)
      page_ent[0][page_ent_n[0]++] = i;
    uint8_t p = d->area + 1;
    if (p < MAX_PAGES && page_ent_n[p] < MAX_PER_PAGE)
      page_ent[p][page_ent_n[p]++] = i;
  }
  dev_pages = 1 + area_n;
}

static void create_device_page(lv_obj_t *tile, uint8_t page) {
  uint8_t n = page_ent_n[page];
  if (!n) {
    lv_obj_t *l = make_label(tile, "Nothing here", &lv_font_montserrat_14, COL_MUTED);
    lv_obj_center(l);
    return;
  }
  lv_coord_t w, h;
  uint8_t rows;
  grid_for(n, &w, &h, &rows);
  for (uint8_t k = 0; k < n; k++) {
    create_device_tile(tile, page_ent[page][k],
                       GAP + (k % 2) * (w + GAP),
                       GAP + (k / 2) * (h + GAP), w, h);
  }
}

static void create_automation_pages() {
  lv_coord_t w = (320 - 3 * GAP) / 2;
  lv_coord_t h = (BODY_H - 3 * GAP) / 2;
  auto_pages = (auto_n + 3) / 4;

  for (uint8_t p = 0; p < auto_pages; p++) {
    lv_obj_t *tile = lv_tileview_add_tile(tv_autos, p, 0, LV_DIR_NONE);
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

  // Tiles are created LV_DIR_NONE so this never scrolls; paging is by
  // gesture and jumps instantly.
  lv_obj_set_style_anim_time(tv, 0, 0);
  lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLL_MOMENTUM);

  *tv_out = tv;
  return scr;
}

void dashboard_create() {
  ent_n = transport_entity_count();
  if (ent_n > MAX_ENTITIES) ent_n = MAX_ENTITIES;
  auto_n = transport_automation_count();
  if (auto_n > MAX_AUTOMATIONS) auto_n = MAX_AUTOMATIONS;
  area_n = transport_area_count();
  if (area_n > MAX_AREAS) area_n = MAX_AREAS;

  build_pages();
  create_header();

  scr_devices = make_screen(&tv_devices);
  scr_autos = make_screen(&tv_autos);

  for (uint8_t p = 0; p < dev_pages; p++) {
    lv_obj_t *tile = lv_tileview_add_tile(tv_devices, p, 0, LV_DIR_NONE);
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
    create_device_page(tile, p);
  }

  create_automation_pages();
  create_detail();  // after the header, so it covers it when shown

  lv_obj_add_event_cb(scr_devices, gesture_cb, LV_EVENT_GESTURE, scr_devices);
  lv_obj_add_event_cb(scr_autos, gesture_cb, LV_EVENT_GESTURE, scr_autos);

  dev_page = 0;
  auto_page = 0;
  cur_screen = scr_devices;
  lv_scr_load(scr_devices);

  for (uint8_t i = 0; i < ent_n; i++) refresh_entity(i);
  refresh_header();
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
      if (e.entity < ent_n) {
        bright[e.entity] = (uint8_t)LV_MAX(0, LV_MIN(255, e.value));
        refresh_entity(e.entity);
      }
      break;

    case EventType::Color:
      if (e.entity < ent_n) {
        color[e.entity] = e.value;
        refresh_entity(e.entity);
      }
      break;

    case EventType::Temperature:
      if (e.entity < area_n) {
        area_temp[e.entity] = e.fvalue;
        area_temp_ok[e.entity] = true;
      }
      break;

    case EventType::Humidity:
      if (e.entity < area_n) {
        area_hum[e.entity] = e.fvalue;
        area_hum_ok[e.entity] = true;
      }
      break;

    case EventType::AutomationRan:
      if (e.entity < auto_n && auto_btn[e.entity]) {
        lv_obj_add_state(auto_btn[e.entity], LV_STATE_CHECKED);
        lv_timer_t *t = lv_timer_create(unflash_cb, 500, auto_btn[e.entity]);
        lv_timer_set_repeat_count(t, 1);
      }
      break;
  }
  refresh_header();
}
