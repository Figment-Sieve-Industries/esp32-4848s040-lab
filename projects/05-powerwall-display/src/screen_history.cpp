// History: energy per hour (Day), per day (Week, Month) from the SD-backed day store (day_history.h).
// Segmented DAY / WEEK / MONTH control, period stepper, self-powered pill, a chart card (solar bars up,
// grid import bars down, battery % line on Day, tap a bar for a readout) and Solar / Home / Grid tiles.
//
// Performance: every object is created once in historyCreate(). Each mode owns a group with two BarCharts
// (bar width and spacing are fixed per chart), only the active group is visible. The chart area has ONE
// transparent clickable overlay (slot from the click x). historyRefresh() returns after a few integer
// compares unless the view (seq), tab, period or selection changed.
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "screens.h"
#include "ui_common.h"
#include "ui_chart.h"
#include "icons.h"
#include "day_history.h"

// Optional visual check without data: -DHISTORY_FAKE_DATA fills the view with the design mockup numbers.
namespace {

using dayhist::Mode;
using dayhist::View;

#ifdef HISTORY_FAKE_DATA
// The mockup's numbers: Day 18 hours so far, Week 7 days, Month 11 days so far.
void fakeView(View &v, int mode, int offset) {
    static const float SOL[18] = {0, 0, 0, 0, 0, 0, 0.05f, 0.3f, 0.9f, 1.6f, 2.2f, 2.5f, 2.6f, 2.3f, 1.9f, 1.4f, 0.8f, 0.25f};
    static const float IMP[18] = {0, 0, 0, 0, 0, 0, 0.6f, 0.8f, 0.4f, 0, 0, 0, 0, 0, 0, 0, 0, 0.6f};
    static const float SOC[18] = {62, 58, 54, 50, 46, 42, 40, 40, 46, 58, 70, 82, 92, 98, 100, 100, 100, 36};
    v = View();
    v.seq = 1;
    v.mode = static_cast<Mode>(mode);
    v.offset = offset;
    v.canNext = offset < 0;
    v.haveTotals = true;
    v.count = mode == 0 ? 24 : (mode == 1 ? 7 : 31);
    snprintf(v.label, sizeof(v.label), "%s", mode == 0 ? "Today" : (mode == 1 ? "5-11 Oct" : "October 2026"));
    const int have = mode == 0 ? 18 : (mode == 1 ? 7 : 11);
    for (int i = 0; i < v.count; i++) {
        v.dates[i] = 20261000 + (mode == 1 ? 5 : 1) + i;
        v.have[i] = i < have;
        v.future[i] = i >= have;
        if (i >= have) continue;
        v.solar[i] = mode == 0 ? SOL[i] : 6.0f + static_cast<float>((i * 7) % 15);
        v.imp[i] = mode == 0 ? IMP[i] : static_cast<float>((i * 5) % 9) * 0.8f;
        v.home[i] = mode == 0 ? 0.5f : 11.0f;
        v.exp[i] = mode == 0 ? 0.4f : 6.0f;
        v.totSolar += v.solar[i];
        v.totImp += v.imp[i];
        v.totHome += v.home[i];
        v.totExp += v.exp[i];
    }
    memset(v.soc, dayhist::SOC_NONE, sizeof(v.soc));
    if (mode == 0)
        for (int i = 0; i < 18; i++) v.soc[i] = static_cast<uint8_t>(SOC[i]);
    v.nowSlot = have - 1;
}
#endif

// ---- geometry (card-relative unless noted) ----
constexpr int CARD_X = 12, CARD_Y = 148, CARD_W = 456, CARD_H = 210;
constexpr int PLOT_X = 12, PLOT_Y = 32, PLOT_W = 432, PLOT_H = 174; // chart area inside the card
constexpr int UP_H = 112;      // solar bars: above the axis (axis line at y = UP_H)
constexpr int DOWN_H = 45;     // import bars: below the axis, capped at DOWN_MAX
constexpr int DOWN_MAX = 44;
constexpr int BAR_SPAN = 106;  // px of the tallest bar
constexpr int STUB_H = 2;
constexpr int TICK_Y = 160;    // tick labels, plot-relative
constexpr int SOC_Y_MAX = 110; // battery 0% line y; 100% is 104 px above
constexpr int GROW_MS = 300;

struct ModeGeo {
    int count, step, barW, x0;     // bars: slots, left-edge spacing, width, offset of the first bar
    int growStagger;
};
// Day: 24 hours at 18 px; Week: 7 days at ~62 px; Month: 31 days at 14 px (fewer in short months)
constexpr ModeGeo GEO[3] = {{24, 18, 11, 3, 8}, {7, 62, 34, 14, 20}, {31, 14, 8, 3, 6}};

constexpr uint8_t SOC_NONE = dayhist::SOC_NONE;

const char *const TAB_NAMES[3] = {"DAY", "WEEK", "MONTH"};
const char *const WD3[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const MON3[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

// ---- objects ----
lv_obj_t *s_scr = nullptr;
lv_obj_t *s_seg[3] = {}, *s_segLbl[3] = {};
lv_obj_t *s_prev = nullptr, *s_next = nullptr, *s_prevLine = nullptr, *s_nextLine = nullptr;
lv_obj_t *s_period = nullptr;
lv_obj_t *s_pill = nullptr, *s_pillPct = nullptr;
lv_obj_t *s_legend = nullptr, *s_legBatt = nullptr, *s_note = nullptr;
lv_obj_t *s_read = nullptr, *s_rTitle = nullptr, *s_rSolar = nullptr, *s_rImp = nullptr, *s_rNet = nullptr,
         *s_rExtra = nullptr;
lv_obj_t *s_grp[3] = {}, *s_up[3] = {}, *s_dn[3] = {};
lv_obj_t *s_tick[3][8] = {};   // tick labels per mode (Day 4, Week 7, Month 5)
lv_obj_t *s_socLine = nullptr, *s_socDot = nullptr;
lv_point_precise_t s_socPts[24]; // 192 B
lv_obj_t *s_tSolar = nullptr, *s_tHome = nullptr;
lv_obj_t *s_gExp = nullptr, *s_gImp = nullptr, *s_gCap = nullptr, *s_gNet = nullptr;

// ---- state ----
int s_mode = 0;          // 0 day, 1 week, 2 month (what the user chose)
int s_offset = 0;        // 0 current period, -1 previous, ...
int s_sel = -1;          // selected slot or -1
bool s_canNext = false;  // from the last drawn view
bool s_growPending = false; // play the bar grow once the data is in (user action / screen entry)

struct Key {
    uint32_t seq = 0;
    int mode = -1, offset = 0, sel = -2;
    bool operator==(const Key &) const = default;
};
ui::ChangeGate<Key> s_gate;

// ---- helpers ----
void fmtSigned(char *buf, size_t n, float x) {
    const char sign = x > 0.05f ? '+' : (x < -0.05f ? '-' : ' ');
    if (sign == ' ') snprintf(buf, n, "%.1f", fabsf(x));
    else snprintf(buf, n, "%c%.1f", sign, fabsf(x));
}

// yyyymmdd -> "Sat 10 Oct" (Sakamoto weekday)
void fmtDate(char *buf, size_t n, int32_t ymd) {
    int y = ymd / 10000, m = (ymd / 100) % 100, d = ymd % 100;
    if (m < 1 || m > 12 || d < 1) {
        snprintf(buf, n, "--");
        return;
    }
    static const int T[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int yy = m < 3 ? y - 1 : y;
    int wd = (yy + yy / 4 - yy / 100 + yy / 400 + T[m - 1] + d) % 7; // 0 = Sunday
    snprintf(buf, n, "%s %d %s", WD3[wd], d, MON3[m - 1]);
}

int selfPct(float home, float imp) {
    if (home < 0.05f) return -1;
    int p = static_cast<int>(lroundf(100.0f * (home - imp) / home));
    return p < 0 ? 0 : (p > 100 ? 100 : p);
}

// A clickable, invisible, non-scrolling object. Swipes still reach the screen (gesture bubbles up).
lv_obj_t *hotspot(lv_obj_t *parent, int x, int y, int w, int h, lv_event_cb_t cb, intptr_t arg) {
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, reinterpret_cast<void *>(arg));
    return o;
}

void setLineColor(lv_obj_t *line, lv_color_t c) {
    if (!lv_color_eq(lv_obj_get_style_line_color(line, LV_PART_MAIN), c)) lv_obj_set_style_line_color(line, c, 0);
}

// ---- drawing ----
void drawView(const View &v, bool force);

void redraw(bool force) {
    View v;
#ifdef HISTORY_FAKE_DATA
    fakeView(v, s_mode, s_offset);
#else
    dayhist::dayHistoryView(v);
#endif
    drawView(v, force);
}

void setSelection(int sel) {
    s_sel = sel;
    redraw(false);
}

void requestPeriod(bool grow) {
    s_sel = -1;
    s_growPending = grow;
    dayhist::dayHistoryRequest(static_cast<Mode>(s_mode), s_offset);
    redraw(true);
}

void onTab(lv_event_t *e) {
    int m = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (m == s_mode) return;
    s_mode = m;
    s_offset = 0;
    requestPeriod(true);
}

void onStep(lv_event_t *e) {
    int dir = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (dir > 0 && !s_canNext) return;
    s_offset += dir;
    requestPeriod(true);
}

void onChartTap(lv_event_t *e) {
    lv_obj_t *o = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int x = p.x - a.x1;
    const ModeGeo &g = GEO[s_mode];
    int slot = (x * g.count) / PLOT_W; // slots tile the plot width evenly (as the bars/ticks do)
    if (x < 0 || slot < 0 || slot >= g.count) return;
    // The view says whether the slot has data; setSelection redraws, which drops a selection without data.
    setSelection(slot == s_sel ? -1 : slot);
}

// Heights/colours of every bar of the active mode, plus tick labels etc.
void drawBars(const View &v, int mi, bool anim) {
    const ModeGeo &g = GEO[mi];
    lv_obj_t *up = s_up[mi], *dn = s_dn[mi];

    float peak = 0.3f; // kWh at the top (BAR_SPAN px)
    if (!v.loading)
        for (int i = 0; i < v.count && i < g.count; i++)
            if (v.have[i] && !v.future[i]) peak = fmaxf(peak, fmaxf(v.solar[i], v.imp[i]));
    const float scale = BAR_SPAN / peak;

    for (int i = 0; i < g.count; i++) {
        lv_opa_t opa = (s_sel >= 0 && s_sel != i) ? LV_OPA_40 : LV_OPA_COVER;
        if (v.count > 0 && i >= v.count) { // short month: slot does not exist
            ui::barChartSet(up, i, 0, 0, ui::TRACK);
            ui::barChartSet(dn, i, 0, 0, ui::TRACK);
            continue;
        }
        const bool data = !v.loading && i < v.count && v.have[i] && !v.future[i];
        int upH = STUB_H, dnH = 0;
        lv_color_t upCol = ui::TRACK;
        if (data) {
            if (v.solar[i] > 0.0f) {
                upH = static_cast<int>(lroundf(v.solar[i] * scale));
                upH = upH < STUB_H ? STUB_H : (upH > BAR_SPAN ? BAR_SPAN : upH);
                upCol = ui::C_SOLAR;
            }
            if (v.imp[i] > 0.0f) {
                dnH = static_cast<int>(lroundf(v.imp[i] * scale));
                dnH = dnH < STUB_H ? STUB_H : (dnH > DOWN_MAX ? DOWN_MAX : dnH);
            }
        }
        ui::barChartSet(up, i, UP_H - upH, upH, upCol, opa);
        ui::barChartSet(dn, i, 0, dnH, ui::C_GRID, opa);
    }
    if (anim) {
        ui::barChartGrow(up, GROW_MS, 0, static_cast<uint32_t>(g.growStagger));
        ui::barChartGrow(dn, GROW_MS, 40, static_cast<uint32_t>(g.growStagger));
    }

    // scale note / SD hint
    if (v.noCard && v.offset < 0) {
        ui::setText(s_note, "No SD card");
    } else if (v.loading || !v.haveTotals) {
        ui::setText(s_note, "--");
    } else {
        char b[24];
        snprintf(b, sizeof(b), "top = %.1f kWh", peak);
        ui::setText(s_note, b);
    }

    // month ticks: the 29th only exists in longer months
    if (mi == 2) ui::setHidden(s_tick[2][4], v.count < 29);
}

void drawSoc(const View &v) {
    // dense polyline over the hours that have a reading (gaps are joined straight); dot at the last one
    int n = 0, lastX = 0, lastY = 0;
    for (int h = 0; h < 24; h++) {
        if (v.soc[h] == SOC_NONE || v.loading) continue;
        int pct = v.soc[h] > 100 ? 100 : v.soc[h];
        lastX = h * GEO[0].step + 9;
        lastY = static_cast<int>(lroundf(SOC_Y_MAX - pct * 1.04f));
        s_socPts[n].x = lastX;
        s_socPts[n].y = lastY;
        n++;
    }
    if (n >= 2) {
        lv_line_set_points(s_socLine, s_socPts, static_cast<uint32_t>(n));
        ui::setHidden(s_socLine, false);
    } else {
        ui::setHidden(s_socLine, true);
    }
    if (n >= 1) {
        lv_obj_set_pos(s_socDot, lastX - 3, lastY - 3);
        ui::setHidden(s_socDot, false);
    } else {
        ui::setHidden(s_socDot, true);
    }
}

void drawReadout(const View &v) {
    const bool sel = s_sel >= 0 && s_sel < v.count && !v.loading && v.have[s_sel] && !v.future[s_sel];
    ui::setHidden(s_legend, sel);
    ui::setHidden(s_note, sel);
    ui::setHidden(s_read, !sel);
    if (!sel) return;
    const int i = s_sel;
    char t[32], b[24];
    if (v.mode == Mode::Day) snprintf(t, sizeof(t), "%02d:00-%02d:00", i, i + 1);
    else fmtDate(t, sizeof(t), v.dates[i]);
    ui::setText(s_rTitle, t);
    ui::setTextF(s_rSolar, "Solar %.1f", static_cast<double>(v.solar[i]));
    ui::setTextF(s_rImp, "Import %.1f", static_cast<double>(v.imp[i]));
    fmtSigned(b, sizeof(b), v.exp[i] - v.imp[i]);
    ui::setTextF(s_rNet, "Net %s", b);
    if (v.mode == Mode::Day) {
        if (v.soc[i] != SOC_NONE) ui::setTextF(s_rExtra, "Battery %d%%", v.soc[i]);
        else ui::setText(s_rExtra, "");
    } else {
        int p = selfPct(v.home[i], v.imp[i]);
        if (p >= 0) ui::setTextF(s_rExtra, "%d%% self-powered", p);
        else ui::setText(s_rExtra, "");
    }
    ui::setHidden(s_rExtra, lv_label_get_text(s_rExtra)[0] == '\0');
}

void drawTotals(const View &v) {
    char b[24];
    const bool ok = v.haveTotals && !v.loading;
    if (ok) {
        ui::formatKWh(b, sizeof(b), v.totSolar);
        ui::setText(s_tSolar, b);
        ui::formatKWh(b, sizeof(b), v.totHome);
        ui::setText(s_tHome, b);
        ui::formatKWh(b, sizeof(b), v.totExp);
        ui::setText(s_gExp, b);
        ui::formatKWh(b, sizeof(b), v.totImp);
        ui::setText(s_gImp, b);
        const float net = v.totExp - v.totImp;
        fmtSigned(b, sizeof(b), net);
        ui::setText(s_gNet, b);
        ui::setText(s_gCap, net >= 0.0f ? "Net export" : "Net import");
        ui::setTextColor(s_gNet, net >= 0.0f ? ui::C_BATT : ui::TEXT);
    } else {
        ui::setText(s_tSolar, "--");
        ui::setText(s_tHome, "--");
        ui::setText(s_gExp, "--");
        ui::setText(s_gImp, "--");
        ui::setText(s_gNet, "--");
        ui::setText(s_gCap, "Net");
        ui::setTextColor(s_gNet, ui::TEXT);
    }
    int p = ok ? selfPct(v.totHome, v.totImp) : -1;
    ui::setHidden(s_pill, p < 0);
    if (p >= 0) ui::setTextF(s_pillPct, "%d%%", p);
}

void drawView(const View &v, bool force) {
    const int mi = static_cast<int>(v.mode);
    // The view may lag the request by a frame; draw only what matches what the user asked for.
    if (mi != s_mode || v.offset != s_offset) return;

    // drop a selection that no longer points at a slot with data
    if (s_sel >= 0 && (v.loading || s_sel >= v.count || !v.have[s_sel] || v.future[s_sel])) s_sel = -1;

    Key k;
    k.seq = v.seq;
    k.mode = mi;
    k.offset = v.offset;
    k.sel = s_sel;
    if (!s_gate.changed(k, force)) return;

    s_canNext = v.canNext;

    for (int m = 0; m < 3; m++) {
        ui::setHidden(s_grp[m], m != mi);
        ui::setBgColor(s_seg[m], m == mi ? ui::TRACK : ui::CARD);
        ui::setTextColor(s_segLbl[m], m == mi ? ui::TEXT : ui::MUTED);
    }
    ui::setText(s_period, v.label);
    setLineColor(s_nextLine, v.canNext ? ui::TEXT : ui::DIM_BAR);
    ui::setHidden(s_legBatt, mi != 0);

    const bool grow = s_growPending && !v.loading;
    if (grow) s_growPending = false;
    drawBars(v, mi, grow);
    if (mi == 0) drawSoc(v);
    drawReadout(v);
    drawTotals(v);
}

// ---- construction ----
lv_obj_t *tickLabel(lv_obj_t *parent, const ModeGeo &g, int slot, const char *text) {
    const int w = 28;
    const int cx = g.x0 + slot * g.step + g.barW / 2;
    lv_obj_t *l = ui::labelAt(parent, &lv_font_montserrat_12, ui::MUTED, text, cx - w / 2, TICK_Y, w, LV_TEXT_ALIGN_CENTER);
    return l;
}

// "square + text" legend entry
lv_obj_t *legendItem(lv_obj_t *parent, lv_color_t col, const char *text, int swatchW, int swatchH) {
    lv_obj_t *row = ui::flex(parent, LV_FLEX_FLOW_ROW, 5);
    ui::rect(row, 0, 0, swatchW, swatchH, col, swatchH > 4 ? 2 : 0);
    ui::label(row, &lv_font_montserrat_12, ui::MUTED, text);
    return row;
}

lv_obj_t *makeTile(lv_obj_t *scr, int x, int w, lv_color_t dot, const char *title, lv_obj_t **value) {
    lv_obj_t *t = ui::card(scr, x, 366, w, 74);
    lv_obj_set_style_radius(t, 12, 0);
    lv_obj_t *cap = ui::flex(t, LV_FLEX_FLOW_ROW, 5);
    lv_obj_set_pos(cap, 10, 10);
    ui::rect(cap, 0, 0, 8, 8, dot, 4);
    ui::label(cap, &lv_font_montserrat_12, ui::MUTED, title);
    lv_obj_t *unit = nullptr;
    lv_obj_t *row = ui::valueUnit(t, &lv_font_montserrat_20, &lv_font_montserrat_12, value, &unit, 2);
    lv_obj_set_pos(row, 10, 32);
    ui::setText(*value, "--");
    ui::setText(unit, "kWh");
    return t;
}

// icon + "Exported " + value
lv_obj_t *flowRow(lv_obj_t *parent, Icon icon, lv_color_t col, const char *title, lv_obj_t **value) {
    lv_obj_t *row = ui::flex(parent, LV_FLEX_FLOW_ROW, 5);
    lv_obj_t *ic = iconCreate(row, icon, 12, col, 1.8f);
    (void)ic;
    ui::label(row, &lv_font_montserrat_14, ui::MUTED, title);
    *value = ui::label(row, &lv_font_montserrat_14, ui::TEXT, "--");
    return row;
}

} // namespace

lv_obj_t *historyCreate() {
    s_scr = ui::screenCreate(ui::SCR_HISTORY);

    // ---- segmented control ----
    lv_obj_t *tabs = ui::card(s_scr, 12, 52, 456, 44);
    for (int i = 0; i < 3; i++) {
        s_seg[i] = hotspot(tabs, 4 + i * 150, 4, 146, 36, onTab, i);
        lv_obj_set_style_radius(s_seg[i], 10, 0);
        lv_obj_set_style_bg_opa(s_seg[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(s_seg[i], i == 0 ? ui::TRACK : ui::CARD, 0);
        s_segLbl[i] = ui::labelAt(s_seg[i], &lv_font_montserrat_14, i == 0 ? ui::TEXT : ui::MUTED, TAB_NAMES[i], 0, 10, 146,
                                  LV_TEXT_ALIGN_CENTER);
        lv_obj_set_style_text_letter_space(s_segLbl[i], 1, 0);
    }

    // ---- period row ----
    static const lv_point_precise_t PREV_PTS[3] = {{7, 1}, {2, 7}, {7, 13}};
    static const lv_point_precise_t NEXT_PTS[3] = {{2, 1}, {7, 7}, {2, 13}};
    s_prev = hotspot(s_scr, 12, 104, 36, 36, onStep, -1);
    lv_obj_set_style_radius(s_prev, 10, 0);
    lv_obj_set_style_bg_color(s_prev, ui::CARD, 0);
    lv_obj_set_style_bg_opa(s_prev, LV_OPA_COVER, 0);
    s_prevLine = lv_line_create(s_prev);
    lv_line_set_points(s_prevLine, PREV_PTS, 3);
    lv_obj_set_pos(s_prevLine, 13, 11);
    s_next = hotspot(s_scr, 184, 104, 36, 36, onStep, 1);
    lv_obj_set_style_radius(s_next, 10, 0);
    lv_obj_set_style_bg_color(s_next, ui::CARD, 0);
    lv_obj_set_style_bg_opa(s_next, LV_OPA_COVER, 0);
    s_nextLine = lv_line_create(s_next);
    lv_line_set_points(s_nextLine, NEXT_PTS, 3);
    lv_obj_set_pos(s_nextLine, 14, 11);
    for (lv_obj_t *ln : {s_prevLine, s_nextLine}) {
        ui::inert(ln);
        lv_obj_set_style_line_width(ln, 2, 0);
        lv_obj_set_style_line_rounded(ln, true, 0);
        lv_obj_set_style_line_color(ln, ui::TEXT, 0);
    }
    lv_obj_set_style_line_color(s_nextLine, ui::DIM_BAR, 0);
    s_period = ui::labelAt(s_scr, &lv_font_montserrat_16, ui::TEXT, "", 52, 113, 128, LV_TEXT_ALIGN_CENTER);

    s_pill = ui::flex(s_scr, LV_FLEX_FLOW_ROW, 6);
    lv_obj_set_height(s_pill, 30);
    lv_obj_set_style_pad_hor(s_pill, 12, 0);
    lv_obj_set_style_radius(s_pill, 15, 0);
    lv_obj_set_style_bg_color(s_pill, ui::C_OK_BG, 0);
    lv_obj_set_style_bg_opa(s_pill, LV_OPA_COVER, 0);
    s_pillPct = ui::label(s_pill, &lv_font_montserrat_16, ui::C_BATT, "--%");
    ui::label(s_pill, &lv_font_montserrat_12, ui::C_BATT, "self-powered");
    lv_obj_align(s_pill, LV_ALIGN_TOP_RIGHT, -12, 107);
    lv_obj_add_flag(s_pill, LV_OBJ_FLAG_HIDDEN);

    // ---- chart card ----
    lv_obj_t *cc = ui::card(s_scr, CARD_X, CARD_Y, CARD_W, CARD_H);
    // gridlines first (under the bars): axis and a faint mid line
    ui::rect(cc, PLOT_X, PLOT_Y + UP_H, PLOT_W, 1, ui::TRACK);
    ui::rect(cc, PLOT_X, PLOT_Y + UP_H / 2, PLOT_W, 1, lv_color_hex(0x1C232C));

    for (int m = 0; m < 3; m++) {
        const ModeGeo &g = GEO[m];
        s_grp[m] = ui::box(cc);
        lv_obj_set_pos(s_grp[m], PLOT_X, PLOT_Y);
        lv_obj_set_size(s_grp[m], PLOT_W, PLOT_H);
        s_up[m] = ui::barChartCreate(s_grp[m], g.x0, 0, PLOT_W - g.x0, UP_H, g.count, g.barW, g.step, 2);
        s_dn[m] = ui::barChartCreate(s_grp[m], g.x0, UP_H + 1, PLOT_W - g.x0, DOWN_H, g.count, g.barW, g.step, 2);
        for (int i = 0; i < g.count; i++) ui::barChartSet(s_up[m], i, UP_H - STUB_H, STUB_H, ui::TRACK);
        if (m != 0) lv_obj_add_flag(s_grp[m], LV_OBJ_FLAG_HIDDEN);
    }
    // ticks
    for (int i = 0; i < 4; i++) {
        char b[4];
        snprintf(b, sizeof(b), "%02d", i * 6);
        s_tick[0][i] = tickLabel(s_grp[0], GEO[0], i * 6, b);
    }
    static const char *const WEEK_T[7] = {"M", "T", "W", "T", "F", "S", "S"};
    for (int i = 0; i < 7; i++) s_tick[1][i] = tickLabel(s_grp[1], GEO[1], i, WEEK_T[i]);
    static const char *const MONTH_T[5] = {"1", "8", "15", "22", "29"};
    for (int i = 0; i < 5; i++) s_tick[2][i] = tickLabel(s_grp[2], GEO[2], i * 7, MONTH_T[i]);

    // battery % line (Day group, above the bars)
    s_socLine = lv_line_create(s_grp[0]);
    ui::inert(s_socLine);
    lv_obj_set_pos(s_socLine, 0, 0);
    lv_obj_set_size(s_socLine, PLOT_W, UP_H);
    lv_obj_set_style_line_width(s_socLine, 2, 0);
    lv_obj_set_style_line_color(s_socLine, ui::C_BATT, 0);
    lv_obj_set_style_line_rounded(s_socLine, true, 0);
    lv_obj_add_flag(s_socLine, LV_OBJ_FLAG_HIDDEN);
    s_socDot = ui::rect(s_grp[0], 0, 0, 7, 7, ui::C_BATT, 4);
    lv_obj_add_flag(s_socDot, LV_OBJ_FLAG_HIDDEN);

    // legend row / bar readout (same place), scale note on the right
    s_legend = ui::flex(cc, LV_FLEX_FLOW_ROW, 14);
    lv_obj_set_pos(s_legend, 12, 10);
    legendItem(s_legend, ui::C_SOLAR, "Solar", 10, 10);
    legendItem(s_legend, ui::C_GRID, "Grid import", 10, 10);
    s_legBatt = legendItem(s_legend, ui::C_BATT, "Battery %", 12, 2);
    s_note = ui::labelAt(cc, &lv_font_montserrat_12, ui::MUTED, "", 12, 10, 432, LV_TEXT_ALIGN_RIGHT);

    s_read = ui::flex(cc, LV_FLEX_FLOW_ROW, 8);
    lv_obj_set_pos(s_read, 12, 10);
    s_rTitle = ui::label(s_read, &lv_font_montserrat_12, ui::TEXT, "");
    s_rSolar = ui::label(s_read, &lv_font_montserrat_12, ui::C_SOLAR, "");
    s_rImp = ui::label(s_read, &lv_font_montserrat_12, ui::C_GRID, "");
    s_rNet = ui::label(s_read, &lv_font_montserrat_12, ui::TEXT, "");
    s_rExtra = ui::label(s_read, &lv_font_montserrat_12, ui::C_BATT, "");
    lv_obj_add_flag(s_read, LV_OBJ_FLAG_HIDDEN);

    // the one tap target over the plot
    hotspot(cc, PLOT_X, PLOT_Y, PLOT_W, PLOT_H, onChartTap, 0);

    // ---- tiles ----
    makeTile(s_scr, 12, 108, ui::C_SOLAR, "Solar", &s_tSolar);
    makeTile(s_scr, 128, 108, ui::C_HOME, "Home", &s_tHome);
    lv_obj_t *gt = ui::card(s_scr, 244, 366, 224, 74);
    lv_obj_set_style_radius(gt, 12, 0);
    lv_obj_t *left = ui::flex(gt, LV_FLEX_FLOW_COLUMN, 5, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_align(left, LV_ALIGN_LEFT_MID, 10, 0);
    flowRow(left, Icon::ArrowUp, ui::C_BATT, "Exported", &s_gExp);
    flowRow(left, Icon::ArrowDown, ui::C_GRID, "Imported", &s_gImp);
    lv_obj_t *right = ui::flex(gt, LV_FLEX_FLOW_COLUMN, 2, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, -10, 0);
    s_gCap = ui::label(right, &lv_font_montserrat_12, ui::MUTED, "Net");
    lv_obj_t *unit = nullptr;
    ui::valueUnit(right, &lv_font_montserrat_20, &lv_font_montserrat_12, &s_gNet, &unit, 2);
    ui::setText(s_gNet, "--");
    ui::setText(unit, "kWh");

    return s_scr;
}

void historyRefresh() { redraw(false); }

void historyOnShow() {
    dayhist::dayHistoryRequest(static_cast<Mode>(s_mode), s_offset);
    s_sel = -1;
    s_growPending = true; // entry animation (delayed by ui::setShowDelay until the slide is over)
    redraw(true);
}

bool historySetMode(int mode) {
    if (mode < 0 || mode > 2) return false;
    s_mode = mode;
    s_offset = 0;
    requestPeriod(true);
    return true;
}

bool historyStep(int dir) {
    if (dir == 0) return false;
    if (dir > 0 && s_offset >= 0) return false;
    s_offset += dir > 0 ? 1 : -1;
    requestPeriod(true);
    return true;
}

void historyDescribe(char *buf, size_t n) {
    if (!n) return;
    View v;
#ifdef HISTORY_FAKE_DATA
    fakeView(v, s_mode, s_offset);
#else
    dayhist::dayHistoryView(v);
#endif
    int have = 0;
    for (int i = 0; i < v.count && i < dayhist::MAX_SLOTS; i++)
        if (v.have[i]) have++;
    const char *mn = v.mode == Mode::Day ? "day" : (v.mode == Mode::Week ? "week" : "month");
    snprintf(buf, n,
             "history: mode %s offset %d label \"%s\" slots %d have %d loading %d seq %u totals solar %.1f home %.1f "
             "exp %.1f imp %.1f",
             mn, v.offset, v.label, v.count, have, v.loading ? 1 : 0, static_cast<unsigned>(v.seq),
             static_cast<double>(v.totSolar), static_cast<double>(v.totHome), static_cast<double>(v.totExp),
             static_cast<double>(v.totImp));
}
