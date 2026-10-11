// Moon screen (design canvas board "P / Moon", 480x480): a real photograph of the full moon
// (moon_photo.c, NASA LRO imagery) shaded on-device to the current phase, plus phase name,
// illumination, age, rise/set, next full/new moon and an 8-step phase strip.
//
// Phase shading: the 180x180 canvas (PSRAM) is a copy of the photo with the unlit part multiplied
// by ~0.12 (earthshine). Per row y the terminator is at x = +-w(y) * (1 - 2 * illum), where w(y) is
// the disc's half-width on that row; waxing (northern hemisphere) is lit on the right, waning on
// the left. The edge is blended over ~3 px. Re-rendered only when the illumination moved by more
// than 0.5 % or waxing flipped.
#include "screens.h"
#include "ui_common.h"
#include "mem_util.h"
#include "moon_calc.h"
#include "moon_photo.h"
#include "sky.h"
#include "time_util.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <time.h>

namespace {

constexpr int MOON_X = 24, MOON_Y = 58;
constexpr int COL_X = 224, COL_W = 232;
constexpr int CARD_W = 210;
constexpr int INNER_W = CARD_W - 28; // card padding 14 each side
constexpr int STRIP_N = 8, STRIP_RING = 32, STRIP_DISC = 24;

constexpr float SOFT_PX = 3.0f;        // terminator blend width
constexpr int EARTHSHINE_256 = 31;     // 0.12 * 256
constexpr double RERENDER_DELTA = 0.005;
constexpr uint32_t UPDATE_MS = 60000, RETRY_MS = 2000;

const lv_color_t CREAM = lv_color_hex(0xEDE6CF);
const lv_color_t DISC_DARK = lv_color_hex(0x1A222C);

const char *const COMPASS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

// Phase strip: lit fraction per octant (0 new .. 4 full .. 7 waning crescent)
const float STRIP_FRAC[STRIP_N] = {0.0f, 0.146f, 0.5f, 0.854f, 1.0f, 0.854f, 0.5f, 0.146f};

lv_obj_t *s_moonCanvas;
uint8_t *s_moonBuf;
size_t s_moonStride;
lv_obj_t *s_lblName, *s_lblIllum, *s_lblAge;
ui::HBar s_cycleBar;
lv_obj_t *s_cardRise, *s_cardSet, *s_cardFull, *s_cardNew, *s_strip;
lv_obj_t *s_lblRise, *s_lblRiseAz, *s_lblSet, *s_lblSetDay, *s_lblSetAz;
lv_obj_t *s_lblFullIn, *s_lblFullDate, *s_lblFullTime, *s_lblFullName;
lv_obj_t *s_lblNewIn, *s_lblNewDate, *s_lblNewTime;
lv_obj_t *s_stripRing[STRIP_N];

double s_renderedIllum = -1;
bool s_renderedWaxing = true;
int s_shownOctant = -1;
uint32_t s_lastUpdateMs, s_retryAfterMs;
bool s_haveUpdated;

// Icon strokes: lv_line keeps a pointer to its points, so they live in statics.
constexpr float IS = 28.0f / 24.0f; // px per design unit of the 24x24 icon grid
lv_point_precise_t s_ptsHorizon[2], s_ptsRiseShaft[2], s_ptsRiseHead[3], s_ptsSetShaft[2], s_ptsSetHead[3];

void pt(lv_point_precise_t &p, float x, float y) {
    p.x = (lv_value_precise_t)(x * IS);
    p.y = (lv_value_precise_t)(y * IS);
}

void initIconPoints() {
    pt(s_ptsHorizon[0], 3, 20);
    pt(s_ptsHorizon[1], 21, 20);
    pt(s_ptsRiseShaft[0], 12, 11);
    pt(s_ptsRiseShaft[1], 12, 5);
    pt(s_ptsRiseHead[0], 9.5f, 7.5f);
    pt(s_ptsRiseHead[1], 12, 5);
    pt(s_ptsRiseHead[2], 14.5f, 7.5f);
    pt(s_ptsSetShaft[0], 12, 5);
    pt(s_ptsSetShaft[1], 12, 11);
    pt(s_ptsSetHead[0], 9.5f, 8.5f);
    pt(s_ptsSetHead[1], 12, 11);
    pt(s_ptsSetHead[2], 14.5f, 8.5f);
}

void strokeLine(lv_obj_t *parent, const lv_point_precise_t *pts, uint32_t n) {
    lv_obj_t *l = lv_line_create(parent);
    ui::inert(l);
    lv_line_set_points(l, pts, n);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_color(l, CREAM, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
}

// Moonrise / moonset glyph (same shapes as the design): horizon, half disc, arrow up / down.
lv_obj_t *horizonIcon(lv_obj_t *parent, bool rising) {
    lv_obj_t *c = ui::box(parent);
    lv_obj_set_size(c, 28, 28);
    strokeLine(c, s_ptsHorizon, 2);

    const int d = (int)lroundf(2 * 5 * IS + 2);
    lv_obj_t *a = lv_arc_create(c);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, (int)lroundf(12 * IS - d / 2.0f), (int)lroundf(20 * IS - d / 2.0f));
    lv_arc_set_bg_angles(a, 180, 360);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    ui::inert(a);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, CREAM, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);

    if (rising) {
        strokeLine(c, s_ptsRiseShaft, 2);
        strokeLine(c, s_ptsRiseHead, 3);
    } else {
        strokeLine(c, s_ptsSetShaft, 2);
        strokeLine(c, s_ptsSetHead, 3);
    }
    return c;
}

// Rise / set card: glyph on the left, three text lines on the right.
lv_obj_t *timeCard(lv_obj_t *scr, int x, bool rising, lv_obj_t *&time, lv_obj_t *&day, lv_obj_t *&az) {
    lv_obj_t *c = ui::card(scr, x, 258, CARD_W, 68);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 10, 0);
    horizonIcon(c, rising);

    lv_obj_t *col = ui::flex(c, LV_FLEX_FLOW_COLUMN, 1, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    ui::caption(col, ui::MUTED, rising ? "MOONRISE" : "MOONSET");
    lv_obj_t *row = ui::flex(col, LV_FLEX_FLOW_ROW, 6, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
    time = ui::label(row, &lv_font_montserrat_26, ui::TEXT, "--");
    day = nullptr;
    if (!rising) {
        day = ui::label(row, &lv_font_montserrat_14, ui::MUTED, "+1 day");
        lv_obj_set_style_pad_bottom(day, 3, 0);
        lv_obj_add_flag(day, LV_OBJ_FLAG_HIDDEN);
    }
    az = ui::label(col, &lv_font_montserrat_12, ui::MUTED, "--");
    return c;
}

// "NEXT FULL MOON ... in 5 d" card: date + time, then a name / hint line.
lv_obj_t *nextCard(lv_obj_t *scr, int x, const char *head, lv_obj_t *&in, lv_obj_t *&date, lv_obj_t *&tm,
                   lv_obj_t *&sub, lv_color_t subColor, const char *subText) {
    lv_obj_t *c = ui::card(scr, x, 334, CARD_W, 66);
    lv_obj_set_style_pad_hor(c, 14, 0);
    lv_obj_set_style_pad_ver(c, 7, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *top = ui::flex(c, LV_FLEX_FLOW_ROW, 0, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_width(top, INNER_W);
    ui::caption(top, ui::MUTED, head);
    in = ui::label(top, &lv_font_montserrat_14, ui::MUTED, "--");

    lv_obj_t *mid = ui::flex(c, LV_FLEX_FLOW_ROW, 8, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
    lv_obj_set_width(mid, INNER_W);
    date = ui::label(mid, &lv_font_montserrat_18, ui::TEXT, "--");
    tm = ui::label(mid, &lv_font_montserrat_14, ui::MUTED, "");

    sub = ui::label(c, &lv_font_montserrat_14, subColor, subText);
    return c;
}

inline uint16_t scale565(uint16_t v, int m256) {
    const int r = (((v >> 11) & 31) * m256) >> 8;
    const int g = (((v >> 5) & 63) * m256) >> 8;
    const int b = ((v & 31) * m256) >> 8;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// Lit coverage 0..1 at disc-relative pixel offset (dx, dy) for a lit fraction and direction.
// xt is stretched by SOFT_PX so a full moon is fully lit right up to the limb and a new moon is dark.
inline float litAmount(float dx, float wf, float illum, bool waxing, float soft) {
    const float xt = (wf + soft) * (1.0f - 2.0f * illum);
    const float ex = waxing ? dx : -dx;
    return clamp01((ex - xt) / soft + 0.5f);
}

void renderMoon(double illum, bool waxing) {
    if (!s_moonBuf) return;
    const uint16_t bg = lv_color_to_u16(ui::BG);
    const float il = (float)(illum < 0 ? 0 : (illum > 1 ? 1 : illum));
    const float r2 = (MOON_PHOTO_R + 0.5f) * (MOON_PHOTO_R + 0.5f);
    const uint8_t *src = moon_photo.data;
    const size_t srcStride = moon_photo.header.stride;

    for (int y = 0; y < MOON_PHOTO_H; y++) {
        uint16_t *dst = (uint16_t *)(s_moonBuf + (size_t)y * s_moonStride);
        const float dy = y - MOON_PHOTO_CY + 0.5f;
        const float wsq = (float)(MOON_PHOTO_R * MOON_PHOTO_R) - dy * dy;
        const float wf = wsq > 0 ? sqrtf(wsq) : 0.0f;
        const uint8_t *srow = src + (size_t)y * srcStride;
        for (int x = 0; x < MOON_PHOTO_W; x++) {
            const float dx = x - MOON_PHOTO_CX + 0.5f;
            if (dx * dx + dy * dy > r2) {
                dst[x] = bg;
                continue;
            }
            const uint16_t px = (uint16_t)(srow[2 * x] | (srow[2 * x + 1] << 8));
            const float t = litAmount(dx, wf, il, waxing, SOFT_PX);
            const int m = EARTHSHINE_256 + (int)((256 - EARTHSHINE_256) * t);
            dst[x] = scale565(px, m);
        }
    }
    lv_obj_invalidate(s_moonCanvas);
    s_renderedIllum = illum;
    s_renderedWaxing = waxing;
}

// One small disc of the phase strip (no photo): dark disc with the lit part in cream.
void renderMini(lv_obj_t *canvas, uint8_t *buf, size_t stride, float frac, bool waxing) {
    const uint16_t cardBg = lv_color_to_u16(ui::CARD);
    const lv_color_t dark = DISC_DARK, lit = CREAM, rim = ui::TRACK;
    const float R = STRIP_DISC / 2.0f - 1.0f; // 11
    const float c = STRIP_DISC / 2.0f;
    for (int y = 0; y < STRIP_DISC; y++) {
        uint16_t *dst = (uint16_t *)(buf + (size_t)y * stride);
        const float dy = y + 0.5f - c;
        const float wsq = R * R - dy * dy;
        const float wf = wsq > 0 ? sqrtf(wsq) : 0.0f;
        for (int x = 0; x < STRIP_DISC; x++) {
            const float dx = x + 0.5f - c;
            const float d = sqrtf(dx * dx + dy * dy);
            const float cover = clamp01(R + 0.5f - d);
            if (cover <= 0) {
                dst[x] = cardBg;
                continue;
            }
            const float t = litAmount(dx, wf, frac, waxing, 1.0f);
            lv_color_t col = lv_color_mix(lit, t < 0.5f && d > R - 1.0f ? rim : dark, (uint8_t)(t * 255));
            col = lv_color_mix(col, ui::CARD, (uint8_t)(cover * 255));
            dst[x] = lv_color_to_u16(col);
        }
    }
    lv_obj_invalidate(canvas);
}

void compass(char *buf, size_t n, float az) {
    if (az < 0) {
        snprintf(buf, n, "--");
        return;
    }
    const int idx = ((int)((az + 22.5f) / 45.0f)) & 7;
    snprintf(buf, n, "%s %d\xC2\xB0", COMPASS[idx], (int)lroundf(az) % 360);
}

void fmtDate(char *buf, size_t n, time_t t, char *tbuf, size_t tn) {
    struct tm lt;
    localtime_r(&t, &lt);
    snprintf(buf, n, "%s %d %s", tu::WDAY_ABBR[lt.tm_wday % 7], lt.tm_mday, tu::MONTH_ABBR[lt.tm_mon % 12]);
    tu::fmtHHMM(tbuf, tn, t);
}

void fmtIn(char *buf, size_t n, time_t diff) {
    if (diff < 0) snprintf(buf, n, "now");
    else if (diff < 3600) snprintf(buf, n, "in <1 h");
    else if (diff < 86400) snprintf(buf, n, "in %d h", (int)(diff / 3600));
    else snprintf(buf, n, "in %d d", (int)((diff + 43200) / 86400));
}

void showNoPhase() {
    ui::setText(s_lblName, "--");
    ui::setText(s_lblIllum, "--");
    ui::setText(s_lblAge, "--");
    ui::hbarSet(s_cycleBar, 0.0f, CREAM);
    ui::setText(s_lblFullIn, "--");
    ui::setText(s_lblFullDate, "--");
    ui::setText(s_lblFullTime, "");
    ui::setText(s_lblFullName, "--");
    ui::setText(s_lblNewIn, "--");
    ui::setText(s_lblNewDate, "--");
    ui::setText(s_lblNewTime, "");
}

void showRiseSet(time_t now) {
    struct tm today;
    localtime_r(&now, &today);
    const MoonTimes mt = moonTimes();
    const bool ok = mt.valid && mt.year == today.tm_year && mt.yday == today.tm_yday;
    char buf[32];

    if (ok && mt.rise > 0) {
        tu::fmtHHMM(buf, sizeof(buf), mt.rise);
        ui::setText(s_lblRise, buf);
        compass(buf, sizeof(buf), mt.riseAzDeg);
        ui::setText(s_lblRiseAz, buf);
    } else {
        ui::setText(s_lblRise, "--");
        ui::setText(s_lblRiseAz, "--");
    }

    bool nextDay = false;
    if (ok && mt.set > 0) {
        struct tm lt;
        localtime_r(&mt.set, &lt);
        tu::fmtHHMM(buf, sizeof(buf), mt.set);
        ui::setText(s_lblSet, buf);
        compass(buf, sizeof(buf), mt.setAzDeg);
        ui::setText(s_lblSetAz, buf);
        nextDay = lt.tm_yday != today.tm_yday || lt.tm_year != today.tm_year;
    } else {
        ui::setText(s_lblSet, "--");
        ui::setText(s_lblSetAz, "--");
    }
    ui::setHidden(s_lblSetDay, !nextDay);
}

void update() {
    const time_t now = time(nullptr);
    if (!tu::clockValid(now)) { // clock not set yet: show a dark disc and dashes
        if (s_renderedIllum < 0) renderMoon(0.0, true);
        showNoPhase();
        showRiseSet(now);
        s_retryAfterMs = RETRY_MS;
        return;
    }
    s_retryAfterMs = UPDATE_MS;

    const MoonPhase ph = moonPhase(now);
    char buf[40], buf2[16];

    ui::setText(s_lblName, ph.name);
    ui::setTextF(s_lblIllum, "%d%%", (int)lround(ph.illum * 100.0));
    ui::setTextF(s_lblAge, "%.1f days", ph.ageDays);
    ui::hbarSet(s_cycleBar, (float)ph.cycle, CREAM);

    if (s_renderedIllum < 0 || fabs(ph.illum - s_renderedIllum) > RERENDER_DELTA || ph.waxing != s_renderedWaxing)
        renderMoon(ph.illum, ph.waxing);

    if (ph.octant != s_shownOctant) {
        for (int i = 0; i < STRIP_N; i++)
            lv_obj_set_style_border_opa(s_stripRing[i], i == ph.octant ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        s_shownOctant = ph.octant;
    }

    fmtDate(buf, sizeof(buf), ph.nextFull, buf2, sizeof(buf2));
    ui::setText(s_lblFullDate, buf);
    ui::setText(s_lblFullTime, buf2);
    fmtIn(buf, sizeof(buf), ph.nextFull - now);
    ui::setText(s_lblFullIn, buf);
    ui::setText(s_lblFullName, ph.nextFullName);

    fmtDate(buf, sizeof(buf), ph.nextNew, buf2, sizeof(buf2));
    ui::setText(s_lblNewDate, buf);
    ui::setText(s_lblNewTime, buf2);
    fmtIn(buf, sizeof(buf), ph.nextNew - now);
    ui::setText(s_lblNewIn, buf);

    showRiseSet(now);
}

} // namespace

lv_obj_t *moonCreate() {
    initIconPoints();
    lv_obj_t *scr = ui::screenCreate(ui::SCR_MOON);

    // Shaded photo (180 px slot)
    s_moonBuf = allocCanvas(MOON_PHOTO_W, MOON_PHOTO_H, s_moonStride);
    s_moonCanvas = lv_canvas_create(scr);
    ui::inert(s_moonCanvas);
    lv_obj_set_pos(s_moonCanvas, MOON_X, MOON_Y);
    if (s_moonBuf) {
        lv_canvas_set_buffer(s_moonCanvas, s_moonBuf, MOON_PHOTO_W, MOON_PHOTO_H, LV_COLOR_FORMAT_RGB565);
        renderMoon(0.0, true);
    } else {
        lv_obj_set_size(s_moonCanvas, MOON_PHOTO_W, MOON_PHOTO_H); // out of PSRAM: leave the slot empty
    }

    // Right column: name, illumination, age, lunation bar
    s_lblName = ui::labelAt(scr, &lv_font_montserrat_22, ui::TEXT, "--", COL_X, 62, COL_W);
    lv_label_set_long_mode(s_lblName, LV_LABEL_LONG_MODE_CLIP);
    s_lblIllum = ui::labelAt(scr, &lv_font_montserrat_48, ui::TEXT, "--", COL_X, 94);
    ui::labelAt(scr, &lv_font_montserrat_14, ui::MUTED, "illuminated", COL_X, 150);

    ui::cardCaption(scr, "MOON AGE", COL_X, 178);
    s_lblAge = ui::labelAt(scr, &lv_font_montserrat_20, ui::TEXT, "--", COL_X, 194);

    s_cycleBar = ui::hbar(scr, COL_X, 226, COL_W, 6, ui::TRACK, CREAM, 3);
    ui::hbarSet(s_cycleBar, 0.0f, CREAM);

    const char *const ends[3] = {"New", "Full", "New"};
    const lv_text_align_t aligns[3] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT};
    for (int i = 0; i < 3; i++) ui::labelAt(scr, &lv_font_montserrat_12, ui::MUTED, ends[i], COL_X, 236, COL_W, aligns[i]);

    // Moonrise / moonset
    lv_obj_t *noDay;
    s_cardRise = timeCard(scr, 24, true, s_lblRise, noDay, s_lblRiseAz);
    s_cardSet = timeCard(scr, 246, false, s_lblSet, s_lblSetDay, s_lblSetAz);

    // Next full / next new
    lv_obj_t *dummy;
    s_cardFull = nextCard(scr, 24, "NEXT FULL MOON", s_lblFullIn, s_lblFullDate, s_lblFullTime, s_lblFullName,
                          ui::C_SOLAR, "--");
    s_cardNew = nextCard(scr, 246, "NEXT NEW MOON", s_lblNewIn, s_lblNewDate, s_lblNewTime, dummy, ui::MUTED,
                         "Darkest skies");

    // 8-step phase strip; the current octant gets a ring
    s_strip = ui::card(scr, 24, 408, 432, 40);
    lv_obj_set_style_pad_hor(s_strip, 14, 0);
    lv_obj_set_flex_flow(s_strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_strip, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < STRIP_N; i++) {
        lv_obj_t *ring = ui::box(s_strip);
        lv_obj_set_size(ring, STRIP_RING, STRIP_RING);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, ui::TEXT, 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_TRANSP, 0);
        s_stripRing[i] = ring;

        size_t stride = 0;
        uint8_t *buf = allocCanvas(STRIP_DISC, STRIP_DISC, stride);
        if (!buf) continue;
        lv_obj_t *cv = lv_canvas_create(ring);
        ui::inert(cv);
        lv_canvas_set_buffer(cv, buf, STRIP_DISC, STRIP_DISC, LV_COLOR_FORMAT_RGB565);
        lv_obj_center(cv);
        renderMini(cv, buf, stride, STRIP_FRAC[i], i <= 4);
    }

    return scr;
}

void moonRefresh() {
    const uint32_t ms = millis();
    if (!s_haveUpdated || ms - s_lastUpdateMs >= s_retryAfterMs) {
        s_haveUpdated = true;
        s_lastUpdateMs = ms;
        update();
    }
}

void moonOnShow() {
    s_haveUpdated = false; // refresh phase + rise/set right away
    ui::fadeIn(&s_moonCanvas, 1, 0, 0);
    lv_obj_t *const cards[5] = {s_cardRise, s_cardSet, s_cardFull, s_cardNew, s_strip};
    ui::fadeIn(cards, 5, 150, 100);
}
