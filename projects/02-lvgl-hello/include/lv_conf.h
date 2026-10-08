// Minimal LVGL 9.5 config. Anything not set here takes its default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (128 * 1024U)

#define LV_DEF_REFR_PERIOD 16
#define LV_USE_OS LV_OS_NONE

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1 // FPS/CPU overlay, bottom right. Set 0 when done tuning.

#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_BUILD_EXAMPLES 0

#endif
