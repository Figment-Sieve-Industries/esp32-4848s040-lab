#include "ui_chart.h"
#include "ui_common.h"
#include <src/draw/lv_draw_private.h> // layer->_clip_area
#include <src/misc/lv_area_private.h> // lv_area_intersect, lv_area_is_on

// BarChart: a single lv_obj that paints its own bars in LV_EVENT_DRAW_MAIN. Compared with one lv_obj
// per bar this saves the per-object memory in the LVGL (PSRAM) pool and the tree walk on every render,
// and a changed bar invalidates only its own few pixels.

namespace ui {

namespace {

constexpr uint8_t GROW_FULL = 255; // gp value for a fully grown bar

struct Bar {
    int16_t top;     // chart coordinates
    int16_t height;  // <= 0: hidden
    lv_color_t color;
    lv_opa_t opa;
    uint8_t gp;      // grow progress 0..255 (255 outside a grow animation)
};

struct Chart {
    int count, barW, step, radius;
    int h;                  // chart height as created (coords aren't valid until LVGL's first layout pass)
    int32_t growT;          // ms into the grow timeline
    uint32_t growMs, growStagger;
    bool growing;
    Bar *bars;              // count entries, allocated together with this struct
};

Chart *stateOf(lv_obj_t *c) { return c ? static_cast<Chart *>(lv_obj_get_user_data(c)) : nullptr; }

// Area covered by bar i when drawn at full height (x part) and the given top/height, in absolute coordinates.
void barArea(const lv_obj_t *c, const Chart *s, int i, int top, int height, lv_area_t *a) {
    lv_area_t oc;
    lv_obj_get_coords(c, &oc);
    a->x1 = oc.x1 + i * s->step;
    a->x2 = a->x1 + s->barW - 1;
    a->y1 = oc.y1 + top;
    a->y2 = a->y1 + height - 1;
}

// Height shown for a bar while it grows: ease-out from the baseline (bottom edge of the bar).
int shownHeight(const Bar &b) {
    if (b.gp == GROW_FULL) return b.height;
    int inv = 255 - b.gp;                      // quadratic ease-out: 1 - (1 - x)^2
    int p = 255 * 255 - inv * inv;             // 0..65025
    return (b.height * p + 65025 / 2) / 65025;
}

void invalidateBar(lv_obj_t *c, const Chart *s, int i) {
    const Bar &b = s->bars[i];
    lv_area_t a;
    barArea(c, s, i, b.top, b.height > 0 ? b.height : 1, &a);
    lv_obj_invalidate_area(c, &a);
}

void drawCb(lv_event_t *e) {
    lv_obj_t *c = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    const Chart *s = stateOf(c);
    lv_layer_t *layer = lv_event_get_layer(e);
    if (!s || !layer) return;
    lv_area_t oc;
    lv_obj_get_coords(c, &oc);

    lv_draw_fill_dsc_t dsc;
    lv_draw_fill_dsc_init(&dsc);
    dsc.base.obj = c;
    dsc.base.part = LV_PART_MAIN;
    dsc.base.layer = layer;
    dsc.radius = s->radius;

    for (int i = 0; i < s->count; i++) {
        const Bar &b = s->bars[i];
        if (b.height <= 0 || b.opa <= LV_OPA_MIN) continue;
        int h = shownHeight(b);
        if (h <= 0) continue;
        lv_area_t a;
        barArea(c, s, i, b.top + b.height - h, h, &a); // grows upward from the bar's bottom edge
        lv_area_t vis;
        if (!lv_area_intersect(&vis, &a, &oc)) continue;        // outside the object
        if (!lv_area_is_on(&a, &layer->_clip_area)) continue;   // outside what is being rendered now
        dsc.color = b.color;
        dsc.opa = b.opa;
        lv_draw_fill(layer, &dsc, &a);
    }
}

void deleteCb(lv_event_t *e) {
    lv_obj_t *c = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    lv_anim_delete(c, nullptr);
    void *s = lv_obj_get_user_data(c);
    lv_obj_set_user_data(c, nullptr);
    lv_free(s);
}

void growExec(void *var, int32_t t) {
    lv_obj_t *c = static_cast<lv_obj_t *>(var);
    Chart *s = stateOf(c);
    if (!s) return;
    s->growT = t;
    for (int i = 0; i < s->count; i++) {
        Bar &b = s->bars[i];
        int64_t local = static_cast<int64_t>(t) - static_cast<int64_t>(i) * s->growStagger;
        uint8_t gp;
        if (local <= 0) gp = 0;
        else if (local >= static_cast<int64_t>(s->growMs)) gp = GROW_FULL;
        else gp = static_cast<uint8_t>(local * GROW_FULL / static_cast<int64_t>(s->growMs));
        if (gp == b.gp) continue;
        b.gp = gp;
        if (b.height > 0) invalidateBar(c, s, i);
    }
}

void growDone(lv_anim_t *a) {
    lv_obj_t *c = static_cast<lv_obj_t *>(a->var);
    Chart *s = stateOf(c);
    if (!s) return;
    s->growing = false;
    growExec(c, INT32_MAX); // every bar fully grown
}

} // namespace

lv_obj_t *barChartCreate(lv_obj_t *parent, int x, int y, int w, int h, int count, int barW, int step, int radius) {
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    inert(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    if (count < 1) count = 1;
    auto *s = static_cast<Chart *>(lv_malloc_zeroed(sizeof(Chart) + sizeof(Bar) * static_cast<size_t>(count)));
    if (!s) return c; // out of LVGL heap: an empty, inert object (all setters tolerate it)
    s->count = count;
    s->h = h;
    s->barW = barW;
    s->step = step;
    s->radius = radius;
    s->bars = reinterpret_cast<Bar *>(s + 1);
    for (int i = 0; i < count; i++) s->bars[i].gp = GROW_FULL;
    lv_obj_set_user_data(c, s);
    lv_obj_add_event_cb(c, drawCb, LV_EVENT_DRAW_MAIN, nullptr);
    lv_obj_add_event_cb(c, deleteCb, LV_EVENT_DELETE, nullptr);
    return c;
}

void barChartSet(lv_obj_t *chart, int i, int top, int height, lv_color_t color, lv_opa_t opa) {
    Chart *s = stateOf(chart);
    if (!s || i < 0 || i >= s->count) return;
    Bar &b = s->bars[i];
    if (height <= 0) { // hidden: normalise so the change test works and clear the old pixels
        if (b.height <= 0) return;
        lv_area_t a;
        barArea(chart, s, i, b.top, b.height, &a);
        b.height = 0;
        lv_obj_invalidate_area(chart, &a);
        return;
    }
    if (b.height == height && b.top == top && b.opa == opa && lv_color_eq(b.color, color)) return;
    lv_area_t a;
    int oldTop = b.height > 0 ? b.top : top, oldH = b.height > 0 ? b.height : height; // union old and new
    int y1 = LV_MIN(oldTop, top), y2 = LV_MAX(oldTop + oldH, top + height);
    barArea(chart, s, i, y1, y2 - y1, &a);
    b.top = static_cast<int16_t>(top);
    b.height = static_cast<int16_t>(height);
    b.color = color;
    b.opa = opa;
    lv_obj_invalidate_area(chart, &a);
}

void barChartSetNewest(lv_obj_t *chart, int n, const int16_t *heights, const lv_color_t *colors) {
    Chart *s = stateOf(chart);
    if (!s) return;
    if (n < 0) n = 0;
    int skip = n > s->count ? n - s->count : 0; // more values than slots: keep the newest
    int used = n - skip;
    int first = s->count - used;
    for (int i = 0; i < first; i++) barChartSet(chart, i, 0, 0, lv_color_black()); // hidden: the colour is unused
    if (used <= 0) return;
    if (!heights || !colors) return; // caller bug: the slots stay hidden
    for (int k = 0; k < used; k++) {
        int h = heights[skip + k];
        barChartSet(chart, first + k, s->h - h, h, colors[skip + k]);
    }
}

void barChartGrow(lv_obj_t *chart, uint32_t ms, uint32_t delay0, uint32_t stagger) {
    Chart *s = stateOf(chart);
    if (!s) return;
    delay0 += showDelay(); // main delays entry animations until the screen slide is over
    lv_anim_delete(chart, nullptr); // restart cleanly (also ends a previous grow)
    s->growMs = ms ? ms : 1;
    s->growStagger = stagger;
    s->growing = true;
    growExec(chart, 0); // bars start hidden during the delay
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, chart);
    lv_anim_set_exec_cb(&a, growExec);
    lv_anim_set_values(&a, 0, static_cast<int32_t>(ms + stagger * static_cast<uint32_t>(s->count - 1)));
    lv_anim_set_duration(&a, ms + stagger * static_cast<uint32_t>(s->count - 1));
    lv_anim_set_delay(&a, delay0);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_completed_cb(&a, growDone);
    animTag(&a);
    lv_anim_start(&a);
}

int barChartCount(lv_obj_t *chart) {
    const Chart *s = stateOf(chart);
    return s ? s->count : 0;
}

} // namespace ui
