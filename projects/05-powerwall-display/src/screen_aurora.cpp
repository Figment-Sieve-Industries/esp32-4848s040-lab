// Q: Aurora (UK) - status card, 24 h activity bars, 3-day Kp strip, visibility hint.
// Geometry follows the "Q Aurora (UK)" board of the design canvas (480x480). No data credits
// here: they live on the About screen.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "screens.h"
#include "ui_common.h"
#include "ui_chart.h"
#include "shared_state.h"
#include "time_util.h"
#include "sky.h"
#include "powerwall.h"
#include "moon_calc.h"
#include "secrets.h"

namespace {

constexpr int BAR_COUNT = 24;
constexpr int BAR_W = 12;
constexpr int BAR_STEP = 16;      // 12 + 4 gap
constexpr int CHART_H = 68;
constexpr float CHART_CAP = 250.0f;
constexpr int CHART_W = 380;      // bars + lines; threshold labels sit to the right
constexpr int KP_ROWS = 3;
constexpr int KP_COLS = 8;
constexpr int KP_LABEL_W = 44;
constexpr int KP_CELL_W = 42;
constexpr int KP_CELL_STEP = 45;
constexpr int KP_CELL_H = 18;
constexpr int NOW_X = 428 - 14 - 76 - 2; // right-hand "nT now" / Kp column in the status card
constexpr int NOW_W = 76;

// Status card
ui::Banner s_status;
lv_obj_t *s_glyph[3] = {};
lv_obj_t *s_nt = nullptr;
lv_obj_t *s_kpNow = nullptr;

// Activity card
lv_obj_t *s_chartCard = nullptr;
lv_obj_t *s_bars = nullptr;
lv_obj_t *s_thrLine[3] = {};
lv_obj_t *s_thrLbl[3] = {};
lv_obj_t *s_axis[4] = {};
lv_obj_t *s_axisNow = nullptr;

// Kp card
lv_obj_t *s_kpCard = nullptr;
lv_obj_t *s_kpDay[KP_ROWS] = {};
lv_obj_t *s_kpCell[KP_ROWS][KP_COLS] = {};
lv_obj_t *s_kpTxt[KP_ROWS][KP_COLS] = {};

// Hint + updated
lv_obj_t *s_hintBox = nullptr;
lv_obj_t *s_hint = nullptr;
lv_obj_t *s_updated = nullptr;

// Change detection (refresh runs every 500 ms; only touch LVGL when something changed)
struct Key {
    uint32_t auroraMs = 0, kpMs = 0;
    bool stale = true;
    int level = -2;
    int cloudPct = -2;
    int dark = -2;          // -1 unknown, 0 light, 1 dark
    int slot = -2;
    bool valid = false;
    bool privacy = false;   // place name in the hint (ui::privacy)
    bool operator==(const Key &) const = default;
};
ui::ChangeGate<Key> s_gate;

lv_color_t kpColor(float k) {
    const int r = static_cast<int>(lroundf(k));
    return ui::levelColor(r < 4 ? 0 : r - 3); // Kp 4 yellow, 5 orange, 6+ red
}

lv_color_t levelBg(AuroraLevel l) {
    switch (l) {
        case AuroraLevel::Green: return lv_color_hex(0x102418);
        case AuroraLevel::Yellow: return lv_color_hex(0x26240A);
        case AuroraLevel::Amber: return lv_color_hex(0x2B1D0A);
        case AuroraLevel::Red: return ui::C_ALERT_BG;
        default: return ui::CARD;
    }
}

const char *levelTag(AuroraLevel l) {
    switch (l) {
        case AuroraLevel::Green: return "GREEN";
        case AuroraLevel::Yellow: return "YELLOW";
        case AuroraLevel::Amber: return "AMBER ALERT";
        case AuroraLevel::Red: return "RED ALERT";
        default: return "NO DATA";
    }
}

const char *levelDesc(AuroraLevel l) {
    switch (l) {
        case AuroraLevel::Green: return "Aurora unlikely anywhere in the UK";
        case AuroraLevel::Yellow: return "Camera in the north, eye in Scotland";
        case AuroraLevel::Amber: return "Scotland / N England likely, S maybe";
        case AuroraLevel::Red: return "Visible by eye anywhere in the UK";
        default: return "";
    }
}

int barHeight(float v) {
    if (v < 0) v = 0;
    if (v > CHART_CAP) v = CHART_CAP;
    int h = static_cast<int>(lroundf(v / CHART_CAP * CHART_H));
    return h < 2 ? 2 : h;
}

int thresholdY(float nt) { return CHART_H - static_cast<int>(lroundf(nt / CHART_CAP * CHART_H)); }

// "LONDON" -> "London"; very long names fall back to "here".
void titleCasePlace(char *out, size_t n) {
    const char *src = ui::placeName();
    if (!src || !*src || strlen(src) > 14) {
        snprintf(out, n, "here");
        return;
    }
    size_t j = 0;
    bool start = true;
    for (size_t i = 0; src[i] && j + 1 < n; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') out[j++] = start ? c : static_cast<char>(c + 32);
        else if (c >= 'a' && c <= 'z') out[j++] = start ? static_cast<char>(c - 32) : c;
        else out[j++] = c;
        start = (c == ' ' || c == '-');
    }
    out[j] = 0;
}

lv_obj_t *legendSwatch(lv_obj_t *parent, bool solid) {
    if (solid) return ui::rect(parent, 0, 0, 10, 10, ui::MUTED, 3);
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_size(o, 10, 10);
    lv_obj_set_style_radius(o, 3, 0);
    lv_obj_set_style_border_color(o, ui::MUTED, 0);
    lv_obj_set_style_border_width(o, 2, 0);
    return o;
}

// ---- updates ---------------------------------------------------------------------------------

void updateStatus(const AuroraData &a, const KpData &k, bool aStale, bool kStale) {
    const AuroraLevel lvl = aStale ? AuroraLevel::Unknown : a.level;
    const lv_color_t col = aStale ? ui::MUTED : lv_color_hex(auroraLevelColor(lvl));
    ui::bannerSet(s_status, col, levelBg(lvl), aStale ? "--" : auroraLevelName(lvl), aStale,
                  aStale ? "" : levelDesc(lvl), 24, &lv_font_montserrat_20, &lv_font_montserrat_16);
    ui::setText(s_status.cap, aStale ? "NO DATA" : levelTag(lvl));
    for (int i = 0; i < 3; i++) ui::setBgColor(s_glyph[i], col);

    const bool haveNt = !aStale && a.count >= 1;
    const float nt = haveNt ? a.activityNt[a.count - 1] : 0.0f;
    if (nt >= 100.0f) ui::setValueF(s_nt, haveNt, false, ui::TEXT, "%d", static_cast<int>(lroundf(nt)));
    else ui::setValueF(s_nt, haveNt, false, ui::TEXT, "%.1f", static_cast<double>(nt));

    if (kStale || k.nowKp < 0) {
        ui::setText(s_kpNow, "Kp --");
        ui::setTextColor(s_kpNow, ui::MUTED);
    } else {
        ui::setTextF(s_kpNow, "Kp %.1f", static_cast<double>(k.nowKp));
        ui::setTextColor(s_kpNow, kpColor(k.nowKp));
    }
}

void updateChart(const AuroraData &a, bool stale) {
    for (int j = 0; j < 3; j++) {
        float t = a.thresholdNt[j + 1];
        bool show = t > 0 && t <= CHART_CAP;
        ui::setHidden(s_thrLine[j], !show);
        ui::setHidden(s_thrLbl[j], !show);
        if (!show) continue;
        int y = thresholdY(t);
        lv_obj_set_y(s_thrLine[j], y);
        lv_obj_set_y(s_thrLbl[j], y - 6);
        ui::setTextF(s_thrLbl[j], "%d", static_cast<int>(lroundf(t)));
    }

    const int n = (stale || a.count < 0) ? 0 : (a.count > BAR_COUNT ? BAR_COUNT : a.count);
    const int first = BAR_COUNT - n;
    int16_t heights[BAR_COUNT];
    lv_color_t cols[BAR_COUNT];
    for (int k = 0; k < n; k++) {
        heights[k] = static_cast<int16_t>(barHeight(a.activityNt[k]));
        cols[k] = lv_color_hex(auroraLevelColor(a.activityLevel[k]));
    }
    ui::barChartSetNewest(s_bars, n, heights, cols);

    for (int q = 0; q < 4; q++) {
        int idx = q * 6 - first;
        char b[8] = "";
        if (n > 0 && idx >= 0 && idx < n) {
            struct tm tmv;
            time_t t = a.activityHour[idx];
            localtime_r(&t, &tmv);
            snprintf(b, sizeof(b), "%02d:00", tmv.tm_hour);
        }
        ui::setText(s_axis[q], b);
    }
    ui::setHidden(s_axisNow, n == 0);
}

void updateKp(const KpData &k, bool stale) {
    for (int r = 0; r < KP_ROWS; r++) {
        if (r == 0) {
            ui::setText(s_kpDay[r], "Today");
        } else if (k.valid && k.day0 > 0) {
            struct tm tmv;
            time_t t = k.day0 + static_cast<time_t>(r) * 86400 + 12 * 3600;
            gmtime_r(&t, &tmv);
            ui::setText(s_kpDay[r], tu::WDAY_ABBR[tmv.tm_wday % 7]);
        } else {
            ui::setText(s_kpDay[r], "--");
        }
        for (int c = 0; c < KP_COLS; c++) {
            int slot = r * KP_COLS + c;
            lv_obj_t *cell = s_kpCell[r][c];
            lv_obj_t *txt = s_kpTxt[r][c];
            KpKind kind = (stale || !k.valid) ? KpKind::None : k.kind[slot];
            bool now = !stale && k.valid && slot == k.nowSlot;
            if (kind == KpKind::None) {
                lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_color(cell, now ? ui::TEXT : ui::TRACK, 0);
                ui::setText(txt, "");
                continue;
            }
            lv_color_t col = kpColor(k.kp[slot]);
            bool solid = kind != KpKind::Predicted;
            lv_obj_set_style_bg_color(cell, col, 0);
            lv_obj_set_style_bg_opa(cell, solid ? LV_OPA_COVER : LV_OPA_20, 0);
            lv_obj_set_style_border_color(cell, now ? ui::TEXT : col, 0);
            ui::setTextColor(txt, solid ? ui::BG : col);
            ui::setTextF(txt, "%d", static_cast<int>(lroundf(k.kp[slot])));
        }
    }
}

// Heuristic hint, not a forecast: level + darkness + cloud cover.
void updateHint(const AuroraData &a, bool stale, int cloudPct, int dark) {
    char buf[64];
    lv_color_t col = ui::MUTED;
    const bool quiet = a.level == AuroraLevel::Green || a.level == AuroraLevel::Yellow ||
                       a.level == AuroraLevel::Unknown;
    if (stale || (!quiet && dark < 0)) { // no data, or an alert but darkness unknown (clock not set)
        snprintf(buf, sizeof(buf), "--");
    } else if (quiet) {
        char place[20];
        titleCasePlace(place, sizeof(place));
        snprintf(buf, sizeof(buf), "Visible from %s? Not expected tonight", place);
    } else if (dark == 0) {
        snprintf(buf, sizeof(buf), "Possible after dark: look north");
    } else if (cloudPct < 0) {
        snprintf(buf, sizeof(buf), "Dark, cloud unknown: look north");
        col = ui::C_YELLOW;
    } else if (cloudPct > 80) {
        snprintf(buf, sizeof(buf), "Too cloudy (%d%%): aurora %s", cloudPct,
                 a.level == AuroraLevel::Red ? "likely" : "possible");
        col = ui::C_ORANGE;
    } else if (cloudPct >= 50) {
        snprintf(buf, sizeof(buf), "Possible, patchy cloud (%d%%): look north", cloudPct);
        col = ui::C_YELLOW;
    } else {
        snprintf(buf, sizeof(buf), a.level == AuroraLevel::Red ? "Possible, clear (%d%% cloud): look north now"
                                                               : "Possible, %d%% cloud: look north, low down",
                 cloudPct);
        col = ui::C_GREEN;
    }
    lv_obj_set_style_border_color(s_hintBox, col, 0);
    ui::setTextColor(s_hint, col);
    ui::setText(s_hint, buf);
}

void updateUpdated(const AuroraData &a, bool stale) {
    char hhmm[8];
    tu::fmtHHMM(hhmm, sizeof(hhmm), stale ? 0 : a.updatedAt);
    ui::setTextF(s_updated, "Updated %s", hhmm);
}

// Rebuilds everything if the inputs changed (or force). Returns nothing; cheap when unchanged.
void update(bool force) {
    const AuroraData a = auroraData();
    const KpData k = kpData();
    const WeatherData w = weatherData();

    const bool aStale = isStale(a.valid, a.fetchedMs, SKY_STALE_MS);
    const bool kStale = isStale(k.valid, k.fetchedMs, SKY_STALE_MS);

    int dark = -1;
    const time_t now = time(nullptr);
    if (tu::clockValid(now)) {
        dark = sunAltitudeDeg(now, static_cast<double>(WEATHER_LAT), static_cast<double>(WEATHER_LON)) < -12.0 ? 1 : 0;
    }
    const int cloud = w.valid ? w.cloudPct : -1;

    Key key;
    key.auroraMs = a.fetchedMs;
    key.kpMs = k.fetchedMs;
    key.stale = aStale;
    key.level = static_cast<int>(a.level) + (kStale ? 100 : 0);
    key.cloudPct = cloud;
    key.dark = dark;
    key.slot = k.nowSlot;
    key.valid = a.valid;
    key.privacy = ui::privacy();
    if (!s_gate.changed(key, force)) return;

    updateStatus(a, k, aStale, kStale);
    updateChart(a, aStale);
    updateKp(k, kStale);
    updateHint(a, aStale, cloud, dark);
    updateUpdated(a, aStale);
}

} // namespace

lv_obj_t *auroraCreate() {
    lv_obj_t *scr = ui::screenCreate(ui::SCR_AURORA);

    // ---- Status card (official level colour as the border) ----
    s_status = ui::bannerCreate(scr, 52, 82, "NO DATA", 276);
    // Three little curtains of light
    static const int GX[3] = {9, 15, 21};
    static const int GH[3] = {14, 18, 10};
    for (int i = 0; i < 3; i++) s_glyph[i] = ui::rect(s_status.ring, GX[i] - 2, 14 - GH[i] / 2 + 3, 3, GH[i], ui::MUTED, 2);

    s_nt = ui::labelAt(s_status.card, &lv_font_montserrat_26, ui::TEXT, "--", NOW_X, 6, NOW_W, LV_TEXT_ALIGN_RIGHT);
    ui::labelAt(s_status.card, &lv_font_montserrat_12, ui::MUTED, "nT now", NOW_X, 36, NOW_W, LV_TEXT_ALIGN_RIGHT);
    s_kpNow = ui::labelAt(s_status.card, &lv_font_montserrat_16, ui::MUTED, "Kp --", NOW_X, 52, NOW_W, LV_TEXT_ALIGN_RIGHT);

    // ---- Activity chart ----
    s_chartCard = ui::card(scr, 24, 142, 432, 126);
    ui::cardCaption(s_chartCard, "ACTIVITY, LAST 24 H");
    lv_obj_t *cu = ui::label(s_chartCard, &lv_font_montserrat_12, ui::MUTED, "nT per hour");
    lv_obj_align(cu, LV_ALIGN_TOP_RIGHT, -14, 10);

    lv_obj_t *plot = ui::box(s_chartCard);
    lv_obj_set_pos(plot, 14, 30);
    lv_obj_set_size(plot, 404, CHART_H);
    static const AuroraLevel THR_LVL[3] = {AuroraLevel::Yellow, AuroraLevel::Amber, AuroraLevel::Red};
    static const float THR_NT[3] = {50.0f, 100.0f, 200.0f};
    for (int j = 0; j < 3; j++) {
        s_thrLine[j] = ui::rect(plot, 0, thresholdY(THR_NT[j]), CHART_W, 1, lv_color_hex(auroraLevelColor(THR_LVL[j])));
        lv_obj_set_style_bg_opa(s_thrLine[j], LV_OPA_50, 0);
        s_thrLbl[j] = ui::labelAt(plot, &lv_font_montserrat_12, ui::MUTED, "", 384, thresholdY(THR_NT[j]) - 6);
    }
    s_bars = ui::barChartCreate(plot, 0, 0, CHART_W, CHART_H, BAR_COUNT, BAR_W, BAR_STEP, 2);
    for (int q = 0; q < 4; q++) s_axis[q] = ui::labelAt(s_chartCard, &lv_font_montserrat_12, ui::MUTED, "", 14 + q * 96, 30 + CHART_H + 3);
    s_axisNow = ui::labelAt(s_chartCard, &lv_font_montserrat_12, ui::TEXT, "now", 14 + 380 - 24, 30 + CHART_H + 3);

    // ---- Kp strip ----
    s_kpCard = ui::card(scr, 24, 276, 432, 111);
    ui::cardCaption(s_kpCard, "KP INDEX, 3-HOURLY (UTC)");
    // Legend, right-aligned: [solid] seen  [outline] forecast
    lv_obj_t *leg = ui::flex(s_kpCard, LV_FLEX_FLOW_ROW, 6);
    legendSwatch(leg, true);
    ui::label(leg, &lv_font_montserrat_12, ui::MUTED, "seen");
    legendSwatch(leg, false);
    ui::label(leg, &lv_font_montserrat_12, ui::MUTED, "forecast");
    lv_obj_align(leg, LV_ALIGN_TOP_RIGHT, -14, 9);

    static const char *const HDR[4] = {"00", "06", "12", "18"};
    for (int h = 0; h < 4; h++)
        ui::labelAt(s_kpCard, &lv_font_montserrat_12, ui::MUTED, HDR[h], 14 + KP_LABEL_W + h * 2 * KP_CELL_STEP, 28);
    for (int r = 0; r < KP_ROWS; r++) {
        int y = 44 + r * (KP_CELL_H + 3);
        s_kpDay[r] = ui::labelAt(s_kpCard, &lv_font_montserrat_12, ui::MUTED, "--", 14, y + 2);
        for (int c = 0; c < KP_COLS; c++) {
            lv_obj_t *cell = ui::box(s_kpCard);
            lv_obj_set_size(cell, KP_CELL_W, KP_CELL_H);
            lv_obj_set_pos(cell, 14 + KP_LABEL_W + c * KP_CELL_STEP, y);
            lv_obj_set_style_radius(cell, 5, 0);
            lv_obj_set_style_border_width(cell, 2, 0);
            lv_obj_set_style_border_color(cell, ui::TRACK, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, 0);
            lv_obj_t *t = ui::label(cell, &lv_font_montserrat_14, ui::BG, "");
            lv_obj_center(t);
            s_kpCell[r][c] = cell;
            s_kpTxt[r][c] = t;
        }
    }

    // ---- Visibility hint ----
    s_hintBox = ui::rect(scr, 24, 393, 432, 28, ui::CARD, 14);
    lv_obj_set_style_border_width(s_hintBox, 2, 0);
    lv_obj_set_style_border_color(s_hintBox, ui::MUTED, 0);
    s_hint = ui::label(s_hintBox, &lv_font_montserrat_14, ui::MUTED, "--");
    lv_obj_set_width(s_hint, 404);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 0);

    s_updated = ui::labelAt(scr, &lv_font_montserrat_12, ui::MUTED, "Updated --", 26, 430);

    return scr;
}

void auroraRefresh() { update(false); }

void auroraOnShow() {
    lv_obj_t *const cards[4] = {s_status.card, s_chartCard, s_kpCard, s_hintBox};
    ui::fadeIn(cards, 4, 80, 120);
}
