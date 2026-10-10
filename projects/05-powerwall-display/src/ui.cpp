// Layout B ("house hub"), 480x480 in the rotated (USB-down) orientation:
//
//   13:05  Sat 10 Oct                     (wifi) -62 dBm
//   (sun)-------->-------+        [ weather | grid status ]
//   SOLAR 3.36 kW        v
//                      /HOME\
//        +------<------|    |------>------+
//        v              504 W             v
//   (grid) GRID 2.61 kW        POWERWALL 98% (battery)
//
// Chevrons travel along each elbow in the direction power flows; more power = faster.
#include "ui.h"
#include "icons.h"
#include "powerwall.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include <time.h>

void setBacklightLevel(uint8_t level); // main.cpp

namespace {

const lv_color_t BG = lv_color_hex(0x0B0F14);
const lv_color_t CARD = lv_color_hex(0x151B23);
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

constexpr float MIN_FLOW_W = 30;
constexpr uint32_t STALE_MS = 10000;
constexpr float CHEVRON_SPACING = 64; // px between chevrons along a path
constexpr int MAX_CHEVRONS = 6;

// ---------- chevron flow paths ----------
enum Dir { RIGHT, LEFT, DOWN, UP };
const lv_point_precise_t CHEV_PTS[4][3] = {
    {{4, 2}, {9, 7}, {4, 12}},   // right
    {{10, 2}, {5, 7}, {10, 12}}, // left
    {{2, 4}, {7, 9}, {12, 4}},   // down
    {{2, 10}, {7, 5}, {12, 10}}, // up
};

struct FlowPath {
    lv_point_t p[3];   // elbow: start -> corner -> end (forward direction)
    lv_color_t color;
    lv_obj_t *chev[MAX_CHEVRONS];
    Dir chevDir[MAX_CHEVRONS];
    int count = 0;     // chevrons in use
    float len = 0;
    float offset = 0;  // px travelled
    float speed = 0;   // px/s, signed: > 0 forward
    bool active = false;
};

enum { P_SOLAR, P_GRID, P_BATT, P_COUNT };
FlowPath paths[P_COUNT];

float segLen(const lv_point_t &a, const lv_point_t &b) { return fabsf(b.x - a.x) + fabsf(b.y - a.y); }

Dir segDir(const lv_point_t &a, const lv_point_t &b, bool forward) {
    Dir d = b.x > a.x ? RIGHT : b.x < a.x ? LEFT : b.y > a.y ? DOWN : UP;
    if (forward) return d;
    return d == RIGHT ? LEFT : d == LEFT ? RIGHT : d == DOWN ? UP : DOWN;
}

void pathCreate(FlowPath &f, lv_point_t a, lv_point_t b, lv_point_t c, lv_color_t color) {
    f.p[0] = a;
    f.p[1] = b;
    f.p[2] = c;
    f.color = color;
    f.len = segLen(a, b) + segLen(b, c);
    for (int i = 0; i < MAX_CHEVRONS; i++) {
        lv_obj_t *l = lv_line_create(lv_screen_active());
        lv_line_set_points(l, CHEV_PTS[RIGHT], 3);
        f.chevDir[i] = RIGHT;
        lv_obj_set_style_line_width(l, 3, 0);
        lv_obj_set_style_line_color(l, color, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        f.chev[i] = l;
    }
}

void setHidden(lv_obj_t *o, bool hidden) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

// power > 0 = forward along the path
void pathSet(FlowPath &f, float power, bool stale) {
    float p = fabsf(power);
    f.active = !stale && p >= MIN_FLOW_W;
    // 35 px/s at 30 W up to ~180 px/s at 5 kW+
    float v = 35.0f + 145.0f * fminf(p, 5000.0f) / 5000.0f;
    f.speed = power >= 0 ? v : -v;
    // Fewer chevrons for small flows
    int n = (int)(f.len / CHEVRON_SPACING);
    if (p < 1000) n = (n + 1) / 2;
    f.count = f.active ? (n < 1 ? 1 : n > MAX_CHEVRONS ? MAX_CHEVRONS : n) : 0;
    for (int i = 0; i < MAX_CHEVRONS; i++) setHidden(f.chev[i], i >= f.count);
}

void pathTick(FlowPath &f, float dt) {
    if (!f.active) return;
    f.offset = fmodf(f.offset + dt * f.speed + f.len, f.len);
    bool fwd = f.speed >= 0;
    float l1 = segLen(f.p[0], f.p[1]);
    for (int i = 0; i < f.count; i++) {
        float s = fmodf(f.offset + i * f.len / f.count, f.len);
        const lv_point_t &a = s < l1 ? f.p[0] : f.p[1];
        const lv_point_t &b = s < l1 ? f.p[1] : f.p[2];
        float t = s < l1 ? s / l1 : (s - l1) / (f.len - l1);
        float x = a.x + (b.x - a.x) * t;
        float y = a.y + (b.y - a.y) * t;
        Dir d = segDir(a, b, fwd);
        if (d != f.chevDir[i]) {
            lv_line_set_points(f.chev[i], CHEV_PTS[d], 3);
            f.chevDir[i] = d;
        }
        lv_obj_set_pos(f.chev[i], (int32_t)lroundf(x) - 7, (int32_t)lroundf(y) - 7);
    }
}

// ---------- widgets ----------
lv_obj_t *lblTime, *lblDate, *lblWifi, *icoWifi, *lblStatusLine;
lv_obj_t *card, *icoWeather, *lblTemp, *lblCond, *lblHiLo, *statusDot, *lblGridStatus;
lv_obj_t *valSolar, *valHome, *valGrid, *subGrid, *valSoc, *subBatt, *icoBatt;
int weatherIconKey = -100;
uint32_t lastAnimMs;

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

lv_obj_t *track(int x, int y, int w, int h) {
    lv_obj_t *o = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_color(o, TRACK, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// Round chip (72 px) with an icon in the middle
lv_obj_t *chip(int x, int y, lv_color_t color, Icon icon) {
    lv_obj_t *c = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, 72, 72);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, color, 0);
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *i = iconCreate(c, icon, 34, color);
    lv_obj_center(i);
    return i;
}

// Column of caption/value/sub labels; align = text alignment within width w
lv_obj_t *column(int x, int y, int w, lv_text_align_t align) {
    lv_obj_t *col = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(col);
    lv_obj_set_pos(col, x, y);
    lv_obj_set_size(col, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 1, 0);
    lv_obj_set_style_text_align(col, align, 0);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_CLICKABLE);
    return col;
}

lv_obj_t *colLabel(lv_obj_t *col, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *l = label(col, font, color, text);
    lv_obj_set_width(l, lv_pct(100));
    return l;
}

void animTick(lv_timer_t *) {
    uint32_t now = lv_tick_get();
    float dt = (now - lastAnimMs) / 1000.0f;
    lastAnimMs = now;
    for (auto &f : paths) pathTick(f, dt);
}

void dotOpa(void *obj, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

void formatPower(char *buf, size_t n, float w) {
    float a = fabsf(w);
    if (a < 1000) snprintf(buf, n, "%d W", (int)lroundf(a));
    else snprintf(buf, n, "%.2f kW", a / 1000.0f);
}

uint8_t brightness = 255;
void screenTap(lv_event_t *) {
    brightness = brightness == 255 ? 160 : brightness == 160 ? 100 : 255;
    setBacklightLevel(brightness);
}

} // namespace

void uiCreate() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screenTap, LV_EVENT_CLICKED, nullptr);

    // Tracks (drawn first so chips and the house cover their ends)
    track(104, 110, 138, 4);
    track(238, 112, 4, 84);
    track(66, 234, 141, 4);
    track(66, 236, 4, 132);
    track(273, 234, 141, 4);
    track(410, 236, 4, 132);

    // Flow paths, forward = solar->house, house->grid (export), house->battery (charge)
    pathCreate(paths[P_SOLAR], {104, 112}, {240, 112}, {240, 196}, C_SOLAR);
    pathCreate(paths[P_GRID], {207, 236}, {68, 236}, {68, 368}, C_GRID);
    pathCreate(paths[P_BATT], {273, 236}, {412, 236}, {412, 368}, C_BATT);

    // House + home value
    lv_obj_t *house = iconCreate(scr, Icon::House, 120, C_HOME, 1.4f);
    lv_obj_set_pos(house, 180, 176);
    lv_obj_t *colHome = column(160, 300, 160, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *capHome = caption(colHome, C_HOME, "HOME");
    lv_obj_set_width(capHome, lv_pct(100));
    valHome = colLabel(colHome, &lv_font_montserrat_26, TEXT, "--");

    // Solar
    chip(32, 76, C_SOLAR, Icon::Sun);
    lv_obj_t *colSolar = column(8, 156, 120, LV_TEXT_ALIGN_CENTER);
    lv_obj_t *capSolar = caption(colSolar, C_SOLAR, "SOLAR");
    lv_obj_set_width(capSolar, lv_pct(100));
    valSolar = colLabel(colSolar, &lv_font_montserrat_20, TEXT, "--");

    // Grid
    chip(32, 368, C_GRID, Icon::Pylon);
    lv_obj_t *colGrid = column(116, 380, 120, LV_TEXT_ALIGN_LEFT);
    caption(colGrid, C_GRID, "GRID");
    valGrid = colLabel(colGrid, &lv_font_montserrat_20, TEXT, "--");
    subGrid = colLabel(colGrid, &lv_font_montserrat_14, MUTED, "");

    // Powerwall
    icoBatt = chip(376, 368, C_BATT, Icon::Battery);
    lv_obj_t *colBatt = column(236, 380, 128, LV_TEXT_ALIGN_RIGHT);
    lv_obj_t *capBatt = caption(colBatt, C_BATT, "POWERWALL");
    lv_obj_set_width(capBatt, lv_pct(100));
    valSoc = colLabel(colBatt, &lv_font_montserrat_20, TEXT, "--%");
    subBatt = colLabel(colBatt, &lv_font_montserrat_14, MUTED, "");

    // Top bar
    lblTime = label(scr, &lv_font_montserrat_28, TEXT, "--:--");
    lv_obj_set_pos(lblTime, 14, 8);
    lblDate = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_set_pos(lblDate, 104, 18);
    lblWifi = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_align(lblWifi, LV_ALIGN_TOP_RIGHT, -14, 17);
    icoWifi = iconCreate(scr, Icon::Wifi, 20, MUTED);
    lv_obj_align_to(icoWifi, lblWifi, LV_ALIGN_OUT_LEFT_MID, -6, -1);

    // Weather + grid status card
    card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 282, 52);
    lv_obj_set_size(card, 184, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(card, 14, 0);
    lv_obj_set_style_pad_ver(card, 12, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_bg_color(card, CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, TRACK, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *wrow = lv_obj_create(card);
    lv_obj_remove_style_all(wrow);
    lv_obj_set_size(wrow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(wrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(wrow, 8, 0);
    icoWeather = iconCreate(wrow, Icon::Cloud, 38, C_WEATHER, 1.8f);
    lblTemp = label(wrow, &lv_font_montserrat_30, TEXT, "--");
    lv_obj_t *wcol = lv_obj_create(wrow);
    lv_obj_remove_style_all(wcol);
    lv_obj_set_size(wcol, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(wcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wcol, 2, 0);
    lblCond = label(wcol, &lv_font_montserrat_12, TEXT, "Weather");
    lblHiLo = label(wcol, &lv_font_montserrat_12, MUTED, "");

    lv_obj_t *divider = lv_obj_create(card);
    lv_obj_remove_style_all(divider);
    lv_obj_set_size(divider, lv_pct(100), 1);
    lv_obj_set_style_bg_color(divider, TRACK, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    lv_obj_t *srow = lv_obj_create(card);
    lv_obj_remove_style_all(srow);
    lv_obj_set_size(srow, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(srow, 8, 0);
    statusDot = lv_obj_create(srow);
    lv_obj_remove_style_all(statusDot);
    lv_obj_set_size(statusDot, 9, 9);
    lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(statusDot, C_BATT, 0);
    lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, 0);
    lblGridStatus = label(srow, &lv_font_montserrat_14, C_BATT, "Grid connected");

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, statusDot);
    lv_anim_set_exec_cb(&a, dotOpa);
    lv_anim_set_values(&a, LV_OPA_COVER, 90);
    lv_anim_set_duration(&a, 1000);
    lv_anim_set_playback_duration(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    // Errors / stale data, bottom centre
    lblStatusLine = label(scr, &lv_font_montserrat_12, C_ALERT, "");
    lv_obj_set_width(lblStatusLine, 480);
    lv_obj_set_style_text_align(lblStatusLine, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblStatusLine, 0, 458);

    lastAnimMs = lv_tick_get();
    lv_timer_create(animTick, LV_DEF_REFR_PERIOD, nullptr);
}

void uiRefresh() {
    PwData d = powerwallData();
    NetStatus n = networkStatus();
    WeatherData w = weatherData();
    bool stale = !d.valid || millis() - d.updatedMs > STALE_MS;
    char buf[64];

    // Clock
    struct tm tm;
    if (getLocalTime(&tm, 0)) {
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        setText(lblTime, buf);
        strftime(buf, sizeof(buf), "%a %d %b", &tm);
        setText(lblDate, buf);
    }

    // Wi-Fi: icon + dBm
    if (n.wifiUp) {
        snprintf(buf, sizeof(buf), "%d dBm", n.rssi);
        setText(lblWifi, buf);
        setTextColor(lblWifi, MUTED);
    } else {
        setText(lblWifi, "offline");
        setTextColor(lblWifi, C_ALERT);
    }
    lv_obj_align(lblWifi, LV_ALIGN_TOP_RIGHT, -14, 17);
    lv_obj_align_to(icoWifi, lblWifi, LV_ALIGN_OUT_LEFT_MID, -6, -1);

    // Weather
    if (w.valid) {
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(w.tempC)); // UTF-8 degree sign
        setText(lblTemp, buf);
        setText(lblCond, weatherText(w.code));
        snprintf(buf, sizeof(buf), "H %d\xC2\xB0 / L %d\xC2\xB0", (int)lroundf(w.hiC), (int)lroundf(w.loC));
        setText(lblHiLo, buf);
        int key = (int)weatherIcon(w.code, w.isDay);
        if (key != weatherIconKey) {
            iconSet(icoWeather, (Icon)key, C_WEATHER, 1.8f);
            weatherIconKey = key;
        }
    }

    // Grid status card
    bool offGrid = !stale && !d.gridConnected;
    lv_color_t sc = offGrid ? C_ALERT : C_BATT;
    if (!lv_color_eq(lv_obj_get_style_bg_color(statusDot, LV_PART_MAIN), sc)) {
        lv_obj_set_style_bg_color(statusDot, sc, 0);
        lv_obj_set_style_bg_color(card, offGrid ? C_ALERT_BG : CARD, 0);
        lv_obj_set_style_border_color(card, offGrid ? C_ALERT : TRACK, 0);
    }
    setTextColor(lblGridStatus, sc);
    setText(lblGridStatus, stale ? "Grid: no data" : offGrid ? "Off-grid: on battery" : "Grid connected");

    if (stale) {
        setText(valSolar, "--");
        setText(valHome, "--");
        setText(valGrid, "--");
        setText(subGrid, "");
        setText(subBatt, "");
        if (!d.valid) setText(valSoc, "--%");
        setText(lblStatusLine, d.error[0] ? d.error : "Waiting for Powerwall...");
    } else {
        formatPower(buf, sizeof(buf), d.solarW);
        setText(valSolar, buf);
        formatPower(buf, sizeof(buf), d.loadW);
        setText(valHome, buf);
        formatPower(buf, sizeof(buf), d.siteW);
        setText(valGrid, buf);
        setText(subGrid, offGrid ? "Outage" : fabsf(d.siteW) < MIN_FLOW_W ? "Idle" : d.siteW > 0 ? "Importing" : "Exporting");

        snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(d.soc));
        setText(valSoc, buf);
        iconSetBatteryLevel(icoBatt, d.soc);
        char pw[16];
        formatPower(pw, sizeof(pw), d.batteryW);
        if (fabsf(d.batteryW) < MIN_FLOW_W) snprintf(buf, sizeof(buf), "Idle");
        else snprintf(buf, sizeof(buf), "%s %s", d.batteryW > 0 ? "Discharging" : "Charging", pw);
        setText(subBatt, buf);
        setText(lblStatusLine, "");
    }

    // Flows: forward = solar->house, house->grid (export), house->battery (charge)
    pathSet(paths[P_SOLAR], d.solarW, stale);
    pathSet(paths[P_GRID], -d.siteW, stale);
    pathSet(paths[P_BATT], -d.batteryW, stale);
}
