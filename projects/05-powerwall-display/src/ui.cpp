// Power-flow screen, 480x480:
//
//                [ Solar ]
//                    |
//   ( Powerwall % ) -- hub -- [ Home ]
//                    |
//                 [ Grid ]
//
// Dots run along each link in the direction power flows; faster for more power.
#include "ui.h"
#include "powerwall.h"
#include <Arduino.h>
#include <math.h>
#include <time.h>
#include <string.h>

void setBacklightLevel(uint8_t level); // main.cpp

namespace {

constexpr int NODE_D = 108;       // node circle diameter
constexpr int HUB_X = 240, HUB_Y = 252;
constexpr int SOLAR_X = HUB_X, SOLAR_Y = 110;
constexpr int BATT_X = 84, BATT_Y = HUB_Y;   // left; leaves room for the SOC ring
constexpr int HOME_X = 404, HOME_Y = HUB_Y;
constexpr int GRID_X = HUB_X, GRID_Y = 392;
constexpr int ARC_D = NODE_D + 22;
constexpr int DOTS_PER_LINK = 3;
constexpr float MIN_FLOW_W = 30;  // below this a link shows no flow
constexpr uint32_t STALE_MS = 10000;

const lv_color_t BG = lv_color_hex(0x0B0F14);
const lv_color_t CARD = lv_color_hex(0x151B23);
const lv_color_t LINE = lv_color_hex(0x2A3440);
const lv_color_t TEXT = lv_color_hex(0xE6EDF3);
const lv_color_t MUTED = lv_color_hex(0x8B98A5);
const lv_color_t C_SOLAR = lv_color_hex(0xF5B700);
const lv_color_t C_GRID = lv_color_hex(0x9AA5B1);
const lv_color_t C_HOME = lv_color_hex(0x3DB2FF);
const lv_color_t C_BATT = lv_color_hex(0x3DDC84);
const lv_color_t C_ALERT = lv_color_hex(0xE5484D);

struct Link {
    int x, y;              // node centre
    lv_color_t color;
    lv_obj_t *dots[DOTS_PER_LINK];
    float phase = 0;       // 0..1 along the link
    float speed = 0;       // links per second
    bool toHub = true;     // flow direction
    bool active = false;
    lv_point_precise_t pts[2];
};

enum { L_SOLAR, L_GRID, L_HOME, L_BATT, L_COUNT };
Link links[L_COUNT];

lv_obj_t *lblTime, *lblDate, *lblWifi, *lblStatus, *banner;
lv_obj_t *valSolar, *valGrid, *valHome, *valSoc, *subGrid, *subBatt;
lv_obj_t *socArc;
uint32_t lastAnimMs;

lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

// Round node with a title and a value label inside. Returns the value label.
lv_obj_t *node(int cx, int cy, lv_color_t color, const char *title, lv_obj_t **outCircle = nullptr) {
    lv_obj_t *c = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, NODE_D, NODE_D);
    lv_obj_set_pos(c, cx - NODE_D / 2, cy - NODE_D / 2);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(c, CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(c, color, 0);
    lv_obj_set_style_border_width(c, 3, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *t = label(c, &lv_font_montserrat_14, color, title);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_t *v = label(c, &lv_font_montserrat_20, TEXT, "--");
    lv_obj_align(v, LV_ALIGN_CENTER, 0, 10);
    if (outCircle) *outCircle = c;
    return v;
}

void linkCreate(Link &l, int x, int y, lv_color_t color) {
    l.x = x;
    l.y = y;
    l.color = color;
    l.pts[0] = {(lv_value_precise_t)x, (lv_value_precise_t)y};
    l.pts[1] = {(lv_value_precise_t)HUB_X, (lv_value_precise_t)HUB_Y};
    lv_obj_t *line = lv_line_create(lv_screen_active());
    lv_line_set_points(line, l.pts, 2);
    lv_obj_set_style_line_width(line, 4, 0);
    lv_obj_set_style_line_color(line, LINE, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    for (auto &d : l.dots) {
        d = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, color, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_add_flag(d, LV_OBJ_FLAG_HIDDEN);
    }
}

void setHidden(lv_obj_t *o, bool hidden);

// power > 0 means flowing from the node towards the hub.
void linkSet(Link &l, float powerTowardHub, bool stale) {
    float p = fabsf(powerTowardHub);
    l.active = !stale && p >= MIN_FLOW_W;
    l.toHub = powerTowardHub > 0;
    // 0.3 links/s at 30 W up to ~1.5 links/s at 5 kW+
    l.speed = 0.3f + 1.2f * fminf(p, 5000.0f) / 5000.0f;
    for (auto d : l.dots) setHidden(d, !l.active);
}

void animTick(lv_timer_t *) {
    uint32_t now = lv_tick_get();
    float dt = (now - lastAnimMs) / 1000.0f;
    lastAnimMs = now;
    const float r = NODE_D / 2.0f + 4;  // start just outside the node circle
    for (auto &l : links) {
        if (!l.active) continue;
        l.phase = fmodf(l.phase + dt * l.speed, 1.0f);
        float dx = HUB_X - l.x, dy = HUB_Y - l.y;
        float len = sqrtf(dx * dx + dy * dy);
        float ux = dx / len, uy = dy / len;
        float run = len - r;  // node edge -> hub
        for (int k = 0; k < DOTS_PER_LINK; k++) {
            float t = fmodf(l.phase + (float)k / DOTS_PER_LINK, 1.0f);
            if (!l.toHub) t = 1.0f - t;
            float px = l.x + ux * (r + t * run);
            float py = l.y + uy * (r + t * run);
            lv_obj_set_pos(l.dots[k], (int32_t)px - 5, (int32_t)py - 5);
        }
    }
}

// Only touch a label when its text actually changes: lv_label_set_text() always invalidates,
// and needless redraws steal frame time from the flow animation.
void setText(lv_obj_t *l, const char *text) {
    if (strcmp(lv_label_get_text(l), text) != 0) lv_label_set_text(l, text);
}

void setHidden(lv_obj_t *o, bool hidden) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) != hidden) {
        if (hidden) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

void formatPower(char *buf, size_t n, float w) {
    float a = fabsf(w);
    if (a < 1000) snprintf(buf, n, "%d W", (int)lroundf(a));
    else snprintf(buf, n, "%.2f kW", a / 1000.0f);
}

uint8_t brightness = 255;
void screenTap(lv_event_t *) {
    // Tap anywhere: cycle backlight full -> medium -> dim
    brightness = brightness == 255 ? 160 : brightness == 160 ? 100 : 255;
    setBacklightLevel(brightness);
}

} // namespace

void uiCreate() {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, screenTap, LV_EVENT_CLICKED, nullptr);

    // Links first so the nodes draw over their ends
    linkCreate(links[L_SOLAR], SOLAR_X, SOLAR_Y, C_SOLAR);
    linkCreate(links[L_GRID], GRID_X, GRID_Y, C_GRID);
    linkCreate(links[L_HOME], HOME_X, HOME_Y, C_HOME);
    linkCreate(links[L_BATT], BATT_X, BATT_Y, C_BATT);

    lv_obj_t *hub = lv_obj_create(scr);
    lv_obj_remove_style_all(hub);
    lv_obj_set_size(hub, 18, 18);
    lv_obj_set_pos(hub, HUB_X - 9, HUB_Y - 9);
    lv_obj_set_style_radius(hub, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(hub, LINE, 0);
    lv_obj_set_style_bg_opa(hub, LV_OPA_COVER, 0);

    valSolar = node(SOLAR_X, SOLAR_Y, C_SOLAR, LV_SYMBOL_CHARGE " SOLAR");
    valGrid = node(GRID_X, GRID_Y, C_GRID, LV_SYMBOL_POWER " GRID");
    valHome = node(HOME_X, HOME_Y, C_HOME, LV_SYMBOL_HOME " HOME");

    // Grid direction text, right of the grid node
    subGrid = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_set_width(subGrid, 140);
    lv_obj_set_style_text_align(subGrid, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_pos(subGrid, GRID_X + NODE_D / 2 + 12, GRID_Y - 8);

    // Powerwall: SOC arc around the node, % inside
    lv_obj_t *battCircle;
    lv_obj_t *battVal = node(BATT_X, BATT_Y, C_BATT, LV_SYMBOL_BATTERY_FULL " POWERWALL", &battCircle);
    lv_obj_add_flag(battVal, LV_OBJ_FLAG_HIDDEN);
    valSoc = label(battCircle, &lv_font_montserrat_28, TEXT, "--%");
    lv_obj_align(valSoc, LV_ALIGN_CENTER, 0, 12);
    socArc = lv_arc_create(scr);
    lv_obj_set_size(socArc, ARC_D, ARC_D);
    lv_obj_set_pos(socArc, BATT_X - ARC_D / 2, BATT_Y - ARC_D / 2);
    lv_arc_set_rotation(socArc, 270);
    lv_arc_set_bg_angles(socArc, 0, 360);
    lv_arc_set_range(socArc, 0, 100);
    lv_arc_set_value(socArc, 0);
    lv_obj_remove_style(socArc, nullptr, LV_PART_KNOB);
    lv_obj_remove_flag(socArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(socArc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(socArc, LINE, LV_PART_MAIN);
    lv_obj_set_style_arc_width(socArc, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(socArc, C_BATT, LV_PART_INDICATOR);

    // Battery state, centred under the SOC ring
    subBatt = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_set_width(subBatt, 150);
    lv_obj_set_style_text_align(subBatt, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(subBatt, BATT_X - 75, BATT_Y + ARC_D / 2 + 6);

    // Top bar
    lblTime = label(scr, &lv_font_montserrat_28, TEXT, "--:--");
    lv_obj_set_pos(lblTime, 14, 8);
    lblDate = label(scr, &lv_font_montserrat_14, MUTED, "");
    lv_obj_set_pos(lblDate, 104, 18);
    lblWifi = label(scr, &lv_font_montserrat_14, MUTED, LV_SYMBOL_WIFI " ...");
    lv_obj_align(lblWifi, LV_ALIGN_TOP_RIGHT, -14, 16);

    // Status line (errors / staleness), bottom left
    lblStatus = label(scr, &lv_font_montserrat_14, MUTED, "Connecting...");
    lv_obj_set_width(lblStatus, 150);
    lv_label_set_long_mode(lblStatus, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(lblStatus, 14, 420);

    // Off-grid banner
    banner = lv_obj_create(scr);
    lv_obj_remove_style_all(banner);
    lv_obj_set_size(banner, 300, 34);
    lv_obj_align(banner, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_radius(banner, 17, 0);
    lv_obj_set_style_bg_color(banner, C_ALERT, 0);
    lv_obj_set_style_bg_opa(banner, LV_OPA_COVER, 0);
    lv_obj_t *bt = label(banner, &lv_font_montserrat_16, lv_color_white(), LV_SYMBOL_WARNING " OFF-GRID - on battery");
    lv_obj_center(bt);
    lv_obj_add_flag(banner, LV_OBJ_FLAG_HIDDEN);

    lastAnimMs = lv_tick_get();
    lv_timer_create(animTick, LV_DEF_REFR_PERIOD, nullptr); // move dots every frame
}

void uiRefresh() {
    PwData d = powerwallData();
    NetStatus n = networkStatus();
    uint32_t age = millis() - d.updatedMs;
    bool stale = !d.valid || age > STALE_MS;
    char buf[48];

    // Clock (valid once NTP has synced)
    struct tm tm;
    if (getLocalTime(&tm, 0)) {
        strftime(buf, sizeof(buf), "%H:%M", &tm);
        setText(lblTime, buf);
        strftime(buf, sizeof(buf), "%a %d %b", &tm);
        setText(lblDate, buf);
    }

    if (n.wifiUp) {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " %s  %d dBm", n.ssid, n.rssi);
        if (!lv_color_eq(lv_obj_get_style_text_color(lblWifi, LV_PART_MAIN), MUTED)) lv_obj_set_style_text_color(lblWifi, MUTED, 0);
    } else {
        snprintf(buf, sizeof(buf), LV_SYMBOL_WIFI " offline");
        if (!lv_color_eq(lv_obj_get_style_text_color(lblWifi, LV_PART_MAIN), C_ALERT)) lv_obj_set_style_text_color(lblWifi, C_ALERT, 0);
    }
    setText(lblWifi, buf);

    if (stale) {
        setText(valSolar, "--");
        setText(valGrid, "--");
        setText(valHome, "--");
        setText(subGrid, "");
        setText(subBatt, "");
        if (!d.valid) setText(valSoc, "--%");
        setText(lblStatus, d.error[0] ? d.error : "Waiting for Powerwall...");
        lv_obj_set_style_text_color(lblStatus, d.error[0] ? C_ALERT : MUTED, 0);
    } else {
        formatPower(buf, sizeof(buf), d.solarW);
        setText(valSolar, buf);
        formatPower(buf, sizeof(buf), d.siteW);
        setText(valGrid, buf);
        formatPower(buf, sizeof(buf), d.loadW);
        setText(valHome, buf);

        setText(subGrid, fabsf(d.siteW) < MIN_FLOW_W ? "" : d.siteW > 0 ? "Importing" : "Exporting");

        snprintf(buf, sizeof(buf), "%d%%", (int)lroundf(d.soc));
        setText(valSoc, buf);
        if (lv_arc_get_value(socArc) != (int)lroundf(d.soc)) lv_arc_set_value(socArc, (int)lroundf(d.soc));
        char pw[16];
        formatPower(pw, sizeof(pw), d.batteryW);
        if (fabsf(d.batteryW) < MIN_FLOW_W) snprintf(buf, sizeof(buf), "Idle");
        else snprintf(buf, sizeof(buf), "%s\n%s", d.batteryW > 0 ? "Discharging" : "Charging", pw);
        setText(subBatt, buf);

        setText(lblStatus, "");
    }

    setHidden(banner, stale || d.gridConnected);

    // Flow directions (positive = node -> hub)
    linkSet(links[L_SOLAR], d.solarW, stale);
    linkSet(links[L_GRID], d.siteW, stale);
    linkSet(links[L_HOME], -d.loadW, stale);
    linkSet(links[L_BATT], d.batteryW, stale);
}
