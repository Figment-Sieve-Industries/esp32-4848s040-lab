#pragma once
// Shared look and helpers for all screens (480x480, rotated so USB-C is at the bottom).
#include <lvgl.h>
#include <stddef.h>

namespace ui {

// Palette (matches the design canvas)
extern const lv_color_t BG, CARD, CARD_HI, TRACK, TEXT, MUTED;
extern const lv_color_t C_SOLAR, C_GRID, C_HOME, C_BATT, C_ALERT, C_ALERT_BG, C_WEATHER, C_OK_BG;

// Mains voltage normal range (230 V nominal, 207-253 V)
constexpr float MAINS_V_MIN = 207.0f, MAINS_V_MAX = 253.0f;

// 0 = house (N/O), 1 = today (G), 2 = battery & grid (H), 3 = weather (I), 4 = moon (P),
// 5 = aurora (Q), 6 = sun (R), 7 = El Nino (S), 8 = settings, 9 = about
constexpr int SCREEN_COUNT = 10;
enum ScreenIndex {
    SCR_HOUSE, SCR_TODAY, SCR_HEALTH, SCR_WEATHER, SCR_MOON, SCR_AURORA, SCR_SUN, SCR_ENSO, SCR_SETTINGS, SCR_ABOUT
};

// Fonts available (built-in Montserrat): 12 14 16 18 20 22 26 28 30 40 48.
// Built-in fonts have ASCII + degree sign (use "\xC2\xB0") but NO middle dot: use "/" or "-".

// New screen object: dark background, not scrollable, swipe + tap handling attached.
lv_obj_t *screenCreate();

// Plain transparent container (no style, not clickable/scrollable). Size defaults to content.
lv_obj_t *box(lv_obj_t *parent);
// Rounded card (CARD background, radius 14) at a fixed position/size (h may be LV_SIZE_CONTENT).
lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h);

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
// Small uppercase caption: Montserrat 12 with letter spacing
lv_obj_t *caption(lv_obj_t *parent, lv_color_t color, const char *text);

// Change-only setters (avoid needless redraws, which make animations judder)
void setText(lv_obj_t *label, const char *text);
void setTextColor(lv_obj_t *label, lv_color_t c);
void setBgColor(lv_obj_t *obj, lv_color_t c);
void setHidden(lv_obj_t *obj, bool hidden);
void setWidth(lv_obj_t *obj, int32_t w);
void setHeight(lv_obj_t *obj, int32_t h);
void setX(lv_obj_t *obj, int32_t x);

void formatPower(char *buf, size_t n, float watts);   // "504 W" / "3.36 kW" (absolute value)
void formatKWh(char *buf, size_t n, double kwh);      // "0.8" / "18.4" / "1,234" (number only)

// Top bar: time (28), date (14), Wi-Fi icon + "SSID  -56 dBm" right-aligned. Same on every screen.
struct TopBar {
    lv_obj_t *time, *date, *wifiIcon, *wifi;
};
TopBar topBarCreate(lv_obj_t *scr);
void topBarUpdate(TopBar &tb);

// Page indicator centred at y = 456: active page is a 20x8 pill (TEXT), others 8x8 dots (TRACK).
void pageDots(lv_obj_t *scr, int activeIndex);

// Animations (all LVGL-cheap; deleted automatically with the object)
void animPulse(lv_obj_t *obj, uint32_t periodMs);            // bg opacity 255 <-> 90, forever
void animTextPulse(lv_obj_t *label, uint32_t periodMs);      // text opacity 255 <-> 150, forever
void animDrift(lv_obj_t *obj, int px, uint32_t periodMs);    // translate_x -px <-> +px, forever
void animFadeIn(lv_obj_t *obj, uint32_t delayMs);            // opa 0 -> 255 over 400 ms, once
// Animate an int value from -> to over ms (after delay) calling cb(var, v); used for ring sweeps
// and bars growing.
void animValue(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delayMs);

// Pause (true) or resume (false) every running animation whose var is `screen` or any of its
// descendants. Call with pause = true for the screen being left, false for the one shown.
void pauseScreenAnimations(lv_obj_t *screen, bool pause);

} // namespace ui
