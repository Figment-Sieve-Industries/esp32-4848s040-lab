#include "ui_common.h"
#include "icons.h"
#include "powerwall.h"
#include "secrets.h" // WEATHER_NAME (placeName)
#include <Arduino.h>
#include <src/core/lv_global.h> // private: anim list access for pauseScreenAnimations
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Navigation hooks, implemented in main.cpp
void navSwipe(int direction); // +1 = next screen, -1 = previous

namespace ui {

const lv_color_t BG = lv_color_hex(0x0B0F14);
const lv_color_t CARD = lv_color_hex(0x151B23);
const lv_color_t CARD_HI = lv_color_hex(0x1F2731);
const lv_color_t TRACK = lv_color_hex(0x2A3440);
const lv_color_t TEXT = lv_color_hex(0xE6EDF3);
const lv_color_t MUTED = lv_color_hex(0x8B98A5);
const lv_color_t C_SOLAR = lv_color_hex(0xF5B700);
const lv_color_t C_GRID = lv_color_hex(0x9AA5B1);
const lv_color_t C_HOME = lv_color_hex(0x3DB2FF);
const lv_color_t C_BATT = lv_color_hex(0x3DDC84);
const lv_color_t C_ALERT = lv_color_hex(0xE5484D);
const lv_color_t C_ALERT_BG = lv_color_hex(0x2A1215);
const lv_color_t C_WEATHER = lv_color_hex(0xC9D1D9);
const lv_color_t C_OK_BG = lv_color_hex(0x1F3A2B);

// Named colours the screens used as literals (values unchanged)
const lv_color_t C_RED = lv_color_hex(0xE5484D); // = C_ALERT
const lv_color_t C_ORANGE = lv_color_hex(0xFF8A3D);
const lv_color_t C_YELLOW = lv_color_hex(0xF5D400);
const lv_color_t C_GREEN = lv_color_hex(0x3DDC84); // = C_BATT
const lv_color_t C_BLUE = lv_color_hex(0x3B82F6);
const lv_color_t C_PURPLE = lv_color_hex(0xB266FF);
const lv_color_t DIM_BAR = lv_color_hex(0x3A4654);

namespace {

// Extra delay added to every entry animation started while a screen slides in (see setShowDelay)
uint32_t s_showDelayMs = 0;

// Top bars made by screenCreate(index), updated centrally by main
TopBar s_bars[SCREEN_COUNT];
bool s_haveBar[SCREEN_COUNT];

void gestureCb(lv_event_t *) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) navSwipe(+1);
    else if (dir == LV_DIR_RIGHT) navSwipe(-1);
    else return;
    lv_indev_wait_release(lv_indev_active());
}

void opaBg(void *o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
void opaText(void *o, int32_t v) { lv_obj_set_style_text_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
void opaObj(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }
void transX(void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }

void loop(lv_obj_t *obj, lv_anim_exec_xcb_t cb, int32_t a, int32_t b, uint32_t periodMs) {
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, obj);
    lv_anim_set_exec_cb(&an, cb);
    lv_anim_set_values(&an, a, b);
    lv_anim_set_duration(&an, periodMs / 2);
    lv_anim_set_playback_duration(&an, periodMs / 2);
    lv_anim_set_repeat_count(&an, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_in_out);
    lv_anim_start(&an);
}

} // namespace

lv_obj_t *screenCreate() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, gestureCb, LV_EVENT_GESTURE, nullptr);
    return scr;
}

lv_obj_t *box(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *c = box(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 14, 0);
    lv_obj_set_style_bg_color(c, CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    return c;
}

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *caption(lv_obj_t *parent, lv_color_t color, const char *text) {
    lv_obj_t *l = label(parent, &lv_font_montserrat_12, color, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

void setText(lv_obj_t *l, const char *text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

void setTextColor(lv_obj_t *l, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_text_color(l, LV_PART_MAIN), c)) lv_obj_set_style_text_color(l, c, 0);
}

void setBgColor(lv_obj_t *o, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(o, LV_PART_MAIN), c)) lv_obj_set_style_bg_color(o, c, 0);
}

void setHidden(lv_obj_t *o, bool hidden) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void setWidth(lv_obj_t *o, int32_t w) {
    if (lv_obj_get_style_width(o, LV_PART_MAIN) != w) lv_obj_set_width(o, w);
}

void setHeight(lv_obj_t *o, int32_t h) {
    if (lv_obj_get_style_height(o, LV_PART_MAIN) != h) lv_obj_set_height(o, h);
}

void setX(lv_obj_t *o, int32_t x) {
    if (lv_obj_get_style_x(o, LV_PART_MAIN) != x) lv_obj_set_x(o, x);
}

void formatPower(char *buf, size_t n, float w) {
    float a = fabsf(w);
    long rw = lroundf(a); // round first so 999.6 W becomes "1.00 kW", not "1000 W"
    if (rw < 1000) snprintf(buf, n, "%ld W", rw);
    else snprintf(buf, n, "%.2f kW", a / 1000.0f);
}

void formatKWh(char *buf, size_t n, double kwh) {
    if (kwh < 0) kwh = 0;
    if (lround(kwh * 10.0) < 10000) { // rounds to < 1000.0, so "999.96" does not print "1000.0"
        snprintf(buf, n, "%.1f", kwh);
        return;
    }
    long v = lround(kwh);
    snprintf(buf, n, "%ld,%03ld", v / 1000, v % 1000); // < 1,000,000 kWh is plenty
}

TopBar topBarCreate(lv_obj_t *scr) {
    TopBar tb;
    tb.time = label(scr, &lv_font_montserrat_28, TEXT, "--:--");
    lv_obj_set_pos(tb.time, 12, 8);
    tb.date = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_set_pos(tb.date, 102, 18);
    tb.wifi = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_align(tb.wifi, LV_ALIGN_TOP_RIGHT, -12, 17);
    tb.wifiIcon = iconCreate(scr, Icon::Wifi, 20, MUTED);
    lv_obj_align_to(tb.wifiIcon, tb.wifi, LV_ALIGN_OUT_LEFT_MID, -6, -1);
    return tb;
}

static bool s_privacy;
void setPrivacy(bool on) { s_privacy = on; }
bool privacy() { return s_privacy; }
const char *placeName() { return s_privacy ? "MY TOWN" : WEATHER_NAME; }

void topBarUpdate(TopBar &tb) {
    char buf[64];
    struct tm tm;
    if (getLocalTime(&tm, 0)) {
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        setText(tb.time, buf);
        strftime(buf, sizeof(buf), "%a %d %b", &tm);
        setText(tb.date, buf);
    }
    NetStatus n = networkStatus();
    if (n.wifiUp) snprintf(buf, sizeof(buf), "%s  %d dBm", privacy() ? "Wi-Fi" : n.ssid, n.rssi);
    else snprintf(buf, sizeof(buf), "offline");
    setTextColor(tb.wifi, n.wifiUp ? MUTED : C_ALERT);
    if (strcmp(lv_label_get_text(tb.wifi), buf) != 0) { // re-align only when the width may have changed
        lv_label_set_text(tb.wifi, buf);
        lv_obj_align(tb.wifi, LV_ALIGN_TOP_RIGHT, -12, 17);
        lv_obj_align_to(tb.wifiIcon, tb.wifi, LV_ALIGN_OUT_LEFT_MID, -6, -1);
    }
}

void pageDots(lv_obj_t *scr, int active) {
    lv_obj_t *row = box(scr);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    for (int i = 0; i < SCREEN_COUNT; i++) {
        lv_obj_t *d = box(row);
        lv_obj_set_size(d, i == active ? 20 : 8, 8);
        lv_obj_set_style_radius(d, 4, 0);
        lv_obj_set_style_bg_color(d, i == active ? TEXT : TRACK, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 456);
}

void animPulse(lv_obj_t *obj, uint32_t periodMs) { loop(obj, opaBg, LV_OPA_COVER, 90, periodMs); }

void animTextPulse(lv_obj_t *l, uint32_t periodMs) { loop(l, opaText, LV_OPA_COVER, 150, periodMs); }

void animDrift(lv_obj_t *obj, int px, uint32_t periodMs) { loop(obj, transX, -px, px, periodMs); }

void animFadeIn(lv_obj_t *obj, uint32_t delayMs) {
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    animValue(obj, opaObj, LV_OPA_TRANSP, LV_OPA_COVER, 400, delayMs);
}

void setShowDelay(uint32_t ms) { s_showDelayMs = ms; }
uint32_t showDelay() { return s_showDelayMs; }

void animValue(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delayMs) {
    delayMs += s_showDelayMs; // while a screen slides in, start after the slide
    lv_anim_t an;
    lv_anim_init(&an);
    lv_anim_set_var(&an, var);
    lv_anim_set_exec_cb(&an, cb);
    lv_anim_set_values(&an, from, to);
    lv_anim_set_duration(&an, ms);
    lv_anim_set_delay(&an, delayMs);
    lv_anim_set_path_cb(&an, lv_anim_path_ease_out);
    if (delayMs) cb(var, from); // apply the start value immediately during the delay
    lv_anim_start(&an);
}

namespace {

} // namespace

void pauseScreenAnimations(lv_obj_t *screen, bool pause) {
    if (!screen) return;
    // One pass over LVGL's animation list (it has no public enumeration; lv_anim_get returns only the
    // first match). Every animation in this app has an lv_obj as its var (no lv_bar/lv_slider
    // animations, whose var is a struct), so the owning screen is found from the var.
    // Animations on the screen object itself are LVGL's slide transitions: never frozen (e.g. swiping
    // again while a slide is still running).
    lv_ll_t *ll = &LV_GLOBAL_DEFAULT()->anim_state.anim_ll;
    for (auto *a = static_cast<lv_anim_t *>(lv_ll_get_head(ll)); a; a = static_cast<lv_anim_t *>(lv_ll_get_next(ll, a))) {
        auto *o = static_cast<lv_obj_t *>(a->var);
        if (!o || o == screen || lv_obj_get_screen(o) != screen) continue;
        if (pause) {
            if (!lv_anim_is_paused(a)) lv_anim_pause(a);
        } else if (lv_anim_is_paused(a)) {
            lv_anim_resume(a);
        }
    }
}

// ---------------- shared helpers (consolidation round) ----------------

lv_color_t levelColor(int level) {
    if (level <= 0) return C_GREEN;
    if (level == 1) return C_YELLOW;
    if (level == 2) return C_ORANGE;
    return C_RED;
}

void inert(lv_obj_t *o) {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, int radius) {
    lv_obj_t *o = box(parent); // style-less, inert
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap, lv_flex_align_t main, lv_flex_align_t cross) {
    lv_obj_t *o = box(parent);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_set_flex_align(o, main, cross, cross);
    lv_obj_set_style_pad_row(o, gap, 0);
    lv_obj_set_style_pad_column(o, gap, 0);
    return o;
}

lv_obj_t *valueUnit(lv_obj_t *parent, const lv_font_t *vf, const lv_font_t *uf, lv_obj_t **value, lv_obj_t **unit,
                    int unitPadBottom) {
    lv_obj_t *row = flex(parent, LV_FLEX_FLOW_ROW, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
    lv_obj_t *v = label(row, vf, TEXT, "--");
    lv_obj_t *u = label(row, uf, MUTED, "");
    lv_obj_set_style_pad_bottom(u, unitPadBottom, 0);
    if (value) *value = v;
    if (unit) *unit = u;
    return row;
}

lv_obj_t *labelAt(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *t, int x, int y, int w,
                  lv_text_align_t align) {
    lv_obj_t *l = label(parent, f, c, t);
    if (w > 0) {
        lv_obj_set_width(l, w);
        lv_obj_set_style_text_align(l, align, 0);
    }
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *cardCaption(lv_obj_t *card, const char *text, int x, int y) {
    lv_obj_t *l = caption(card, MUTED, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

HBar hbar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t track, lv_color_t fill, int radius) {
    HBar b;
    b.w = w;
    b.track = rect(parent, x, y, w, h, track, radius);
    b.fill = rect(b.track, 0, 0, 0, h, fill, radius);
    lv_obj_add_flag(b.fill, LV_OBJ_FLAG_HIDDEN);
    return b;
}

void hbarSet(HBar &b, float frac, lv_color_t fill) {
    if (!b.fill) return;
    if (!(frac > 0.0f)) { // also catches NaN
        setHidden(b.fill, true);
        return;
    }
    if (frac > 1.0f) frac = 1.0f;
    int w = static_cast<int>(lroundf(frac * static_cast<float>(b.w)));
    if (w < 1) w = 1;
    setWidth(b.fill, w);
    setBgColor(b.fill, fill);
    setHidden(b.fill, false);
}

void setTextF(lv_obj_t *l, const char *fmt, ...) {
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    setText(l, buf);
}

void setValueF(lv_obj_t *l, bool have, bool stale, lv_color_t ok, const char *fmt, ...) {
    if (!have) {
        setText(l, "--");
        setTextColor(l, MUTED);
        return;
    }
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    setText(l, buf);
    setTextColor(l, stale ? MUTED : ok);
}

void formatKWh(char *buf, size_t n, double kwh, bool withUnit) {
    char num[16];
    formatKWh(num, sizeof(num), kwh);
    snprintf(buf, n, withUnit ? "%s kWh" : "%s", num);
}

void fadeIn(lv_obj_t *const *objs, int n, uint32_t firstDelayMs, uint32_t stepMs) {
    for (int i = 0; i < n; i++)
        if (objs[i]) animFadeIn(objs[i], firstDelayMs + static_cast<uint32_t>(i) * stepMs);
}

// ---- Banner ----

Banner bannerCreate(lv_obj_t *scr, int y, int h, const char *cap, int subW) {
    Banner b;
    b.card = card(scr, 24, y, 432, h);
    lv_obj_set_style_border_width(b.card, 2, 0);
    lv_obj_set_style_border_color(b.card, MUTED, 0);

    b.ring = box(b.card);
    lv_obj_set_size(b.ring, 36, 36);
    lv_obj_set_pos(b.ring, 12, (h - 40) / 2 - 1);
    lv_obj_set_style_radius(b.ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(b.ring, 2, 0);
    lv_obj_set_style_border_color(b.ring, MUTED, 0);

    b.cap = caption(b.card, MUTED, cap);
    b.name = label(b.card, &lv_font_montserrat_20, MUTED, "--");
    lv_obj_set_width(b.name, subW);
    lv_label_set_long_mode(b.name, LV_LABEL_LONG_MODE_CLIP);
    b.sub = label(b.card, &lv_font_montserrat_12, MUTED, "");
    lv_obj_set_width(b.sub, subW);
    lv_label_set_long_mode(b.sub, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_pos(b.cap, 60, 9);
    lv_obj_set_pos(b.name, 60, 25);
    lv_obj_set_pos(b.sub, 60, h - 28);
    return b;
}

void bannerSet(Banner &b, lv_color_t fg, lv_color_t bg, const char *name, bool dim, const char *sub, size_t bigMaxLen,
               const lv_font_t *big, const lv_font_t *small) {
    setBgColor(b.card, bg);
    if (!lv_color_eq(lv_obj_get_style_border_color(b.card, LV_PART_MAIN), fg))
        lv_obj_set_style_border_color(b.card, fg, 0);
    if (!lv_color_eq(lv_obj_get_style_border_color(b.ring, LV_PART_MAIN), fg))
        lv_obj_set_style_border_color(b.ring, fg, 0);
    setTextColor(b.cap, fg);

    // Caption and name sit higher for a taller name font (tuned so the 20 px and 26 px fonts reproduce
    // the original aurora and ENSO banners: caption y 9 / 6, name y 25 / 20).
    int dl = static_cast<int>(lv_font_get_line_height(big)) - 23;
    lv_obj_set_y(b.cap, 9 - dl * 3 / 7); // change-only inside LVGL
    lv_obj_set_y(b.name, 25 - dl * 5 / 7);

    const lv_font_t *f = strlen(name) > bigMaxLen ? small : big;
    if (lv_obj_get_style_text_font(b.name, LV_PART_MAIN) != f) lv_obj_set_style_text_font(b.name, f, 0);
    setTextColor(b.name, dim ? MUTED : TEXT);
    setText(b.name, name);
    setText(b.sub, sub ? sub : "");
}

// ---- screen registry ----

lv_obj_t *screenCreate(int index) {
    lv_obj_t *scr = screenCreate();
    if (index >= 0 && index < SCREEN_COUNT) {
        s_bars[index] = topBarCreate(scr);
        s_haveBar[index] = true;
    }
    pageDots(scr, index);
    return scr;
}

void topBarUpdateFor(int index) {
    if (index >= 0 && index < SCREEN_COUNT && s_haveBar[index]) topBarUpdate(s_bars[index]);
}

} // namespace ui
