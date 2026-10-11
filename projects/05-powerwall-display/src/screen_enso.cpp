// S: El Nino (ENSO) - CPC status banner, weekly Nino 3.4 with diverging bar, ONI / RONI pills,
// 12-month Nino 3.4 history bars, odds strip. Geometry follows the "S El Nino (ENSO)" board of the
// design canvas (480x480). No data credits here: they live on the About screen.
#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "ui_common.h"
#include "ui_chart.h"
#include "shared_state.h"
#include "enso.h"

namespace {

constexpr int HIST_BARS = 12;
constexpr int HIST_STEP = 34;
constexpr int HIST_BAR_W = 26;
constexpr int HIST_W = 404;
constexpr int HIST_H = 46;

constexpr int GRAD_W = 254;           // diverging bar, -3 .. +4 degC
constexpr float GRAD_LO = -3.0f;
constexpr float GRAD_HI = 4.0f;

constexpr int ODDS_W = 124;
constexpr int ODDS_STEP = 140;

const lv_color_t C_NEUTRAL_BAR = lv_color_hex(0x5B6775);

// Banner
ui::Banner s_banner;
lv_obj_t *s_therm[2] = {};

// Weekly card
lv_obj_t *s_weekCard = nullptr;
lv_obj_t *s_weekCap = nullptr;
lv_obj_t *s_weekVal = nullptr;
lv_obj_t *s_marker = nullptr;

// Pills
lv_obj_t *s_pillCard[2] = {};
lv_obj_t *s_pillCap[2] = {};
lv_obj_t *s_pillVal[2] = {};

// History
lv_obj_t *s_histCard = nullptr;
lv_obj_t *s_zeroLine = nullptr;
lv_obj_t *s_thrLine = nullptr;
lv_obj_t *s_bars = nullptr;
lv_obj_t *s_mon[HIST_BARS] = {};

// Odds
lv_obj_t *s_oddsCard = nullptr;
lv_obj_t *s_oddsCap = nullptr;
lv_obj_t *s_next = nullptr;
lv_obj_t *s_oddSlot[EnsoData::PROBS] = {};
lv_obj_t *s_oddSeason[EnsoData::PROBS] = {};
lv_obj_t *s_oddVal[EnsoData::PROBS] = {};
ui::HBar s_oddBar[EnsoData::PROBS];

// Change detection (refresh runs every 500 ms; only touch LVGL when something changed)
struct Key {
    uint32_t fetchedMs = 0;
    bool stale = true;
    bool valid = false;
    bool operator==(const Key &) const = default;
};
ui::ChangeGate<Key> s_gate;

struct Look {
    lv_color_t fg, bg;
    const char *fallbackName;
};

Look alertLook(EnsoAlert a) {
    switch (a) {
        case EnsoAlert::ElNinoAdvisory: return {ui::C_ORANGE, lv_color_hex(0x2B1D0A), "El Nino Advisory"};
        case EnsoAlert::ElNinoWatch: return {ui::C_SOLAR, lv_color_hex(0x26240A), "El Nino Watch"};
        case EnsoAlert::LaNinaAdvisory: return {ui::C_BLUE, lv_color_hex(0x0F1B33), "La Nina Advisory"};
        case EnsoAlert::LaNinaWatch: return {lv_color_hex(0x5EA0F7), lv_color_hex(0x0F1B33), "La Nina Watch"};
        case EnsoAlert::FinalAdvisory: return {ui::MUTED, ui::CARD, "Final Advisory"};
        case EnsoAlert::None: return {ui::MUTED, ui::CARD, "ENSO-neutral"};
        default: return {ui::MUTED, ui::CARD, "--"};
    }
}

// Colour of an anomaly value (degC); callers show "--" / muted themselves for missing or stale data.
lv_color_t valueColor(float v) {
    if (v >= 2.0f) return ui::C_RED;
    if (v >= 0.5f) return ui::C_ORANGE;
    if (v <= -0.5f) return ui::C_BLUE;
    return ui::TEXT;
}

lv_color_t barColor(float v, bool stale) {
    if (stale) return ui::DIM_BAR;
    if (v >= 2.0f) return ui::C_RED;
    if (v >= 1.0f) return ui::C_ORANGE;
    if (v >= 0.5f) return ui::C_SOLAR;
    if (v <= -0.5f) return ui::C_BLUE;
    return C_NEUTRAL_BAR;
}

// One horizontal gradient segment of the diverging bar
void gradSeg(lv_obj_t *parent, int x, int w, lv_color_t c1, lv_color_t c2) {
    lv_obj_t *o = ui::rect(parent, x, 0, w, 12, c1);
    if (!lv_color_eq(c1, c2)) {
        lv_obj_set_style_bg_grad_color(o, c2, 0);
        lv_obj_set_style_bg_grad_dir(o, LV_GRAD_DIR_HOR, 0);
    }
}

int gradX(float v) {
    if (v < GRAD_LO) v = GRAD_LO;
    if (v > GRAD_HI) v = GRAD_HI;
    return static_cast<int>(lroundf((v - GRAD_LO) / (GRAD_HI - GRAD_LO) * GRAD_W));
}

// "30SEP2026" -> "30 SEP"
void shortDate(const char *src, char *out, size_t n) {
    if (strlen(src) >= 5 && isdigit(static_cast<unsigned char>(src[0])) && isdigit(static_cast<unsigned char>(src[1]))) {
        if (n >= 7) {
            out[0] = src[0];
            out[1] = src[1];
            out[2] = ' ';
            out[3] = src[2];
            out[4] = src[3];
            out[5] = src[4];
            out[6] = 0;
            return;
        }
    }
    out[0] = 0;
}

// ---- updates ---------------------------------------------------------------------------------

void updateBanner(const EnsoData &d, bool stale) {
    const EnsoAlert a = (stale && !d.valid) ? EnsoAlert::Unknown : d.alert;
    const Look lk = alertLook(a);
    const char *name = lk.fallbackName;
    if (a != EnsoAlert::Unknown && d.alertText[0]) name = d.alertText;
    ui::bannerSet(s_banner, lk.fg, lk.bg, name, a == EnsoAlert::Unknown || stale, a == EnsoAlert::Unknown ? "" : d.synopsis,
                  20, &lv_font_montserrat_26, &lv_font_montserrat_20);
    for (int i = 0; i < 2; i++) ui::setBgColor(s_therm[i], lk.fg);
}

void updateWeekly(const EnsoData &d, bool stale) {
    char date[8];
    shortDate(d.weeklyDate, date, sizeof(date));
    if (date[0]) ui::setTextF(s_weekCap, "NINO 3.4, WEEKLY ANOMALY (%s)", date);
    else ui::setText(s_weekCap, "NINO 3.4, WEEKLY ANOMALY");

    const bool have = !isnan(d.weeklyAnom);
    ui::setValueF(s_weekVal, have, stale, valueColor(d.weeklyAnom), "%+.1f", static_cast<double>(d.weeklyAnom));

    const bool show = have && !stale;
    ui::setHidden(s_marker, !show);
    if (show) ui::setX(s_marker, 164 + gradX(d.weeklyAnom) - 6);
}

void updatePill(int i, const char *name, float v, const char *season, bool stale) {
    if (season[0]) ui::setTextF(s_pillCap[i], "%s (%s)", name, season);
    else ui::setText(s_pillCap[i], name);
    ui::setValueF(s_pillVal[i], !isnan(v), stale, valueColor(v), "%+.2f", static_cast<double>(v));
}

void updateHistory(const EnsoData &d, bool stale) {
    int n = d.monthCount;
    if (n > HIST_BARS) n = HIST_BARS;
    if (n < 0) n = 0;
    const float *vals = d.monthAnom + (d.monthCount - n);
    int first = HIST_BARS - n;

    float hi = 1.0f, lo = -0.6f;
    for (int k = 0; k < n; k++) {
        if (isnan(vals[k])) continue;
        if (vals[k] > hi) hi = vals[k];
        if (vals[k] < lo) lo = vals[k];
    }
    const float ppu = static_cast<float>(HIST_H) / (hi - lo);
    const int zeroTop = static_cast<int>(lroundf(hi * ppu));
    const int thrTop = static_cast<int>(lroundf((hi - 0.5f) * ppu));
    lv_obj_set_y(s_zeroLine, zeroTop);
    lv_obj_set_y(s_thrLine, thrTop);

    static const char INITIALS[] = "JFMAMJJASOND";
    for (int i = 0; i < HIST_BARS; i++) {
        int k = i - first;
        bool has = k >= 0 && !isnan(vals[k < 0 ? 0 : k]);
        bool hasMon = k >= 0 && d.lastMonth >= 1 && d.lastMonth <= 12;
        ui::setHidden(s_mon[i], !hasMon);
        if (hasMon) {
            int m = ((d.lastMonth - 1 - (n - 1 - k)) % 12 + 12) % 12;
            char b[2] = {INITIALS[m], 0};
            ui::setText(s_mon[i], b);
            ui::setTextColor(s_mon[i], (m == 0) ? ui::TEXT : ui::MUTED); // January stands out
        }
        if (!has) {
            ui::barChartSet(s_bars, i, 0, 0, C_NEUTRAL_BAR); // hidden
            continue;
        }
        float v = vals[k];
        int h = static_cast<int>(lroundf(fabsf(v) * ppu));
        if (h < 2) h = 2;
        int top = v >= 0 ? zeroTop - h : zeroTop;
        if (top < 0) top = 0;
        if (top + h > HIST_H) h = HIST_H - top;
        ui::barChartSet(s_bars, i, top, h, barColor(v, stale));
    }
}

void updateOdds(const EnsoData &d, bool stale) {
    if (d.probWhat[0]) {
        char buf[40];
        size_t j = snprintf(buf, sizeof(buf), "CHANCE OF ");
        for (size_t i = 0; d.probWhat[i] && j + 1 < sizeof(buf); i++)
            buf[j++] = static_cast<char>(toupper(static_cast<unsigned char>(d.probWhat[i])));
        buf[j] = 0;
        ui::setText(s_oddsCap, buf);
    } else {
        ui::setText(s_oddsCap, "PROBABILITY");
    }
    if (d.nextUpdate[0]) ui::setTextF(s_next, "next update %s", d.nextUpdate);
    else ui::setText(s_next, "");

    const bool cool = d.alert == EnsoAlert::LaNinaWatch || d.alert == EnsoAlert::LaNinaAdvisory;
    for (int i = 0; i < EnsoData::PROBS; i++) {
        bool show = i < d.probCount;
        ui::setHidden(s_oddSlot[i], !show);
        if (!show) continue;
        const int pct{d.probPct[i]}; // -1 = unknown
        ui::setText(s_oddSeason[i], d.probSeason[i]);
        ui::setValueF(s_oddVal[i], pct >= 0, stale, ui::TEXT, "%d%%", pct);
        const int w = pct < 0 ? 0 : (pct > 100 ? ODDS_W : pct * ODDS_W / 100);
        ui::hbarSet(s_oddBar[i], static_cast<float>(w) / ODDS_W, stale ? ui::DIM_BAR : (cool ? ui::C_BLUE : ui::C_ORANGE));
    }
}

void update(bool force) {
    const EnsoData d = ensoData();
    const bool stale = isStale(d.valid, d.fetchedMs, ENSO_STALE_MS);
    Key key;
    key.fetchedMs = d.fetchedMs;
    key.stale = stale;
    key.valid = d.valid;
    if (!s_gate.changed(key, force)) return;

    updateBanner(d, stale);
    updateWeekly(d, stale);
    updatePill(0, "ONI", d.oni, d.oniSeason, stale);
    updatePill(1, "RONI", d.roni, d.roniSeason, stale);
    updateHistory(d, stale);
    updateOdds(d, stale);
}

} // namespace

lv_obj_t *ensoCreate() {
    lv_obj_t *scr = ui::screenCreate(ui::SCR_ENSO);

    // ---- Status banner ----
    s_banner = ui::bannerCreate(scr, 52, 78, "ENSO STATUS", 350);
    // Thermometer glyph: stem + bulb
    s_therm[0] = ui::rect(s_banner.ring, 13, 4, 6, 15, ui::MUTED, 3);
    s_therm[1] = ui::rect(s_banner.ring, 10, 15, 12, 12, ui::MUTED, LV_RADIUS_CIRCLE);
    lv_label_set_long_mode(s_banner.sub, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);          // CPC synopsis, ~160 chars
    lv_obj_set_style_anim_duration(s_banner.sub, lv_anim_speed(20), 0); // px/s; LVGL's default 40 is hard to read

    // ---- Weekly Nino 3.4 ----
    s_weekCard = ui::card(scr, 24, 138, 432, 80);
    s_weekCap = ui::cardCaption(s_weekCard, "NINO 3.4, WEEKLY ANOMALY", 14, 8);

    lv_obj_t *unit = nullptr;
    lv_obj_t *vrow = ui::valueUnit(s_weekCard, &lv_font_montserrat_48, &lv_font_montserrat_20, &s_weekVal, &unit, 0);
    lv_obj_set_pos(vrow, 14, 22);
    ui::setText(s_weekVal, "--");
    ui::setTextColor(s_weekVal, ui::MUTED);
    ui::setText(unit, "\xC2\xB0" "C");
    ui::setTextColor(unit, ui::MUTED);

    // Diverging bar -3..+4 degC: blue -> neutral at 0 -> orange (+3) -> red (+4)
    lv_obj_t *bar = ui::box(s_weekCard);
    lv_obj_set_size(bar, GRAD_W, 12);
    lv_obj_set_pos(bar, 164, 48);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_clip_corner(bar, true, 0);
    gradSeg(bar, 0, 91, ui::C_BLUE, ui::TRACK);
    gradSeg(bar, 91, 36, ui::TRACK, ui::TRACK);
    gradSeg(bar, 127, 64, ui::TRACK, ui::C_ORANGE);
    gradSeg(bar, 191, 63, ui::C_ORANGE, ui::C_RED);

    static const lv_point_precise_t TRI[3] = {{1, 1}, {7, 9}, {13, 1}};
    s_marker = lv_line_create(s_weekCard);
    lv_line_set_points(s_marker, TRI, 3);
    lv_obj_set_style_line_width(s_marker, 3, 0);
    lv_obj_set_style_line_color(s_marker, ui::TEXT, 0);
    lv_obj_set_style_line_rounded(s_marker, true, 0);
    lv_obj_set_pos(s_marker, 164 + gradX(0.0f) - 6, 34);
    lv_obj_add_flag(s_marker, LV_OBJ_FLAG_HIDDEN);

    struct Tick { const char *txt; int center; };
    static const Tick TICKS[5] = {{"-3", 0}, {"0", 109}, {"+0.5", 127}, {"+3", 218}, {"+4", GRAD_W}};
    for (int i = 0; i < 5; i++) {
        int x = TICKS[i].center - 20;
        lv_text_align_t al = LV_TEXT_ALIGN_CENTER;
        if (i == 0) { x = 0; al = LV_TEXT_ALIGN_LEFT; }
        if (i == 4) { x = GRAD_W - 40; al = LV_TEXT_ALIGN_RIGHT; }
        ui::labelAt(s_weekCard, &lv_font_montserrat_12, ui::MUTED, TICKS[i].txt, 164 + x, 62, 40, al);
    }

    // ---- ONI / RONI pills (no SOI in the data: two wider pills) ----
    static const char *const PILL_CAP[2] = {"ONI", "RONI"};
    for (int i = 0; i < 2; i++) {
        s_pillCard[i] = ui::card(scr, 24 + i * 222, 226, 210, 56);
        s_pillCap[i] = ui::cardCaption(s_pillCard[i], PILL_CAP[i], 12, 6);
        s_pillVal[i] = ui::labelAt(s_pillCard[i], &lv_font_montserrat_26, ui::MUTED, "--", 12, 22);
    }

    // ---- Monthly history ----
    s_histCard = ui::card(scr, 24, 290, 432, 94);
    ui::cardCaption(s_histCard, "NINO 3.4, LAST 12 MONTHS", 14, 8);
    lv_obj_t *hl = ui::label(s_histCard, &lv_font_montserrat_12, ui::MUTED, "dashed = +0.5 El Nino threshold");
    lv_obj_align(hl, LV_ALIGN_TOP_RIGHT, -14, 8);

    lv_obj_t *plot = ui::box(s_histCard);
    lv_obj_set_pos(plot, 14, 26);
    lv_obj_set_size(plot, HIST_W, HIST_H);
    s_zeroLine = ui::rect(plot, 0, 30, HIST_W, 1, ui::TRACK);
    static const lv_point_precise_t DASH[2] = {{0, 0}, {HIST_W - 1, 0}};
    s_thrLine = lv_line_create(plot);
    lv_line_set_points(s_thrLine, DASH, 2);
    lv_obj_set_style_line_width(s_thrLine, 1, 0);
    lv_obj_set_style_line_color(s_thrLine, ui::MUTED, 0);
    lv_obj_set_style_line_opa(s_thrLine, LV_OPA_70, 0);
    lv_obj_set_style_line_dash_width(s_thrLine, 4, 0);
    lv_obj_set_style_line_dash_gap(s_thrLine, 3, 0);
    lv_obj_set_pos(s_thrLine, 0, 20);
    s_bars = ui::barChartCreate(plot, 0, 0, HIST_W, HIST_H, HIST_BARS, HIST_BAR_W, HIST_STEP, 3);
    for (int i = 0; i < HIST_BARS; i++) {
        s_mon[i] = ui::labelAt(s_histCard, &lv_font_montserrat_12, ui::MUTED, "", 14 + i * HIST_STEP, 26 + HIST_H + 2,
                               HIST_BAR_W, LV_TEXT_ALIGN_CENTER);
        lv_obj_add_flag(s_mon[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Odds + next update ----
    s_oddsCard = ui::card(scr, 24, 392, 432, 52);
    s_oddsCap = ui::cardCaption(s_oddsCard, "PROBABILITY", 14, 6);
    s_next = ui::label(s_oddsCard, &lv_font_montserrat_12, ui::TEXT, "");
    lv_obj_align(s_next, LV_ALIGN_TOP_RIGHT, -14, 6);
    for (int i = 0; i < EnsoData::PROBS; i++) {
        lv_obj_t *slot = ui::box(s_oddsCard);
        lv_obj_set_size(slot, ODDS_W, 24);
        lv_obj_set_pos(slot, 14 + i * ODDS_STEP, 24);
        lv_obj_add_flag(slot, LV_OBJ_FLAG_HIDDEN);
        s_oddSlot[i] = slot;
        s_oddSeason[i] = ui::labelAt(slot, &lv_font_montserrat_14, ui::MUTED, "", 0, 0);
        s_oddVal[i] = ui::labelAt(slot, &lv_font_montserrat_14, ui::TEXT, "--", ODDS_W - 70, 0, 70, LV_TEXT_ALIGN_RIGHT);
        s_oddBar[i] = ui::hbar(slot, 0, 19, ODDS_W, 4, ui::TRACK, ui::C_ORANGE, 2);
        ui::hbarSet(s_oddBar[i], 0.0f, ui::C_ORANGE);
    }

    return scr;
}

void ensoRefresh() { update(false); }

void ensoOnShow() {
    lv_obj_t *const cards[6] = {s_banner.card, s_weekCard, s_pillCard[0], s_pillCard[1], s_histCard, s_oddsCard};
    ui::fadeIn(cards, 6, 80, 100);
}
