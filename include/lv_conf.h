/*
 * LVGL 8.3 configuration for HomeMesh Panel.
 *
 * Only settings that differ from LVGL's defaults are listed here;
 * lv_conf_internal.h fills in everything else.
 */

#if 1

#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Colour: RGB565. Byte swapping is done by TFT_eSPI in the flush
 * callback (pushColors(..., true)), so LVGL must not swap too. */
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

/* Memory: no PSRAM on this board. Every widget comes out of this pool,
 * and so do the temporary buffers drawing takes (radius masks,
 * animations), so the peak is higher than what the built widgets alone
 * use. main.cpp logs the real peak (max_used) - size this from that.
 *
 * Taken from the heap once at boot (LV_MEM_POOL_ALLOC), not .bss. It
 * was a 40 KB static array until 22 Oct: env:ha's static DRAM sits
 * within ~1.5 KB of the linker's limit (dram0_0_seg), so it could not
 * grow there, and a 10-device layout ran it out (an assert in
 * lv_anim.c at boot - a restart loop on the board). On the heap the
 * pool can be 64 KB for a net cost of 24 KB: the 40 KB the array used
 * returns to the heap, which had ~160 KB free. The designer's
 * simulator builds against this file, so its pool matches. */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (64U * 1024U)
#define LV_MEM_POOL_INCLUDE <stdlib.h>
#define LV_MEM_POOL_ALLOC malloc

/* A failed LVGL assert - most often an allocation from the pool above
 * - used to run LVGL's default handler, while(1). That freezes the
 * screen mid-redraw, half the new page drawn over the old one, with
 * nothing on serial: the network task on the other core keeps logging
 * as if all were well. lv_assert_hit() in main.cpp names the LVGL file
 * and line, reports the pool, and restarts. */
#ifdef __cplusplus
extern "C" {
#endif
void lv_assert_hit(const char *file, int line);
#ifdef __cplusplus
}
#endif
#define LV_ASSERT_HANDLER lv_assert_hit(__FILE__, __LINE__);

/* Tick: taken from millis(), so no lv_tick_inc() timer is needed. */
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_DISP_DEF_REFR_PERIOD 30
#define LV_INDEV_DEF_READ_PERIOD 20

#define LV_USE_LOG 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

/* Fonts */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* LV_CONF_H */

#endif /* Enable content */
