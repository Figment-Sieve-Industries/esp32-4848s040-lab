// R: Sun (space weather) - SOHO photo, X-ray flare class + odds, NOAA R/S/G scales, solar wind
// strip, sunspots / F10.7. Geometry follows the "R Sun (space weather)" board of the design
// canvas (480x480). No data credits here: they live on the About screen.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <src/misc/cache/instance/lv_image_cache.h>
#include "screens.h"
#include "ui_common.h"
#include "space.h"

namespace {

constexpr int WIND_BARS = SunData::WIND_POINTS;
constexpr int STRIP_W = 404;
constexpr int STRIP_H = 22;
constexpr int BAR_W = 5;
constexpr int PROB_BAR_W = 142;

// NOAA scale colours: 0 none, 1 green, 2 yellow, 3 orange, 4 red, 5 purple
const uint32_t SCALE_COL[6] = {0x2A3440, 0x3DDC84, 0xF5D400, 0xFF8A3D, 0xE5484D, 0xB266FF};
const char *const SCALE_NAME[6] = {"none", "minor", "moderate", "strong", "severe", "extreme"};
const uint32_t C_NORTH = 0x5AB0FF;
const uint32_t C_SOUTH = 0xFF8A3D;
const uint32_t C_FLARE_C = 0xF5D400;
const uint32_t C_FLARE_M = 0xFF8A3D;
const uint32_t C_FLARE_X = 0xE5484D;
const uint32_t C_PLACEHOLDER = 0x3A2008;
const uint32_t C_SUN_BG = 0x000000;

lv_obj_t *s_scr = nullptr;
ui::TopBar s_top;

lv_obj_t *s_sunHolder = nullptr;
lv_obj_t *s_placeholder = nullptr;
lv_obj_t *s_img = nullptr;
lv_image_dsc_t s_dsc;

lv_obj_t *s_flareNow = nullptr;
lv_obj_t *s_flareMax = nullptr;
lv_obj_t *s_probBox = nullptr;
lv_obj_t *s_probKey[3] = {};
lv_obj_t *s_probFill[3] = {};
lv_obj_t *s_probVal[3] = {};

lv_obj_t *s_chip[3] = {};
lv_obj_t *s_chipBadge[3] = {};
lv_obj_t *s_chipK[3] = {};
lv_obj_t *s_chipName[3] = {};

lv_obj_t *s_windCard = nullptr;
lv_obj_t *s_speed = nullptr;
lv_obj_t *s_density = nullptr;
lv_obj_t *s_bz = nullptr;
lv_obj_t *s_bzUnit = nullptr;
lv_obj_t *s_bars[WIND_BARS] = {};

lv_obj_t *s_rowCard = nullptr;
lv_obj_t *s_spots = nullptr;
lv_obj_t *s_f107 = nullptr;
lv_obj_t *s_updated = nullptr;

struct Key {
    uint32_t fetchedMs = 0;
    bool stale = true;
    bool valid = false;
};
Key s_key;
bool s_haveKey = false;
uint32_t s_imgVer = 0;
const uint16_t *s_imgPtr = nullptr;
bool s_haveImg = false;

lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

lv_color_t flareColor(const char *cls, bool stale) {
    if (stale || !cls || !*cls) return ui::MUTED;
    switch (cls[0]) {
        case 'C': return hex(C_FLARE_C);
        case 'M': return hex(C_FLARE_M);
        case 'X': return hex(C_FLARE_X);
        default: return ui::MUTED; // A, B
    }
}

lv_color_t flareColorOf(char letter) {
    switch (letter) {
        case 'C': return hex(C_FLARE_C);
        case 'M': return hex(C_FLARE_M);
        default: return hex(C_FLARE_X);
    }
}

// Value + unit on one baseline-ish row; returns the container, value label via out param.
lv_obj_t *column(lv_obj_t *parent, const char *title, const char *unit, lv_obj_t **value, lv_obj_t **unitLbl) {
    lv_obj_t *col = ui::box(parent);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 0, 0);
    ui::label(col, &lv_font_montserrat_12, ui::MUTED, title);
    lv_obj_t *row = ui::box(col);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 4, 0);
    *value = ui::label(row, &lv_font_montserrat_26, ui::TEXT, "--");
    lv_obj_t *u = ui::label(row, &lv_font_montserrat_12, ui::MUTED, unit);
    if (unitLbl) *unitLbl = u;
    return col;
}

int barHeight(int kms) {
    int h = static_cast<int>(lroundf(static_cast<float>(kms - 300) / 400.0f * STRIP_H));
    if (h < 4) h = 4;
    if (h > STRIP_H) h = STRIP_H;
    return h;
}

// ---- updates ---------------------------------------------------------------------------------

void updateImage() {
    uint32_t ver = 0;
    time_t taken = 0;
    const uint16_t *px = sunImage(ver, taken);
    if (!px) return;
    if (s_haveImg && px == s_imgPtr && ver == s_imgVer) return;
    s_imgPtr = px;
    s_imgVer = ver;
    s_dsc.data = reinterpret_cast<const uint8_t *>(px);
    if (!s_haveImg) {
        s_haveImg = true;
        lv_image_set_src(s_img, &s_dsc);
        lv_obj_remove_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_image_cache_drop(&s_dsc);
        lv_obj_invalidate(s_img);
    }
}

void updateFlare(const SunData &d, bool stale) {
    const bool have = d.valid && d.flareNow[0];
    ui::setTextColor(s_flareNow, flareColor(d.flareNow, stale || !have));
    ui::setText(s_flareNow, have ? d.flareNow : "--");

    char buf[40];
    if (have && d.flareMax[0] && d.flareMaxAt > 0) {
        struct tm tmv;
        time_t t = d.flareMaxAt;
        localtime_r(&t, &tmv);
        snprintf(buf, sizeof(buf), "latest max %s at %02d:%02d", d.flareMax, tmv.tm_hour, tmv.tm_min);
    } else if (have && d.flareMax[0]) {
        snprintf(buf, sizeof(buf), "latest max %s", d.flareMax);
    } else {
        snprintf(buf, sizeof(buf), "latest max --");
    }
    ui::setText(s_flareMax, buf);

    const int8_t p[3] = {d.probC, d.probM, d.probX};
    static const char KEYS[3] = {'C', 'M', 'X'};
    for (int i = 0; i < 3; i++) {
        const bool known = d.valid && p[i] >= 0;
        lv_color_t col = (known && !stale) ? flareColorOf(KEYS[i]) : ui::MUTED;
        ui::setTextColor(s_probKey[i], col);
        ui::setBgColor(s_probFill[i], col);
        int pct = known ? (p[i] > 100 ? 100 : p[i]) : 0;
        int w = pct * PROB_BAR_W / 100;
        ui::setHidden(s_probFill[i], w < 2);
        if (w >= 2) ui::setWidth(s_probFill[i], w);
        char b[8];
        if (known) snprintf(b, sizeof(b), "%d%%", pct);
        else snprintf(b, sizeof(b), "--");
        ui::setText(s_probVal[i], b);
        ui::setTextColor(s_probVal[i], stale ? ui::MUTED : ui::TEXT);
    }
}

void updateScales(const SunData &d, bool stale) {
    const int8_t v[3] = {d.scaleR, d.scaleS, d.scaleG};
    for (int i = 0; i < 3; i++) {
        int n = v[i]; // NOLINT(bugprone-signed-char-misuse) small signed value, -1 = unknown
        const bool known = d.valid && n >= 0;
        if (n > 5) n = 5;
        if (!known) n = 0;
        char b[4];
        static const char KEYS[3] = {'R', 'S', 'G'};
        snprintf(b, sizeof(b), "%c%c", KEYS[i], known ? static_cast<char>('0' + n) : '-');
        ui::setText(s_chipK[i], b);
        const bool lit = known && !stale && n > 0;
        lv_obj_set_style_border_color(s_chipBadge[i], hex(lit ? SCALE_COL[n] : SCALE_COL[0]), 0);
        lv_obj_set_style_bg_opa(s_chipBadge[i], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (lit) ui::setBgColor(s_chipBadge[i], hex(SCALE_COL[n]));
        ui::setTextColor(s_chipK[i], lit ? ui::BG : ui::MUTED);
        ui::setText(s_chipName[i], known ? SCALE_NAME[n] : "--");
        ui::setTextColor(s_chipName[i], (known && !stale) ? ui::TEXT : ui::MUTED);
    }
}

void updateWind(const SunData &d, bool stale) {
    char b[16];
    const bool spd = d.valid && d.windKms >= 0;
    if (spd) snprintf(b, sizeof(b), "%d", static_cast<int>(lroundf(d.windKms)));
    else snprintf(b, sizeof(b), "--");
    ui::setText(s_speed, b);
    ui::setTextColor(s_speed, (spd && !stale) ? ui::TEXT : ui::MUTED);

    const bool den = d.valid && d.densityCc >= 0;
    if (den) snprintf(b, sizeof(b), d.densityCc >= 10.0f ? "%.0f" : "%.1f", static_cast<double>(d.densityCc));
    else snprintf(b, sizeof(b), "--");
    ui::setText(s_density, b);
    ui::setTextColor(s_density, (den && !stale) ? ui::TEXT : ui::MUTED);

    const bool bzOk = d.valid && d.bzValid;
    if (bzOk) snprintf(b, sizeof(b), "%+.1f", static_cast<double>(d.bzNt));
    else snprintf(b, sizeof(b), "--");
    ui::setText(s_bz, b);
    ui::setText(s_bzUnit, !bzOk ? "nT" : (d.bzNt < 0 ? "nT south" : "nT north"));
    ui::setTextColor(s_bz, (bzOk && !stale) ? hex(d.bzNt < 0 ? C_SOUTH : C_NORTH) : ui::MUTED);

    const int n = (d.valid && d.windCount > 0) ? (d.windCount > WIND_BARS ? WIND_BARS : d.windCount) : 0;
    const int first = WIND_BARS - n; // newest bar sits at the right edge
    for (int i = 0; i < WIND_BARS; i++) {
        if (i < first) {
            ui::setHidden(s_bars[i], true);
            continue;
        }
        const int k = i - first;
        ui::setHidden(s_bars[i], false);
        ui::setHeight(s_bars[i], barHeight(d.windSpeed[k]));
        lv_color_t col = ui::TRACK;
        if (!stale) col = hex(d.windBz[k] < 0 ? C_SOUTH : C_NORTH);
        ui::setBgColor(s_bars[i], col);
    }
}

void updateRow(const SunData &d, bool stale) {
    char b[16];
    const bool sp = d.valid && d.sunspots >= 0;
    if (sp) snprintf(b, sizeof(b), "%d", d.sunspots);
    else snprintf(b, sizeof(b), "--");
    ui::setText(s_spots, b);
    ui::setTextColor(s_spots, (sp && !stale) ? ui::TEXT : ui::MUTED);

    const bool fl = d.valid && d.f107 >= 0;
    if (fl) snprintf(b, sizeof(b), "%d", d.f107);
    else snprintf(b, sizeof(b), "--");
    ui::setText(s_f107, b);
    ui::setTextColor(s_f107, (fl && !stale) ? ui::TEXT : ui::MUTED);

    char u[24];
    if (d.valid && d.updatedAt > 0) {
        struct tm tmv;
        time_t t = d.updatedAt;
        localtime_r(&t, &tmv);
        snprintf(u, sizeof(u), "updated %02d:%02d", tmv.tm_hour, tmv.tm_min);
    } else {
        snprintf(u, sizeof(u), "updated --");
    }
    ui::setText(s_updated, u);
}

void update(bool force) {
    updateImage();

    const SunData d = sunData();
    const bool stale = !d.valid || (millis() - d.fetchedMs) > SPACE_STALE_MS;
    if (!force && s_haveKey && d.fetchedMs == s_key.fetchedMs && stale == s_key.stale && d.valid == s_key.valid) return;
    s_key.fetchedMs = d.fetchedMs;
    s_key.stale = stale;
    s_key.valid = d.valid;
    s_haveKey = true;

    updateFlare(d, stale);
    updateScales(d, stale);
    updateWind(d, stale);
    updateRow(d, stale);
}

} // namespace

lv_obj_t *sunCreate() {
    s_scr = ui::screenCreate();
    s_top = ui::topBarCreate(s_scr);

    // ---- Sun image: black round holder, dark-orange placeholder disc until the photo arrives ----
    s_sunHolder = ui::box(s_scr);
    lv_obj_set_pos(s_sunHolder, 24, 52);
    lv_obj_set_size(s_sunHolder, SUN_IMG_PX, SUN_IMG_PX);
    lv_obj_set_style_radius(s_sunHolder, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_clip_corner(s_sunHolder, true, 0);
    lv_obj_set_style_bg_color(s_sunHolder, hex(C_SUN_BG), 0);
    lv_obj_set_style_bg_opa(s_sunHolder, LV_OPA_COVER, 0);

    s_placeholder = ui::box(s_sunHolder);
    lv_obj_set_size(s_placeholder, 188, 188);
    lv_obj_align(s_placeholder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(s_placeholder, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_placeholder, hex(C_PLACEHOLDER), 0);
    lv_obj_set_style_bg_opa(s_placeholder, LV_OPA_COVER, 0);

    memset(&s_dsc, 0, sizeof(s_dsc));
    s_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_dsc.header.w = SUN_IMG_PX;
    s_dsc.header.h = SUN_IMG_PX;
    s_dsc.header.stride = SUN_IMG_PX * 2;
    s_dsc.data_size = static_cast<uint32_t>(SUN_IMG_PX) * SUN_IMG_PX * 2;
    s_img = lv_image_create(s_sunHolder);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

    // ---- Flare class now ----
    lv_obj_t *fc = ui::caption(s_scr, ui::MUTED, "X-RAY FLARE CLASS NOW");
    lv_obj_set_pos(fc, 244, 54);
    s_flareNow = ui::label(s_scr, &lv_font_montserrat_48, ui::MUTED, "--");
    lv_obj_set_pos(s_flareNow, 244, 68);
    s_flareMax = ui::label(s_scr, &lv_font_montserrat_12, ui::MUTED, "latest max --");
    lv_obj_set_pos(s_flareMax, 244, 124);

    lv_obj_t *pc = ui::caption(s_scr, ui::MUTED, "FLARE CHANCE TODAY");
    lv_obj_set_pos(pc, 244, 160);
    s_probBox = ui::box(s_scr);
    lv_obj_set_pos(s_probBox, 244, 180);
    lv_obj_set_size(s_probBox, 212, 3 * 20 + 2 * 6);
    static const char *const KEYS[3] = {"C", "M", "X"};
    for (int i = 0; i < 3; i++) {
        const int y = i * 26;
        s_probKey[i] = ui::label(s_probBox, &lv_font_montserrat_14, ui::MUTED, KEYS[i]);
        lv_obj_set_pos(s_probKey[i], 0, y + 2);
        lv_obj_t *track = ui::box(s_probBox);
        lv_obj_set_size(track, PROB_BAR_W, 8);
        lv_obj_set_pos(track, 22, y + 6);
        lv_obj_set_style_radius(track, 4, 0);
        lv_obj_set_style_bg_color(track, ui::TRACK, 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        s_probFill[i] = ui::box(track);
        lv_obj_set_size(s_probFill[i], 4, 8);
        lv_obj_set_pos(s_probFill[i], 0, 0);
        lv_obj_set_style_radius(s_probFill[i], 4, 0);
        lv_obj_set_style_bg_color(s_probFill[i], ui::MUTED, 0);
        lv_obj_set_style_bg_opa(s_probFill[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(s_probFill[i], LV_OBJ_FLAG_HIDDEN);
        s_probVal[i] = ui::label(s_probBox, &lv_font_montserrat_14, ui::MUTED, "--");
        lv_obj_set_width(s_probVal[i], 40);
        lv_obj_set_style_text_align(s_probVal[i], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_pos(s_probVal[i], 172, y + 2);
    }

    // ---- NOAA R / S / G chips ----
    static const char *const SC_LABEL[3] = {"Radio", "Radiation", "Geomagnetic"};
    for (int i = 0; i < 3; i++) {
        s_chip[i] = ui::card(s_scr, 24 + i * 147, 260, 138, 44);
        s_chipBadge[i] = ui::box(s_chip[i]);
        lv_obj_set_size(s_chipBadge[i], 40, 30);
        lv_obj_set_pos(s_chipBadge[i], 8, 7);
        lv_obj_set_style_radius(s_chipBadge[i], 8, 0);
        lv_obj_set_style_border_width(s_chipBadge[i], 2, 0);
        lv_obj_set_style_border_color(s_chipBadge[i], hex(SCALE_COL[0]), 0);
        lv_obj_set_style_bg_color(s_chipBadge[i], hex(SCALE_COL[0]), 0);
        lv_obj_set_style_bg_opa(s_chipBadge[i], LV_OPA_TRANSP, 0);
        s_chipK[i] = ui::label(s_chipBadge[i], &lv_font_montserrat_16, ui::MUTED, "--");
        lv_obj_center(s_chipK[i]);
        lv_obj_t *l = ui::label(s_chip[i], &lv_font_montserrat_12, ui::MUTED, SC_LABEL[i]);
        lv_obj_set_pos(l, 56, 6);
        s_chipName[i] = ui::label(s_chip[i], &lv_font_montserrat_14, ui::MUTED, "--");
        lv_obj_set_pos(s_chipName[i], 56, 21);
    }

    // ---- Solar wind ----
    s_windCard = ui::card(s_scr, 24, 312, 432, 100);
    lv_obj_t *wt = ui::caption(s_windCard, ui::MUTED, "SOLAR WIND, LAST HOUR");
    lv_obj_set_pos(wt, 14, 8);
    lv_obj_t *wh = ui::label(s_windCard, &lv_font_montserrat_12, ui::MUTED, "bar = speed, orange = Bz south");
    lv_obj_align(wh, LV_ALIGN_TOP_RIGHT, -14, 8);

    lv_obj_t *c1 = column(s_windCard, "Speed", "km/s", &s_speed, nullptr);
    lv_obj_align(c1, LV_ALIGN_TOP_LEFT, 14, 24);
    lv_obj_t *c2 = column(s_windCard, "Density", "p/cc", &s_density, nullptr);
    lv_obj_align(c2, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *c3 = column(s_windCard, "Bz", "nT", &s_bz, &s_bzUnit);
    lv_obj_align(c3, LV_ALIGN_TOP_RIGHT, -14, 24);

    lv_obj_t *strip = ui::box(s_windCard);
    lv_obj_set_pos(strip, 14, 70);
    lv_obj_set_size(strip, STRIP_W, STRIP_H);
    for (int i = 0; i < WIND_BARS; i++) {
        lv_obj_t *b = ui::box(strip);
        lv_obj_set_size(b, BAR_W, 4);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_color(b, ui::TRACK, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_align(b, LV_ALIGN_BOTTOM_LEFT, i * (STRIP_W - BAR_W) / (WIND_BARS - 1), 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_HIDDEN);
        s_bars[i] = b;
    }

    // ---- Sunspots / F10.7 / updated ----
    s_rowCard = ui::card(s_scr, 24, 418, 432, 30);
    lv_obj_t *r1 = ui::box(s_rowCard);
    lv_obj_set_flex_flow(r1, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r1, 5, 0);
    ui::label(r1, &lv_font_montserrat_14, ui::MUTED, "Sunspots");
    s_spots = ui::label(r1, &lv_font_montserrat_14, ui::MUTED, "--");
    lv_obj_align(r1, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *r2 = ui::box(s_rowCard);
    lv_obj_set_flex_flow(r2, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(r2, 5, 0);
    ui::label(r2, &lv_font_montserrat_14, ui::MUTED, "F10.7");
    s_f107 = ui::label(r2, &lv_font_montserrat_14, ui::MUTED, "--");
    lv_obj_align(r2, LV_ALIGN_CENTER, -20, 0);
    s_updated = ui::label(s_rowCard, &lv_font_montserrat_14, ui::MUTED, "updated --");
    lv_obj_align(s_updated, LV_ALIGN_RIGHT_MID, -14, 0);

    ui::pageDots(s_scr, ui::SCR_SUN);
    return s_scr;
}

void sunRefresh() {
    if (!s_scr) return;
    ui::topBarUpdate(s_top);
    update(false);
}

void sunOnShow() {
    if (!s_scr) return;
    update(true);
    lv_obj_t *blocks[5] = {s_sunHolder, s_probBox, s_windCard, s_rowCard, s_chip[0]};
    for (int i = 0; i < 5; i++) {
        lv_obj_set_style_opa(blocks[i], LV_OPA_TRANSP, 0);
        ui::animFadeIn(blocks[i], 80 + i * 100);
    }
    for (int i = 1; i < 3; i++) {
        lv_obj_set_style_opa(s_chip[i], LV_OPA_TRANSP, 0);
        ui::animFadeIn(s_chip[i], 480);
    }
}
