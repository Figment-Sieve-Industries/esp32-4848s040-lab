// Screen 1 - power flow, design N with design O's battery pill, 480x480 rotated (USB-C at the bottom):
//
//   13:05  Sat 10 Oct                     (wifi) <home-ssid>  -62 dBm
//   ( SOLAR 3.68 kW )--->---+        [ weather / rain% / grid status ]
//   [TODAY            ]     v
//   [Self-powered 86% ]   /HOME\
//   [Exported 6.1 kWh ]
//          +------<-------+------->------+
//          v   ( HOME 566 W | 240.6 V )   v
//   ( GRID 1.58 kW      )                ( POWERWALL 98%     )
//   ( 1.60 kVA/-258 var )                ( Charging 1.55 kW  )
//                          o == o o
//
// Chevrons travel along each elbow in the direction power flows; more power = faster.
#include "energy.h"
#include "icons.h"
#include "powerwall.h"
#include "screens.h"
#include "shared_state.h"
#include "ui_common.h"
#include <Arduino.h>
#include <math.h>

namespace {

constexpr float MIN_FLOW_W = 30;
constexpr float CHEVRON_SPACING = 64;    // px between chevrons along a path
constexpr int MAX_CHEVRONS = 6;
constexpr uint32_t CHEVRON_TICK_MS = 16; // chevron timer period (33 would halve the redraw load)
constexpr int BIG_PILL_W = 176, BIG_PILL_H = 76;
// Battery graphic in the Powerwall pill: inner width 176 - 2*3 border - 2*14 padding = 142 =
// body + 2 px gap + 4 px nub. The fill sits inside the body's 2 px border and 2 px padding.
constexpr int BATT_BODY_W = 136;
constexpr int SOC_FILL_W = BATT_BODY_W - 8;

// Battery fill colour by charge level: red < 15% < orange < 30% < yellow < 50% < green
lv_color_t socColor(float soc) {
    return ui::levelColor(soc < 15 ? 3 : soc < 30 ? 2 : soc < 50 ? 1 : 0);
}

lv_obj_t *s_scr;

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
FlowPath s_paths[P_COUNT];

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
        lv_obj_t *l = lv_line_create(s_scr);
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
    if (n < 1) n = 1;
    if (n > MAX_CHEVRONS) n = MAX_CHEVRONS;
    f.count = f.active ? n : 0;
    for (int i = 0; i < MAX_CHEVRONS; i++) ui::setHidden(f.chev[i], i >= f.count);
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
lv_obj_t *s_lblStatusLine;
lv_obj_t *s_wCard, *s_icoWeather, *s_lblTemp, *s_lblCond, *s_lblHiLo, *s_rainRow, *s_lblRain, *s_statusDot,
    *s_lblGridStatus;
lv_obj_t *s_vppDot, *s_lblVpp;
bool s_vppShownActive = true; // forces the first refresh to style the row
lv_obj_t *s_valSolar, *s_valHome, *s_valGrid, *s_valSoc, *s_socFill;
lv_obj_t *s_lblVolt, *s_lblHomeVa, *s_lblGridVa, *s_lblBatt, *s_valSelf, *s_valExp;
int s_weatherIconKey = -100;
uint32_t s_lastAnimMs;

lv_obj_t *track(int x, int y, int w, int h) { return ui::rect(s_scr, x, y, w, h, ui::TRACK, 2); }

// Rounded pill with a coloured 3 px border and CARD background.
lv_obj_t *pillBox(int x, int y, int w, int h, int radius, lv_color_t color) {
    lv_obj_t *p = ui::rect(s_scr, x, y, w, h, ui::CARD, radius);
    lv_obj_set_style_border_color(p, color, 0);
    lv_obj_set_style_border_width(p, 3, 0);
    return p;
}

// Pill: icon left, caption over value (over an optional 12 px sub line). Returns the value label.
// With subOut the pill is the tall 176x76 variant.
lv_obj_t *pill(int x, int y, int w, lv_color_t color, Icon icon, const char *cap, lv_obj_t **subOut = nullptr) {
    bool tall = subOut != nullptr;
    lv_obj_t *p = tall ? pillBox(x, y, w, BIG_PILL_H, 24, color) : pillBox(x, y, w, 56, 28, color);
    lv_obj_t *i = iconCreate(p, icon, 28, color);
    lv_obj_align(i, LV_ALIGN_LEFT_MID, 11, 0); // 14 px from the outer edge (3 px border)
    lv_obj_t *col = ui::flex(p, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 47, 0);
    ui::caption(col, color, cap);
    lv_obj_t *v = ui::label(col, &lv_font_montserrat_20, ui::TEXT, "--");
    if (tall) *subOut = ui::label(col, &lv_font_montserrat_12, ui::C_WEATHER, "--");
    return v;
}

// Full-width flex row, children centred vertically
lv_obj_t *fullRow(lv_obj_t *parent, int gap) {
    lv_obj_t *r = ui::flex(parent, LV_FLEX_FLOW_ROW, gap);
    lv_obj_set_width(r, lv_pct(100));
    return r;
}

// Card row: label left, value right, both bottom-aligned
lv_obj_t *statRow(lv_obj_t *parent, const lv_font_t *lf, lv_color_t lc, const char *text, const lv_font_t *vf) {
    lv_obj_t *r = ui::flex(parent, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END);
    lv_obj_set_width(r, lv_pct(100));
    ui::label(r, lf, lc, text);
    return ui::label(r, vf, ui::TEXT, "--");
}

void animTick(lv_timer_t *) {
    uint32_t now = lv_tick_get();
    float dt = (now - s_lastAnimMs) / 1000.0f;
    s_lastAnimMs = now;
    if (lv_screen_active() != s_scr) return; // only animate while visible
    if (dt > 0.2f) dt = 0.2f;                 // no jump after being hidden
    for (auto &f : s_paths) pathTick(f, dt);
}

// "622 VA / -258 var" / "1.60 kVA / -258 var"
void formatVaVar(char *buf, size_t n, float va, float var) {
    if (fabsf(va) < 1000) snprintf(buf, n, "%d VA / %d var", (int)lroundf(fabsf(va)), (int)lroundf(var));
    else snprintf(buf, n, "%.2f kVA / %d var", fabsf(va) / 1000.0f, (int)lroundf(var));
}

} // namespace

lv_obj_t *houseCreate() {
    s_scr = ui::screenCreate(ui::SCR_HOUSE);

    // Tracks (drawn first so pills and the house cover their ends)
    track(162, 90, 80, 4);
    track(238, 92, 4, 104);
    track(85, 258, 122, 4);
    track(85, 260, 4, 102);
    track(273, 258, 122, 4);
    track(391, 260, 4, 102);

    // Flow paths, forward = solar->house, house->grid (export), house->battery (charge)
    pathCreate(s_paths[P_SOLAR], {162, 92}, {240, 92}, {240, 196}, ui::C_SOLAR);
    pathCreate(s_paths[P_GRID], {207, 260}, {87, 260}, {87, 360}, ui::C_GRID);
    pathCreate(s_paths[P_BATT], {273, 260}, {393, 260}, {393, 360}, ui::C_BATT);

    // House
    lv_obj_t *house = iconCreate(s_scr, Icon::House, 120, ui::C_HOME, 1.4f);
    lv_obj_set_pos(house, 180, 176);

    // Home pill under the house: watts left, divider, voltage and VA/var right
    lv_obj_t *homePill = pillBox(114, 288, 252, 60, 30, ui::C_HOME);
    lv_obj_set_flex_flow(homePill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(homePill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(homePill, 15, 0);
    lv_obj_set_style_pad_column(homePill, 10, 0);
    lv_obj_t *hcol = ui::flex(homePill, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui::caption(hcol, ui::C_HOME, "HOME");
    s_valHome = ui::label(hcol, &lv_font_montserrat_22, ui::TEXT, "--");
    ui::rect(homePill, 0, 0, 1, 36, ui::TRACK);
    lv_obj_t *vcol = ui::flex(homePill, LV_FLEX_FLOW_COLUMN, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    s_lblVolt = ui::label(vcol, &lv_font_montserrat_12, ui::C_WEATHER, "--");
    s_lblHomeVa = ui::label(vcol, &lv_font_montserrat_12, ui::C_WEATHER, "--");

    // Today card (left, under the solar pill)
    lv_obj_t *tCard = ui::card(s_scr, 12, 134, 170, 108);
    lv_obj_set_style_pad_hor(tCard, 14, 0);
    lv_obj_set_style_pad_ver(tCard, 10, 0);
    lv_obj_set_style_pad_row(tCard, 6, 0);
    lv_obj_set_style_border_color(tCard, ui::TRACK, 0);
    lv_obj_set_style_border_width(tCard, 2, 0);
    lv_obj_set_flex_flow(tCard, LV_FLEX_FLOW_COLUMN);
    ui::caption(tCard, ui::MUTED, "TODAY");
    s_valSelf = statRow(tCard, &lv_font_montserrat_12, ui::C_BATT, "Self-powered", &lv_font_montserrat_22);
    s_valExp = statRow(tCard, &lv_font_montserrat_12, ui::C_GRID, "Exported", &lv_font_montserrat_18);

    // Pills
    s_valSolar = pill(12, 64, 150, ui::C_SOLAR, Icon::Sun, "SOLAR");
    s_valGrid = pill(12, 362, BIG_PILL_W, ui::C_GRID, Icon::Pylon, "GRID", &s_lblGridVa);
    // Powerwall pill (design O): caption, a full-width battery filled to the charge level (colour by
    // level) with the % on a dark badge in its centre, then the charge rate on its own line
    lv_obj_t *battPill = pillBox(292, 362, BIG_PILL_W, BIG_PILL_H, 24, ui::C_BATT);
    lv_obj_set_style_pad_hor(battPill, 14, 0);
    lv_obj_set_style_pad_ver(battPill, 6, 0);
    lv_obj_set_flex_flow(battPill, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(battPill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(battPill, 4, 0);
    ui::caption(battPill, ui::C_BATT, "POWERWALL");
    lv_obj_t *battRow = fullRow(battPill, 2);
    lv_obj_t *body = ui::box(battRow);
    lv_obj_set_size(body, BATT_BODY_W, 24);
    lv_obj_set_style_border_color(body, ui::C_BATT, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 5, 0);
    lv_obj_set_style_pad_all(body, 2, 0);
    s_socFill = ui::rect(body, 0, 0, 0, lv_pct(100), ui::C_BATT, 2);
    // Dark badge keeps the % readable over any fill colour and over the empty part
    s_valSoc = ui::label(body, &lv_font_montserrat_14, ui::TEXT, "--%");
    lv_obj_set_style_bg_color(s_valSoc, ui::BG, 0);
    lv_obj_set_style_bg_opa(s_valSoc, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_valSoc, 4, 0);
    lv_obj_set_style_pad_hor(s_valSoc, 6, 0);
    lv_obj_set_style_pad_ver(s_valSoc, 1, 0);
    lv_obj_align(s_valSoc, LV_ALIGN_CENTER, 0, 0);
    ui::rect(battRow, 0, 0, 4, 10, ui::C_BATT, 2); // nub
    s_lblBatt = ui::label(battPill, &lv_font_montserrat_12, ui::C_WEATHER, "--");

    // Weather + grid status card (top right)
    s_wCard = ui::card(s_scr, 266, 52, 202, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(s_wCard, 14, 0);
    lv_obj_set_style_pad_ver(s_wCard, 12, 0);
    lv_obj_set_style_pad_row(s_wCard, 8, 0);
    lv_obj_set_style_border_color(s_wCard, ui::TRACK, 0);
    lv_obj_set_style_border_width(s_wCard, 2, 0);
    lv_obj_set_flex_flow(s_wCard, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *wrow = fullRow(s_wCard, 8);
    s_icoWeather = iconCreate(wrow, Icon::Cloud, 38, ui::C_WEATHER, 1.8f);
    iconAnimate(s_icoWeather, Icon::Cloud);
    s_lblTemp = ui::label(wrow, &lv_font_montserrat_30, ui::TEXT, "--");
    lv_obj_t *wcol = ui::flex(wrow, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_flex_grow(wcol, 1);
    s_lblCond = ui::label(wcol, &lv_font_montserrat_12, ui::TEXT, "Weather");
    lv_obj_set_width(s_lblCond, lv_pct(100));
    lv_label_set_long_mode(s_lblCond, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR); // long conditions scroll
    lv_obj_set_style_anim_duration(s_lblCond, lv_anim_speed_clamped(20, 300, 10000), 0); // 20 px/s (default 40)
    s_lblHiLo = ui::label(wcol, &lv_font_montserrat_12, ui::MUTED, "");

    s_rainRow = fullRow(s_wCard, 6);
    iconCreate(s_rainRow, Icon::Drop, 14, ui::C_HOME);
    s_lblRain = ui::label(s_rainRow, &lv_font_montserrat_12, ui::MUTED, "");
    ui::setHidden(s_rainRow, true);

    ui::rect(s_wCard, 0, 0, lv_pct(100), 1, ui::TRACK); // divider

    lv_obj_t *srow = fullRow(s_wCard, 8);
    s_statusDot = ui::rect(srow, 0, 0, 9, 9, ui::C_BATT, LV_RADIUS_CIRCLE);
    ui::animPulse(s_statusDot, 2000);
    s_lblGridStatus = ui::label(srow, &lv_font_montserrat_14, ui::C_BATT, "Grid connected");

    // VPP / grid-services row: hollow ring when idle, pulsing amber dot during an event
    lv_obj_t *vrow = fullRow(s_wCard, 8);
    s_vppDot = ui::box(vrow);
    lv_obj_set_size(s_vppDot, 9, 9);
    lv_obj_set_style_radius(s_vppDot, LV_RADIUS_CIRCLE, 0);
    s_lblVpp = ui::label(vrow, &lv_font_montserrat_12, ui::MUTED, "VPP idle");

    // Errors / stale data, bottom centre (below the page dots)
    s_lblStatusLine = ui::labelAt(s_scr, &lv_font_montserrat_12, ui::C_ALERT, "", 0, 466, 480, LV_TEXT_ALIGN_CENTER);

    s_lastAnimMs = lv_tick_get();
    lv_timer_create(animTick, CHEVRON_TICK_MS, nullptr);
    return s_scr;
}

void houseRefresh() {
    PwData d = powerwallData();
    TodayStats ts = energyToday();
    WeatherData w = weatherData();
    bool stale = isStale(d.valid, d.updatedMs, PW_STALE_MS);
    char buf[64];

    // Weather (stale after WEATHER_STALE_MS: show placeholders rather than old conditions)
    bool wStale = isStale(w.valid, w.updatedMs, WEATHER_STALE_MS);
    if (wStale) {
        ui::setText(s_lblTemp, "--");
        ui::setText(s_lblCond, w.valid ? "Weather out of date" : "Weather");
        ui::setText(s_lblHiLo, "");
        ui::setHidden(s_rainRow, true);
    } else {
        ui::setTextF(s_lblTemp, "%d\xC2\xB0", (int)lroundf(w.tempC)); // UTF-8 degree sign
        ui::setText(s_lblCond, weatherText(w.code));
        ui::setTextF(s_lblHiLo, "H %d\xC2\xB0 / L %d\xC2\xB0", (int)lroundf(w.hiC), (int)lroundf(w.loC));
        int key = (int)weatherIcon(w.code, w.isDay);
        if (key != s_weatherIconKey) {
            iconSet(s_icoWeather, (Icon)key, ui::C_WEATHER, 1.8f);
            iconAnimate(s_icoWeather, (Icon)key);
            s_weatherIconKey = key;
        }
        if (w.rainPct >= 0) ui::setTextF(s_lblRain, "%d%% chance of rain", w.rainPct);
        ui::setHidden(s_rainRow, w.rainPct < 0);
    }

    // Grid status
    bool offGrid = !stale && !d.gridConnected;
    lv_color_t sc = offGrid ? ui::C_ALERT : ui::C_BATT;
    if (!lv_color_eq(lv_obj_get_style_bg_color(s_statusDot, LV_PART_MAIN), sc)) {
        lv_obj_set_style_bg_color(s_statusDot, sc, 0);
        lv_obj_set_style_bg_color(s_wCard, offGrid ? ui::C_ALERT_BG : ui::CARD, 0);
        lv_obj_set_style_border_color(s_wCard, offGrid ? ui::C_ALERT : ui::TRACK, 0);
    }
    ui::setTextColor(s_lblGridStatus, sc);
    ui::setText(s_lblGridStatus, stale ? "Grid: no data" : offGrid ? "Off-grid: on battery" : "Grid connected");

    // VPP (grid services) indicator
    bool vpp = !stale && d.gridServicesActive;
    if (vpp != s_vppShownActive) {
        s_vppShownActive = vpp;
        lv_anim_delete(s_vppDot, nullptr);
        if (vpp) {
            lv_obj_set_style_border_width(s_vppDot, 0, 0);
            lv_obj_set_style_bg_color(s_vppDot, ui::C_SOLAR, 0);
            lv_obj_set_style_bg_opa(s_vppDot, LV_OPA_COVER, 0);
            ui::animPulse(s_vppDot, 1400);
        } else {
            lv_obj_set_style_bg_opa(s_vppDot, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(s_vppDot, ui::MUTED, 0);
            lv_obj_set_style_border_width(s_vppDot, 2, 0);
        }
        ui::setTextColor(s_lblVpp, vpp ? ui::C_SOLAR : ui::MUTED);
        ui::setText(s_lblVpp, vpp ? "VPP event active" : "VPP idle");
    }

    if (stale) {
        ui::setText(s_valSolar, "--");
        ui::setText(s_valHome, "--");
        ui::setText(s_valGrid, "--");
        ui::setText(s_lblVolt, "--");
        ui::setText(s_lblHomeVa, "--");
        ui::setText(s_lblGridVa, "--");
        ui::setText(s_lblBatt, "--");
        ui::setText(s_valSoc, "--%"); // don't leave a frozen SOC on screen
        ui::setWidth(s_socFill, 0);
        ui::setText(s_lblStatusLine, d.error[0] ? d.error : "Waiting for Powerwall...");
    } else {
        ui::formatPower(buf, sizeof(buf), d.solarW);
        ui::setText(s_valSolar, buf);
        ui::formatPower(buf, sizeof(buf), d.loadW);
        ui::setText(s_valHome, buf);
        ui::formatPower(buf, sizeof(buf), d.siteW);
        ui::setText(s_valGrid, buf);

        // Home pill: voltage (amber outside the normal range) and VA / var
        if (d.voltageV > 50) {
            ui::setTextF(s_lblVolt, "%.1f V", d.voltageV);
            ui::setTextColor(s_lblVolt, d.voltageV < ui::MAINS_V_MIN || d.voltageV > ui::MAINS_V_MAX
                                            ? ui::C_SOLAR
                                            : ui::C_WEATHER);
        } else {
            ui::setText(s_lblVolt, "-- V");
        }
        formatVaVar(buf, sizeof(buf), d.loadVA, d.loadVar);
        ui::setText(s_lblHomeVa, buf);

        // Grid pill VA / var (also shown during an outage)
        formatVaVar(buf, sizeof(buf), d.siteVA, d.siteVar);
        ui::setText(s_lblGridVa, buf);

        // Powerwall
        if (d.socValid) {
            ui::setTextF(s_valSoc, "%d%%", (int)lroundf(d.soc));
            ui::setWidth(s_socFill, (int32_t)lroundf(SOC_FILL_W * fminf(fmaxf(d.soc, 0), 100) / 100.0f));
            ui::setBgColor(s_socFill, socColor(d.soc));
        } else {
            ui::setText(s_valSoc, "--%");
            ui::setWidth(s_socFill, 0);
        }
        char pw[16];
        ui::formatPower(pw, sizeof(pw), d.batteryW);
        if (fabsf(d.batteryW) < MIN_FLOW_W) ui::setText(s_lblBatt, "Idle");
        else ui::setTextF(s_lblBatt, "%s %s", d.batteryW > 0 ? "Discharging" : "Charging", pw);
        ui::setText(s_lblStatusLine, "");
    }

    // Today card (energy totals; independent of live-data staleness)
    if (ts.valid && ts.selfPoweredPct >= 0) ui::setTextF(s_valSelf, "%d%%", (int)lroundf(ts.selfPoweredPct));
    else ui::setText(s_valSelf, "--");
    if (ts.valid) {
        ui::formatKWh(buf, sizeof(buf), ts.exportKWh, true);
        ui::setText(s_valExp, buf);
    } else {
        ui::setText(s_valExp, "--");
    }

    // Flows: forward = solar->house, house->grid (export), house->battery (charge)
    pathSet(s_paths[P_SOLAR], d.solarW, stale);
    pathSet(s_paths[P_GRID], -d.siteW, stale);
    pathSet(s_paths[P_BATT], -d.batteryW, stale);
}
