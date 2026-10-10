// LVGL 9.5 config for 04-lvgl-fb2-bench (copied from 03-lvgl-demos, keep comparable). Anything not set here takes its default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Use the C heap: with PSRAM malloc enabled, big LVGL allocations land in PSRAM,
// so the demos don't need a large static pool in internal RAM.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16
// Experiment switches (set via build flags):
//   B4848_LV_DUAL_CORE: render with 2 LVGL draw threads (FreeRTOS) to use both S3 cores
//   B4848_LV_FAST_MEM:  put LVGL's hot functions in IRAM
#ifdef B4848_LV_DUAL_CORE
#define LV_USE_OS LV_OS_FREERTOS
#define LV_DRAW_SW_DRAW_UNIT_CNT 2
#else
#define LV_USE_OS LV_OS_NONE
#endif

#if defined(B4848_LV_FAST_MEM) && !defined(__ASSEMBLY__)
#include "esp_attr.h"
#define LV_ATTRIBUTE_FAST_MEM IRAM_ATTR
#endif

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_SYSMON 1
#define LV_USE_PERF_MONITOR 1

// Fonts used by the demos
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_DEFAULT &lv_font_montserrat_20

#define LV_BUILD_EXAMPLES 0

#define LV_BUILD_DEMOS 1
#define LV_USE_DEMO_WIDGETS 1
#define LV_USE_DEMO_BENCHMARK 1
#define LV_USE_DEMO_STRESS 1
#define LV_USE_DEMO_MUSIC 1
#define LV_DEMO_MUSIC_SQUARE 1    // layout for 480x480
#define LV_DEMO_MUSIC_AUTO_PLAY 1 // runs through tracks by itself

#endif
