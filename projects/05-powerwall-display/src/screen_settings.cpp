// Settings screen (last page): backlight slider, SD card status.
#include "screens.h"
#include "ui_common.h"
#include "history.h"
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

namespace {

constexpr int CARD_X = 12, CARD_W = 456, PAD = 14;
constexpr int INNER_W = CARD_W - 2 * PAD;

lv_obj_t *s_cards[2];
lv_obj_t *s_slider, *s_lblPct;
lv_obj_t *s_lblStatus, *s_lblType, *s_lblSpace, *s_lblFile, *s_lblLines, *s_lblBackup, *s_lblErrors;

void formatBytes(char *buf, size_t n, uint64_t b) {
    if (b >= (1ULL << 30)) snprintf(buf, n, "%.1f GB", (double)b / (double)(1ULL << 30));
    else snprintf(buf, n, "%u MB", (unsigned)(b >> 20));
}

void formatAge(char *buf, size_t n, uint32_t s) {
    if (s == UINT32_MAX) snprintf(buf, n, "--");
    else if (s < 60) snprintf(buf, n, "%u s ago", (unsigned)s);
    else if (s < 3600) snprintf(buf, n, "%u min ago", (unsigned)(s / 60));
    else snprintf(buf, n, "%u h ago", (unsigned)(s / 3600));
}

// The slider position (0..100 %) is what is saved; history.cpp maps it through the gamma curve
// (backlight_curve.h) onto the usable duty range. 0 % is the dimmest level that is still lit.
void setPercent(int pct) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", pct);
    ui::setText(s_lblPct, buf);
}

void sliderCb(lv_event_t *e) {
    int pct = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
    setPercent(pct);
    settingsSetBrightness(pct);
}

// Info line at the given y inside a card (full inner width, ellipsis when too long).
lv_obj_t *infoRow(lv_obj_t *parent, int y, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *l = ui::labelAt(parent, font, color, text, PAD, y, INNER_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    return l;
}

} // namespace

lv_obj_t *settingsCreate() {
    lv_obj_t *scr = ui::screenCreate(ui::SCR_SETTINGS);

    ui::cardCaption(scr, "SETTINGS", 24, 52);

    // Brightness card: y 72..164
    s_cards[0] = ui::card(scr, CARD_X, 72, CARD_W, 92);
    ui::cardCaption(s_cards[0], "BRIGHTNESS", PAD, 14);
    s_lblPct = ui::label(s_cards[0], &lv_font_montserrat_20, ui::TEXT, "100%");
    lv_obj_align(s_lblPct, LV_ALIGN_TOP_RIGHT, -PAD, 10);

    s_slider = lv_slider_create(s_cards[0]);
    lv_slider_set_range(s_slider, blcurve::PCT_MIN, blcurve::PCT_MAX);
    lv_slider_set_value(s_slider, settingsGet().brightnessPct, LV_ANIM_OFF);
    lv_obj_set_pos(s_slider, PAD + 14, 56);
    lv_obj_set_size(s_slider, INNER_W - 28, 12);
    lv_obj_set_ext_click_area(s_slider, 18); // 12 + 2*18 = 48 px touch height
    lv_obj_set_style_bg_color(s_slider, ui::TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_slider, ui::C_HOME, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, ui::TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_slider, 8, LV_PART_KNOB); // knob 12 + 16 = 28 px
    lv_obj_set_style_shadow_width(s_slider, 0, LV_PART_KNOB);
    // A horizontal drag on the slider must not switch pages (screens listen for LV_EVENT_GESTURE),
    // and its clicks must not reach the screen's tap handler.
    lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_slider, sliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
    setPercent(lv_slider_get_value(s_slider));

    // SD card: y 176..398
    s_cards[1] = ui::card(scr, CARD_X, 176, CARD_W, 222);
    ui::cardCaption(s_cards[1], "SD CARD", PAD, 14);
    s_lblStatus = ui::label(s_cards[1], &lv_font_montserrat_14, ui::MUTED, "Checking...");
    lv_obj_align(s_lblStatus, LV_ALIGN_TOP_RIGHT, -PAD, 12);
    s_lblType = infoRow(s_cards[1], 44, &lv_font_montserrat_14, ui::TEXT, "--");
    s_lblSpace = infoRow(s_cards[1], 74, &lv_font_montserrat_14, ui::TEXT, "--");
    s_lblFile = infoRow(s_cards[1], 104, &lv_font_montserrat_14, ui::TEXT, "--");
    s_lblLines = infoRow(s_cards[1], 134, &lv_font_montserrat_14, ui::TEXT, "--");
    s_lblBackup = infoRow(s_cards[1], 164, &lv_font_montserrat_14, ui::TEXT, "--");
    s_lblErrors = infoRow(s_cards[1], 194, &lv_font_montserrat_12, ui::MUTED, "errors 0 / lost 0 / clock steps 0");

    return scr;
}

void settingsRefresh() {
    SdStatus s = historyStatus();
    char buf[96], a[24], b[24];

    if (!s.firstMountDone) {
        ui::setText(s_lblStatus, "Checking...");
        ui::setTextColor(s_lblStatus, ui::MUTED);
    } else if (s.mounted) {
        ui::setText(s_lblStatus, "OK");
        ui::setTextColor(s_lblStatus, ui::C_BATT);
    } else {
        ui::setText(s_lblStatus, "No card");
        ui::setTextColor(s_lblStatus, ui::C_SOLAR);
    }

    if (!s.mounted) {
        ui::setText(s_lblType, "--");
        ui::setText(s_lblSpace, "--");
        ui::setText(s_lblFile, "--");
        ui::setText(s_lblLines, "--");
        ui::setText(s_lblBackup, "--");
    } else {
        snprintf(buf, sizeof(buf), "%s @ %u MHz", s.cardType, (unsigned)s.clockMHz);
        ui::setText(s_lblType, buf);
        formatBytes(a, sizeof(a), s.usedBytes);
        formatBytes(b, sizeof(b), s.totalBytes);
        snprintf(buf, sizeof(buf), "%s used of %s", a, b);
        ui::setText(s_lblSpace, buf);
        ui::setText(s_lblFile, s.logFile[0] ? s.logFile : "--");
        formatAge(a, sizeof(a), s.lastWriteAgeS);
        snprintf(buf, sizeof(buf), "%u lines / last write %s", (unsigned)s.linesToday, a);
        ui::setText(s_lblLines, buf);
        ui::setText(s_lblBackup, s.baselineSaved ? "Today's totals backed up" : "Today's totals not backed up yet");
    }

    snprintf(buf, sizeof(buf), "errors %u / lost %u / clock steps %u", (unsigned)s.ioErrors,
             (unsigned)s.cardLost, (unsigned)s.clockStepDowns);
    ui::setText(s_lblErrors, buf);
    bool bad = s.ioErrors || s.cardLost || s.clockStepDowns;
    ui::setTextColor(s_lblErrors, bad ? ui::C_SOLAR : ui::MUTED);
}

void settingsOnShow() {
    int pct = settingsGet().brightnessPct;
    lv_slider_set_value(s_slider, pct, LV_ANIM_OFF);
    setPercent(pct);
    ui::fadeIn(s_cards, 2, 0, 120);
}
