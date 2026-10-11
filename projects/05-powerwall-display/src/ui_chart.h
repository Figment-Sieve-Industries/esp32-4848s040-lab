#pragma once
// BarChart: one LVGL object that draws a strip of bars itself (LV_EVENT_DRAW_MAIN), replacing rows of
// lv_obj bars. Bars are indexed left to right; changing one bar invalidates only that bar's area.
// Implemented in ui_chart.cpp.
#include <lvgl.h>

namespace ui {

// count bars, each barW wide, `step` px apart (left edges), with the given corner radius. The chart
// object is inert (not clickable/scrollable) and sized w x h at (x, y) inside parent.
lv_obj_t *barChartCreate(lv_obj_t *parent, int x, int y, int w, int h, int count, int barW, int step, int radius);
// Bar i spans [top, top + height) in chart coordinates; height <= 0 hides it. Change-only.
void barChartSet(lv_obj_t *chart, int i, int top, int height, lv_color_t color, lv_opa_t opa = LV_OPA_COVER);
// Bottom-aligned bars from the right: the newest n values occupy the last n slots (oldest first in
// heights/colors); slots before them are hidden. heights in px from the chart bottom.
void barChartSetNewest(lv_obj_t *chart, int n, const int16_t *heights, const lv_color_t *colors);
// Grow animation for all bars from the baseline (bottom): ms per bar, first delay, stagger per bar.
// The anim var is the chart, so ui::pauseScreenAnimations covers it.
void barChartGrow(lv_obj_t *chart, uint32_t ms, uint32_t delay0, uint32_t stagger);
int barChartCount(lv_obj_t *chart);

} // namespace ui
