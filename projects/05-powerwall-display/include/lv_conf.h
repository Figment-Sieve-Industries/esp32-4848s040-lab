// LVGL 9.5 config for the Powerwall display. Unset options use lv_conf_internal.h defaults.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// C heap: big LVGL allocations go to PSRAM, leaving internal RAM for Wi-Fi/TLS
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB

#define LV_DEF_REFR_PERIOD 16
#define LV_USE_OS LV_OS_NONE // LVGL only runs in loop(); the network task never calls LVGL

// No LV_ATTRIBUTE_FAST_MEM here: the Arduino core's mbedTLS allocates only from internal RAM
// (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC) and needs ~40-50 KB per TLS session. IRAM functions cost ~55 KB.

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_SYSMON 0

// Built-in Montserrat: ASCII + degree sign + LVGL symbols (no middle dot)
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_26 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_30 1
#define LV_FONT_DEFAULT &lv_font_montserrat_16

#define LV_BUILD_EXAMPLES 0

#endif
