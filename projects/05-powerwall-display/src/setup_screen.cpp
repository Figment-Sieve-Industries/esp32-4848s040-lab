// Setup portal screens (see setup_screen.h). Layout follows design board U: 480x480, 24 px margins.
#include "setup_screen.h"
#include "ui_common.h"
#include <stdio.h>
#include <string.h>

namespace setupscreen {
namespace {

constexpr int M = 24, W = 480 - 2 * M;
const lv_color_t NUM_BG = lv_color_hex(0x1F2731);

lv_obj_t *makeScreen() {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(scr, ui::BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_color_t toneColor(Tone t) {
    switch (t) {
    case Tone::Warn: return ui::C_SOLAR;
    case Tone::Ok: return ui::C_BATT;
    case Tone::Fail: return ui::C_ALERT;
    default: return ui::MUTED;
    }
}

// ---- countdown ----
lv_obj_t *s_cdScr, *s_cdPrev, *s_cdNum;
ui::HBar s_cdBar;

// ---- portal ----
lv_obj_t *s_title, *s_join, *s_test, *s_dot, *s_status, *s_result;
struct Row {
    lv_obj_t *mark, *markTxt, *label, *note;
};
Row s_rows[STEPS];
Mode s_mode = Mode::Join;
bool s_modeSet = false;

lv_obj_t *stepRow(lv_obj_t *parent, int n, const char *text) {
    lv_obj_t *row = ui::flex(parent, LV_FLEX_FLOW_ROW, 10, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_width(row, 228);
    lv_obj_t *num = ui::box(row);
    lv_obj_set_size(num, 24, 24);
    lv_obj_set_style_radius(num, 12, 0);
    lv_obj_set_style_bg_color(num, NUM_BG, 0);
    lv_obj_set_style_bg_opa(num, LV_OPA_COVER, 0);
    char d[4];
    snprintf(d, sizeof(d), "%d", n);
    lv_obj_t *nl = ui::label(num, &lv_font_montserrat_14, ui::C_HOME, d);
    lv_obj_center(nl);
    lv_obj_t *t = ui::label(row, &lv_font_montserrat_14, ui::TEXT, text);
    lv_obj_set_width(t, 194);
    lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_text_line_space(t, 3, 0);
    return row;
}

void setMark(Row &r, Mark m) {
    static const char *const TXT[] = {"", "...", "OK", "X"};
    const lv_color_t bg[] = {ui::TRACK, ui::C_SOLAR, ui::C_BATT, ui::C_ALERT};
    const int i = static_cast<int>(m);
    ui::setBgColor(r.mark, bg[i]);
    ui::setText(r.markTxt, TXT[i]);
}

} // namespace

void countdownShow() {
    s_cdPrev = lv_screen_active();
    s_cdScr = makeScreen();
    lv_obj_set_pos(ui::caption(s_cdScr, ui::MUTED, "SETUP"), M, 20);
    ui::labelAt(s_cdScr, &lv_font_montserrat_26, ui::TEXT, "Keep holding to run setup", M, 38);
    s_cdNum = ui::labelAt(s_cdScr, &lv_font_montserrat_48, ui::C_HOME, "5", 0, 170, 480, LV_TEXT_ALIGN_CENTER);
    s_cdBar = ui::hbar(s_cdScr, M + 40, 250, W - 80, 10, ui::TRACK, ui::C_HOME, 5);
    ui::labelAt(s_cdScr, &lv_font_montserrat_16, ui::MUTED, "Let go to start the display as usual", 0, 290, 480,
                LV_TEXT_ALIGN_CENTER);
    lv_screen_load(s_cdScr);
}

void countdownSet(uint32_t heldMs, uint32_t needMs) {
    if (!s_cdScr) return;
    const uint32_t left = heldMs >= needMs ? 0 : (needMs - heldMs + 999) / 1000;
    ui::setTextF(s_cdNum, "%u", (unsigned)left);
    ui::hbarSet(s_cdBar, needMs ? (float)heldMs / (float)needMs : 1.0f, ui::C_HOME);
}

void countdownClose() {
    if (!s_cdScr) return;
    if (s_cdPrev) lv_screen_load_anim(s_cdPrev, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true); // deletes the countdown
    else lv_obj_delete(s_cdScr);
    s_cdScr = s_cdPrev = s_cdNum = nullptr;
    s_cdBar = ui::HBar();
}

void portalShow(const char *apSsid, const char *qr) {
    lv_obj_t *scr = makeScreen();
    lv_obj_set_pos(ui::caption(scr, ui::MUTED, "SETUP"), M, 20);
    s_title = ui::labelAt(scr, &lv_font_montserrat_26, ui::TEXT, "Let's get connected", M, 38);

    // Join view: QR card, 3 steps, status card
    s_join = ui::box(scr);
    lv_obj_set_pos(s_join, M, 92);
    lv_obj_set_size(s_join, W, 260);
    lv_obj_t *qrCard = ui::card(s_join, 0, 0, 184, 184);
    lv_obj_set_style_bg_color(qrCard, ui::TEXT, 0);
    lv_obj_t *code = lv_qrcode_create(qrCard);
    lv_qrcode_set_size(code, 152);
    lv_qrcode_set_dark_color(code, ui::BG);
    lv_qrcode_set_light_color(code, ui::TEXT);
    lv_qrcode_update(code, qr, static_cast<uint32_t>(strlen(qr)));
    lv_obj_center(code);

    lv_obj_t *steps = ui::flex(s_join, LV_FLEX_FLOW_COLUMN, 14);
    lv_obj_set_pos(steps, 204, 0);
    char s1[80];
    snprintf(s1, sizeof(s1), "Scan the code with your phone, or join Wi-Fi %s", apSsid);
    stepRow(steps, 1, s1);
    stepRow(steps, 2, "The setup page opens. If not, go to 192.168.4.1");
    stepRow(steps, 3, "Choose your Wi-Fi and enter your Powerwall details");

    lv_obj_t *card = ui::card(s_join, 0, 208, W, 52);
    s_dot = ui::rect(card, 16, 22, 8, 8, ui::C_SOLAR, 4);
    s_status = ui::labelAt(card, &lv_font_montserrat_14, ui::MUTED, "Starting...", 34, 17, 250);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *pw = ui::flex(card, LV_FLEX_FLOW_ROW, 4);
    ui::label(pw, &lv_font_montserrat_14, ui::MUTED, "Password:");
    ui::label(pw, &lv_font_montserrat_14, ui::TEXT, "none");
    lv_obj_align(pw, LV_ALIGN_RIGHT_MID, -16, 0);

    // Test view: 4 step rows and a result line
    s_test = ui::box(scr);
    lv_obj_set_pos(s_test, M, 96);
    lv_obj_set_size(s_test, W, 320);
    for (int i = 0; i < STEPS; i++) {
        lv_obj_t *row = ui::card(s_test, 0, i * 62, W, 52);
        Row &r = s_rows[i];
        r.mark = ui::rect(row, 16, 15, 34, 22, ui::TRACK, 11);
        r.markTxt = ui::label(r.mark, &lv_font_montserrat_12, ui::BG, "");
        lv_obj_center(r.markTxt);
        r.label = ui::labelAt(row, &lv_font_montserrat_16, ui::TEXT, "", 62, 16, 220);
        lv_label_set_long_mode(r.label, LV_LABEL_LONG_MODE_DOTS);
        r.note = ui::labelAt(row, &lv_font_montserrat_14, ui::MUTED, "", 286, 18, 130, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_long_mode(r.note, LV_LABEL_LONG_MODE_DOTS);
    }
    s_result = ui::labelAt(s_test, &lv_font_montserrat_18, ui::C_SOLAR, "", 0, 256, W, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_result, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_add_flag(s_test, LV_OBJ_FLAG_HIDDEN);

    ui::labelAt(scr, &lv_font_montserrat_12, ui::MUTED, "Hold the screen for 5 s at power-on to run setup again", M, 432, W,
                LV_TEXT_ALIGN_CENTER);
    s_modeSet = false;
    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_NONE, 0, 0, true); // deletes the boot screen
}

void portalUpdate(const View &v) {
    if (!s_title) return;
    if (!s_modeSet || v.mode != s_mode) {
        s_mode = v.mode;
        s_modeSet = true;
        ui::setHidden(s_join, v.mode != Mode::Join);
        ui::setHidden(s_test, v.mode != Mode::Test);
    }
    if (v.mode == Mode::Join) {
        ui::setText(s_title, "Let's get connected");
        ui::setBgColor(s_dot, toneColor(v.statusTone));
        ui::setText(s_status, v.status);
        ui::setTextColor(s_status, v.statusTone == Tone::Fail ? ui::C_ALERT : ui::MUTED);
        return;
    }
    ui::setText(s_title, v.resultTone == Tone::Ok ? "All set" : "Testing your settings");
    for (int i = 0; i < STEPS; i++) {
        Row &r = s_rows[i];
        setMark(r, v.mark[i]);
        ui::setText(r.label, v.label[i]);
        ui::setText(r.note, v.note[i]);
        const lv_color_t nc = v.mark[i] == Mark::Run ? ui::C_SOLAR : v.mark[i] == Mark::Fail ? ui::C_ALERT : ui::MUTED;
        ui::setTextColor(r.note, nc);
    }
    ui::setText(s_result, v.result);
    ui::setTextColor(s_result, toneColor(v.resultTone));
}

} // namespace setupscreen
