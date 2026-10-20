// Entry points for the browser simulator: the panel's real UI code
// (ui/dashboard.cpp, LVGL 8.3, the icon fonts, layout_store.cpp) compiled
// to WebAssembly against the firmware's own lv_conf.h.
//
// What is the same as the board: every line of UI and layout code, the
// 16-bit RGB565 colour, the 40 KB LVGL pool, the fonts, the 320x24 draw
// buffer. What is not: the display is a frame buffer JavaScript paints
// onto a canvas, touch is the mouse, and the network is JavaScript.
//
// Lifecycle mirrors the board. A layout is applied by storing it and
// *restarting* - JavaScript throws the module instance away and starts a
// fresh one - exactly as the firmware restarts after a push.
#include <emscripten.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "../src/layout_store.h"
#include "../src/transport.h"
#include "../src/ui/dashboard.h"

void sim_push_event(const PanelEvent &e);

static const int W = 320, H = 240;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf_mem[W * 24];   // the firmware's BUF_PIXELS
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static uint16_t fb[W * H];
static bool dirty;
static int ptr_x, ptr_y;
static bool ptr_down;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  int w = a->x2 - a->x1 + 1;
  for (int y = a->y1; y <= a->y2; y++) {
    memcpy(&fb[y * W + a->x1], &px[(y - a->y1) * w], w * sizeof(uint16_t));
  }
  dirty = true;
  lv_disp_flush_ready(drv);
}

static void read_cb(lv_indev_drv_t *, lv_indev_data_t *d) {
  d->point.x = ptr_x;
  d->point.y = ptr_y;
  d->state = ptr_down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

extern "C" {

// As on the board: an LVGL assert names itself instead of hanging.
void lv_assert_hit(const char *file, int line) {
  printf("[lvgl] ASSERT FAILED at %s:%d\n", file, line);
  fflush(stdout);
  emscripten_force_exit(1);
}

// Store a layout the way a push does - validated by the firmware's own
// layout_store_json(). Returns 1, or 0 with the reason written to err.
EMSCRIPTEN_KEEPALIVE int sim_store_layout(const char *json, char *err, int err_n) {
  return layout_store_json(json, strlen(json), err, err_n) ? 1 : 0;
}

// The board's setup(): display, touch, transport, UI.
EMSCRIPTEN_KEEPALIVE void sim_boot() {
  printf("HomeMesh Panel starting (simulator)\n");
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf_mem, NULL, W * 24);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = W;
  disp_drv.ver_res = H;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = read_cb;
  lv_indev_drv_register(&indev_drv);

  transport_start();
  dashboard_create();
  lv_timer_handler();
  fflush(stdout);
}

// One pass of the board's loop(). Returns 1 if the frame buffer changed.
EMSCRIPTEN_KEEPALIVE int sim_step() {
  PanelEvent e;
  while (transport_next_event(&e)) dashboard_apply(e);
  lv_timer_handler();
  int d = dirty;
  dirty = false;
  return d;
}

EMSCRIPTEN_KEEPALIVE uint16_t *sim_framebuffer() { return fb; }

EMSCRIPTEN_KEEPALIVE void sim_pointer(int x, int y, int down) {
  ptr_x = x;
  ptr_y = y;
  ptr_down = down != 0;
}

// A state event, as the network task would post it.
EMSCRIPTEN_KEEPALIVE void sim_event(int type, int entity, int value, float fvalue) {
  PanelEvent e;
  e.type = (EventType)type;
  e.entity = (uint8_t)entity;
  e.value = value;
  e.fvalue = fvalue;
  sim_push_event(e);
}

EMSCRIPTEN_KEEPALIVE int sim_layout_from_nvs() { return g_layout.from_nvs ? 1 : 0; }

// How a tile would show `text` in a label `width` px wide: one line of
// the default font, LONG_DOT - exactly as create_device_tile() sets up
// a name. Returns LVGL's own result, dots and all, so the designer can
// show what the panel will draw without re-implementing LVGL's wrapping,
// per-glyph rounding and kerning. The probe lives hidden on the system
// layer and never draws.
EMSCRIPTEN_KEEPALIVE const char *sim_fit_label(const char *text, int width) {
  static lv_obj_t *probe;
  if (!probe) {
    probe = lv_label_create(lv_layer_sys());
    lv_obj_add_flag(probe, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_long_mode(probe, LV_LABEL_LONG_DOT);
  }
  lv_obj_set_size(probe, width, lv_font_get_line_height(lv_obj_get_style_text_font(probe, LV_PART_MAIN)));
  lv_obj_update_layout(probe);
  lv_label_set_text(probe, text);
  return lv_label_get_text(probe);
}

EMSCRIPTEN_KEEPALIVE int sim_pool_free() {
  lv_mem_monitor_t m;
  lv_mem_monitor(&m);
  return (int)m.free_size;
}

}  // extern "C"
