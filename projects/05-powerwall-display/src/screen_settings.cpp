// Settings screen (last page but one): backlight slider, switches for the optional screens, SD card summary.
#include "screens.h"
#include "ui_common.h"
#include "history.h"
#include "settings_codec.h"
#include <Arduino.h>
#include <stdint.h>
#include <stdio.h>

namespace {

constexpr int CARD_X = 12, CARD_W = 456, PAD = 14;
constexpr int INNER_W = CARD_W - 2 * PAD;

// Screens card grid: 2 columns of 44 px rows, 4 px apart
constexpr int GRID_Y = 38, ROW_H = 44, ROW_GAP = 4, COL_GAP = 16;
constexpr int COL_W = (INNER_W - COL_GAP) / 2;

struct Toggle {
    int screen;       // ui::ScreenIndex
    const char *name;
    uint32_t dotRgb;  // dot colour while on
};
const Toggle TOGGLES[] = {
    {ui::SCR_HISTORY, "History", 0xF5B700},   {ui::SCR_HEALTH, "Battery & grid", 0x3DDC84},
    {ui::SCR_WEATHER, "Weather", 0xC9D1D9},   {ui::SCR_MOON, "Moon", 0xC9D1D9},
    {ui::SCR_AURORA, "Aurora", 0x3DDC84},     {ui::SCR_SUN, "Sun", 0xFF8A3D},
    {ui::SCR_ENSO, "El Nino", 0x3B82F6},
};
constexpr int TOGGLE_COUNT = sizeof(TOGGLES) / sizeof(TOGGLES[0]);

struct ToggleUi {
    lv_obj_t *dot, *name, *sw;
};

lv_obj_t *s_cards[3];
lv_obj_t *s_slider, *s_lblPct;
lv_obj_t *s_lblStatus, *s_lblSummary;
ToggleUi s_tog[TOGGLE_COUNT];

constexpr lv_style_selector_t SEL_IND_CHECKED = static_cast<lv_style_selector_t>(LV_PART_INDICATOR) | LV_STATE_CHECKED;
constexpr lv_style_selector_t SEL_MAIN_FOCUS_KEY = static_cast<lv_style_selector_t>(LV_PART_MAIN) | LV_STATE_FOCUS_KEY;

const lv_color_t DOT_OFF = lv_color_hex(0x3A4654);
const lv_color_t ROW_LINE = lv_color_hex(0x1F2731);

void formatBytes(char *buf, size_t n, uint64_t b) {
    if (b >= (1ULL << 30)) snprintf(buf, n, "%.1f GB", (double)b / (double)(1ULL << 30));
    else snprintf(buf, n, "%u MB", (unsigned)(b >> 20));
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

// Row colours follow the switch state (change-only through the ui setters / LVGL's own checks)
void paintToggle(int i, bool on) {
    ui::setBgColor(s_tog[i].dot, on ? lv_color_hex(TOGGLES[i].dotRgb) : DOT_OFF);
    ui::setTextColor(s_tog[i].name, on ? ui::TEXT : ui::MUTED);
}

void switchCb(lv_event_t *e) {
    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    const int i = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    paintToggle(i, on);
    settingsSetScreenEnabled(TOGGLES[i].screen, on); // main.cpp applies the mask within its next 500 ms tick
}

lv_obj_t *makeSwitch(lv_obj_t *parent, int i) {
    lv_obj_t *sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 48, 28);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_pad_all(sw, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, ui::TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, ui::C_HOME, SEL_IND_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, SEL_IND_CHECKED);
    lv_obj_set_style_bg_color(sw, ui::TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB); // knob 28 - 2*3 = 22 px
    lv_obj_set_style_shadow_width(sw, 0, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(sw, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(sw, 0, SEL_MAIN_FOCUS_KEY);
    // Like the slider: a tap or drag on a switch must not turn pages or reach the screen's tap handler.
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_remove_flag(sw, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_ext_click_area(sw, 8); // 28 + 16 = 44 px touch height
    lv_obj_add_event_cb(sw, switchCb, LV_EVENT_VALUE_CHANGED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    return sw;
}

// Puts the switches in line with the saved mask (the e serial command can change it too).
void syncToggles() {
    const uint16_t mask = settingsGet().screenMask;
    for (int i = 0; i < TOGGLE_COUNT; i++) {
        const bool on = setcodec::screenOn(mask, TOGGLES[i].screen);
        if (lv_obj_has_state(s_tog[i].sw, LV_STATE_CHECKED) != on) {
            if (on) lv_obj_add_state(s_tog[i].sw, LV_STATE_CHECKED);
            else lv_obj_remove_state(s_tog[i].sw, LV_STATE_CHECKED);
        }
        paintToggle(i, on);
    }
}

} // namespace

lv_obj_t *settingsCreate() {
    lv_obj_t *scr = ui::screenCreate(ui::SCR_SETTINGS);

    ui::cardCaption(scr, "SETTINGS", 24, 52);

    // Brightness card: y 72..148
    s_cards[0] = ui::card(scr, CARD_X, 72, CARD_W, 76);
    ui::cardCaption(s_cards[0], "BRIGHTNESS", PAD, 12);
    s_lblPct = ui::label(s_cards[0], &lv_font_montserrat_20, ui::TEXT, "100%");
    lv_obj_align(s_lblPct, LV_ALIGN_TOP_RIGHT, -PAD, 8);

    s_slider = lv_slider_create(s_cards[0]);
    lv_slider_set_range(s_slider, blcurve::PCT_MIN, blcurve::PCT_MAX);
    lv_slider_set_value(s_slider, settingsGet().brightnessPct, LV_ANIM_OFF);
    lv_obj_set_pos(s_slider, PAD + 14, 42);
    lv_obj_set_size(s_slider, INNER_W - 28, 12);
    lv_obj_set_ext_click_area(s_slider, 14); // 12 + 2*14 = 40 px touch height (the card is 76 high)
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

    // Screens card: y 158..394
    s_cards[1] = ui::card(scr, CARD_X, 158, CARD_W, 236);
    ui::cardCaption(s_cards[1], "SCREENS", PAD, 12);
    lv_obj_t *note = ui::label(s_cards[1], &lv_font_montserrat_12, ui::MUTED, "Off = hidden, no fetching");
    lv_obj_align(note, LV_ALIGN_TOP_RIGHT, -PAD, 12);
    for (int i = 0; i < TOGGLE_COUNT; i++) {
        lv_obj_t *row = ui::box(s_cards[1]);
        lv_obj_set_pos(row, PAD + (i % 2) * (COL_W + COL_GAP), GRID_Y + (i / 2) * (ROW_H + ROW_GAP));
        lv_obj_set_size(row, COL_W, ROW_H);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, ROW_LINE, 0);

        lv_obj_t *dot = ui::box(row);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, 4, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 0, 0);
        s_tog[i].dot = dot;

        s_tog[i].name = ui::label(row, &lv_font_montserrat_16, ui::TEXT, TOGGLES[i].name);
        lv_obj_align(s_tog[i].name, LV_ALIGN_LEFT_MID, 18, 0);
        s_tog[i].sw = makeSwitch(row, i);
    }
    syncToggles();

    // SD card summary: y 402..446
    s_cards[2] = ui::card(scr, CARD_X, 402, CARD_W, 44);
    lv_obj_set_flex_flow(s_cards[2], LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_cards[2], LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(s_cards[2], PAD, 0);
    lv_obj_set_style_pad_column(s_cards[2], 10, 0);
    ui::cardCaption(s_cards[2], "SD CARD", 0, 0);
    s_lblStatus = ui::label(s_cards[2], &lv_font_montserrat_14, ui::MUTED, "Checking...");
    s_lblSummary = ui::label(s_cards[2], &lv_font_montserrat_14, ui::MUTED, "--");
    lv_label_set_long_mode(s_lblSummary, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_flex_grow(s_lblSummary, 1);

    return scr;
}

void settingsRefresh() {
    syncToggles();

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
        ui::setText(s_lblSummary, "--");
    } else {
        formatBytes(a, sizeof(a), s.usedBytes);
        formatBytes(b, sizeof(b), s.totalBytes);
        snprintf(buf, sizeof(buf), "%u MHz / %s of %s / errors %u", (unsigned)s.clockMHz, a, b, (unsigned)s.ioErrors);
        ui::setText(s_lblSummary, buf);
    }
    bool bad = s.ioErrors || s.cardLost || s.clockStepDowns;
    ui::setTextColor(s_lblSummary, bad ? ui::C_SOLAR : ui::MUTED);
}

void settingsOnShow() {
    int pct = settingsGet().brightnessPct;
    lv_slider_set_value(s_slider, pct, LV_ANIM_OFF);
    setPercent(pct);
    ui::fadeIn(s_cards, 3, 0, 120);
}
