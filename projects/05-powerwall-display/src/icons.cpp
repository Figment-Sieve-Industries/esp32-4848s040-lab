#include "icons.h"
#include "ui_common.h"
#include <initializer_list>
#include <math.h>

namespace {

struct Ctx {
    lv_obj_t *parent;
    float s;          // px per design unit
    lv_color_t color;
    int stroke;       // px
};

// lv_line keeps a pointer to its points, so each line owns a heap copy freed on delete.
void freePoints(lv_event_t *e) { lv_free(lv_event_get_user_data(e)); }

struct FP {
    float x, y;
};

// Icon parts are never clickable or scrollable, so swipes reach the screen's gesture handler.
using ui::inert;

lv_obj_t *polyline(const Ctx &c, std::initializer_list<FP> pts, float dx = 0, float dy = 0) {
    auto *buf = (lv_point_precise_t *)lv_malloc(sizeof(lv_point_precise_t) * pts.size());
    if (!buf) return nullptr; // out of LVGL heap: skip this stroke (callers tolerate null)
    int i = 0;
    for (auto p : pts) buf[i++] = {(lv_value_precise_t)((p.x + dx) * c.s), (lv_value_precise_t)((p.y + dy) * c.s)};
    lv_obj_t *l = lv_line_create(c.parent);
    inert(l);
    lv_line_set_points(l, buf, pts.size());
    lv_obj_add_event_cb(l, freePoints, LV_EVENT_DELETE, buf);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, c.stroke, 0);
    lv_obj_set_style_line_color(l, c.color, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return l;
}

// Children flagged as "falling" (rain drops, snowflakes) are animated by iconAnimate()
void markFalling(lv_obj_t *o) {
    if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_USER_1);
}

// Arc of radius r around (cx, cy); angles in degrees, 0 = 3 o'clock, clockwise.
void arc(const Ctx &c, float cx, float cy, float r, int a0, int a1) {
    int d = (int)lroundf((2 * r) * c.s + c.stroke);
    lv_obj_t *a = lv_arc_create(c.parent);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, (int)lroundf(cx * c.s - d / 2.0f), (int)lroundf(cy * c.s - d / 2.0f));
    lv_arc_set_bg_angles(a, a0, a1);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    inert(a);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, c.stroke, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, c.color, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);
}

// Circle outline (or filled when fill = true).
lv_obj_t *circle(const Ctx &c, float cx, float cy, float r, bool fill = false) {
    int d = (int)lroundf(2 * r * c.s + (fill ? 0 : c.stroke));
    lv_obj_t *o = lv_obj_create(c.parent);
    lv_obj_remove_style_all(o);
    inert(o);
    lv_obj_set_size(o, d, d);
    lv_obj_set_pos(o, (int)lroundf(cx * c.s - d / 2.0f), (int)lroundf(cy * c.s - d / 2.0f));
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    if (fill) {
        lv_obj_set_style_bg_color(o, c.color, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_color(o, c.color, 0);
        lv_obj_set_style_border_width(o, c.stroke, 0);
    }
    return o;
}

lv_obj_t *rect(const Ctx &c, float x, float y, float w, float h, float r, bool fill) {
    lv_obj_t *o = lv_obj_create(c.parent);
    lv_obj_remove_style_all(o);
    inert(o);
    float pad = fill ? 0 : c.stroke / 2.0f;
    lv_obj_set_pos(o, (int)lroundf(x * c.s - pad), (int)lroundf(y * c.s - pad));
    lv_obj_set_size(o, (int)lroundf(w * c.s + 2 * pad), (int)lroundf(h * c.s + 2 * pad));
    lv_obj_set_style_radius(o, (int)lroundf(r * c.s), 0);
    if (fill) {
        lv_obj_set_style_bg_color(o, c.color, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_color(o, c.color, 0);
        lv_obj_set_style_border_width(o, c.stroke, 0);
    }
    return o;
}

void cloud(const Ctx &c, float dy = 0) {
    polyline(c, {{7, 18.5f}, {17.5f, 18.5f}}, 0, dy);
    arc(c, 17.5f, 15 + dy, 3.5f, 277, 90);   // right lobe
    arc(c, 12.6f, 12.5f + dy, 5.6f, 204, 349); // top bump
    arc(c, 7, 14.25f + dy, 4.25f, 90, 270);   // left lobe
}

void draw(lv_obj_t *cont, Icon icon, lv_color_t color, float stroke) {
    lv_obj_clean(cont);
    float s = lv_obj_get_style_width(cont, LV_PART_MAIN) / 24.0f;
    Ctx c{cont, s, color, (int)lroundf(fmaxf(1.0f, stroke * s))};
    switch (icon) {
    case Icon::Sun:
        circle(c, 12, 12, 4);
        polyline(c, {{12, 2}, {12, 4}});
        polyline(c, {{12, 20}, {12, 22}});
        polyline(c, {{2, 12}, {4, 12}});
        polyline(c, {{20, 12}, {22, 12}});
        polyline(c, {{4.9f, 4.9f}, {6.3f, 6.3f}});
        polyline(c, {{17.7f, 17.7f}, {19.1f, 19.1f}});
        polyline(c, {{4.9f, 19.1f}, {6.3f, 17.7f}});
        polyline(c, {{17.7f, 6.3f}, {19.1f, 4.9f}});
        break;
    case Icon::House:
        polyline(c, {{3, 11.5f}, {12, 4}, {21, 11.5f}});
        polyline(c, {{5.5f, 10}, {5.5f, 20}, {18.5f, 20}, {18.5f, 10}});
        polyline(c, {{10, 20}, {10, 14.5f}, {14, 14.5f}, {14, 20}});
        break;
    case Icon::Pylon:
        polyline(c, {{8, 22}, {12, 3}, {16, 22}});
        polyline(c, {{6, 8}, {18, 8}});
        polyline(c, {{7.2f, 13}, {16.8f, 13}});
        polyline(c, {{9.4f, 18}, {14.6f, 18}});
        break;
    case Icon::Battery: {
        rect(c, 2.5f, 7, 17, 10, 2, false);
        polyline(c, {{22, 10.5f}, {22, 13.5f}});
        rect(c, 5, 9.5f, 12, 5, 1, true);
        break;
    }
    case Icon::Drop:
        polyline(c, {{12, 3}, {7.6f, 13}});
        polyline(c, {{12, 3}, {16.4f, 13}});
        arc(c, 12, 15, 5, 335, 205);
        break;
    case Icon::ArrowUp:
        polyline(c, {{12, 20}, {12, 5}});
        polyline(c, {{6, 11}, {12, 5}, {18, 11}});
        break;
    case Icon::ArrowDown:
        polyline(c, {{12, 4}, {12, 19}});
        polyline(c, {{6, 13}, {12, 19}, {18, 13}});
        break;
    case Icon::Wifi:
        arc(c, 12, 19.8f, 15, 228, 312);
        arc(c, 12, 19.8f, 10, 226, 314);
        arc(c, 12, 19.8f, 5, 226, 314);
        circle(c, 12, 19.5f, 1.2f, true);
        break;
    case Icon::Moon:
        arc(c, 12, 12, 8, 60, 300);
        arc(c, 15, 10, 6, 120, 230);
        break;
    case Icon::Cloud:
        cloud(c);
        break;
    case Icon::PartlyCloudy: {
        Ctx small = c;
        circle(small, 8, 7.5f, 3);
        polyline(small, {{8, 1.5f}, {8, 2.5f}});
        polyline(small, {{2, 7.5f}, {3, 7.5f}});
        polyline(small, {{3.8f, 3.3f}, {4.5f, 4}});
        polyline(small, {{12.2f, 3.3f}, {11.5f, 4}});
        cloud(c, 1.5f);
        break;
    }
    case Icon::Fog:
        polyline(c, {{4, 9}, {20, 9}});
        polyline(c, {{3, 13}, {21, 13}});
        polyline(c, {{5, 17}, {19, 17}});
        break;
    case Icon::Rain:
        cloud(c, -3);
        markFalling(polyline(c, {{9, 18.5f}, {8, 21.5f}}));
        markFalling(polyline(c, {{13, 18.5f}, {12, 21.5f}}));
        markFalling(polyline(c, {{17, 18.5f}, {16, 21.5f}}));
        break;
    case Icon::Snow:
        cloud(c, -3);
        markFalling(circle(c, 8.5f, 20, 0.9f, true));
        markFalling(circle(c, 12.5f, 21.5f, 0.9f, true));
        markFalling(circle(c, 16.5f, 20, 0.9f, true));
        break;
    case Icon::Thunder:
        cloud(c, -3);
        polyline(c, {{13, 16}, {10.5f, 19.5f}, {13.5f, 19.5f}, {11, 23}});
        break;
    }
}

} // namespace

lv_obj_t *iconCreate(lv_obj_t *parent, Icon icon, int size, lv_color_t color, float stroke) {
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, size, size);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_CLICKABLE);
    draw(cont, icon, color, stroke);
    return cont;
}

void iconSet(lv_obj_t *container, Icon icon, lv_color_t color, float stroke) { draw(container, icon, color, stroke); }

namespace {
void driftX(void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }
void breathe(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
// v: 0..100 -> fall 0..3 px (scaled) while fading in then out
void fall(void *o, int32_t v) {
    auto *obj = (lv_obj_t *)o;
    lv_obj_t *icon = lv_obj_get_parent(obj);
    int dist = lv_obj_get_style_width(icon, LV_PART_MAIN) / 8;
    lv_obj_set_style_translate_y(obj, dist * v / 100, 0);
    lv_obj_set_style_opa(obj, (lv_opa_t)(v < 30 ? v * 255 / 30 : 255 - (v - 30) * 255 / 70), 0);
}
void loopAnim(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t a, int32_t b, uint32_t ms, bool reverse, uint32_t delay) {
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, obj);
    lv_anim_set_exec_cb(&an, cb);
    lv_anim_set_values(&an, a, b);
    lv_anim_set_duration(&an, ms);
    if (reverse) lv_anim_set_playback_duration(&an, ms);
    lv_anim_set_delay(&an, delay);
    lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&an, reverse ? lv_anim_path_ease_in_out : lv_anim_path_linear);
    lv_anim_start(&an);
}
} // namespace

void iconAnimate(lv_obj_t *container, Icon icon) {
    lv_anim_delete(container, nullptr);
    lv_obj_set_style_translate_x(container, 0, 0);
    lv_obj_set_style_opa(container, LV_OPA_COVER, 0);
    int size = lv_obj_get_style_width(container, LV_PART_MAIN);
    switch (icon) {
    case Icon::Sun:
        loopAnim(container, breathe, LV_OPA_COVER, 170, 2000, true, 0);
        break;
    case Icon::Moon:
        break;
    default: // clouds, fog, precipitation: slow drift
        loopAnim(container, driftX, -size / 14, size / 14, 2000, true, 0);
        break;
    }
    // Falling drops / flakes, staggered
    uint32_t n = lv_obj_get_child_count(container), k = 0;
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *ch = lv_obj_get_child(container, i);
        if (lv_obj_has_flag(ch, LV_OBJ_FLAG_USER_1)) loopAnim(ch, fall, 0, 100, 1400, false, 450 * k++);
    }
}

Icon weatherIcon(int code, bool isDay) {
    if (code == 0) return isDay ? Icon::Sun : Icon::Moon;
    if (code <= 2) return Icon::PartlyCloudy;
    if (code == 3) return Icon::Cloud;
    if (code == 45 || code == 48) return Icon::Fog;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return Icon::Snow;
    if (code >= 95) return Icon::Thunder;
    if (code >= 51) return Icon::Rain; // drizzle, rain, showers
    return Icon::Cloud;
}

const char *weatherText(int code) {
    if (code == 0) return "Clear";
    if (code == 1) return "Mainly clear";
    if (code == 2) return "Partly cloudy";
    if (code == 3) return "Overcast";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 57) return "Drizzle";
    if (code >= 61 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code == 85 || code == 86) return "Snow showers";
    if (code >= 95) return "Thunder";
    return "--";
}
