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
#include "moon_calc.h"
#include "moon_photo.h"
#include "sky.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
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
constexpr time_t MIN_VALID_TIME = 1700000000; // before NTP sync time() is near 1970
constexpr uint32_t UPDATE_MS = 60000, RETRY_MS = 2000;

const lv_color_t CREAM = lv_color_hex(0xEDE6CF);
const lv_color_t DISC_DARK = lv_color_hex(0x1A222C);
const lv_color_t DISC_RIM = lv_color_hex(0x2A3440);

const char *const WDAY[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const MONTH[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
const char *const COMPASS[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};

// Phase strip: lit fraction per octant (0 new .. 4 full .. 7 waning crescent)
const float STRIP_FRAC[STRIP_N] = {0.0f, 0.146f, 0.5f, 0.854f, 1.0f, 0.854f, 0.5f, 0.146f};

ui::TopBar topBar;
lv_obj_t *moonCanvas;
uint8_t *moonBuf;
size_t moonStride;
lv_obj_t *lblName, *lblIllum, *lblAge, *barFill;
lv_obj_t *cardRise, *cardSet, *cardFull, *cardNew, *strip;
lv_obj_t *lblRise, *lblRiseAz, *lblSet, *lblSetDay, *lblSetAz;
lv_obj_t *lblFullIn, *lblFullDate, *lblFullTime, *lblFullName;
lv_obj_t *lblNewIn, *lblNewDate, *lblNewTime;
lv_obj_t *stripRing[STRIP_N];

double renderedIllum = -1;
bool renderedWaxing = true;
int shownOctant = -1;
uint32_t lastUpdateMs, retryAfterMs;
bool haveUpdated;

// Icon strokes: lv_line keeps a pointer to its points, so they live in statics.
constexpr float IS = 28.0f / 24.0f; // px per design unit of the 24x24 icon grid
lv_point_precise_t ptsHorizon[2], ptsRiseShaft[2], ptsRiseHead[3], ptsSetShaft[2], ptsSetHead[3];

void inert(lv_obj_t *o) {
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

void pt(lv_point_precise_t &p, float x, float y) {
    p.x = (lv_value_precise_t)(x * IS);
    p.y = (lv_value_precise_t)(y * IS);
}

void initIconPoints() {
    pt(ptsHorizon[0], 3, 20);
    pt(ptsHorizon[1], 21, 20);
    pt(ptsRiseShaft[0], 12, 11);
    pt(ptsRiseShaft[1], 12, 5);
    pt(ptsRiseHead[0], 9.5f, 7.5f);
    pt(ptsRiseHead[1], 12, 5);
    pt(ptsRiseHead[2], 14.5f, 7.5f);
    pt(ptsSetShaft[0], 12, 5);
    pt(ptsSetShaft[1], 12, 11);
    pt(ptsSetHead[0], 9.5f, 8.5f);
    pt(ptsSetHead[1], 12, 11);
    pt(ptsSetHead[2], 14.5f, 8.5f);
}

void strokeLine(lv_obj_t *parent, const lv_point_precise_t *pts, uint32_t n) {
    lv_obj_t *l = lv_line_create(parent);
    inert(l);
    lv_line_set_points(l, pts, n);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, 2, 0);
    lv_obj_set_style_line_color(l, CREAM, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
}

// Moonrise / moonset glyph (same shapes as the design): horizon, half disc, arrow up / down.
lv_obj_t *horizonIcon(lv_obj_t *parent, bool rising) {
    lv_obj_t *c = ui::box(parent);
    inert(c);
    lv_obj_set_size(c, 28, 28);
    strokeLine(c, ptsHorizon, 2);

    const int d = (int)lroundf(2 * 5 * IS + 2);
    lv_obj_t *a = lv_arc_create(c);
    lv_obj_set_size(a, d, d);
    lv_obj_set_pos(a, (int)lroundf(12 * IS - d / 2.0f), (int)lroundf(20 * IS - d / 2.0f));
    lv_arc_set_bg_angles(a, 180, 360);
    lv_obj_remove_style(a, nullptr, LV_PART_KNOB);
    inert(a);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, CREAM, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(a, LV_OPA_TRANSP, LV_PART_INDICATOR);

    if (rising) {
        strokeLine(c, ptsRiseShaft, 2);
        strokeLine(c, ptsRiseHead, 3);
    } else {
        strokeLine(c, ptsSetShaft, 2);
        strokeLine(c, ptsSetHead, 3);
    }
    return c;
}

lv_obj_t *plainBox(lv_obj_t *parent, int w, int h, lv_flex_flow_t flow) {
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_flex_flow(o, flow);
    inert(o);
    return o;
}

void staticCard(lv_obj_t *c) {
    inert(c);
    lv_obj_set_style_pad_hor(c, 14, 0);
}

// Rise / set card: glyph on the left, three text lines on the right.
lv_obj_t *timeCard(lv_obj_t *scr, int x, bool rising, lv_obj_t *&time, lv_obj_t *&day, lv_obj_t *&az) {
    lv_obj_t *c = ui::card(scr, x, 258, CARD_W, 68);
    staticCard(c);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(c, 10, 0);
    horizonIcon(c, rising);

    lv_obj_t *col = plainBox(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 1, 0);
    ui::caption(col, ui::MUTED, rising ? "MOONRISE" : "MOONSET");
    lv_obj_t *row = plainBox(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, 0);
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
    staticCard(c);
    lv_obj_set_style_pad_ver(c, 7, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    lv_obj_t *top = plainBox(c, INNER_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui::caption(top, ui::MUTED, head);
    in = ui::label(top, &lv_font_montserrat_14, ui::MUTED, "--");

    lv_obj_t *mid = plainBox(c, INNER_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(mid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(mid, 8, 0);
    date = ui::label(mid, &lv_font_montserrat_18, ui::TEXT, "--");
    tm = ui::label(mid, &lv_font_montserrat_14, ui::MUTED, "");

    sub = ui::label(c, &lv_font_montserrat_14, subColor, subText);
    return c;
}

// Allocate a PSRAM RGB565 canvas buffer with LVGL's own stride; null if out of memory.
uint8_t *allocCanvasBuf(int w, int h, size_t &stride) {
    stride = lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
    return (uint8_t *)heap_caps_aligned_alloc(64, stride * (size_t)h, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
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
    if (!moonBuf) return;
    const uint16_t bg = lv_color_to_u16(ui::BG);
    const float il = (float)(illum < 0 ? 0 : (illum > 1 ? 1 : illum));
    const float r2 = (MOON_PHOTO_R + 0.5f) * (MOON_PHOTO_R + 0.5f);
    const uint8_t *src = moon_photo.data;
    const size_t srcStride = moon_photo.header.stride;

    for (int y = 0; y < MOON_PHOTO_H; y++) {
        uint16_t *dst = (uint16_t *)(moonBuf + (size_t)y * moonStride);
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
    lv_obj_invalidate(moonCanvas);
    renderedIllum = illum;
    renderedWaxing = waxing;
}

// One small disc of the phase strip (no photo): dark disc with the lit part in cream.
void renderMini(lv_obj_t *canvas, uint8_t *buf, size_t stride, float frac, bool waxing) {
    const uint16_t cardBg = lv_color_to_u16(ui::CARD);
    const lv_color_t dark = DISC_DARK, lit = CREAM, rim = DISC_RIM;
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
    snprintf(buf, n, "%s %d %s", WDAY[lt.tm_wday % 7], lt.tm_mday, MONTH[lt.tm_mon % 12]);
    snprintf(tbuf, tn, "%02d:%02d", lt.tm_hour, lt.tm_min);
}

void fmtIn(char *buf, size_t n, time_t diff) {
    if (diff < 0) snprintf(buf, n, "now");
    else if (diff < 3600) snprintf(buf, n, "in <1 h");
    else if (diff < 86400) snprintf(buf, n, "in %d h", (int)(diff / 3600));
    else snprintf(buf, n, "in %d d", (int)((diff + 43200) / 86400));
}

void showNoPhase() {
    ui::setText(lblName, "--");
    ui::setText(lblIllum, "--");
    ui::setText(lblAge, "--");
    ui::setWidth(barFill, 0);
    ui::setText(lblFullIn, "--");
    ui::setText(lblFullDate, "--");
    ui::setText(lblFullTime, "");
    ui::setText(lblFullName, "--");
    ui::setText(lblNewIn, "--");
    ui::setText(lblNewDate, "--");
    ui::setText(lblNewTime, "");
}

void showRiseSet(time_t now) {
    struct tm today;
    localtime_r(&now, &today);
    const MoonTimes mt = moonTimes();
    const bool ok = mt.valid && mt.year == today.tm_year && mt.yday == today.tm_yday;
    char buf[32];

    if (ok && mt.rise > 0) {
        struct tm lt;
        localtime_r(&mt.rise, &lt);
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
        ui::setText(lblRise, buf);
        compass(buf, sizeof(buf), mt.riseAzDeg);
        ui::setText(lblRiseAz, buf);
    } else {
        ui::setText(lblRise, "--");
        ui::setText(lblRiseAz, "--");
    }

    bool nextDay = false;
    if (ok && mt.set > 0) {
        struct tm lt;
        localtime_r(&mt.set, &lt);
        snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
        ui::setText(lblSet, buf);
        compass(buf, sizeof(buf), mt.setAzDeg);
        ui::setText(lblSetAz, buf);
        nextDay = lt.tm_yday != today.tm_yday || lt.tm_year != today.tm_year;
    } else {
        ui::setText(lblSet, "--");
        ui::setText(lblSetAz, "--");
    }
    ui::setHidden(lblSetDay, !nextDay);
}

void update() {
    const time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) { // clock not set yet: show a dark disc and dashes
        if (renderedIllum < 0) renderMoon(0.0, true);
        showNoPhase();
        showRiseSet(now);
        retryAfterMs = RETRY_MS;
        return;
    }
    retryAfterMs = UPDATE_MS;

    const MoonPhase ph = moonPhase(now);
    char buf[40], buf2[16];

    ui::setText(lblName, ph.name);
    snprintf(buf, sizeof(buf), "%d%%", (int)lround(ph.illum * 100.0));
    ui::setText(lblIllum, buf);
    snprintf(buf, sizeof(buf), "%.1f days", ph.ageDays);
    ui::setText(lblAge, buf);
    ui::setWidth(barFill, (int)lround(COL_W * (ph.cycle < 0 ? 0.0 : (ph.cycle > 1 ? 1.0 : ph.cycle))));

    if (renderedIllum < 0 || fabs(ph.illum - renderedIllum) > RERENDER_DELTA || ph.waxing != renderedWaxing)
        renderMoon(ph.illum, ph.waxing);

    if (ph.octant != shownOctant) {
        for (int i = 0; i < STRIP_N; i++)
            lv_obj_set_style_border_opa(stripRing[i], i == ph.octant ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        shownOctant = ph.octant;
    }

    fmtDate(buf, sizeof(buf), ph.nextFull, buf2, sizeof(buf2));
    ui::setText(lblFullDate, buf);
    ui::setText(lblFullTime, buf2);
    fmtIn(buf, sizeof(buf), ph.nextFull - now);
    ui::setText(lblFullIn, buf);
    ui::setText(lblFullName, ph.nextFullName);

    fmtDate(buf, sizeof(buf), ph.nextNew, buf2, sizeof(buf2));
    ui::setText(lblNewDate, buf);
    ui::setText(lblNewTime, buf2);
    fmtIn(buf, sizeof(buf), ph.nextNew - now);
    ui::setText(lblNewIn, buf);

    showRiseSet(now);
}

} // namespace

lv_obj_t *moonCreate() {
    initIconPoints();
    lv_obj_t *scr = ui::screenCreate();
    topBar = ui::topBarCreate(scr);

    // Shaded photo (180 px slot)
    moonBuf = allocCanvasBuf(MOON_PHOTO_W, MOON_PHOTO_H, moonStride);
    moonCanvas = lv_canvas_create(scr);
    inert(moonCanvas);
    lv_obj_set_pos(moonCanvas, MOON_X, MOON_Y);
    if (moonBuf) {
        lv_canvas_set_buffer(moonCanvas, moonBuf, MOON_PHOTO_W, MOON_PHOTO_H, LV_COLOR_FORMAT_RGB565);
        renderMoon(0.0, true);
    } else {
        lv_obj_set_size(moonCanvas, MOON_PHOTO_W, MOON_PHOTO_H); // out of PSRAM: leave the slot empty
    }

    // Right column: name, illumination, age, lunation bar
    lblName = ui::label(scr, &lv_font_montserrat_22, ui::TEXT, "--");
    lv_obj_set_pos(lblName, COL_X, 62);
    lv_obj_set_width(lblName, COL_W);
    lv_label_set_long_mode(lblName, LV_LABEL_LONG_MODE_CLIP);
    lblIllum = ui::label(scr, &lv_font_montserrat_48, ui::TEXT, "--");
    lv_obj_set_pos(lblIllum, COL_X, 94);
    lv_obj_t *illumCap = ui::label(scr, &lv_font_montserrat_14, ui::MUTED, "illuminated");
    lv_obj_set_pos(illumCap, COL_X, 150);

    lv_obj_t *ageCap = ui::caption(scr, ui::MUTED, "MOON AGE");
    lv_obj_set_pos(ageCap, COL_X, 178);
    lblAge = ui::label(scr, &lv_font_montserrat_20, ui::TEXT, "--");
    lv_obj_set_pos(lblAge, COL_X, 194);

    lv_obj_t *track = ui::box(scr);
    inert(track);
    lv_obj_set_pos(track, COL_X, 226);
    lv_obj_set_size(track, COL_W, 6);
    lv_obj_set_style_radius(track, 3, 0);
    lv_obj_set_style_bg_color(track, ui::TRACK, 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    barFill = ui::box(track);
    inert(barFill);
    lv_obj_set_size(barFill, 0, 6);
    lv_obj_set_style_radius(barFill, 3, 0);
    lv_obj_set_style_bg_color(barFill, CREAM, 0);
    lv_obj_set_style_bg_opa(barFill, LV_OPA_COVER, 0);

    const char *const ends[3] = {"New", "Full", "New"};
    const lv_text_align_t aligns[3] = {LV_TEXT_ALIGN_LEFT, LV_TEXT_ALIGN_CENTER, LV_TEXT_ALIGN_RIGHT};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *l = ui::label(scr, &lv_font_montserrat_12, ui::MUTED, ends[i]);
        lv_obj_set_width(l, COL_W);
        lv_obj_set_style_text_align(l, aligns[i], 0);
        lv_obj_set_pos(l, COL_X, 236);
    }

    // Moonrise / moonset
    lv_obj_t *noDay;
    cardRise = timeCard(scr, 24, true, lblRise, noDay, lblRiseAz);
    cardSet = timeCard(scr, 246, false, lblSet, lblSetDay, lblSetAz);

    // Next full / next new
    lv_obj_t *dummy;
    cardFull = nextCard(scr, 24, "NEXT FULL MOON", lblFullIn, lblFullDate, lblFullTime, lblFullName, ui::C_SOLAR, "--");
    cardNew = nextCard(scr, 246, "NEXT NEW MOON", lblNewIn, lblNewDate, lblNewTime, dummy, ui::MUTED, "Darkest skies");

    // 8-step phase strip; the current octant gets a ring
    strip = ui::card(scr, 24, 408, 432, 40);
    inert(strip);
    lv_obj_set_style_pad_hor(strip, 14, 0);
    lv_obj_set_flex_flow(strip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(strip, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    for (int i = 0; i < STRIP_N; i++) {
        lv_obj_t *ring = ui::box(strip);
        inert(ring);
        lv_obj_set_size(ring, STRIP_RING, STRIP_RING);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(ring, 2, 0);
        lv_obj_set_style_border_color(ring, ui::TEXT, 0);
        lv_obj_set_style_border_opa(ring, LV_OPA_TRANSP, 0);
        stripRing[i] = ring;

        size_t stride = 0;
        uint8_t *buf = allocCanvasBuf(STRIP_DISC, STRIP_DISC, stride);
        if (!buf) continue;
        lv_obj_t *cv = lv_canvas_create(ring);
        inert(cv);
        lv_canvas_set_buffer(cv, buf, STRIP_DISC, STRIP_DISC, LV_COLOR_FORMAT_RGB565);
        lv_obj_center(cv);
        renderMini(cv, buf, stride, STRIP_FRAC[i], i <= 4);
    }

    ui::pageDots(scr, ui::SCR_MOON);
    return scr;
}

void moonRefresh() {
    ui::topBarUpdate(topBar);
    const uint32_t ms = millis();
    if (!haveUpdated || ms - lastUpdateMs >= retryAfterMs) {
        haveUpdated = true;
        lastUpdateMs = ms;
        update();
    }
}

void moonOnShow() {
    haveUpdated = false; // refresh phase + rise/set right away
    ui::animFadeIn(moonCanvas, 0);
    ui::animFadeIn(cardRise, 150);
    ui::animFadeIn(cardSet, 250);
    ui::animFadeIn(cardFull, 350);
    ui::animFadeIn(cardNew, 450);
    ui::animFadeIn(strip, 550);
}
