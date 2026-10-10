#include "ui_common.h"
#include "icons.h"
#include "powerwall.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <time.h>

// Navigation hooks, implemented in main.cpp
void navSwipe(int direction); // +1 = next screen, -1 = previous
void navTap();

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

namespace {

uint32_t lastGestureMs = 0;

void gestureCb(lv_event_t *) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) navSwipe(+1);
    else if (dir == LV_DIR_RIGHT) navSwipe(-1);
    else return;
    lastGestureMs = millis();
    lv_indev_wait_release(lv_indev_active());
}

void clickCb(lv_event_t *) {
    if (millis() - lastGestureMs > 400) navTap(); // a swipe also ends in a release; ignore that
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
    lv_obj_add_event_cb(scr, clickCb, LV_EVENT_CLICKED, nullptr);
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
    if (a < 1000) snprintf(buf, n, "%d W", (int)lroundf(a));
    else snprintf(buf, n, "%.2f kW", a / 1000.0f);
}

void formatKWh(char *buf, size_t n, double kwh) {
    if (kwh < 0) kwh = 0;
    if (kwh < 1000) {
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
    if (n.wifiUp) {
        snprintf(buf, sizeof(buf), "%s  %d dBm", n.ssid, n.rssi);
        setText(tb.wifi, buf);
        setTextColor(tb.wifi, MUTED);
    } else {
        setText(tb.wifi, "offline");
        setTextColor(tb.wifi, C_ALERT);
    }
    lv_obj_align(tb.wifi, LV_ALIGN_TOP_RIGHT, -12, 17);
    lv_obj_align_to(tb.wifiIcon, tb.wifi, LV_ALIGN_OUT_LEFT_MID, -6, -1);
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

void animValue(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delayMs) {
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

} // namespace ui
