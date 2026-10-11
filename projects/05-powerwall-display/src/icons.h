#pragma once
#include <lvgl.h>

// Outline icons drawn with LVGL lines/arcs on a 24x24 design grid (same shapes as the
// design canvas), scaled to `size` px. Each icon is a transparent container.
enum class Icon {
    Sun, House, Pylon, Battery, Wifi, Drop, ArrowUp, ArrowDown,
    // weather
    Moon, Cloud, PartlyCloudy, Fog, Rain, Snow, Thunder,
};

lv_obj_t *iconCreate(lv_obj_t *parent, Icon icon, int size, lv_color_t color, float stroke = 2.0f);

// Replace an icon container's drawing (used for the weather icon).
void iconSet(lv_obj_t *container, Icon icon, lv_color_t color, float stroke = 2.0f);

// Gentle looping motion for a weather icon: clouds drift, rain/snow falls, sun breathes.
// Call after iconCreate/iconSet (iconSet restarts it).
void iconAnimate(lv_obj_t *container, Icon icon);

// WMO weather code (Open-Meteo) -> icon and a short description.
Icon weatherIcon(int code, bool isDay);
const char *weatherText(int code);
