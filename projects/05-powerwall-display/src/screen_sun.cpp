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
#include "ui_chart.h"
#include "shared_state.h"
#include "time_util.h"
#include "space.h"

namespace {

constexpr int WIND_BARS = 58;   // newest 58 of the 60 wind points: 58 bars of 5 px at a 7 px step fill the 404 px strip
constexpr int WIND_STEP = 7;
constexpr int STRIP_W = 404;
constexpr int STRIP_H = 22;
constexpr int BAR_W = 5;
constexpr int PROB_BAR_W = 142;

const char *const SCALE_NAME[6] = {"none", "minor", "moderate", "strong", "severe", "extreme"};
const lv_color_t C_NORTH = lv_color_hex(0x5AB0FF);
const lv_color_t C_PLACEHOLDER = lv_color_hex(0x3A2008);

lv_obj_t *s_sunHolder = nullptr;
lv_obj_t *s_placeholder = nullptr;
lv_obj_t *s_img = nullptr;
lv_image_dsc_t s_dsc;

lv_obj_t *s_flareNow = nullptr;
lv_obj_t *s_flareMax = nullptr;
lv_obj_t *s_probBox = nullptr;
lv_obj_t *s_probKey[3] = {};
ui::HBar s_probBar[3];
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
lv_obj_t *s_bars = nullptr;

lv_obj_t *s_rowCard = nullptr;
lv_obj_t *s_spots = nullptr;
lv_obj_t *s_f107 = nullptr;
lv_obj_t *s_updated = nullptr;

struct Key {
    uint32_t fetchedMs = 0;
    bool stale = true;
    bool valid = false;
    bool operator==(const Key &) const = default;
};
ui::ChangeGate<Key> s_gate;
uint32_t s_imgVer = 0;
const uint16_t *s_imgPtr = nullptr;
bool s_haveImg = false;

// NOAA scale colours: 0 none, 1 green, 2 yellow, 3 orange, 4 red, 5 purple
lv_color_t scaleColor(int n) {
    if (n <= 0) return ui::TRACK;
    if (n >= 5) return ui::C_PURPLE;
    return ui::levelColor(n - 1);
}

lv_color_t flareColor(const char *cls, bool stale) {
    if (stale || !cls || !*cls) return ui::MUTED;
    switch (cls[0]) {
        case 'C': return ui::C_YELLOW;
        case 'M': return ui::C_ORANGE;
        case 'X': return ui::C_RED;
        default: return ui::MUTED; // A, B
    }
}

lv_color_t flareColorOf(char letter) {
    switch (letter) {
        case 'C': return ui::C_YELLOW;
        case 'M': return ui::C_ORANGE;
        default: return ui::C_RED;
    }
}

// Small caption over a value + unit pair; returns the column, value/unit labels via out params.
lv_obj_t *column(lv_obj_t *parent, const char *title, const char *unit, lv_obj_t **value, lv_obj_t **unitLbl) {
    lv_obj_t *col = ui::flex(parent, LV_FLEX_FLOW_COLUMN, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui::label(col, &lv_font_montserrat_12, ui::MUTED, title);
    lv_obj_t *u = nullptr;
    ui::valueUnit(col, &lv_font_montserrat_26, &lv_font_montserrat_12, value, &u, 0);
    ui::setText(*value, "--");
    ui::setTextColor(*value, ui::TEXT);
    ui::setText(u, unit);
    ui::setTextColor(u, ui::MUTED);
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

    if (have && d.flareMax[0] && d.flareMaxAt > 0) {
        char hhmm[8];
        tu::fmtHHMM(hhmm, sizeof(hhmm), d.flareMaxAt);
        ui::setTextF(s_flareMax, "latest max %s at %s", d.flareMax, hhmm);
    } else if (have && d.flareMax[0]) {
        ui::setTextF(s_flareMax, "latest max %s", d.flareMax);
    } else {
        ui::setText(s_flareMax, "latest max --");
    }

    const int p[3] = {d.probC, d.probM, d.probX};
    static const char KEYS[3] = {'C', 'M', 'X'};
    for (int i = 0; i < 3; i++) {
        const bool known = d.valid && p[i] >= 0;
        const lv_color_t col = (known && !stale) ? flareColorOf(KEYS[i]) : ui::MUTED;
        ui::setTextColor(s_probKey[i], col);
        const int pct = known ? (p[i] > 100 ? 100 : p[i]) : 0;
        const int w = pct * PROB_BAR_W / 100;
        ui::hbarSet(s_probBar[i], w >= 2 ? static_cast<float>(w) / PROB_BAR_W : 0.0f, col);
        ui::setValueF(s_probVal[i], known, stale, ui::TEXT, "%d%%", pct);
    }
}

void updateScales(const SunData &d, bool stale) {
    const int v[3] = {d.scaleR, d.scaleS, d.scaleG};
    static const char KEYS[3] = {'R', 'S', 'G'};
    for (int i = 0; i < 3; i++) {
        int n = v[i];
        const bool known = d.valid && n >= 0;
        if (n > 5) n = 5;
        if (!known) n = 0;
        ui::setTextF(s_chipK[i], "%c%c", KEYS[i], known ? static_cast<char>('0' + n) : '-');
        const bool lit = known && !stale && n > 0;
        lv_obj_set_style_border_color(s_chipBadge[i], scaleColor(lit ? n : 0), 0);
        lv_obj_set_style_bg_opa(s_chipBadge[i], lit ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        if (lit) ui::setBgColor(s_chipBadge[i], scaleColor(n));
        ui::setTextColor(s_chipK[i], lit ? ui::BG : ui::MUTED);
        ui::setText(s_chipName[i], known ? SCALE_NAME[n] : "--");
        ui::setTextColor(s_chipName[i], (known && !stale) ? ui::TEXT : ui::MUTED);
    }
}

void updateWind(const SunData &d, bool stale) {
    const bool spd = d.valid && d.windKms >= 0;
    ui::setValueF(s_speed, spd, stale, ui::TEXT, "%d", static_cast<int>(lroundf(d.windKms)));

    const bool den = d.valid && d.densityCc >= 0;
    ui::setValueF(s_density, den, stale, ui::TEXT, d.densityCc >= 10.0f ? "%.0f" : "%.1f",
                  static_cast<double>(d.densityCc));

    const bool bzOk = d.valid && d.bzValid;
    ui::setValueF(s_bz, bzOk, stale, d.bzNt < 0 ? ui::C_ORANGE : C_NORTH, "%+.1f", static_cast<double>(d.bzNt));
    ui::setText(s_bzUnit, !bzOk ? "nT" : (d.bzNt < 0 ? "nT south" : "nT north"));

    // The newest WIND_BARS points, newest at the right edge
    const int avail = (d.valid && d.windCount > 0) ? (d.windCount > SunData::WIND_POINTS ? SunData::WIND_POINTS : d.windCount) : 0;
    const int n = avail > WIND_BARS ? WIND_BARS : avail;
    const int off = avail - n;
    int16_t heights[WIND_BARS];
    lv_color_t cols[WIND_BARS];
    for (int k = 0; k < n; k++) {
        heights[k] = static_cast<int16_t>(barHeight(d.windSpeed[off + k]));
        cols[k] = stale ? ui::TRACK : (d.windBz[off + k] < 0 ? ui::C_ORANGE : C_NORTH);
    }
    ui::barChartSetNewest(s_bars, n, heights, cols);
}

void updateRow(const SunData &d, bool stale) {
    ui::setValueF(s_spots, d.valid && d.sunspots >= 0, stale, ui::TEXT, "%d", d.sunspots);
    ui::setValueF(s_f107, d.valid && d.f107 >= 0, stale, ui::TEXT, "%d", d.f107);

    char hhmm[8];
    tu::fmtHHMM(hhmm, sizeof(hhmm), d.valid ? d.updatedAt : 0);
    ui::setTextF(s_updated, "updated %s", hhmm);
}

void update(bool force) {
    updateImage();

    const SunData d = sunData();
    const bool stale = isStale(d.valid, d.fetchedMs, SPACE_STALE_MS);
    Key key;
    key.fetchedMs = d.fetchedMs;
    key.stale = stale;
    key.valid = d.valid;
    if (!s_gate.changed(key, force)) return;

    updateFlare(d, stale);
    updateScales(d, stale);
    updateWind(d, stale);
    updateRow(d, stale);
}

} // namespace

lv_obj_t *sunCreate() {
    lv_obj_t *scr = ui::screenCreate(ui::SCR_SUN);

    // ---- Sun image: black round holder, dark-orange placeholder disc until the photo arrives.
    // The photo's corners (r >= 100) are painted in the background colour by space.cpp, so no clip_corner.
    s_sunHolder = ui::rect(scr, 24, 52, SUN_IMG_PX, SUN_IMG_PX, lv_color_black(), LV_RADIUS_CIRCLE);
    s_placeholder = ui::rect(s_sunHolder, (SUN_IMG_PX - 188) / 2, (SUN_IMG_PX - 188) / 2, 188, 188, C_PLACEHOLDER,
                             LV_RADIUS_CIRCLE);

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
    ui::cardCaption(scr, "X-RAY FLARE CLASS NOW", 244, 54);
    s_flareNow = ui::labelAt(scr, &lv_font_montserrat_48, ui::MUTED, "--", 244, 68);
    s_flareMax = ui::labelAt(scr, &lv_font_montserrat_12, ui::MUTED, "latest max --", 244, 124);

    ui::cardCaption(scr, "FLARE CHANCE TODAY", 244, 160);
    s_probBox = ui::box(scr);
    lv_obj_set_pos(s_probBox, 244, 180);
    lv_obj_set_size(s_probBox, 212, 3 * 20 + 2 * 6);
    static const char *const KEYS[3] = {"C", "M", "X"};
    for (int i = 0; i < 3; i++) {
        const int y = i * 26;
        s_probKey[i] = ui::labelAt(s_probBox, &lv_font_montserrat_14, ui::MUTED, KEYS[i], 0, y + 2);
        s_probBar[i] = ui::hbar(s_probBox, 22, y + 6, PROB_BAR_W, 8, ui::TRACK, ui::MUTED, 4);
        ui::hbarSet(s_probBar[i], 0.0f, ui::MUTED);
        s_probVal[i] = ui::labelAt(s_probBox, &lv_font_montserrat_14, ui::MUTED, "--", 172, y + 2, 40, LV_TEXT_ALIGN_RIGHT);
    }

    // ---- NOAA R / S / G chips ----
    static const char *const SC_LABEL[3] = {"Radio", "Radiation", "Geomagnetic"};
    for (int i = 0; i < 3; i++) {
        s_chip[i] = ui::card(scr, 24 + i * 147, 260, 138, 44);
        s_chipBadge[i] = ui::rect(s_chip[i], 7, 7, 36, 30, ui::TRACK, 8);
        lv_obj_set_style_border_width(s_chipBadge[i], 2, 0);
        lv_obj_set_style_border_color(s_chipBadge[i], ui::TRACK, 0);
        lv_obj_set_style_bg_opa(s_chipBadge[i], LV_OPA_TRANSP, 0);
        s_chipK[i] = ui::label(s_chipBadge[i], &lv_font_montserrat_16, ui::MUTED, "--");
        lv_obj_center(s_chipK[i]);
        ui::labelAt(s_chip[i], &lv_font_montserrat_12, ui::MUTED, SC_LABEL[i], 50, 6); // "Geomagnetic" needs x <= 50
        s_chipName[i] = ui::labelAt(s_chip[i], &lv_font_montserrat_14, ui::MUTED, "--", 50, 21);
    }

    // ---- Solar wind ----
    s_windCard = ui::card(scr, 24, 312, 432, 100);
    ui::cardCaption(s_windCard, "SOLAR WIND, LAST HOUR", 14, 8);
    lv_obj_t *wh = ui::label(s_windCard, &lv_font_montserrat_12, ui::MUTED, "bar = speed, orange = Bz south");
    lv_obj_align(wh, LV_ALIGN_TOP_RIGHT, -14, 8);

    lv_obj_t *c1 = column(s_windCard, "Speed", "km/s", &s_speed, nullptr);
    lv_obj_align(c1, LV_ALIGN_TOP_LEFT, 14, 24);
    lv_obj_t *c2 = column(s_windCard, "Density", "p/cc", &s_density, nullptr);
    lv_obj_align(c2, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *c3 = column(s_windCard, "Bz", "nT", &s_bz, &s_bzUnit);
    lv_obj_align(c3, LV_ALIGN_TOP_RIGHT, -14, 24);

    s_bars = ui::barChartCreate(s_windCard, 14, 70, STRIP_W, STRIP_H, WIND_BARS, BAR_W, WIND_STEP, 1);

    // ---- Sunspots / F10.7 / updated ----
    s_rowCard = ui::card(scr, 24, 418, 432, 30);
    lv_obj_t *r1 = ui::flex(s_rowCard, LV_FLEX_FLOW_ROW, 5);
    ui::label(r1, &lv_font_montserrat_14, ui::MUTED, "Sunspots");
    s_spots = ui::label(r1, &lv_font_montserrat_14, ui::MUTED, "--");
    lv_obj_align(r1, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_t *r2 = ui::flex(s_rowCard, LV_FLEX_FLOW_ROW, 5);
    ui::label(r2, &lv_font_montserrat_14, ui::MUTED, "F10.7");
    s_f107 = ui::label(r2, &lv_font_montserrat_14, ui::MUTED, "--");
    lv_obj_align(r2, LV_ALIGN_CENTER, -20, 0);
    s_updated = ui::label(s_rowCard, &lv_font_montserrat_14, ui::MUTED, "updated --");
    lv_obj_align(s_updated, LV_ALIGN_RIGHT_MID, -14, 0);

    return scr;
}

void sunRefresh() { update(false); }

void sunOnShow() {
    lv_obj_t *const blocks[5] = {s_sunHolder, s_probBox, s_windCard, s_rowCard, s_chip[0]};
    ui::fadeIn(blocks, 5, 80, 100);
    ui::fadeIn(&s_chip[1], 2, 480, 0);
}
