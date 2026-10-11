// Screen H: battery & grid health (SOC, charge state, voltage, frequency, efficiency, grid services).
#include "screens.h"
#include "shared_state.h"
#include "ui_common.h"
#include "powerwall.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

constexpr float IDLE_W = 30.0f;
constexpr int BAR_W = 432;
constexpr int BAR_H = 14;
constexpr int BAR_RADIUS = BAR_H / 2;
constexpr int SHEEN_W = 60;
constexpr int CARD_W = 210;
constexpr int INNER_W = CARD_W - 28; // card padding 14 each side

constexpr float V_MIN = ui::MAINS_V_MIN, V_MAX = ui::MAINS_V_MAX;
constexpr float F_MIN = 49.5f, F_MAX = 50.5f;

struct Gauge {
    lv_obj_t *value, *unit, *marker, *status;
};

lv_obj_t *s_cards[4];
lv_obj_t *s_barFill, *s_sheen;
lv_obj_t *s_lblSoc, *s_lblSocUnit, *s_statusDot, *s_lblStatus, *s_lblEstimate;
Gauge s_gVolt, s_gFreq;
lv_obj_t *s_lblEffVal, *s_lblEffUnit, *s_lblEffSub;
lv_obj_t *s_pill, *s_lblPill;
int s_fillWidth = 0;

// Card with caption, value + unit, range track with marker, and "lo / status / hi" labels.
lv_obj_t *gaugeCard(lv_obj_t *scr, int x, int y, Gauge &g, const char *title, const char *unit,
                    const char *loTxt, const char *hiTxt) {
    lv_obj_t *c = ui::card(scr, x, y, CARD_W, 112);
    ui::cardCaption(c, title, 14, 12);
    lv_obj_t *vu = ui::valueUnit(c, &lv_font_montserrat_26, &lv_font_montserrat_14, &g.value, &g.unit, 3);
    lv_obj_set_pos(vu, 14, 33);
    ui::setText(g.unit, unit);

    ui::rect(c, 14, 73, INNER_W, 6, ui::TRACK, 3);
    ui::rect(c, 14, 73, INNER_W, 6, ui::C_OK_BG, 3);
    g.marker = ui::rect(c, 14 + INNER_W / 2 - 2, 69, 4, 14, ui::TEXT, 2);

    lv_obj_t *row = ui::flex(c, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN);
    lv_obj_set_pos(row, 14, 87);
    lv_obj_set_width(row, INNER_W);
    ui::label(row, &lv_font_montserrat_12, ui::MUTED, loTxt);
    g.status = ui::label(row, &lv_font_montserrat_12, ui::C_BATT, "Normal");
    ui::label(row, &lv_font_montserrat_12, ui::MUTED, hiTxt);
    return c;
}

// The sheen sweeps across the charge fill (v: 0..1000 permille of the travel). It has its own rounded
// ends and is clamped to the fill, so the bar needs no clip_corner layer.
void sheenExec(void *, int32_t v) {
    if (lv_obj_has_flag(s_sheen, LV_OBJ_FLAG_HIDDEN)) return;
    int32_t x = -SHEEN_W + static_cast<int32_t>(static_cast<int64_t>(s_fillWidth + SHEEN_W) * v / 1000);
    int32_t x0 = x < 0 ? 0 : x;
    int32_t x1 = x + SHEEN_W > s_fillWidth ? s_fillWidth : x + SHEEN_W;
    ui::setX(s_sheen, x0);
    ui::setWidth(s_sheen, x1 > x0 ? x1 - x0 : 0);
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
    lv_obj_t *scr = ui::screenCreate(ui::SCR_HEALTH);

    // Header: POWERWALL + SOC
    lv_obj_set_pos(ui::caption(scr, ui::C_BATT, "POWERWALL"), 24, 54);
    lv_obj_t *vu = ui::valueUnit(scr, &lv_font_montserrat_48, &lv_font_montserrat_26, &s_lblSoc, &s_lblSocUnit, 6);
    lv_obj_set_pos(vu, 24, 68);
    ui::setText(s_lblSocUnit, "%");

    // Right column: dot + state, estimate underneath, right aligned at x = 456
    lv_obj_t *rc = ui::flex(scr, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
    lv_obj_set_pos(rc, 216, 80);
    lv_obj_set_width(rc, 240);
    lv_obj_t *srow = ui::flex(rc, LV_FLEX_FLOW_ROW, 6, LV_FLEX_ALIGN_END);
    s_statusDot = ui::rect(srow, 0, 0, 8, 8, ui::MUTED, LV_RADIUS_CIRCLE);
    s_lblStatus = ui::label(srow, &lv_font_montserrat_14, ui::MUTED, "No data");
    s_lblEstimate = ui::label(rc, &lv_font_montserrat_20, ui::TEXT, "");
    ui::animPulse(s_statusDot, 1600);

    // Battery bar
    lv_obj_t *bar = ui::rect(scr, 24, 132, BAR_W, BAR_H, ui::TRACK, BAR_RADIUS);
    s_barFill = ui::rect(bar, 0, 0, 0, BAR_H, ui::C_BATT, BAR_RADIUS);
    s_sheen = ui::rect(bar, 0, 0, 0, BAR_H, lv_color_white(), BAR_RADIUS);
    lv_obj_set_style_bg_opa(s_sheen, (lv_opa_t)64, 0);
    ui::setHidden(s_sheen, true);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_sheen);
    lv_anim_set_exec_cb(&a, sheenExec);
    lv_anim_set_values(&a, 0, 1000);
    lv_anim_set_duration(&a, 3000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    // Cards
    s_cards[0] = gaugeCard(scr, 24, 166, s_gVolt, "GRID VOLTAGE", "V", "207", "253");
    s_cards[1] = gaugeCard(scr, 246, 166, s_gFreq, "FREQUENCY", "Hz", "49.5", "50.5");

    s_cards[2] = ui::card(scr, 24, 318, CARD_W, 108);
    ui::cardCaption(s_cards[2], "ROUND-TRIP EFFICIENCY", 14, 12);
    lv_obj_t *evu = ui::valueUnit(s_cards[2], &lv_font_montserrat_26, &lv_font_montserrat_14, &s_lblEffVal,
                                  &s_lblEffUnit, 3);
    lv_obj_set_pos(evu, 14, 33);
    ui::setText(s_lblEffUnit, "%");
    s_lblEffSub = ui::labelAt(s_cards[2], &lv_font_montserrat_12, ui::MUTED, "Lifetime: -- kWh", 14, 73, INNER_W);
    lv_label_set_long_mode(s_lblEffSub, LV_LABEL_LONG_MODE_WRAP);

    s_cards[3] = ui::card(scr, 246, 318, CARD_W, 108);
    ui::cardCaption(s_cards[3], "GRID SERVICES", 14, 12);
    s_pill = ui::rect(s_cards[3], 14, 35, 10, 10, ui::TRACK, 13);
    lv_obj_set_size(s_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(s_pill, 12, 0);
    lv_obj_set_style_pad_ver(s_pill, 5, 0);
    s_lblPill = ui::label(s_pill, &lv_font_montserrat_14, ui::C_WEATHER, "Inactive");
    ui::labelAt(s_cards[3], &lv_font_montserrat_12, ui::MUTED, "Lights up during a VPP event", 14, 73);

    return scr;
}

void healthRefresh() {
    PwData d = powerwallData();
    bool stale = isStale(d.valid, d.updatedMs, PW_STALE_MS);
    char buf[48];

    float soc = d.soc < 0 ? 0 : d.soc > 100 ? 100 : d.soc;
    bool charging = false;

    if (stale || !d.socValid) {
        ui::setText(s_lblSoc, "--");
        ui::setHidden(s_lblSocUnit, true);
        ui::setText(s_lblStatus, stale ? "No data" : "Waiting for SOC");
        ui::setTextColor(s_lblStatus, ui::MUTED);
        ui::setBgColor(s_statusDot, ui::MUTED);
        ui::setText(s_lblEstimate, "--");
        s_fillWidth = 0;
        ui::setWidth(s_barFill, 0);
    } else {
        ui::setTextF(s_lblSoc, "%.1f", soc);
        ui::setHidden(s_lblSocUnit, false);

        float w = fabsf(d.batteryW);
        char pw[16];
        ui::formatPower(pw, sizeof(pw), d.batteryW);
        char est[40];
        if (w < IDLE_W) {
            ui::setText(s_lblStatus, "Idle");
            ui::setTextColor(s_lblStatus, ui::MUTED);
            ui::setBgColor(s_statusDot, ui::MUTED);
            snprintf(est, sizeof(est), "%s", soc >= 99.5f ? "Full" : "Standby");
        } else if (d.batteryW < 0) {
            charging = true;
            ui::setTextF(s_lblStatus, "Charging %s", pw);
            ui::setTextColor(s_lblStatus, ui::C_BATT);
            ui::setBgColor(s_statusDot, ui::C_BATT);
            if (soc >= 99.5f) snprintf(est, sizeof(est), "Full");
            else formatDuration(est, sizeof(est), "Full in ", (100.0f - soc) / 100.0f * PW_CAPACITY_KWH * 1000.0f / w);
        } else {
            ui::setTextF(s_lblStatus, "Discharging %s", pw);
            ui::setTextColor(s_lblStatus, ui::C_SOLAR);
            ui::setBgColor(s_statusDot, ui::C_SOLAR);
            formatDuration(est, sizeof(est), "Lasts ", soc / 100.0f * PW_CAPACITY_KWH * 1000.0f / w);
        }
        ui::setText(s_lblEstimate, est);

        s_fillWidth = (int)lroundf(soc * BAR_W / 100.0f);
        ui::setWidth(s_barFill, s_fillWidth);
    }
    ui::setHidden(s_sheen, !(charging && s_fillWidth > 0));

    gaugeUpdate(s_gVolt, stale, d.voltageV, V_MIN, V_MAX, "%.1f");
    gaugeUpdate(s_gFreq, stale, d.frequencyHz, F_MIN, F_MAX, "%.2f");

    // Round-trip efficiency
    if (stale || d.battImportedWh <= 0) {
        ui::setText(s_lblEffVal, "--");
        ui::setHidden(s_lblEffUnit, true);
        ui::setText(s_lblEffSub, "Lifetime: -- kWh");
    } else {
        ui::setTextF(s_lblEffVal, "%.1f", d.battExportedWh / d.battImportedWh * 100.0);
        ui::setHidden(s_lblEffUnit, false);
        char in[16], out[16];
        ui::formatKWh(in, sizeof(in), d.battImportedWh / 1000.0, false);
        ui::formatKWh(out, sizeof(out), d.battExportedWh / 1000.0, false);
        ui::setTextF(s_lblEffSub, "Lifetime: %s in / %s out kWh", in, out);
    }

    // Grid services
    bool active = !stale && d.gridServicesActive;
    ui::setText(s_lblPill, active ? "Active" : "Inactive");
    ui::setTextColor(s_lblPill, active ? ui::BG : ui::C_WEATHER);
    ui::setBgColor(s_pill, active ? ui::C_BATT : ui::TRACK);
}

void healthOnShow() {
    ui::fadeIn(s_cards, 4, 0, 150);
}
