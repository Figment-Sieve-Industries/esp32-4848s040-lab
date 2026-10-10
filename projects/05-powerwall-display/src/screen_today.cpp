// G: Today's energy - self-powered ring, totals, export/import tiles, solar-by-hour bars.
#include <Arduino.h>
#include <stdio.h>
#include <time.h>
#include "screens.h"
#include "ui_common.h"
#include "icons.h"
#include "energy.h"

namespace {

constexpr int BAR_COUNT = 24;
constexpr int BAR_W = 14;
constexpr int BAR_STEP = 17;      // 14 + 3 gap
constexpr int BAR_AREA_H = 56;
constexpr int BAR_MAX_H = 52;
constexpr int BAR_MIN_H = 3;
constexpr uint32_t GROW_LOCK_MS = 2000;

lv_obj_t *s_scr = nullptr;
ui::TopBar s_top;
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
lv_obj_t *s_bars[BAR_COUNT] = {};

uint32_t s_lockUntil = 0;     // millis() until which refresh leaves heights/arc alone
bool s_locked = false;
int s_pulseHour = -1;

void setArcValueCb(void *var, int32_t v) { lv_arc_set_value(static_cast<lv_obj_t *>(var), v); }
void setHeightCb(void *var, int32_t v) { lv_obj_set_height(static_cast<lv_obj_t *>(var), v); }

void kwhText(char *buf, size_t n, double kwh) {
    char num[24];
    ui::formatKWh(num, sizeof(num), kwh);
    snprintf(buf, n, "%s kWh", num);
}

// One right-column row: icon + small label over a value. Returns the row; value label via out.
lv_obj_t *makeRow(lv_obj_t *parent, int y, Icon icon, lv_color_t col, const char *title, lv_obj_t **value) {
    lv_obj_t *row = ui::box(parent);
    lv_obj_set_pos(row, 216, y);
    lv_obj_set_size(row, 240, 40);
    lv_obj_t *ic = iconCreate(row, icon, 26, col);
    lv_obj_set_pos(ic, 0, 7);
    lv_obj_t *t = ui::label(row, &lv_font_montserrat_12, ui::MUTED, title);
    lv_obj_set_pos(t, 38, 0);
    *value = ui::label(row, &lv_font_montserrat_22, ui::TEXT, "--");
    lv_obj_set_pos(*value, 38, 14);
    return row;
}

lv_obj_t *makeTile(lv_obj_t *parent, int x, Icon icon, const char *title, lv_obj_t **value) {
    lv_obj_t *c = ui::card(parent, x, 266, 210, 58);
    lv_obj_t *ic = iconCreate(c, icon, 22, ui::C_GRID);
    lv_obj_set_pos(ic, 14, 18);
    lv_obj_t *t = ui::label(c, &lv_font_montserrat_12, ui::MUTED, title);
    lv_obj_set_pos(t, 46, 8);
    *value = ui::label(c, &lv_font_montserrat_20, ui::TEXT, "--");
    lv_obj_set_pos(*value, 46, 24);
    return c;
}

int barTarget(const TodayStats &st, int i, float scale) {
    float v = st.valid ? st.hourlySolarKWh[i] : 0.0f;
    if (v <= 0.0f) return BAR_MIN_H;
    int h = static_cast<int>(v / scale * BAR_MAX_H + 0.5f);
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
    int v = static_cast<int>(st.selfPoweredPct + 0.5f);
    return v < 0 ? 0 : (v > 100 ? 100 : v);
}

void updatePulse(const TodayStats &st) {
    int h = (st.valid && st.currentHour >= 0 && st.currentHour < BAR_COUNT) ? st.currentHour : -1;
    if (h == s_pulseHour) return;
    if (s_pulseHour >= 0) {
        lv_anim_delete(s_bars[s_pulseHour], nullptr);
        lv_obj_set_style_bg_opa(s_bars[s_pulseHour], LV_OPA_COVER, 0);
    }
    s_pulseHour = h;
    if (h >= 0) ui::animPulse(s_bars[h], 2800);
}

void updateTexts(const TodayStats &st) {
    char buf[40], a[24], b[24];

    if (st.partial && st.since > 0) {
        struct tm tmv;
        time_t t = st.since;
        localtime_r(&t, &tmv);
        snprintf(buf, sizeof(buf), "TODAY SINCE %02d:%02d", tmv.tm_hour, tmv.tm_min);
    } else {
        snprintf(buf, sizeof(buf), "TODAY SO FAR");
    }
    ui::setText(s_caption, buf);

    if (st.valid && st.selfPoweredPct >= 0) snprintf(buf, sizeof(buf), "%d%%", ringValue(st));
    else snprintf(buf, sizeof(buf), "--");
    ui::setText(s_pct, buf);

    if (!st.valid) {
        ui::setText(s_solar, "--");
        ui::setText(s_home, "--");
        ui::setText(s_batt, "--");
        ui::setText(s_export, "--");
        ui::setText(s_import, "--");
        return;
    }
    kwhText(buf, sizeof(buf), st.solarKWh);
    ui::setText(s_solar, buf);
    kwhText(buf, sizeof(buf), st.homeKWh);
    ui::setText(s_home, buf);
    ui::formatKWh(a, sizeof(a), st.battInKWh);
    ui::formatKWh(b, sizeof(b), st.battOutKWh);
    snprintf(buf, sizeof(buf), "%s / %s kWh", a, b);
    ui::setText(s_batt, buf);
    kwhText(buf, sizeof(buf), st.exportKWh);
    ui::setText(s_export, buf);
    kwhText(buf, sizeof(buf), st.importKWh);
    ui::setText(s_import, buf);
}

void updateBars(const TodayStats &st) {
    float scale = barScale(st);
    for (int i = 0; i < BAR_COUNT; i++) {
        ui::setBgColor(s_bars[i], barHasData(st, i) ? ui::C_SOLAR : ui::TRACK);
        ui::setHeight(s_bars[i], barTarget(st, i, scale));
    }
}

} // namespace

lv_obj_t *todayCreate() {
    s_scr = ui::screenCreate();
    s_top = ui::topBarCreate(s_scr);

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

    s_pct = ui::label(s_scr, &lv_font_montserrat_40, ui::TEXT, "--");
    lv_obj_t *sp = ui::label(s_scr, &lv_font_montserrat_12, ui::MUTED, "self-powered");
    lv_obj_set_style_text_align(s_pct, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_align(sp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_pct, 120);
    lv_obj_set_width(sp, 120);
    lv_obj_set_pos(s_pct, 24 + 84 - 60, 82 + 62);
    lv_obj_set_pos(sp, 24 + 84 - 60, 82 + 62 + 48);

    // Right column
    s_rows[0] = makeRow(s_scr, 82, Icon::Sun, ui::C_SOLAR, "Solar generated", &s_solar);
    s_rows[1] = makeRow(s_scr, 132, Icon::House, ui::C_HOME, "Home used", &s_home);
    s_rows[2] = makeRow(s_scr, 182, Icon::Battery, ui::C_BATT, "Powerwall in / out", &s_batt);

    // Export / import tiles
    s_tiles[0] = makeTile(s_scr, 24, Icon::ArrowUp, "Exported", &s_export);
    s_tiles[1] = makeTile(s_scr, 246, Icon::ArrowDown, "Imported", &s_import);

    // Solar by hour
    lv_obj_t *hc = ui::card(s_scr, 24, 342, 432, 96);
    lv_obj_t *hl = ui::label(hc, &lv_font_montserrat_12, ui::MUTED, "Solar by hour");
    lv_obj_set_pos(hl, 14, 10);
    lv_obj_t *hr = ui::label(hc, &lv_font_montserrat_12, ui::MUTED, "00 / 06 / 12 / 18");
    lv_obj_align(hr, LV_ALIGN_TOP_RIGHT, -14, 10);

    lv_obj_t *area = ui::box(hc);
    lv_obj_set_pos(area, 14, 30);
    lv_obj_set_size(area, 404, BAR_AREA_H);
    for (int i = 0; i < BAR_COUNT; i++) {
        lv_obj_t *b = ui::box(area);
        lv_obj_set_size(b, BAR_W, BAR_MIN_H);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * BAR_STEP, 0);
        lv_obj_set_style_bg_color(b, ui::TRACK, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 2, 0);
        s_bars[i] = b;
    }

    ui::pageDots(s_scr, 1);
    return s_scr;
}

void todayRefresh() {
    if (!s_scr) return;
    ui::topBarUpdate(s_top);
    TodayStats st = energyToday();
    updateTexts(st);
    updatePulse(st);

    if (s_locked && static_cast<int32_t>(millis() - s_lockUntil) >= 0) s_locked = false;
    if (s_locked) return;

    if (lv_arc_get_value(s_ring) != ringValue(st)) lv_arc_set_value(s_ring, ringValue(st));
    updateBars(st);
}

void todayOnShow() {
    if (!s_scr) return;
    TodayStats st = energyToday();
    updateTexts(st);
    updatePulse(st);

    s_locked = true;
    s_lockUntil = millis() + GROW_LOCK_MS;

    // Ring sweep
    lv_arc_set_value(s_ring, 0);
    ui::animValue(s_ring, setArcValueCb, 0, ringValue(st), 1200, 150);

    // Bars grow, staggered
    float scale = barScale(st);
    for (int i = 0; i < BAR_COUNT; i++) {
        ui::setBgColor(s_bars[i], barHasData(st, i) ? ui::C_SOLAR : ui::TRACK);
        lv_obj_set_height(s_bars[i], 0);
        ui::animValue(s_bars[i], setHeightCb, 0, barTarget(st, i, scale), 500, 300 + i * 30);
    }

    // Rows and tiles fade in, staggered
    for (int i = 0; i < 3; i++) {
        lv_obj_set_style_opa(s_rows[i], LV_OPA_TRANSP, 0);
        ui::animFadeIn(s_rows[i], 100 + i * 150);
    }
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_opa(s_tiles[i], LV_OPA_TRANSP, 0);
        ui::animFadeIn(s_tiles[i], 550 + i * 100);
    }
}
