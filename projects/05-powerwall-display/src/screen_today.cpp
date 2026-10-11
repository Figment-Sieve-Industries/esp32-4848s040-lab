// G: Today's energy - self-powered ring, totals, export/import tiles, solar-by-hour bars.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include "screens.h"
#include "ui_common.h"
#include "ui_chart.h"
#include "icons.h"
#include "energy.h"
#include "time_util.h"

namespace {

constexpr int BAR_COUNT = 24;
constexpr int BAR_W = 14;
constexpr int BAR_STEP = 17;      // 14 + 3 gap
constexpr int BAR_AREA_H = 56;
constexpr int BAR_MAX_H = 52;
constexpr int BAR_MIN_H = 3;
constexpr uint32_t GROW_LOCK_MS = 2000;
constexpr uint32_t PULSE_PERIOD_MS = 2800;
constexpr lv_opa_t PULSE_LOW_OPA = 90;

lv_obj_t *s_scr = nullptr;
lv_obj_t *s_caption = nullptr;
lv_obj_t *s_ring = nullptr;
lv_obj_t *s_pct = nullptr;
lv_obj_t *s_solar = nullptr;
lv_obj_t *s_home = nullptr;
lv_obj_t *s_batt = nullptr;
lv_obj_t *s_export = nullptr;
lv_obj_t *s_import = nullptr;
lv_obj_t *s_rows[3] = {};
lv_obj_t *s_tiles[2] = {};
lv_obj_t *s_chart = nullptr;
int s_barH[BAR_COUNT] = {};          // current bar heights / colours (kept to re-set one bar for the pulse)
lv_color_t s_barCol[BAR_COUNT] = {};

uint32_t s_lockUntil = 0;     // millis() until which refresh leaves heights/arc alone
bool s_locked = false;
int s_pulseHour = -1;
lv_opa_t s_pulseOpa = LV_OPA_COVER;

void setArcValueCb(void *var, int32_t v) { lv_arc_set_value(static_cast<lv_obj_t *>(var), v); }

// Bars are bottom-aligned in the chart; the pulsing bar carries the pulse opacity.
void setBar(int i, int h, lv_color_t col) {
    s_barH[i] = h;
    s_barCol[i] = col;
    ui::barChartSet(s_chart, i, BAR_AREA_H - h, h, col, i == s_pulseHour ? s_pulseOpa : LV_OPA_COVER);
}

// One right-column row: icon + small label over a value. Returns the row; value label via out.
lv_obj_t *makeRow(lv_obj_t *parent, int y, Icon icon, lv_color_t col, const char *title, lv_obj_t **value) {
    lv_obj_t *row = ui::box(parent);
    lv_obj_set_pos(row, 216, y);
    lv_obj_set_size(row, 240, 40);
    lv_obj_t *ic = iconCreate(row, icon, 26, col);
    lv_obj_set_pos(ic, 0, 7);
    ui::labelAt(row, &lv_font_montserrat_12, ui::MUTED, title, 38, 0);
    *value = ui::labelAt(row, &lv_font_montserrat_22, ui::TEXT, "--", 38, 14);
    return row;
}

lv_obj_t *makeTile(lv_obj_t *parent, int x, Icon icon, const char *title, lv_obj_t **value) {
    lv_obj_t *c = ui::card(parent, x, 266, 210, 58);
    lv_obj_t *ic = iconCreate(c, icon, 22, ui::C_GRID);
    lv_obj_set_pos(ic, 14, 18);
    ui::labelAt(c, &lv_font_montserrat_12, ui::MUTED, title, 46, 8);
    *value = ui::labelAt(c, &lv_font_montserrat_20, ui::TEXT, "--", 46, 24);
    return c;
}

int barTarget(const TodayStats &st, int i, float scale) {
    float v = st.valid ? st.hourlySolarKWh[i] : 0.0f;
    if (v <= 0.0f) return BAR_MIN_H;
    int h = static_cast<int>(lroundf(v / scale * BAR_MAX_H));
    if (h < BAR_MIN_H) h = BAR_MIN_H;
    if (h > BAR_MAX_H) h = BAR_MAX_H;
    return h;
}

float barScale(const TodayStats &st) {
    float mx = 0.3f;
    if (st.valid)
        for (int i = 0; i < BAR_COUNT; i++)
            if (st.hourlySolarKWh[i] > mx) mx = st.hourlySolarKWh[i];
    return mx;
}

bool barHasData(const TodayStats &st, int i) { return st.valid && st.hourlySolarKWh[i] > 0.0f; }

int ringValue(const TodayStats &st) {
    if (!st.valid || st.selfPoweredPct < 0) return 0;
    int v = static_cast<int>(lroundf(st.selfPoweredPct));
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

// Current-hour bar pulse: an anim on the chart object that changes that one bar's opacity.
void pulseExec(void *, int32_t v) {
    if (s_pulseHour < 0) return;
    s_pulseOpa = static_cast<lv_opa_t>(v);
    setBar(s_pulseHour, s_barH[s_pulseHour], s_barCol[s_pulseHour]);
}

void pulseStart() {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_chart);
    lv_anim_set_exec_cb(&a, pulseExec);
    lv_anim_set_values(&a, LV_OPA_COVER, PULSE_LOW_OPA);
    lv_anim_set_duration(&a, PULSE_PERIOD_MS / 2);
    lv_anim_set_playback_duration(&a, PULSE_PERIOD_MS / 2);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    ui::animTag(&a);
    lv_anim_start(&a);
}

void updatePulse(const TodayStats &st) {
    int h = (st.valid && st.currentHour >= 0 && st.currentHour < BAR_COUNT) ? st.currentHour : -1;
    if (h == s_pulseHour) return;
    if (s_pulseHour >= 0) {
        int old = s_pulseHour;
        lv_anim_delete(s_chart, pulseExec);
        s_pulseHour = -1;
        s_pulseOpa = LV_OPA_COVER;
        setBar(old, s_barH[old], s_barCol[old]);
    }
    s_pulseHour = h;
    if (h >= 0) pulseStart();
}

void updateTexts(const TodayStats &st) {
    char buf[40], a[24], b[24];

    if (st.partial && st.since > 0) {
        char hhmm[8];
        tu::fmtHHMM(hhmm, sizeof(hhmm), st.since);
        ui::setTextF(s_caption, "TODAY SINCE %s", hhmm);
    } else {
        ui::setText(s_caption, "TODAY SO FAR");
    }

    if (st.valid && st.selfPoweredPct >= 0) ui::setTextF(s_pct, "%d%%", ringValue(st));
    else ui::setText(s_pct, "--");

    if (!st.valid) {
        ui::setText(s_solar, "--");
        ui::setText(s_home, "--");
        ui::setText(s_batt, "--");
        ui::setText(s_export, "--");
        ui::setText(s_import, "--");
        return;
    }
    ui::formatKWh(buf, sizeof(buf), st.solarKWh, true);
    ui::setText(s_solar, buf);
    ui::formatKWh(buf, sizeof(buf), st.homeKWh, true);
    ui::setText(s_home, buf);
    ui::formatKWh(a, sizeof(a), st.battInKWh, false);
    ui::formatKWh(b, sizeof(b), st.battOutKWh, false);
    ui::setTextF(s_batt, "%s / %s kWh", a, b);
    ui::formatKWh(buf, sizeof(buf), st.exportKWh, true);
    ui::setText(s_export, buf);
    ui::formatKWh(buf, sizeof(buf), st.importKWh, true);
    ui::setText(s_import, buf);
}

void updateBars(const TodayStats &st) {
    float scale = barScale(st);
    for (int i = 0; i < BAR_COUNT; i++)
        setBar(i, barTarget(st, i, scale), barHasData(st, i) ? ui::C_SOLAR : ui::TRACK);
}

} // namespace

lv_obj_t *todayCreate() {
    s_scr = ui::screenCreate(ui::SCR_TODAY);

    s_caption = ui::caption(s_scr, ui::MUTED, "TODAY SO FAR");
    lv_obj_set_pos(s_caption, 24, 56);

    // Self-powered ring
    s_ring = lv_arc_create(s_scr);
    lv_obj_set_size(s_ring, 168, 168);
    lv_obj_set_pos(s_ring, 24, 82);
    lv_arc_set_bg_angles(s_ring, 0, 360);
    lv_arc_set_rotation(s_ring, 270);
    lv_arc_set_range(s_ring, 0, 100);
    lv_arc_set_value(s_ring, 0);
    lv_obj_remove_style(s_ring, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ring, 0, 0);
    lv_obj_set_style_pad_all(s_ring, 0, 0);
    lv_obj_set_style_arc_width(s_ring, 14, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_ring, ui::TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(s_ring, false, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_ring, 14, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_ring, ui::C_BATT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_ring, false, LV_PART_INDICATOR);

    s_pct = ui::labelAt(s_scr, &lv_font_montserrat_40, ui::TEXT, "--", 24 + 84 - 60, 82 + 62, 120, LV_TEXT_ALIGN_CENTER);
    ui::labelAt(s_scr, &lv_font_montserrat_12, ui::MUTED, "self-powered", 24 + 84 - 60, 82 + 62 + 48, 120,
                LV_TEXT_ALIGN_CENTER);

    // Right column
    s_rows[0] = makeRow(s_scr, 82, Icon::Sun, ui::C_SOLAR, "Solar generated", &s_solar);
    s_rows[1] = makeRow(s_scr, 132, Icon::House, ui::C_HOME, "Home used", &s_home);
    s_rows[2] = makeRow(s_scr, 182, Icon::Battery, ui::C_BATT, "Powerwall in / out", &s_batt);

    // Export / import tiles
    s_tiles[0] = makeTile(s_scr, 24, Icon::ArrowUp, "Exported", &s_export);
    s_tiles[1] = makeTile(s_scr, 246, Icon::ArrowDown, "Imported", &s_import);

    // Solar by hour
    lv_obj_t *hc = ui::card(s_scr, 24, 342, 432, 96);
    ui::labelAt(hc, &lv_font_montserrat_12, ui::MUTED, "Solar by hour", 14, 10);
    ui::labelAt(hc, &lv_font_montserrat_12, ui::MUTED, "00 / 06 / 12 / 18", 14, 10, 404, LV_TEXT_ALIGN_RIGHT);

    s_chart = ui::barChartCreate(hc, 14, 30, 404, BAR_AREA_H, BAR_COUNT, BAR_W, BAR_STEP, 2);
    for (int i = 0; i < BAR_COUNT; i++) setBar(i, BAR_MIN_H, ui::TRACK);
    return s_scr;
}

void todayRefresh() {
    TodayStats st = energyToday();
    updateTexts(st);
    updatePulse(st);

    if (s_locked && static_cast<int32_t>(millis() - s_lockUntil) >= 0) s_locked = false;
    if (s_locked) return;

    if (lv_arc_get_value(s_ring) != ringValue(st)) lv_arc_set_value(s_ring, ringValue(st));
    updateBars(st);
}

void todayOnShow() {
    TodayStats st = energyToday();
    updateTexts(st);
    updatePulse(st);

    s_locked = true;
    s_lockUntil = millis() + GROW_LOCK_MS;

    // Ring sweep
    lv_arc_set_value(s_ring, 0);
    ui::animValue(s_ring, setArcValueCb, 0, ringValue(st), 1200, 150);

    // Bars grow from the baseline, staggered
    updateBars(st);
    ui::barChartGrow(s_chart, 500, 300, 30);

    // Rows and tiles fade in, staggered
    ui::fadeIn(s_rows, 3, 100, 150);
    ui::fadeIn(s_tiles, 2, 550, 100);
}
