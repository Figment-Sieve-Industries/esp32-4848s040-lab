#pragma once
#include <lvgl.h>

// Outline icons drawn with LVGL lines/arcs on a 24x24 design grid (same shapes as the
// design canvas), scaled to `size` px. Each icon is a transparent container.
enum class Icon {
    Sun, House, Pylon, Battery, Wifi,
    // weather
    Moon, Cloud, PartlyCloudy, Fog, Rain, Snow, Thunder,
};

lv_obj_t *iconCreate(lv_obj_t *parent, Icon icon, int size, lv_color_t color, float stroke = 2.0f);

// Replace an icon container's drawing (used for the weather icon).
void iconSet(lv_obj_t *container, Icon icon, lv_color_t color, float stroke = 2.0f);

// Battery icon only: set the inner fill level 0..100.
void iconSetBatteryLevel(lv_obj_t *container, float pct);

// WMO weather code (Open-Meteo) -> icon and a short description.
Icon weatherIcon(int code, bool isDay);
const char *weatherText(int code);
