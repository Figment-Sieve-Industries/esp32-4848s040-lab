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

const uint32_t C_ORANGE = 0xFF8A3D;
const uint32_t C_RED = 0xE5484D;
const uint32_t C_AMBER = 0xF5B700;
const uint32_t C_BLUE = 0x3B82F6;
const uint32_t C_NEUTRAL_BAR = 0x5B6775;
const uint32_t C_DIM_BAR = 0x3A4654;

lv_obj_t *s_scr = nullptr;
ui::TopBar s_top;

// Banner
lv_obj_t *s_banner = nullptr;
lv_obj_t *s_ring = nullptr;
lv_obj_t *s_therm[2] = {};
lv_obj_t *s_statusCap = nullptr;
lv_obj_t *s_name = nullptr;
lv_obj_t *s_syn = nullptr;

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
lv_obj_t *s_bars[HIST_BARS] = {};
lv_obj_t *s_mon[HIST_BARS] = {};

// Odds
lv_obj_t *s_oddsCard = nullptr;
lv_obj_t *s_oddsCap = nullptr;
lv_obj_t *s_next = nullptr;
lv_obj_t *s_oddSlot[EnsoData::PROBS] = {};
lv_obj_t *s_oddSeason[EnsoData::PROBS] = {};
lv_obj_t *s_oddVal[EnsoData::PROBS] = {};
lv_obj_t *s_oddFill[EnsoData::PROBS] = {};

// Change detection (refresh runs every 500 ms; only touch LVGL when something changed)
uint32_t s_fetchedMs = 0;
bool s_stale = true;
bool s_valid = false;
bool s_haveKey = false;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

struct Look {
    uint32_t fg, bg;
    const char *fallbackName;
};

Look alertLook(EnsoAlert a) {
    switch (a) {
        case EnsoAlert::ElNinoAdvisory: return {C_ORANGE, 0x2B1D0A, "El Nino Advisory"};
        case EnsoAlert::ElNinoWatch: return {C_AMBER, 0x26240A, "El Nino Watch"};
        case EnsoAlert::LaNinaAdvisory: return {C_BLUE, 0x0F1B33, "La Nina Advisory"};
        case EnsoAlert::LaNinaWatch: return {0x5EA0F7, 0x0F1B33, "La Nina Watch"};
        case EnsoAlert::FinalAdvisory: return {0x8B98A5, 0x151B23, "Final Advisory"};
        case EnsoAlert::None: return {0x8B98A5, 0x151B23, "ENSO-neutral"};
        default: return {0x8B98A5, 0x151B23, "--"};
    }
}

lv_color_t valueColor(float v, bool stale) {
    if (stale || isnan(v)) return ui::MUTED;
    if (v >= 2.0f) return hex(C_RED);
    if (v >= 0.5f) return hex(C_ORANGE);
    if (v <= -0.5f) return hex(C_BLUE);
    return ui::TEXT;
}

uint32_t barColor(float v, bool stale) {
    if (stale) return C_DIM_BAR;
    if (v >= 2.0f) return C_RED;
    if (v >= 1.0f) return C_ORANGE;
    if (v >= 0.5f) return C_AMBER;
    if (v <= -0.5f) return C_BLUE;
    return C_NEUTRAL_BAR;
}

// "+1.69" or "--"
void fmtSigned(char *buf, size_t n, float v, int decimals) {
    if (isnan(v)) snprintf(buf, n, "--");
    else if (decimals == 1) snprintf(buf, n, "%+.1f", static_cast<double>(v));
    else snprintf(buf, n, "%+.2f", static_cast<double>(v));
}

lv_obj_t *rightLabel(lv_obj_t *parent, const lv_font_t *font, lv_color_t col, int w, int x, int y) {
    lv_obj_t *l = ui::label(parent, font, col, "--");
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, uint32_t col) {
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_style_bg_color(o, hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

// One horizontal gradient segment of the diverging bar
void gradSeg(lv_obj_t *parent, int x, int w, uint32_t c1, uint32_t c2) {
    lv_obj_t *o = rect(parent, x, 0, w, 12, c1);
    if (c1 != c2) {
        lv_obj_set_style_bg_grad_color(o, hex(c2), 0);
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
    EnsoAlert a = (stale && !d.valid) ? EnsoAlert::Unknown : d.alert;
    Look lk = alertLook(a);
    lv_color_t fg = hex(lk.fg);
    lv_obj_set_style_bg_color(s_banner, hex(lk.bg), 0);
    lv_obj_set_style_border_color(s_banner, fg, 0);
    lv_obj_set_style_border_color(s_ring, fg, 0);
    for (int i = 0; i < 2; i++) lv_obj_set_style_bg_color(s_therm[i], fg, 0);
    ui::setTextColor(s_statusCap, fg);

    const char *name = lk.fallbackName;
    if (a != EnsoAlert::Unknown && d.alertText[0]) name = d.alertText;
    lv_obj_set_style_text_font(s_name, strlen(name) > 20 ? &lv_font_montserrat_20 : &lv_font_montserrat_26, 0);
    ui::setTextColor(s_name, a == EnsoAlert::Unknown ? ui::MUTED : (stale ? ui::MUTED : ui::TEXT));
    ui::setText(s_name, name);

    const char *syn = (a == EnsoAlert::Unknown) ? "" : d.synopsis;
    ui::setText(s_syn, syn);
}

void updateWeekly(const EnsoData &d, bool stale) {
    char date[8];
    shortDate(d.weeklyDate, date, sizeof(date));
    char buf[48];
    if (date[0]) snprintf(buf, sizeof(buf), "NINO 3.4, WEEKLY ANOMALY (%s)", date);
    else snprintf(buf, sizeof(buf), "NINO 3.4, WEEKLY ANOMALY");
    ui::setText(s_weekCap, buf);

    fmtSigned(buf, sizeof(buf), d.weeklyAnom, 1);
    ui::setText(s_weekVal, buf);
    ui::setTextColor(s_weekVal, valueColor(d.weeklyAnom, stale));

    bool show = !isnan(d.weeklyAnom) && !stale;
    ui::setHidden(s_marker, !show);
    if (show) ui::setX(s_marker, 164 + gradX(d.weeklyAnom) - 6);
}

void updatePill(int i, const char *name, float v, const char *season, bool stale) {
    char buf[24];
    if (season[0]) snprintf(buf, sizeof(buf), "%s (%s)", name, season);
    else snprintf(buf, sizeof(buf), "%s", name);
    ui::setText(s_pillCap[i], buf);
    fmtSigned(buf, sizeof(buf), v, 2);
    ui::setText(s_pillVal[i], buf);
    ui::setTextColor(s_pillVal[i], valueColor(v, stale));
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
        ui::setHidden(s_bars[i], !has);
        bool hasMon = k >= 0 && d.lastMonth >= 1 && d.lastMonth <= 12;
        ui::setHidden(s_mon[i], !hasMon);
        if (hasMon) {
            int m = ((d.lastMonth - 1 - (n - 1 - k)) % 12 + 12) % 12;
            char b[2] = {INITIALS[m], 0};
            ui::setText(s_mon[i], b);
            ui::setTextColor(s_mon[i], (m == 0) ? ui::TEXT : ui::MUTED); // January stands out
        }
        if (!has) continue;
        float v = vals[k];
        int h = static_cast<int>(lroundf(fabsf(v) * ppu));
        if (h < 2) h = 2;
        int top = v >= 0 ? zeroTop - h : zeroTop;
        if (top < 0) top = 0;
        if (top + h > HIST_H) h = HIST_H - top;
        ui::setHeight(s_bars[i], h);
        lv_obj_set_y(s_bars[i], top);
        ui::setBgColor(s_bars[i], hex(barColor(v, stale)));
    }
}

void updateOdds(const EnsoData &d, bool stale) {
    char buf[40];
    if (d.probWhat[0]) {
        size_t j = snprintf(buf, sizeof(buf), "CHANCE OF ");
        for (size_t i = 0; d.probWhat[i] && j + 1 < sizeof(buf); i++)
            buf[j++] = static_cast<char>(toupper(static_cast<unsigned char>(d.probWhat[i])));
        buf[j] = 0;
    } else {
        snprintf(buf, sizeof(buf), "PROBABILITY");
    }
    ui::setText(s_oddsCap, buf);
    if (d.nextUpdate[0]) snprintf(buf, sizeof(buf), "next update %s", d.nextUpdate);
    else buf[0] = 0;
    ui::setText(s_next, buf);

    bool cool = d.alert == EnsoAlert::LaNinaWatch || d.alert == EnsoAlert::LaNinaAdvisory;
    for (int i = 0; i < EnsoData::PROBS; i++) {
        bool show = i < d.probCount;
        ui::setHidden(s_oddSlot[i], !show);
        if (!show) continue;
        int pct = d.probPct[i]; // NOLINT(bugprone-signed-char-misuse): int8_t percentage (-1 = unknown), not a character
        ui::setText(s_oddSeason[i], d.probSeason[i]);
        if (pct < 0) snprintf(buf, sizeof(buf), "--");
        else snprintf(buf, sizeof(buf), "%d%%", pct);
        ui::setText(s_oddVal[i], buf);
        ui::setTextColor(s_oddVal[i], stale ? ui::MUTED : ui::TEXT);
        ui::setWidth(s_oddFill[i], pct < 0 ? 0 : (pct > 100 ? ODDS_W : pct * ODDS_W / 100));
        ui::setBgColor(s_oddFill[i], stale ? hex(C_DIM_BAR) : hex(cool ? C_BLUE : C_ORANGE));
    }
}

void update(bool force) {
    EnsoData d = ensoData();
    bool stale = !d.valid || (millis() - d.fetchedMs) > ENSO_STALE_MS;
    if (!force && s_haveKey && d.fetchedMs == s_fetchedMs && stale == s_stale && d.valid == s_valid) return;
    s_fetchedMs = d.fetchedMs;
    s_stale = stale;
    s_valid = d.valid;
    s_haveKey = true;

    updateBanner(d, stale);
    updateWeekly(d, stale);
    updatePill(0, "ONI", d.oni, d.oniSeason, stale);
    updatePill(1, "RONI", d.roni, d.roniSeason, stale);
    updateHistory(d, stale);
    updateOdds(d, stale);
}

} // namespace

lv_obj_t *ensoCreate() {
    s_scr = ui::screenCreate();
    s_top = ui::topBarCreate(s_scr);

    // ---- Status banner ----
    s_banner = ui::card(s_scr, 24, 52, 432, 78);
    lv_obj_set_style_border_width(s_banner, 2, 0);
    lv_obj_set_style_border_color(s_banner, ui::MUTED, 0);

    s_ring = ui::box(s_banner);
    lv_obj_set_size(s_ring, 36, 36);
    lv_obj_set_pos(s_ring, 12, 17);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, ui::MUTED, 0);
    // Thermometer glyph: stem + bulb
    s_therm[0] = rect(s_ring, 13, 4, 6, 15, 0x8B98A5);
    lv_obj_set_style_radius(s_therm[0], 3, 0);
    s_therm[1] = rect(s_ring, 10, 15, 12, 12, 0x8B98A5);
    lv_obj_set_style_radius(s_therm[1], LV_RADIUS_CIRCLE, 0);

    s_statusCap = ui::caption(s_banner, ui::MUTED, "ENSO STATUS");
    lv_obj_set_pos(s_statusCap, 60, 6);
    s_name = ui::label(s_banner, &lv_font_montserrat_26, ui::MUTED, "--");
    lv_obj_set_width(s_name, 350);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_name, 60, 20);
    s_syn = ui::label(s_banner, &lv_font_montserrat_12, ui::MUTED, "");
    lv_obj_set_width(s_syn, 350);
    lv_label_set_long_mode(s_syn, LV_LABEL_LONG_SCROLL_CIRCULAR); // CPC synopsis, ~160 chars
    lv_obj_set_style_anim_duration(s_syn, lv_anim_speed(20), 0);  // px/s; LVGL's default 40 is hard to read
    lv_obj_set_pos(s_syn, 60, 52);

    // ---- Weekly Nino 3.4 ----
    s_weekCard = ui::card(s_scr, 24, 138, 432, 80);
    s_weekCap = ui::caption(s_weekCard, ui::MUTED, "NINO 3.4, WEEKLY ANOMALY");
    lv_obj_set_pos(s_weekCap, 14, 8);

    lv_obj_t *vrow = ui::box(s_weekCard);
    lv_obj_set_pos(vrow, 14, 22);
    lv_obj_set_flex_flow(vrow, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(vrow, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(vrow, 4, 0);
    s_weekVal = ui::label(vrow, &lv_font_montserrat_48, ui::MUTED, "--");
    ui::label(vrow, &lv_font_montserrat_20, ui::MUTED, "\xC2\xB0" "C");

    // Diverging bar -3..+4 degC: blue -> neutral at 0 -> orange (+3) -> red (+4)
    lv_obj_t *bar = ui::box(s_weekCard);
    lv_obj_set_size(bar, GRAD_W, 12);
    lv_obj_set_pos(bar, 164, 48);
    lv_obj_set_style_radius(bar, 6, 0);
    lv_obj_set_style_clip_corner(bar, true, 0);
    gradSeg(bar, 0, 91, C_BLUE, 0x2A3440);
    gradSeg(bar, 91, 36, 0x2A3440, 0x2A3440);
    gradSeg(bar, 127, 64, 0x2A3440, C_ORANGE);
    gradSeg(bar, 191, 63, C_ORANGE, C_RED);

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
        lv_obj_t *t = ui::label(s_weekCard, &lv_font_montserrat_12, ui::MUTED, TICKS[i].txt);
        lv_obj_set_width(t, 40);
        int x = TICKS[i].center - 20;
        lv_text_align_t al = LV_TEXT_ALIGN_CENTER;
        if (i == 0) { x = 0; al = LV_TEXT_ALIGN_LEFT; }
        if (i == 4) { x = GRAD_W - 40; al = LV_TEXT_ALIGN_RIGHT; }
        lv_obj_set_style_text_align(t, al, 0);
        lv_obj_set_pos(t, 164 + x, 62);
    }

    // ---- ONI / RONI pills (no SOI in the data: two wider pills) ----
    static const char *const PILL_CAP[2] = {"ONI", "RONI"};
    for (int i = 0; i < 2; i++) {
        s_pillCard[i] = ui::card(s_scr, 24 + i * 222, 226, 210, 56);
        s_pillCap[i] = ui::caption(s_pillCard[i], ui::MUTED, PILL_CAP[i]);
        lv_obj_set_pos(s_pillCap[i], 12, 6);
        s_pillVal[i] = ui::label(s_pillCard[i], &lv_font_montserrat_26, ui::MUTED, "--");
        lv_obj_set_pos(s_pillVal[i], 12, 22);
    }

    // ---- Monthly history ----
    s_histCard = ui::card(s_scr, 24, 290, 432, 94);
    lv_obj_t *ht = ui::caption(s_histCard, ui::MUTED, "NINO 3.4, LAST 12 MONTHS");
    lv_obj_set_pos(ht, 14, 8);
    lv_obj_t *hl = ui::label(s_histCard, &lv_font_montserrat_12, ui::MUTED, "dashed = +0.5 El Nino threshold");
    lv_obj_align(hl, LV_ALIGN_TOP_RIGHT, -14, 8);

    lv_obj_t *plot = ui::box(s_histCard);
    lv_obj_set_pos(plot, 14, 26);
    lv_obj_set_size(plot, HIST_W, HIST_H);
    s_zeroLine = rect(plot, 0, 30, HIST_W, 1, 0x2A3440);
    static const lv_point_precise_t DASH[2] = {{0, 0}, {HIST_W - 1, 0}};
    s_thrLine = lv_line_create(plot);
    lv_line_set_points(s_thrLine, DASH, 2);
    lv_obj_set_style_line_width(s_thrLine, 1, 0);
    lv_obj_set_style_line_color(s_thrLine, ui::MUTED, 0);
    lv_obj_set_style_line_opa(s_thrLine, LV_OPA_70, 0);
    lv_obj_set_style_line_dash_width(s_thrLine, 4, 0);
    lv_obj_set_style_line_dash_gap(s_thrLine, 3, 0);
    lv_obj_set_pos(s_thrLine, 0, 20);
    for (int i = 0; i < HIST_BARS; i++) {
        s_bars[i] = rect(plot, i * HIST_STEP, 0, HIST_BAR_W, 2, C_NEUTRAL_BAR);
        lv_obj_set_style_radius(s_bars[i], 3, 0);
        lv_obj_add_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < HIST_BARS; i++) {
        s_mon[i] = ui::label(s_histCard, &lv_font_montserrat_12, ui::MUTED, "");
        lv_obj_set_width(s_mon[i], HIST_BAR_W);
        lv_obj_set_style_text_align(s_mon[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(s_mon[i], 14 + i * HIST_STEP, 26 + HIST_H + 2);
        lv_obj_add_flag(s_mon[i], LV_OBJ_FLAG_HIDDEN);
    }

    // ---- Odds + next update ----
    s_oddsCard = ui::card(s_scr, 24, 392, 432, 52);
    s_oddsCap = ui::caption(s_oddsCard, ui::MUTED, "PROBABILITY");
    lv_obj_set_pos(s_oddsCap, 14, 6);
    s_next = ui::label(s_oddsCard, &lv_font_montserrat_12, ui::TEXT, "");
    lv_obj_align(s_next, LV_ALIGN_TOP_RIGHT, -14, 6);
    for (int i = 0; i < EnsoData::PROBS; i++) {
        lv_obj_t *slot = ui::box(s_oddsCard);
        lv_obj_set_size(slot, ODDS_W, 24);
        lv_obj_set_pos(slot, 14 + i * ODDS_STEP, 24);
        lv_obj_add_flag(slot, LV_OBJ_FLAG_HIDDEN);
        s_oddSlot[i] = slot;
        s_oddSeason[i] = ui::label(slot, &lv_font_montserrat_14, ui::MUTED, "");
        lv_obj_set_pos(s_oddSeason[i], 0, 0);
        s_oddVal[i] = rightLabel(slot, &lv_font_montserrat_14, ui::TEXT, 70, ODDS_W - 70, 0);
        lv_obj_t *track = rect(slot, 0, 19, ODDS_W, 4, 0x2A3440);
        lv_obj_set_style_radius(track, 2, 0);
        s_oddFill[i] = rect(track, 0, 0, 0, 4, C_ORANGE);
        lv_obj_set_style_radius(s_oddFill[i], 2, 0);
    }

    ui::pageDots(s_scr, ui::SCR_ENSO);
    return s_scr;
}

void ensoRefresh() {
    if (!s_scr) return;
    ui::topBarUpdate(s_top);
    update(false);
}

void ensoOnShow() {
    if (!s_scr) return;
    update(true);
    lv_obj_t *cards[6] = {s_banner, s_weekCard, s_pillCard[0], s_pillCard[1], s_histCard, s_oddsCard};
    for (int i = 0; i < 6; i++) {
        lv_obj_set_style_opa(cards[i], LV_OPA_TRANSP, 0);
        ui::animFadeIn(cards[i], 80 + i * 100);
    }
}
