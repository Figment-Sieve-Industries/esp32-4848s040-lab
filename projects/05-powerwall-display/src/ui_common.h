#pragma once
// Shared look and helpers for all screens (480x480, rotated so USB-C is at the bottom).
#include <lvgl.h>
#include <stddef.h>
#include <stdint.h>

namespace ui {

// Palette (matches the design canvas)
extern const lv_color_t BG, CARD, CARD_HI, TRACK, TEXT, MUTED;
extern const lv_color_t C_SOLAR, C_GRID, C_HOME, C_BATT, C_ALERT, C_ALERT_BG, C_WEATHER, C_OK_BG;

// Mains voltage normal range (230 V nominal, 207-253 V)
constexpr float MAINS_V_MIN = 207.0f, MAINS_V_MAX = 253.0f;

// 0 = house, 1 = today, 2 = history, 3 = battery & grid, 4 = weather, 5 = moon,
// 6 = aurora, 7 = sun, 8 = El Nino, 9 = settings, 10 = about
constexpr int SCREEN_COUNT = 11;
enum ScreenIndex {
    SCR_HOUSE, SCR_TODAY, SCR_HISTORY, SCR_HEALTH, SCR_WEATHER, SCR_MOON, SCR_AURORA, SCR_SUN, SCR_ENSO, SCR_SETTINGS,
    SCR_ABOUT
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
    int shownRssi = 0;     // dBm on screen: changes only by >= RSSI_STEP_DB, at most every RSSI_MIN_MS
    uint32_t rssiMs = 0;   // millis() of the last change (0 = nothing shown yet)
};
TopBar topBarCreate(lv_obj_t *scr);
void topBarUpdate(TopBar &tb);

// Page indicator centred at y = 456: active page is a 20x8 pill (TEXT), others 8x8 dots (TRACK).
void pageDots(lv_obj_t *scr, int activeIndex);
// Shows the dots of the enabled screens only (bit per ScreenIndex, settings_codec.h); the row re-centres.
// Cheap: does nothing unless the mask changed. Pages created later pick the current mask up.
void setScreenMask(uint16_t mask);

// Animations (all LVGL-cheap; deleted automatically with the object)
void animPulse(lv_obj_t *obj, uint32_t periodMs);            // bg opacity 255 <-> 90, forever
void animTextPulse(lv_obj_t *label, uint32_t periodMs);      // text opacity 255 <-> 150, forever
void animDrift(lv_obj_t *obj, int px, uint32_t periodMs);    // translate_x -px <-> +px, forever
void animFadeIn(lv_obj_t *obj, uint32_t delayMs);            // opa 0 -> 255 over 400 ms, once
// Animate an int value from -> to over ms (after delay) calling cb(var, v); used for ring sweeps
// and bars growing.
void animValue(void *var, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms, uint32_t delayMs);
// Marks an app animation (call before lv_anim_start in every place the app starts one whose var is an
// lv_obj_t). pauseScreenAnimations only touches marked animations: LVGL's own style transitions use a
// different var type (trans_t*), which must never be read as an object.
void animTag(lv_anim_t *a);

// Pause (true) or resume (false) every running animation whose var is `screen` or any of its
// descendants. Call with pause = true for the screen being left, false for the one shown.
void pauseScreenAnimations(lv_obj_t *screen, bool pause);

// ---------------- shared helpers (consolidation round) ----------------

// Named palette for colours the screens used as literals (values unchanged)
extern const lv_color_t C_RED /*E5484D = C_ALERT*/, C_ORANGE /*FF8A3D*/, C_YELLOW /*F5D400*/, C_GREEN /*3DDC84 = C_BATT*/,
    C_BLUE /*3B82F6*/, C_PURPLE /*B266FF*/, DIM_BAR /*3A4654*/;
// Severity ramp: 0 green, 1 yellow, 2 orange, >= 3 red (SOC bands, Kp, scales, hints)
lv_color_t levelColor(int level);

void inert(lv_obj_t *o); // clear CLICKABLE and SCROLLABLE
// Filled rectangle at x,y (w x h), inert, no border/padding
lv_obj_t *rect(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t bg, int radius = 0);
// Content-sized flex container (transparent, inert)
lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, int gap, lv_flex_align_t main = LV_FLEX_ALIGN_START,
               lv_flex_align_t cross = LV_FLEX_ALIGN_CENTER);
// Row of a big value and a smaller unit, bottom-aligned (unit lifted by unitPadBottom). Returns the row.
lv_obj_t *valueUnit(lv_obj_t *parent, const lv_font_t *vf, const lv_font_t *uf, lv_obj_t **value, lv_obj_t **unit,
                    int unitPadBottom);
// Label at x,y; w > 0 fixes the width (with the given alignment)
lv_obj_t *labelAt(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *t, int x, int y, int w = 0,
                  lv_text_align_t align = LV_TEXT_ALIGN_LEFT);
lv_obj_t *cardCaption(lv_obj_t *card, const char *text, int x = 14, int y = 10); // MUTED caption in a card

// Horizontal track + fill bar
struct HBar {
    lv_obj_t *track = nullptr, *fill = nullptr;
    int w = 0;
};
HBar hbar(lv_obj_t *parent, int x, int y, int w, int h, lv_color_t track, lv_color_t fill, int radius);
void hbarSet(HBar &b, float frac /* <= 0 hides the fill, clamped to 1 */, lv_color_t fill); // change-only

// Change-only printf setters
void setTextF(lv_obj_t *l, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// "--" in MUTED when !have; the text in MUTED when stale; else in `ok` colour
void setValueF(lv_obj_t *l, bool have, bool stale, lv_color_t ok, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));

void formatKWh(char *buf, size_t n, double kwh, bool withUnit); // overload: "18.4 kWh"

// Fade-in for a list of objects: first after firstDelayMs, then stepMs apart. Main starts onShow
// after the screen slide finishes, so these never overlap the slide.
void fadeIn(lv_obj_t *const *objs, int n, uint32_t firstDelayMs, uint32_t stepMs);
// Extra delay added to every entry animation (animFadeIn, animValue, fadeIn, barChartGrow) started
// while set. main sets it to the slide time around onShow(), so the animations start with the
// elements already at their start state but only begin after the 300 ms slide (no pop, no overlap).
void setShowDelay(uint32_t ms);
uint32_t showDelay();

// Skip work when the inputs haven't changed (K needs ==)
template <class K> struct ChangeGate {
    K last{};
    bool have = false;
    bool changed(const K &k, bool force) {
        if (!force && have && k == last) return false;
        last = k;
        have = true;
        return true;
    }
};

// Status banner card (aurora status / ENSO alert): ring with a glyph on the left, caption, name, sub line
struct Banner {
    lv_obj_t *card = nullptr, *ring = nullptr, *cap = nullptr, *name = nullptr, *sub = nullptr;
};
Banner bannerCreate(lv_obj_t *scr, int y, int h, const char *caption, int subW = 276);
// fg = accent (border, ring, caption, name), bg = card background; name switches to `small` when longer
// than bigMaxLen; dim mutes everything (stale).
void bannerSet(Banner &b, lv_color_t fg, lv_color_t bg, const char *name, bool dim, const char *sub, size_t bigMaxLen,
               const lv_font_t *big, const lv_font_t *small);

// Privacy mode for screenshots/demos (serial "v 1"): the top bar shows "Wi-Fi" instead of the SSID and
// placeName() returns a placeholder instead of the configured place name. Not saved; off after a reboot.
void setPrivacy(bool on);
bool privacy();
const char *placeName(); // cfg place name, or "MY TOWN" in privacy mode (upper case)

// Screen registry: screenCreate(index) = screenCreate() + top bar + page dots for that index. The top
// bar is then updated centrally by main (topBarUpdateFor(current)); screens don't keep a TopBar.
lv_obj_t *screenCreate(int index);
void topBarUpdateFor(int index);

} // namespace ui
