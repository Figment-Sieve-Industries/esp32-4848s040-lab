// Screen 1 - power flow, design "M" (values in pills), 480x480 rotated (USB-C at the bottom):
//
//   13:05  Sat 10 Oct                     (wifi) <home-ssid>  -62 dBm
//   ( SOLAR 3.68 kW )--->---+        [ weather / rain% / grid status ]
//                           v
//                         /HOME\
//          +------<-------|    |------->------+
//          v             566 W                v
//                 [240.6 V] [622 VA / -258 var]
//   ( GRID 1.58 kW )                    ( POWERWALL 98% )
//   [Exporting] [1.60 kVA / -258 var]   [Charging 1.55 kW]
//                          o == o o
//
// Chevrons travel along each elbow in the direction power flows; more power = faster.
#include "icons.h"
#include "powerwall.h"
#include "screens.h"
#include "ui_common.h"
#include <Arduino.h>
#include <math.h>

using namespace ui;

namespace {

constexpr float MIN_FLOW_W = 30;
constexpr uint32_t STALE_MS = 10000;
constexpr float CHEVRON_SPACING = 64; // px between chevrons along a path
constexpr int MAX_CHEVRONS = 6;
const lv_color_t CHIP_TEXT = lv_color_hex(0xC9D1D9);
constexpr int BATT_PILL_W = 162;
constexpr int SOC_BAR_W = BATT_PILL_W - 44; // inset 22 px each side

lv_obj_t *scr;

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
    f.len = segLen(a, b) + segLen(b, c);
    for (int i = 0; i < MAX_CHEVRONS; i++) {
        lv_obj_t *l = lv_line_create(scr);
        lv_line_set_points(l, CHEV_PTS[RIGHT], 3);
        f.chevDir[i] = RIGHT;
        lv_obj_set_style_line_width(l, 3, 0);
        lv_obj_set_style_line_color(l, color, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
        f.chev[i] = l;
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
TopBar topBar;
lv_obj_t *lblStatusLine;
lv_obj_t *wCard, *icoWeather, *lblTemp, *lblCond, *lblHiLo, *rainRow, *lblRain, *statusDot, *lblGridStatus;
lv_obj_t *vppDot, *lblVpp;
bool vppShownActive = true; // forces the first refresh to style the row
lv_obj_t *valSolar, *valHome, *valGrid, *valSoc, *icoBatt, *socFill;
lv_obj_t *chipVolt, *chipHomeVa, *chipGridDir, *chipGridVa, *chipBatt;
int weatherIconKey = -100;
uint32_t lastAnimMs;

lv_obj_t *track(int x, int y, int w, int h) {
    lv_obj_t *o = box(scr);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 2, 0);
    lv_obj_set_style_bg_color(o, TRACK, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// w x 56 pill: icon left, caption over value. Returns the value label; *iconOut gets the icon.
lv_obj_t *pill(int x, int y, int w, lv_color_t color, Icon icon, const char *cap, lv_obj_t **pillOut = nullptr,
               lv_obj_t **iconOut = nullptr) {
    lv_obj_t *p = box(scr);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, 56);
    lv_obj_set_style_radius(p, 28, 0);
    lv_obj_set_style_bg_color(p, CARD, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, color, 0);
    lv_obj_set_style_border_width(p, 3, 0);
    lv_obj_t *i = iconCreate(p, icon, 28, color);
    lv_obj_align(i, LV_ALIGN_LEFT_MID, 11, 0); // 14 px from the outer edge (3 px border)
    lv_obj_t *col = box(p);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 47, 0);
    caption(col, color, cap);
    lv_obj_t *v = label(col, &lv_font_montserrat_20, TEXT, "--");
    if (pillOut) *pillOut = p;
    if (iconOut) *iconOut = i;
    return v;
}

// Small tag chip: 22 px tall, TRACK background, 12 px text
lv_obj_t *chip(lv_obj_t *parent, const char *text) {
    lv_obj_t *l = label(parent, &lv_font_montserrat_12, CHIP_TEXT, text);
    lv_obj_set_height(l, 22);
    lv_obj_set_style_pad_hor(l, 10, 0);
    lv_obj_set_style_pad_top(l, 4, 0);
    lv_obj_set_style_radius(l, 10, 0);
    lv_obj_set_style_bg_color(l, TRACK, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    return l;
}

lv_obj_t *chipRow(int gap) {
    lv_obj_t *r = box(scr);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

lv_obj_t *row(lv_obj_t *parent, int gap) {
    lv_obj_t *r = box(parent);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, gap, 0);
    return r;
}

void animTick(lv_timer_t *) {
    uint32_t now = lv_tick_get();
    float dt = (now - lastAnimMs) / 1000.0f;
    lastAnimMs = now;
    if (lv_screen_active() != scr) return; // only animate while visible
    if (dt > 0.2f) dt = 0.2f;               // no jump after being hidden
    for (auto &f : paths) pathTick(f, dt);
}

// "622 VA / -258 var" / "1.60 kVA / -258 var"
void formatVaVar(char *buf, size_t n, float va, float var) {
    if (fabsf(va) < 1000) snprintf(buf, n, "%d VA / %d var", (int)lroundf(fabsf(va)), (int)lroundf(var));
    else snprintf(buf, n, "%.2f kVA / %d var", fabsf(va) / 1000.0f, (int)lroundf(var));
}

} // namespace

lv_obj_t *houseCreate() {
    scr = screenCreate();

    // Tracks (drawn first so pills and the house cover their ends)
    track(162, 90, 80, 4);
    track(238, 92, 4, 104);
    track(85, 234, 122, 4);
    track(85, 236, 4, 124);
    track(273, 234, 116, 4);
    track(385, 236, 4, 124);

    // Flow paths, forward = solar->house, house->grid (export), house->battery (charge)
    pathCreate(paths[P_SOLAR], {162, 92}, {240, 92}, {240, 196}, C_SOLAR);
    pathCreate(paths[P_GRID], {207, 236}, {87, 236}, {87, 360}, C_GRID);
    pathCreate(paths[P_BATT], {273, 236}, {387, 236}, {387, 360}, C_BATT);

    // House, home value and its chips
    lv_obj_t *house = iconCreate(scr, Icon::House, 120, C_HOME, 1.4f);
    lv_obj_set_pos(house, 180, 176);
    valHome = label(scr, &lv_font_montserrat_26, TEXT, "--");
    lv_obj_set_width(valHome, 180);
    lv_obj_set_style_text_align(valHome, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(valHome, 150, 293);
    lv_obj_t *homeChips = chipRow(6);
    chipVolt = chip(homeChips, "--");
    chipHomeVa = chip(homeChips, "--");
    lv_obj_align(homeChips, LV_ALIGN_TOP_MID, 0, 326);

    // Pills
    valSolar = pill(12, 64, 150, C_SOLAR, Icon::Sun, "SOLAR");
    valGrid = pill(12, 360, 150, C_GRID, Icon::Pylon, "GRID");
    lv_obj_t *battPill;
    // Wider than the others so "POWERWALL" fits next to the icon
    valSoc = pill(306, 360, BATT_PILL_W, C_BATT, Icon::Battery, "POWERWALL", &battPill, &icoBatt);
    // Thin SOC bar inside the bottom of the Powerwall pill
    lv_obj_t *socTrack = box(battPill);
    lv_obj_set_size(socTrack, SOC_BAR_W, 4);
    lv_obj_set_pos(socTrack, 19, 43);
    lv_obj_set_style_radius(socTrack, 2, 0);
    lv_obj_set_style_bg_color(socTrack, TRACK, 0);
    lv_obj_set_style_bg_opa(socTrack, LV_OPA_COVER, 0);
    socFill = box(socTrack);
    lv_obj_set_size(socFill, 0, 4);
    lv_obj_set_style_radius(socFill, 2, 0);
    lv_obj_set_style_bg_color(socFill, C_BATT, 0);
    lv_obj_set_style_bg_opa(socFill, LV_OPA_COVER, 0);

    // Chips under the bottom pills
    lv_obj_t *gridChips = chipRow(6);
    lv_obj_set_pos(gridChips, 12, 424);
    chipGridDir = chip(gridChips, "--");
    chipGridVa = chip(gridChips, "--");
    lv_obj_t *battChips = chipRow(6);
    chipBatt = chip(battChips, "--");
    lv_obj_align(battChips, LV_ALIGN_TOP_RIGHT, -12, 424);

    topBar = topBarCreate(scr);

    // Weather + grid status card (top right)
    wCard = card(scr, 266, 52, 202, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(wCard, 14, 0);
    lv_obj_set_style_pad_ver(wCard, 12, 0);
    lv_obj_set_style_pad_row(wCard, 8, 0);
    lv_obj_set_style_border_color(wCard, TRACK, 0);
    lv_obj_set_style_border_width(wCard, 2, 0);
    lv_obj_set_flex_flow(wCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *wrow = row(wCard, 8);
    icoWeather = iconCreate(wrow, Icon::Cloud, 38, C_WEATHER, 1.8f);
    iconAnimate(icoWeather, Icon::Cloud);
    lblTemp = label(wrow, &lv_font_montserrat_30, TEXT, "--");
    lv_obj_t *wcol = box(wrow);
    lv_obj_set_flex_grow(wcol, 1);
    lv_obj_set_flex_flow(wcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(wcol, 2, 0);
    lblCond = label(wcol, &lv_font_montserrat_12, TEXT, "Weather");
    lv_obj_set_width(lblCond, lv_pct(100));
    lv_label_set_long_mode(lblCond, LV_LABEL_LONG_SCROLL_CIRCULAR); // long conditions scroll
    lv_obj_set_style_anim_duration(lblCond, lv_anim_speed_clamped(20, 300, 10000), 0); // 20 px/s (default 40)
    lblHiLo = label(wcol, &lv_font_montserrat_12, MUTED, "");

    rainRow = row(wCard, 6);
    iconCreate(rainRow, Icon::Drop, 14, C_HOME);
    lblRain = label(rainRow, &lv_font_montserrat_12, MUTED, "");
    setHidden(rainRow, true);

    lv_obj_t *divider = box(wCard);
    lv_obj_set_size(divider, lv_pct(100), 1);
    lv_obj_set_style_bg_color(divider, TRACK, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    lv_obj_t *srow = row(wCard, 8);
    statusDot = box(srow);
    lv_obj_set_size(statusDot, 9, 9);
    lv_obj_set_style_radius(statusDot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(statusDot, C_BATT, 0);
    lv_obj_set_style_bg_opa(statusDot, LV_OPA_COVER, 0);
    animPulse(statusDot, 2000);
    lblGridStatus = label(srow, &lv_font_montserrat_14, C_BATT, "Grid connected");

    // VPP / grid-services row: hollow ring when idle, pulsing amber dot during an event
    lv_obj_t *vrow = row(wCard, 8);
    vppDot = box(vrow);
    lv_obj_set_size(vppDot, 9, 9);
    lv_obj_set_style_radius(vppDot, LV_RADIUS_CIRCLE, 0);
    lblVpp = label(vrow, &lv_font_montserrat_12, MUTED, "VPP idle");

    pageDots(scr, 0);

    // Errors / stale data, bottom centre (below the page dots)
    lblStatusLine = label(scr, &lv_font_montserrat_12, C_ALERT, "");
    lv_obj_set_width(lblStatusLine, 480);
    lv_obj_set_style_text_align(lblStatusLine, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lblStatusLine, 0, 466);

    lastAnimMs = lv_tick_get();
    lv_timer_create(animTick, LV_DEF_REFR_PERIOD, nullptr);
    return scr;
}

void houseRefresh() {
    PwData d = powerwallData();
    WeatherData w = weatherData();
    bool stale = !d.valid || millis() - d.updatedMs > STALE_MS;
    char buf[64];

    topBarUpdate(topBar);

    // Weather (stale after WEATHER_STALE_MS: show placeholders rather than old conditions)
    bool wStale = !w.valid || millis() - w.updatedMs > WEATHER_STALE_MS;
    if (wStale) {
        setText(lblTemp, "--");
        setText(lblCond, w.valid ? "Weather out of date" : "Weather");
        setText(lblHiLo, "");
        setHidden(rainRow, true);
    } else {
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(w.tempC)); // UTF-8 degree sign
        setText(lblTemp, buf);
        setText(lblCond, weatherText(w.code));
        snprintf(buf, sizeof(buf), "H %d\xC2\xB0 / L %d\xC2\xB0", (int)lroundf(w.hiC), (int)lroundf(w.loC));
        setText(lblHiLo, buf);
        int key = (int)weatherIcon(w.code, w.isDay);
        if (key != weatherIconKey) {
            iconSet(icoWeather, (Icon)key, C_WEATHER, 1.8f);
            iconAnimate(icoWeather, (Icon)key);
            weatherIconKey = key;
        }
        if (w.rainPct >= 0) {
            snprintf(buf, sizeof(buf), "%d%% chance of rain", w.rainPct);
            setText(lblRain, buf);
        }
        setHidden(rainRow, w.rainPct < 0);
    }

    // Grid status
    bool offGrid = !stale && !d.gridConnected;
    lv_color_t sc = offGrid ? C_ALERT : C_BATT;
    if (!lv_color_eq(lv_obj_get_style_bg_color(statusDot, LV_PART_MAIN), sc)) {
        lv_obj_set_style_bg_color(statusDot, sc, 0);
        lv_obj_set_style_bg_color(wCard, offGrid ? C_ALERT_BG : CARD, 0);
        lv_obj_set_style_border_color(wCard, offGrid ? C_ALERT : TRACK, 0);
    }
    setTextColor(lblGridStatus, sc);
    setText(lblGridStatus, stale ? "Grid: no data" : offGrid ? "Off-grid: on battery" : "Grid connected");

    // VPP (grid services) indicator
    bool vpp = !stale && d.gridServicesActive;
    if (vpp != vppShownActive) {
        vppShownActive = vpp;
        lv_anim_delete(vppDot, nullptr);
        if (vpp) {
            lv_obj_set_style_border_width(vppDot, 0, 0);
            lv_obj_set_style_bg_color(vppDot, C_SOLAR, 0);
            lv_obj_set_style_bg_opa(vppDot, LV_OPA_COVER, 0);
            animPulse(vppDot, 1400);
        } else {
            lv_obj_set_style_bg_opa(vppDot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(vppDot, MUTED, 0);
            lv_obj_set_style_border_width(vppDot, 2, 0);
        }
        setTextColor(lblVpp, vpp ? C_SOLAR : MUTED);
        setText(lblVpp, vpp ? "VPP event active" : "VPP idle");
    }

    if (stale) {
        setText(valSolar, "--");
        setText(valHome, "--");
        setText(valGrid, "--");
        setText(chipVolt, "--");
        setText(chipHomeVa, "--");
        setText(chipGridDir, "--");
        setText(chipGridVa, "--");
        setText(chipBatt, "--");
        setText(valSoc, "--%"); // don't leave a frozen SOC on screen
        setWidth(socFill, 0);
        setText(lblStatusLine, d.error[0] ? d.error : "Waiting for Powerwall...");
    } else {
        formatPower(buf, sizeof(buf), d.solarW);
        setText(valSolar, buf);
        formatPower(buf, sizeof(buf), d.loadW);
        setText(valHome, buf);
        formatPower(buf, sizeof(buf), d.siteW);
        setText(valGrid, buf);

        // Home chips: voltage (amber outside the normal range) and VA / var
        if (d.voltageV > 50) {
            snprintf(buf, sizeof(buf), "%.1f V", d.voltageV);
            setText(chipVolt, buf);
            setTextColor(chipVolt, d.voltageV < MAINS_V_MIN || d.voltageV > MAINS_V_MAX ? C_SOLAR : CHIP_TEXT);
        } else {
            setText(chipVolt, "-- V");
        }
        formatVaVar(buf, sizeof(buf), d.loadVA, d.loadVar);
        setText(chipHomeVa, buf);

        // Grid chips
        setText(chipGridDir, offGrid ? "Outage" : fabsf(d.siteW) < MIN_FLOW_W ? "Idle" : d.siteW > 0 ? "Importing" : "Exporting");
        formatVaVar(buf, sizeof(buf), d.siteVA, d.siteVar);
        setText(chipGridVa, buf);

        // Powerwall
        if (d.socValid) {
            snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(d.soc));
            setText(valSoc, buf);
            iconSetBatteryLevel(icoBatt, d.soc);
            setWidth(socFill, (int32_t)lroundf(SOC_BAR_W * fminf(fmaxf(d.soc, 0), 100) / 100.0f));
        } else {
            setText(valSoc, "--%");
            setWidth(socFill, 0);
        }
        char pw[16];
        formatPower(pw, sizeof(pw), d.batteryW);
        if (fabsf(d.batteryW) < MIN_FLOW_W) snprintf(buf, sizeof(buf), "Idle");
        else snprintf(buf, sizeof(buf), "%s %s", d.batteryW > 0 ? "Discharging" : "Charging", pw);
        setText(chipBatt, buf);
        lv_obj_align(lv_obj_get_parent(chipBatt), LV_ALIGN_TOP_RIGHT, -12, 424); // keep right-aligned as text changes
        setText(lblStatusLine, "");
    }

    // Flows: forward = solar->house, house->grid (export), house->battery (charge)
    pathSet(paths[P_SOLAR], d.solarW, stale);
    pathSet(paths[P_GRID], -d.siteW, stale);
    pathSet(paths[P_BATT], -d.batteryW, stale);
}
