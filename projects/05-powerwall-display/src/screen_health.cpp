// Screen H: battery & grid health (SOC, charge state, voltage, frequency, efficiency, grid services).
#include "screens.h"
#include "ui_common.h"
#include "powerwall.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

constexpr float IDLE_W = 30.0f;
constexpr int BAR_W = 432;
constexpr int BAR_H = 14;
constexpr int SHEEN_W = 60;
constexpr int CARD_W = 210;
constexpr int INNER_W = CARD_W - 28; // card padding 14 each side

constexpr float V_MIN = ui::MAINS_V_MIN, V_MAX = ui::MAINS_V_MAX;
constexpr float F_MIN = 49.5f, F_MAX = 50.5f;

const lv_color_t BAND = lv_color_hex(0x1F3A2B);
const lv_color_t PILL_TXT = lv_color_hex(0xC9D1D9);

struct Gauge {
    lv_obj_t *value, *unit, *marker, *status;
};

lv_obj_t *cards[4];
lv_obj_t *barFill, *sheen;
lv_obj_t *lblSoc, *lblSocUnit, *statusDot, *lblStatus, *lblEstimate;
Gauge gVolt, gFreq;
lv_obj_t *lblEffVal, *lblEffUnit, *lblEffSub;
lv_obj_t *pill, *lblPill;
ui::TopBar topBar;
int fillWidth = 0;

// Horizontal row: big value + small unit, bottom aligned.
void valueRow(lv_obj_t *parent, int x, int y, const lv_font_t *vf, const lv_font_t *uf,
              lv_obj_t *&value, lv_obj_t *&unit, int unitPad) {
    lv_obj_t *row = ui::box(parent);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 4, 0);
    value = ui::label(row, vf, ui::TEXT, "--");
    unit = ui::label(row, uf, ui::MUTED, "");
    lv_obj_set_style_pad_bottom(unit, unitPad, 0);
}

lv_obj_t *plainObj(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, int radius) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// Card with caption, value + unit, range track with marker, and "lo / status / hi" labels.
lv_obj_t *gaugeCard(lv_obj_t *scr, int x, int y, Gauge &g, const char *title, const char *unit,
                    const char *loTxt, const char *hiTxt) {
    lv_obj_t *c = ui::card(scr, x, y, CARD_W, 112);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *cap = ui::caption(c, ui::MUTED, title);
    lv_obj_set_pos(cap, 14, 12);
    valueRow(c, 14, 33, &lv_font_montserrat_26, &lv_font_montserrat_14, g.value, g.unit, 3);
    ui::setText(g.unit, unit);

    plainObj(c, 14, 73, INNER_W, 6, ui::TRACK, 3);
    plainObj(c, 14, 73, INNER_W, 6, BAND, 3);
    g.marker = plainObj(c, 14 + INNER_W / 2 - 2, 69, 4, 14, ui::TEXT, 2);

    lv_obj_t *row = ui::box(c);
    lv_obj_set_pos(row, 14, 87);
    lv_obj_set_size(row, INNER_W, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui::label(row, &lv_font_montserrat_12, ui::MUTED, loTxt);
    g.status = ui::label(row, &lv_font_montserrat_12, ui::C_BATT, "Normal");
    ui::label(row, &lv_font_montserrat_12, ui::MUTED, hiTxt);
    return c;
}

void fmtThousands(char *buf, size_t n, long v) {
    if (v < 0) v = 0;
    if (v >= 1000) snprintf(buf, n, "%ld,%03ld", v / 1000, v % 1000);
    else snprintf(buf, n, "%ld", v);
}

void sheenExec(void *, int32_t v) { // v: 0..1000 permille of the travel
    int32_t x = -SHEEN_W + (int32_t)((int64_t)(fillWidth + SHEEN_W) * v / 1000);
    ui::setX(sheen, x);
}

// Updates one voltage/frequency gauge.
// fmt is only ever a literal with one float conversion; the attribute (arg 0 = not checked against
// the arguments) tells the compiler the non-literal format is deliberate.
__attribute__((format(printf, 6, 0))) void gaugeUpdate(Gauge &g, bool stale, float v, float lo, float hi,
                                                      const char *fmt) {
    if (stale || v <= 0) {
        ui::setText(g.value, "--");
        ui::setHidden(g.unit, true);
        ui::setHidden(g.marker, true);
        ui::setText(g.status, "--");
        ui::setTextColor(g.status, ui::MUTED);
        return;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), fmt, v);
    ui::setText(g.value, buf);
    ui::setHidden(g.unit, false);
    ui::setHidden(g.marker, false);
    float t = (v - lo) / (hi - lo);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    ui::setX(g.marker, 14 + (int)lroundf(t * INNER_W) - 2);
    bool low = v < lo, high = v > hi;
    ui::setText(g.status, low ? "Low" : high ? "High" : "Normal");
    ui::setTextColor(g.status, (low || high) ? ui::C_ALERT : ui::C_BATT);
}

void formatDuration(char *buf, size_t n, const char *prefix, float hours) {
    int mins = (int)lroundf(hours * 60.0f);
    if (mins < 1) mins = 1;
    if (mins > 99 * 60) mins = 99 * 60;
    if (mins < 60) snprintf(buf, n, "%s~%d min", prefix, mins);
    else snprintf(buf, n, "%s~%d h %d min", prefix, mins / 60, mins % 60);
}

} // namespace

lv_obj_t *healthCreate() {
    lv_obj_t *scr = ui::screenCreate();
    topBar = ui::topBarCreate(scr);

    // Header: POWERWALL + SOC
    lv_obj_t *cap = ui::caption(scr, ui::C_BATT, "POWERWALL");
    lv_obj_set_pos(cap, 24, 54);
    valueRow(scr, 24, 68, &lv_font_montserrat_48, &lv_font_montserrat_26, lblSoc, lblSocUnit, 6);
    ui::setText(lblSocUnit, "%");

    // Right column: dot + state, estimate underneath, right aligned at x = 456
    lv_obj_t *rc = ui::box(scr);
    lv_obj_set_pos(rc, 216, 80);
    lv_obj_set_size(rc, 240, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(rc, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(rc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_row(rc, 2, 0);
    lv_obj_t *srow = ui::box(rc);
    lv_obj_set_size(srow, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(srow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(srow, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(srow, 6, 0);
    statusDot = plainObj(srow, 0, 0, 8, 8, ui::MUTED, LV_RADIUS_CIRCLE);
    lblStatus = ui::label(srow, &lv_font_montserrat_14, ui::MUTED, "No data");
    lblEstimate = ui::label(rc, &lv_font_montserrat_20, ui::TEXT, "");
    ui::animPulse(statusDot, 1600);

    // Battery bar
    lv_obj_t *bar = plainObj(scr, 24, 132, BAR_W, BAR_H, ui::TRACK, 7);
    lv_obj_set_style_clip_corner(bar, true, 0);
    barFill = plainObj(bar, 0, 0, 0, BAR_H, ui::C_BATT, 7);
    sheen = plainObj(bar, -SHEEN_W, 0, SHEEN_W, BAR_H, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(sheen, (lv_opa_t)64, 0);
    ui::setHidden(sheen, true);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sheen);
    lv_anim_set_exec_cb(&a, sheenExec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 3000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    // Cards
    cards[0] = gaugeCard(scr, 24, 166, gVolt, "GRID VOLTAGE", "V", "207", "253");
    cards[1] = gaugeCard(scr, 246, 166, gFreq, "FREQUENCY", "Hz", "49.5", "50.5");

    cards[2] = ui::card(scr, 24, 318, CARD_W, 108);
    lv_obj_remove_flag(cards[2], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui::caption(cards[2], ui::MUTED, "ROUND-TRIP EFFICIENCY"), 14, 12);
    valueRow(cards[2], 14, 33, &lv_font_montserrat_26, &lv_font_montserrat_14, lblEffVal, lblEffUnit, 3);
    ui::setText(lblEffUnit, "%");
    lblEffSub = ui::label(cards[2], &lv_font_montserrat_12, ui::MUTED, "Lifetime: -- kWh");
    lv_obj_set_width(lblEffSub, INNER_W);
    lv_label_set_long_mode(lblEffSub, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(lblEffSub, 14, 73);

    cards[3] = ui::card(scr, 246, 318, CARD_W, 108);
    lv_obj_remove_flag(cards[3], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ui::caption(cards[3], ui::MUTED, "GRID SERVICES"), 14, 12);
    pill = plainObj(cards[3], 14, 35, 10, 10, ui::TRACK, 13);
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pill, 12, 0);
    lv_obj_set_style_pad_ver(pill, 5, 0);
    lblPill = ui::label(pill, &lv_font_montserrat_14, PILL_TXT, "Inactive");
    lv_obj_set_pos(ui::label(cards[3], &lv_font_montserrat_12, ui::MUTED, "Lights up during a VPP event"), 14, 73);

    ui::pageDots(scr, 2);
    return scr;
}

void healthRefresh() {
    PwData d = powerwallData();
    bool stale = !d.valid || millis() - d.updatedMs > PW_STALE_MS;
    char buf[48];
    ui::topBarUpdate(topBar);

    float soc = d.soc < 0 ? 0 : d.soc > 100 ? 100 : d.soc;
    bool charging = false;

    if (stale || !d.socValid) {
        ui::setText(lblSoc, "--");
        ui::setHidden(lblSocUnit, true);
        ui::setText(lblStatus, stale ? "No data" : "Waiting for SOC");
        ui::setTextColor(lblStatus, ui::MUTED);
        ui::setBgColor(statusDot, ui::MUTED);
        ui::setText(lblEstimate, "--");
        fillWidth = 0;
        ui::setWidth(barFill, 0);
    } else {
        snprintf(buf, sizeof(buf), "%.1f", soc);
        ui::setText(lblSoc, buf);
        ui::setHidden(lblSocUnit, false);

        float w = fabsf(d.batteryW);
        char pw[16];
        ui::formatPower(pw, sizeof(pw), d.batteryW);
        char est[40];
        if (w < IDLE_W) {
            ui::setText(lblStatus, "Idle");
            ui::setTextColor(lblStatus, ui::MUTED);
            ui::setBgColor(statusDot, ui::MUTED);
            snprintf(est, sizeof(est), "%s", soc >= 99.5f ? "Full" : "Standby");
        } else if (d.batteryW < 0) {
            charging = true;
            snprintf(buf, sizeof(buf), "Charging %s", pw);
            ui::setText(lblStatus, buf);
            ui::setTextColor(lblStatus, ui::C_BATT);
            ui::setBgColor(statusDot, ui::C_BATT);
            if (soc >= 99.5f) snprintf(est, sizeof(est), "Full");
            else formatDuration(est, sizeof(est), "Full in ", (100.0f - soc) / 100.0f * PW_CAPACITY_KWH * 1000.0f / w);
        } else {
            snprintf(buf, sizeof(buf), "Discharging %s", pw);
            ui::setText(lblStatus, buf);
            ui::setTextColor(lblStatus, ui::C_SOLAR);
            ui::setBgColor(statusDot, ui::C_SOLAR);
            formatDuration(est, sizeof(est), "Lasts ", soc / 100.0f * PW_CAPACITY_KWH * 1000.0f / w);
        }
        ui::setText(lblEstimate, est);

        fillWidth = (int)lroundf(soc * BAR_W / 100.0f);
        ui::setWidth(barFill, fillWidth);
    }
    ui::setHidden(sheen, !(charging && fillWidth > 0));

    gaugeUpdate(gVolt, stale, d.voltageV, V_MIN, V_MAX, "%.1f");
    gaugeUpdate(gFreq, stale, d.frequencyHz, F_MIN, F_MAX, "%.2f");

    // Round-trip efficiency
    if (stale || d.battImportedWh <= 0) {
        ui::setText(lblEffVal, "--");
        ui::setHidden(lblEffUnit, true);
        ui::setText(lblEffSub, "Lifetime: -- kWh");
    } else {
        snprintf(buf, sizeof(buf), "%.1f", d.battExportedWh / d.battImportedWh * 100.0);
        ui::setText(lblEffVal, buf);
        ui::setHidden(lblEffUnit, false);
        char in[16], out[16];
        fmtThousands(in, sizeof(in), lroundf((float)(d.battImportedWh / 1000.0)));
        fmtThousands(out, sizeof(out), lroundf((float)(d.battExportedWh / 1000.0)));
        snprintf(buf, sizeof(buf), "Lifetime: %s in / %s out kWh", in, out);
        ui::setText(lblEffSub, buf);
    }

    // Grid services
    bool active = !stale && d.gridServicesActive;
    ui::setText(lblPill, active ? "Active" : "Inactive");
    ui::setTextColor(lblPill, active ? ui::BG : PILL_TXT);
    ui::setBgColor(pill, active ? ui::C_BATT : ui::TRACK);
}

void healthOnShow() {
    for (int i = 0; i < 4; i++) ui::animFadeIn(cards[i], (uint32_t)i * 150);
}
