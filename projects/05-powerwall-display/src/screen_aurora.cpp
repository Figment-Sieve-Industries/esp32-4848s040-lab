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

const uint32_t C_DIM_BAR = 0x3A4654;
const uint32_t C_HINT_MUTED = 0x8B98A5;
const uint32_t C_HINT_BAD = 0xFF8A3D;
const uint32_t C_HINT_MID = 0xF5D400;
const uint32_t C_HINT_GOOD = 0x3DDC84;

lv_obj_t *s_scr = nullptr;
ui::TopBar s_top;

// Status card
lv_obj_t *s_statusCard = nullptr;
lv_obj_t *s_ring = nullptr;
lv_obj_t *s_glyph[3] = {};
lv_obj_t *s_levelLbl = nullptr;
lv_obj_t *s_name = nullptr;
lv_obj_t *s_desc = nullptr;
lv_obj_t *s_nt = nullptr;
lv_obj_t *s_kpNow = nullptr;

// Activity card
lv_obj_t *s_chartCard = nullptr;
lv_obj_t *s_bars[BAR_COUNT] = {};
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
    int cloudBucket = -2;   // -1 unknown, 0 good, 1 patchy, 2 cloudy
    int cloudPct = -2;
    int dark = -2;          // -1 unknown, 0 light, 1 dark
    int slot = -2;
    bool valid = false;
};
Key s_key;
bool s_haveKey = false;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

lv_color_t kpColor(float k) {
    int r = static_cast<int>(lroundf(k));
    if (r >= 6) return hex(0xE5484D);
    if (r == 5) return hex(0xFF8A3D);
    if (r == 4) return hex(0xF5D400);
    return hex(0x3DDC84);
}

uint32_t levelBg(AuroraLevel l) {
    switch (l) {
        case AuroraLevel::Green: return 0x102418;
        case AuroraLevel::Yellow: return 0x26240A;
        case AuroraLevel::Amber: return 0x2B1D0A;
        case AuroraLevel::Red: return 0x2A1215;
        default: return 0x151B23;
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

bool isStale(bool valid, uint32_t fetchedMs) {
    return !valid || (millis() - fetchedMs) > SKY_STALE_MS;
}

int barHeight(float v) {
    if (v < 0) v = 0;
    if (v > CHART_CAP) v = CHART_CAP;
    int h = static_cast<int>(lroundf(v / CHART_CAP * CHART_H));
    return h < 2 ? 2 : h;
}

int thresholdY(float nt) { return CHART_H - static_cast<int>(lroundf(nt / CHART_CAP * CHART_H)); }

// "LONDON" -> "London"; very long names fall back to "here".
void placeName(char *out, size_t n) {
    const char *src = WEATHER_NAME;
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

lv_obj_t *dot(lv_obj_t *parent, int w, int h, uint32_t col) {
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, hex(col), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 2, 0);
    return o;
}

lv_obj_t *legendSwatch(lv_obj_t *parent, bool solid) {
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_size(o, 10, 10);
    lv_obj_set_style_radius(o, 3, 0);
    if (solid) {
        lv_obj_set_style_bg_color(o, ui::MUTED, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_border_color(o, ui::MUTED, 0);
        lv_obj_set_style_border_width(o, 2, 0);
    }
    return o;
}

// ---- updates ---------------------------------------------------------------------------------

void updateStatus(const AuroraData &a, const KpData &k, bool aStale, bool kStale) {
    AuroraLevel lvl = aStale ? AuroraLevel::Unknown : a.level;
    lv_color_t col = aStale ? ui::MUTED : hex(auroraLevelColor(lvl));
    lv_obj_set_style_bg_color(s_statusCard, hex(levelBg(lvl)), 0);
    lv_obj_set_style_border_color(s_statusCard, col, 0);
    lv_obj_set_style_border_color(s_ring, col, 0);
    for (int i = 0; i < 3; i++) lv_obj_set_style_bg_color(s_glyph[i], col, 0);
    ui::setTextColor(s_levelLbl, col);
    ui::setText(s_levelLbl, aStale ? "NO DATA" : levelTag(lvl));

    const char *name = aStale ? "--" : auroraLevelName(lvl);
    lv_obj_set_style_text_font(s_name, strlen(name) > 24 ? &lv_font_montserrat_16 : &lv_font_montserrat_20, 0);
    ui::setTextColor(s_name, aStale ? ui::MUTED : ui::TEXT);
    ui::setText(s_name, name);
    ui::setText(s_desc, aStale ? "" : levelDesc(lvl));

    char buf[24];
    if (aStale || a.count < 1) {
        snprintf(buf, sizeof(buf), "--");
    } else {
        float nt = a.activityNt[a.count - 1];
        if (nt >= 100.0f) snprintf(buf, sizeof(buf), "%d", static_cast<int>(lroundf(nt)));
        else snprintf(buf, sizeof(buf), "%.1f", static_cast<double>(nt));
    }
    ui::setText(s_nt, buf);
    ui::setTextColor(s_nt, aStale ? ui::MUTED : ui::TEXT);

    if (kStale || k.nowKp < 0) {
        snprintf(buf, sizeof(buf), "Kp --");
        ui::setTextColor(s_kpNow, ui::MUTED);
    } else {
        snprintf(buf, sizeof(buf), "Kp %.1f", static_cast<double>(k.nowKp));
        ui::setTextColor(s_kpNow, kpColor(k.nowKp));
    }
    ui::setText(s_kpNow, buf);
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
        char b[12];
        snprintf(b, sizeof(b), "%d", static_cast<int>(lroundf(t)));
        ui::setText(s_thrLbl[j], b);
    }

    int n = (stale || a.count < 0) ? 0 : (a.count > BAR_COUNT ? BAR_COUNT : a.count);
    int first = BAR_COUNT - n;
    for (int i = 0; i < BAR_COUNT; i++) {
        if (i < first) {
            ui::setHidden(s_bars[i], true);
            continue;
        }
        int k = i - first;
        ui::setHidden(s_bars[i], false);
        ui::setHeight(s_bars[i], barHeight(a.activityNt[k]));
        ui::setBgColor(s_bars[i], hex(auroraLevelColor(a.activityLevel[k])));
    }
    // The bars sit at the bottom of the plot area
    for (int i = 0; i < BAR_COUNT; i++) lv_obj_align(s_bars[i], LV_ALIGN_BOTTOM_LEFT, i * BAR_STEP, 0);

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
    static const char *const DAYS[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    for (int r = 0; r < KP_ROWS; r++) {
        if (r == 0) {
            ui::setText(s_kpDay[r], "Today");
        } else if (k.valid && k.day0 > 0) {
            struct tm tmv;
            time_t t = k.day0 + static_cast<time_t>(r) * 86400 + 12 * 3600;
            gmtime_r(&t, &tmv);
            ui::setText(s_kpDay[r], DAYS[tmv.tm_wday % 7]);
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
            char b[8];
            snprintf(b, sizeof(b), "%d", static_cast<int>(lroundf(k.kp[slot])));
            ui::setText(txt, b);
        }
    }
}

// Heuristic hint, not a forecast: level + darkness + cloud cover.
void updateHint(const AuroraData &a, bool stale, int cloudPct, int dark) {
    char buf[64];
    uint32_t col = C_HINT_MUTED;
    const bool quiet = a.level == AuroraLevel::Green || a.level == AuroraLevel::Yellow ||
                       a.level == AuroraLevel::Unknown;
    if (stale || (!quiet && dark < 0)) { // no data, or an alert but darkness unknown (clock not set)
        snprintf(buf, sizeof(buf), "--");
    } else if (quiet) {
        char place[20];
        placeName(place, sizeof(place));
        snprintf(buf, sizeof(buf), "Visible from %s? Not expected tonight", place);
    } else if (dark == 0) {
        snprintf(buf, sizeof(buf), "Possible after dark: look north");
    } else if (cloudPct < 0) {
        snprintf(buf, sizeof(buf), "Dark, cloud unknown: look north");
        col = C_HINT_MID;
    } else if (cloudPct > 80) {
        snprintf(buf, sizeof(buf), "Too cloudy (%d%%): aurora %s", cloudPct,
                 a.level == AuroraLevel::Red ? "likely" : "possible");
        col = C_HINT_BAD;
    } else if (cloudPct >= 50) {
        snprintf(buf, sizeof(buf), "Possible, patchy cloud (%d%%): look north", cloudPct);
        col = C_HINT_MID;
    } else {
        snprintf(buf, sizeof(buf), a.level == AuroraLevel::Red ? "Possible, clear (%d%% cloud): look north now"
                                                               : "Possible, %d%% cloud: look north, low down",
                 cloudPct);
        col = C_HINT_GOOD;
    }
    lv_obj_set_style_border_color(s_hintBox, hex(col), 0);
    ui::setTextColor(s_hint, hex(col));
    ui::setText(s_hint, buf);
}

void updateUpdated(const AuroraData &a, bool stale) {
    char buf[32];
    if (stale || a.updatedAt <= 0) {
        snprintf(buf, sizeof(buf), "Updated --");
    } else {
        struct tm tmv;
        time_t t = a.updatedAt;
        localtime_r(&t, &tmv);
        snprintf(buf, sizeof(buf), "Updated %02d:%02d", tmv.tm_hour, tmv.tm_min);
    }
    ui::setText(s_updated, buf);
}

// Rebuilds everything if the inputs changed (or force). Returns nothing; cheap when unchanged.
void update(bool force) {
    AuroraData a = auroraData();
    KpData k = kpData();
    WeatherData w = weatherData();

    bool aStale = isStale(a.valid, a.fetchedMs);
    bool kStale = isStale(k.valid, k.fetchedMs);

    int dark = -1;
    time_t now = time(nullptr);
    if (now > 1700000000) {
        dark = sunAltitudeDeg(now, static_cast<double>(WEATHER_LAT), static_cast<double>(WEATHER_LON)) < -12.0 ? 1 : 0;
    }
    int cloud = w.valid ? w.cloudPct : -1;

    Key key;
    key.auroraMs = a.fetchedMs;
    key.kpMs = k.fetchedMs;
    key.stale = aStale;
    key.level = static_cast<int>(a.level) + (kStale ? 100 : 0);
    key.cloudPct = cloud;
    key.dark = dark;
    key.slot = k.nowSlot;
    key.valid = a.valid;
    bool changed = force || !s_haveKey || key.auroraMs != s_key.auroraMs || key.kpMs != s_key.kpMs ||
                   key.stale != s_key.stale || key.level != s_key.level || key.cloudPct != s_key.cloudPct ||
                   key.dark != s_key.dark || key.slot != s_key.slot || key.valid != s_key.valid;
    if (!changed) return;
    s_key = key;
    s_haveKey = true;

    updateStatus(a, k, aStale, kStale);
    updateChart(a, aStale);
    updateKp(k, kStale);
    updateHint(a, aStale, cloud, dark);
    updateUpdated(a, aStale);
}

} // namespace

lv_obj_t *auroraCreate() {
    s_scr = ui::screenCreate();
    s_top = ui::topBarCreate(s_scr);

    // ---- Status card (official level colour as the border) ----
    s_statusCard = ui::card(s_scr, 24, 52, 432, 82);
    lv_obj_set_style_border_width(s_statusCard, 2, 0);
    lv_obj_set_style_border_color(s_statusCard, ui::MUTED, 0);

    s_ring = ui::box(s_statusCard);
    lv_obj_set_size(s_ring, 36, 36);
    lv_obj_set_pos(s_ring, 12, 20);
    lv_obj_set_style_radius(s_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_ring, 2, 0);
    lv_obj_set_style_border_color(s_ring, ui::MUTED, 0);
    // Three little curtains of light
    static const int GX[3] = {9, 15, 21};
    static const int GH[3] = {14, 18, 10};
    for (int i = 0; i < 3; i++) {
        s_glyph[i] = dot(s_ring, 3, GH[i], 0x8B98A5);
        lv_obj_set_style_radius(s_glyph[i], 2, 0);
        lv_obj_set_pos(s_glyph[i], GX[i] - 2, 14 - GH[i] / 2 + 3);
    }

    s_levelLbl = ui::caption(s_statusCard, ui::MUTED, "NO DATA");
    lv_obj_set_pos(s_levelLbl, 60, 9);
    s_name = ui::label(s_statusCard, &lv_font_montserrat_20, ui::MUTED, "--");
    lv_obj_set_width(s_name, 276);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_name, 60, 25);
    s_desc = ui::label(s_statusCard, &lv_font_montserrat_12, ui::MUTED, "");
    lv_obj_set_width(s_desc, 276);
    lv_label_set_long_mode(s_desc, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_desc, 60, 54);

    s_nt = ui::label(s_statusCard, &lv_font_montserrat_26, ui::TEXT, "--");
    lv_obj_set_width(s_nt, 76);
    lv_obj_set_style_text_align(s_nt, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_nt, 428 - 14 - 76 - 2, 6);
    lv_obj_t *ntl = ui::label(s_statusCard, &lv_font_montserrat_12, ui::MUTED, "nT now");
    lv_obj_set_width(ntl, 76);
    lv_obj_set_style_text_align(ntl, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(ntl, 428 - 14 - 76 - 2, 36);
    s_kpNow = ui::label(s_statusCard, &lv_font_montserrat_16, ui::MUTED, "Kp --");
    lv_obj_set_width(s_kpNow, 76);
    lv_obj_set_style_text_align(s_kpNow, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(s_kpNow, 428 - 14 - 76 - 2, 52);

    // ---- Activity chart ----
    s_chartCard = ui::card(s_scr, 24, 142, 432, 126);
    lv_obj_t *ct = ui::caption(s_chartCard, ui::MUTED, "ACTIVITY, LAST 24 H");
    lv_obj_set_pos(ct, 14, 10);
    lv_obj_t *cu = ui::label(s_chartCard, &lv_font_montserrat_12, ui::MUTED, "nT per hour");
    lv_obj_align(cu, LV_ALIGN_TOP_RIGHT, -14, 10);

    lv_obj_t *plot = ui::box(s_chartCard);
    lv_obj_set_pos(plot, 14, 30);
    lv_obj_set_size(plot, 404, CHART_H);
    static const AuroraLevel THR_LVL[3] = {AuroraLevel::Yellow, AuroraLevel::Amber, AuroraLevel::Red};
    for (int j = 0; j < 3; j++) {
        s_thrLine[j] = ui::box(plot);
        lv_obj_set_size(s_thrLine[j], CHART_W, 1);
        lv_obj_set_pos(s_thrLine[j], 0, thresholdY(j == 0 ? 50.0f : (j == 1 ? 100.0f : 200.0f)));
        lv_obj_set_style_bg_color(s_thrLine[j], hex(auroraLevelColor(THR_LVL[j])), 0);
        lv_obj_set_style_bg_opa(s_thrLine[j], LV_OPA_50, 0);
        s_thrLbl[j] = ui::label(plot, &lv_font_montserrat_12, ui::MUTED, "");
        lv_obj_set_pos(s_thrLbl[j], 384, thresholdY(j == 0 ? 50.0f : (j == 1 ? 100.0f : 200.0f)) - 6);
    }
    for (int i = 0; i < BAR_COUNT; i++) {
        lv_obj_t *b = dot(plot, BAR_W, 2, 0x3A4654);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * BAR_STEP, 0);
        lv_obj_set_style_radius(b, 2, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        s_bars[i] = b;
    }
    for (int q = 0; q < 4; q++) {
        s_axis[q] = ui::label(s_chartCard, &lv_font_montserrat_12, ui::MUTED, "");
        lv_obj_set_pos(s_axis[q], 14 + q * 96, 30 + CHART_H + 3);
    }
    s_axisNow = ui::label(s_chartCard, &lv_font_montserrat_12, ui::TEXT, "now");
    lv_obj_set_pos(s_axisNow, 14 + 380 - 24, 30 + CHART_H + 3);

    // ---- Kp strip ----
    s_kpCard = ui::card(s_scr, 24, 276, 432, 111);
    lv_obj_t *kt = ui::caption(s_kpCard, ui::MUTED, "KP INDEX, 3-HOURLY (UTC)");
    lv_obj_set_pos(kt, 14, 10);
    // Legend, right-aligned: [solid] seen  [outline] forecast
    lv_obj_t *leg = ui::box(s_kpCard);
    lv_obj_set_flex_flow(leg, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(leg, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(leg, 6, 0);
    legendSwatch(leg, true);
    ui::label(leg, &lv_font_montserrat_12, ui::MUTED, "seen");
    legendSwatch(leg, false);
    ui::label(leg, &lv_font_montserrat_12, ui::MUTED, "forecast");
    lv_obj_align(leg, LV_ALIGN_TOP_RIGHT, -14, 9);

    static const char *const HDR[4] = {"00", "06", "12", "18"};
    for (int h = 0; h < 4; h++) {
        lv_obj_t *hl = ui::label(s_kpCard, &lv_font_montserrat_12, ui::MUTED, HDR[h]);
        lv_obj_set_pos(hl, 14 + KP_LABEL_W + h * 2 * KP_CELL_STEP, 28);
    }
    for (int r = 0; r < KP_ROWS; r++) {
        int y = 44 + r * (KP_CELL_H + 3);
        s_kpDay[r] = ui::label(s_kpCard, &lv_font_montserrat_12, ui::MUTED, "--");
        lv_obj_set_pos(s_kpDay[r], 14, y + 2);
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
    s_hintBox = ui::box(s_scr);
    lv_obj_set_pos(s_hintBox, 24, 393);
    lv_obj_set_size(s_hintBox, 432, 28);
    lv_obj_set_style_radius(s_hintBox, 14, 0);
    lv_obj_set_style_bg_color(s_hintBox, ui::CARD, 0);
    lv_obj_set_style_bg_opa(s_hintBox, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_hintBox, 2, 0);
    lv_obj_set_style_border_color(s_hintBox, ui::MUTED, 0);
    s_hint = ui::label(s_hintBox, &lv_font_montserrat_14, ui::MUTED, "--");
    lv_obj_set_width(s_hint, 404);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_align(s_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 0);

    s_updated = ui::label(s_scr, &lv_font_montserrat_12, ui::MUTED, "Updated --");
    lv_obj_set_pos(s_updated, 26, 430);

    ui::pageDots(s_scr, ui::SCR_AURORA);
    return s_scr;
}

void auroraRefresh() {
    if (!s_scr) return;
    ui::topBarUpdate(s_top);
    update(false);
}

void auroraOnShow() {
    if (!s_scr) return;
    update(true);
    lv_obj_t *cards[4] = {s_statusCard, s_chartCard, s_kpCard, s_hintBox};
    for (int i = 0; i < 4; i++) {
        lv_obj_set_style_opa(cards[i], LV_OPA_TRANSP, 0);
        ui::animFadeIn(cards[i], 80 + i * 120);
    }
}
