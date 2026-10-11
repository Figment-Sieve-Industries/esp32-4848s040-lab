// Screen 1 - power flow, design "N" (home pill, bigger bottom pills), 480x480 rotated (USB-C at the bottom):
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
#include "ui_common.h"
#include <Arduino.h>
#include <math.h>

using namespace ui;

namespace {

constexpr float MIN_FLOW_W = 30;
constexpr float CHEVRON_SPACING = 64; // px between chevrons along a path
constexpr int MAX_CHEVRONS = 6;
const lv_color_t CHIP_TEXT = lv_color_hex(0xC9D1D9);
constexpr int BIG_PILL_W = 176, BIG_PILL_H = 76;
// Battery graphic in the Powerwall pill: inner width 176 - 2*3 border - 2*14 padding = 142 =
// body + 2 px gap + 4 px nub. The fill sits inside the body's 2 px border and 2 px padding.
constexpr int BATT_BODY_W = 136;
constexpr int SOC_FILL_W = BATT_BODY_W - 8;

// Battery fill colour by charge level: red < 15% < orange < 30% < yellow < 50% < green
lv_color_t socColor(float soc) {
    if (soc < 15) return lv_color_hex(0xE5484D);
    if (soc < 30) return lv_color_hex(0xFF8A3D);
    if (soc < 50) return lv_color_hex(0xF5D400);
    return lv_color_hex(0x3DDC84);
}

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
lv_obj_t *valSolar, *valHome, *valGrid, *valSoc, *socFill;
lv_obj_t *lblVolt, *lblHomeVa, *lblGridVa, *lblBatt, *valSelf, *valExp;
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

// Rounded pill with a coloured 3 px border and CARD background.
lv_obj_t *pillBox(int x, int y, int w, int h, int radius, lv_color_t color) {
    lv_obj_t *p = box(scr);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_radius(p, radius, 0);
    lv_obj_set_style_bg_color(p, CARD, 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(p, color, 0);
    lv_obj_set_style_border_width(p, 3, 0);
    return p;
}

// Pill: icon left, caption over value (over an optional 12 px sub line). Returns the value label.
// With subOut the pill is the tall 176x76 variant; dy shifts the content vertically.
lv_obj_t *pill(int x, int y, int w, lv_color_t color, Icon icon, const char *cap, lv_obj_t **pillOut = nullptr,
               lv_obj_t **iconOut = nullptr, lv_obj_t **subOut = nullptr, int dy = 0) {
    bool tall = subOut != nullptr;
    lv_obj_t *p = tall ? pillBox(x, y, w, BIG_PILL_H, 24, color) : pillBox(x, y, w, 56, 28, color);
    lv_obj_t *i = iconCreate(p, icon, 28, color);
    lv_obj_align(i, LV_ALIGN_LEFT_MID, 11, dy); // 14 px from the outer edge (3 px border)
    lv_obj_t *col = box(p);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 2, 0);
    lv_obj_align(col, LV_ALIGN_LEFT_MID, 47, dy);
    caption(col, color, cap);
    lv_obj_t *v = label(col, &lv_font_montserrat_20, TEXT, "--");
    if (tall) *subOut = label(col, &lv_font_montserrat_12, CHIP_TEXT, "--");
    if (pillOut) *pillOut = p;
    if (iconOut) *iconOut = i;
    return v;
}

// Card row: label left, value right, both bottom-aligned
lv_obj_t *statRow(lv_obj_t *parent, const lv_font_t *lf, lv_color_t lc, const char *text, const lv_font_t *vf) {
    lv_obj_t *r = box(parent);
    lv_obj_set_width(r, lv_pct(100));
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    label(r, lf, lc, text);
    return label(r, vf, TEXT, "--");
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
    track(85, 258, 122, 4);
    track(85, 260, 4, 102);
    track(273, 258, 122, 4);
    track(391, 260, 4, 102);

    // Flow paths, forward = solar->house, house->grid (export), house->battery (charge)
    pathCreate(paths[P_SOLAR], {162, 92}, {240, 92}, {240, 196}, C_SOLAR);
    pathCreate(paths[P_GRID], {207, 260}, {87, 260}, {87, 360}, C_GRID);
    pathCreate(paths[P_BATT], {273, 260}, {393, 260}, {393, 360}, C_BATT);

    // House
    lv_obj_t *house = iconCreate(scr, Icon::House, 120, C_HOME, 1.4f);
    lv_obj_set_pos(house, 180, 176);

    // Home pill under the house: watts left, divider, voltage and VA/var right
    lv_obj_t *homePill = pillBox(114, 288, 252, 60, 30, C_HOME);
    lv_obj_set_flex_flow(homePill, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(homePill, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(homePill, 15, 0);
    lv_obj_set_style_pad_column(homePill, 10, 0);
    lv_obj_t *hcol = box(homePill);
    lv_obj_set_flex_flow(hcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(hcol, 2, 0);
    caption(hcol, C_HOME, "HOME");
    valHome = label(hcol, &lv_font_montserrat_22, TEXT, "--");
    lv_obj_t *hdiv = box(homePill);
    lv_obj_set_size(hdiv, 1, 36);
    lv_obj_set_style_bg_color(hdiv, TRACK, 0);
    lv_obj_set_style_bg_opa(hdiv, LV_OPA_COVER, 0);
    lv_obj_t *vcol = box(homePill);
    lv_obj_set_flex_flow(vcol, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(vcol, 4, 0);
    lblVolt = label(vcol, &lv_font_montserrat_12, CHIP_TEXT, "--");
    lblHomeVa = label(vcol, &lv_font_montserrat_12, CHIP_TEXT, "--");

    // Today card (left, under the solar pill)
    lv_obj_t *tCard = card(scr, 12, 134, 170, 108);
    lv_obj_set_style_pad_hor(tCard, 14, 0);
    lv_obj_set_style_pad_ver(tCard, 10, 0);
    lv_obj_set_style_pad_row(tCard, 6, 0);
    lv_obj_set_style_border_color(tCard, TRACK, 0);
    lv_obj_set_style_border_width(tCard, 2, 0);
    lv_obj_set_flex_flow(tCard, LV_FLEX_FLOW_COLUMN);
    caption(tCard, MUTED, "TODAY");
    valSelf = statRow(tCard, &lv_font_montserrat_12, C_BATT, "Self-powered", &lv_font_montserrat_22);
    valExp = statRow(tCard, &lv_font_montserrat_12, C_GRID, "Exported", &lv_font_montserrat_18);

    // Pills
    valSolar = pill(12, 64, 150, C_SOLAR, Icon::Sun, "SOLAR");
    valGrid = pill(12, 362, BIG_PILL_W, C_GRID, Icon::Pylon, "GRID", nullptr, nullptr, &lblGridVa);
    // Powerwall pill (design O): caption, a full-width battery filled to the charge level (colour by
    // level) with the % on a dark badge in its centre, then the charge rate on its own line
    lv_obj_t *battPill = pillBox(292, 362, BIG_PILL_W, BIG_PILL_H, 24, C_BATT);
    lv_obj_set_style_pad_hor(battPill, 14, 0);
    lv_obj_set_style_pad_ver(battPill, 6, 0);
    lv_obj_set_flex_flow(battPill, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(battPill, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(battPill, 4, 0);
    caption(battPill, C_BATT, "POWERWALL");
    lv_obj_t *battRow = row(battPill, 2);
    lv_obj_t *body = box(battRow);
    lv_obj_set_size(body, BATT_BODY_W, 24);
    lv_obj_set_style_border_color(body, C_BATT, 0);
    lv_obj_set_style_border_width(body, 2, 0);
    lv_obj_set_style_radius(body, 5, 0);
    lv_obj_set_style_pad_all(body, 2, 0);
    socFill = box(body);
    lv_obj_set_size(socFill, 0, lv_pct(100));
    lv_obj_set_style_radius(socFill, 2, 0);
    lv_obj_set_style_bg_color(socFill, C_BATT, 0);
    lv_obj_set_style_bg_opa(socFill, LV_OPA_COVER, 0);
    // Dark badge keeps the % readable over any fill colour and over the empty part
    valSoc = label(body, &lv_font_montserrat_14, TEXT, "--%");
    lv_obj_set_style_bg_color(valSoc, BG, 0);
    lv_obj_set_style_bg_opa(valSoc, LV_OPA_80, 0);
    lv_obj_set_style_radius(valSoc, 4, 0);
    lv_obj_set_style_pad_hor(valSoc, 6, 0);
    lv_obj_set_style_pad_ver(valSoc, 1, 0);
    lv_obj_align(valSoc, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *nub = box(battRow);
    lv_obj_set_size(nub, 4, 10);
    lv_obj_set_style_radius(nub, 2, 0);
    lv_obj_set_style_bg_color(nub, C_BATT, 0);
    lv_obj_set_style_bg_opa(nub, LV_OPA_COVER, 0);
    lblBatt = label(battPill, &lv_font_montserrat_12, CHIP_TEXT, "--");

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
    TodayStats ts = energyToday();
    WeatherData w = weatherData();
    bool stale = !d.valid || millis() - d.updatedMs > PW_STALE_MS;
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
        setText(lblVolt, "--");
        setText(lblHomeVa, "--");
        setText(lblGridVa, "--");
        setText(lblBatt, "--");
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

        // Home pill: voltage (amber outside the normal range) and VA / var
        if (d.voltageV > 50) {
            snprintf(buf, sizeof(buf), "%.1f V", d.voltageV);
            setText(lblVolt, buf);
            setTextColor(lblVolt, d.voltageV < MAINS_V_MIN || d.voltageV > MAINS_V_MAX ? C_SOLAR : CHIP_TEXT);
        } else {
            setText(lblVolt, "-- V");
        }
        formatVaVar(buf, sizeof(buf), d.loadVA, d.loadVar);
        setText(lblHomeVa, buf);

        // Grid pill VA / var (also shown during an outage)
        formatVaVar(buf, sizeof(buf), d.siteVA, d.siteVar);
        setText(lblGridVa, buf);

        // Powerwall
        if (d.socValid) {
            snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(d.soc));
            setText(valSoc, buf);
            setWidth(socFill, (int32_t)lroundf(SOC_FILL_W * fminf(fmaxf(d.soc, 0), 100) / 100.0f));
            setBgColor(socFill, socColor(d.soc));
        } else {
            setText(valSoc, "--%");
            setWidth(socFill, 0);
        }
        char pw[16];
        formatPower(pw, sizeof(pw), d.batteryW);
        if (fabsf(d.batteryW) < MIN_FLOW_W) snprintf(buf, sizeof(buf), "Idle");
        else snprintf(buf, sizeof(buf), "%s %s", d.batteryW > 0 ? "Discharging" : "Charging", pw);
        setText(lblBatt, buf);
        setText(lblStatusLine, "");
    }

    // Today card (energy totals; independent of live-data staleness)
    if (ts.valid && ts.selfPoweredPct >= 0) {
        snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(ts.selfPoweredPct));
        setText(valSelf, buf);
    } else {
        setText(valSelf, "--");
    }
    if (ts.valid) {
        char kwh[16];
        formatKWh(kwh, sizeof(kwh), ts.exportKWh);
        snprintf(buf, sizeof(buf), "%s kWh", kwh);
        setText(valExp, buf);
    } else {
        setText(valExp, "--");
    }

    // Flows: forward = solar->house, house->grid (export), house->battery (charge)
    pathSet(paths[P_SOLAR], d.solarW, stale);
    pathSet(paths[P_GRID], -d.siteW, stale);
    pathSet(paths[P_BATT], -d.batteryW, stale);
}
