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

/* Memory: no PSRAM on this board, keep LVGL's heap modest. */
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (40U * 1024U)

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
