#pragma once
// Just enough "Arduino" for the browser simulator: what lv_conf.h's tick
// and layout_store.cpp actually use, and nothing else. Not a port.
//
// Included from C (LVGL, through LV_TICK_CUSTOM_INCLUDE) as well as C++,
// so everything outside the __cplusplus block has to be plain C.
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <emscripten.h>

static inline uint32_t millis(void) { return (uint32_t)emscripten_get_now(); }
static inline void delay(uint32_t ms) { (void)ms; }

#ifdef __cplusplus
// Serial goes to stdout, which the simulator's JavaScript collects as the
// panel's serial log - so a rejected layout says why, as on the board.
struct SimSerial {
  int printf(const char *fmt, ...) __attribute__((format(printf, 2, 3))) {
    va_list ap;
    va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
    return n;
  }
  void println(const char *s) { puts(s); }
  void print(const char *s) { fputs(s, stdout); }
};
extern SimSerial Serial;
#endif
