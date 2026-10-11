// Settings screen (last page): backlight slider, SD card status.
#include "screens.h"
#include "ui_common.h"
#include "history.h"
#include <Arduino.h>
#include <Board4848S040_pins.h>
#include <stdint.h>
#include <stdio.h>

namespace {

constexpr int CARD_X = 12, CARD_W = 456, PAD = 14;
constexpr int INNER_W = CARD_W - 2 * PAD;

ui::TopBar topBar;
lv_obj_t *cards[2];
lv_obj_t *slider, *lblPct;
lv_obj_t *lblStatus, *lblType, *lblSpace, *lblFile, *lblLines, *lblBackup, *lblErrors;

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

void setPercent(int v) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", (v * 100 + 127) / 255);
    ui::setText(lblPct, buf);
}

void sliderCb(lv_event_t *e) {
    int v = lv_slider_get_value(static_cast<lv_obj_t *>(lv_event_get_target(e)));
    setPercent(v);
    settingsSetBrightness((uint8_t)v);
}

// Row label at the given y inside a card.
lv_obj_t *row(lv_obj_t *parent, int y, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *l = ui::label(parent, font, color, text);
    lv_obj_set_width(l, INNER_W);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(l, PAD, y);
    return l;
}

} // namespace

lv_obj_t *settingsCreate() {
    lv_obj_t *scr = ui::screenCreate();
    topBar = ui::topBarCreate(scr);

    lv_obj_set_pos(ui::caption(scr, ui::MUTED, "SETTINGS"), 24, 52);

    // Brightness card: y 72..164
    cards[0] = ui::card(scr, CARD_X, 72, CARD_W, 92);
    lv_obj_set_pos(ui::caption(cards[0], ui::MUTED, "BRIGHTNESS"), PAD, 14);
    lblPct = ui::label(cards[0], &lv_font_montserrat_20, ui::TEXT, "100%");
    lv_obj_align(lblPct, LV_ALIGN_TOP_RIGHT, -PAD, 10);

    slider = lv_slider_create(cards[0]);
    lv_slider_set_range(slider, B4848_LCD_BL_MIN, 255);
    lv_slider_set_value(slider, settingsGet().brightness, LV_ANIM_OFF);
    lv_obj_set_pos(slider, PAD + 14, 56);
    lv_obj_set_size(slider, INNER_W - 28, 12);
    lv_obj_set_ext_click_area(slider, 18); // 12 + 2*18 = 48 px touch height
    lv_obj_set_style_bg_color(slider, ui::TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, ui::C_HOME, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ui::TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB); // knob 12 + 16 = 28 px
    lv_obj_set_style_shadow_width(slider, 0, LV_PART_KNOB);
    // A horizontal drag on the slider must not switch pages (screens listen for LV_EVENT_GESTURE),
    // and its clicks must not reach the screen's tap handler.
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(slider, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(slider, sliderCb, LV_EVENT_VALUE_CHANGED, nullptr);
    setPercent(lv_slider_get_value(slider));

    // SD card: y 176..398
    cards[1] = ui::card(scr, CARD_X, 176, CARD_W, 222);
    lv_obj_set_pos(ui::caption(cards[1], ui::MUTED, "SD CARD"), PAD, 14);
    lblStatus = ui::label(cards[1], &lv_font_montserrat_14, ui::MUTED, "Checking...");
    lv_obj_align(lblStatus, LV_ALIGN_TOP_RIGHT, -PAD, 12);
    lblType = row(cards[1], 44, &lv_font_montserrat_14, ui::TEXT, "--");
    lblSpace = row(cards[1], 74, &lv_font_montserrat_14, ui::TEXT, "--");
    lblFile = row(cards[1], 104, &lv_font_montserrat_14, ui::TEXT, "--");
    lblLines = row(cards[1], 134, &lv_font_montserrat_14, ui::TEXT, "--");
    lblBackup = row(cards[1], 164, &lv_font_montserrat_14, ui::TEXT, "--");
    lblErrors = row(cards[1], 194, &lv_font_montserrat_12, ui::MUTED, "errors 0 / lost 0 / clock steps 0");

    ui::pageDots(scr, ui::SCR_SETTINGS);
    return scr;
}

void settingsRefresh() {
    ui::topBarUpdate(topBar);
    SdStatus s = historyStatus();
    char buf[96], a[24], b[24];

    if (!s.firstMountDone) {
        ui::setText(lblStatus, "Checking...");
        ui::setTextColor(lblStatus, ui::MUTED);
    } else if (s.mounted) {
        ui::setText(lblStatus, "OK");
        ui::setTextColor(lblStatus, ui::C_BATT);
    } else {
        ui::setText(lblStatus, "No card");
        ui::setTextColor(lblStatus, ui::C_SOLAR);
    }

    if (!s.mounted) {
        ui::setText(lblType, "--");
        ui::setText(lblSpace, "--");
        ui::setText(lblFile, "--");
        ui::setText(lblLines, "--");
        ui::setText(lblBackup, "--");
    } else {
        snprintf(buf, sizeof(buf), "%s @ %u MHz", s.cardType, (unsigned)s.clockMHz);
        ui::setText(lblType, buf);
        formatBytes(a, sizeof(a), s.usedBytes);
        formatBytes(b, sizeof(b), s.totalBytes);
        snprintf(buf, sizeof(buf), "%s used of %s", a, b);
        ui::setText(lblSpace, buf);
        ui::setText(lblFile, s.logFile[0] ? s.logFile : "--");
        formatAge(a, sizeof(a), s.lastWriteAgeS);
        snprintf(buf, sizeof(buf), "%u lines / last write %s", (unsigned)s.linesToday, a);
        ui::setText(lblLines, buf);
        ui::setText(lblBackup, s.baselineSaved ? "Today's totals backed up" : "Today's totals not backed up yet");
    }

    snprintf(buf, sizeof(buf), "errors %u / lost %u / clock steps %u", (unsigned)s.ioErrors,
             (unsigned)s.cardLost, (unsigned)s.clockStepDowns);
    ui::setText(lblErrors, buf);
    bool bad = s.ioErrors || s.cardLost || s.clockStepDowns;
    ui::setTextColor(lblErrors, bad ? ui::C_SOLAR : ui::MUTED);
}

void settingsOnShow() {
    uint8_t v = settingsGet().brightness;
    if (v < B4848_LCD_BL_MIN) v = B4848_LCD_BL_MIN;
    lv_slider_set_value(slider, v, LV_ANIM_OFF);
    setPercent(v);
    for (int i = 0; i < 2; i++) ui::animFadeIn(cards[i], (uint32_t)i * 120);
}
