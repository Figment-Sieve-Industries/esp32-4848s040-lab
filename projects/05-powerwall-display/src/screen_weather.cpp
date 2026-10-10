// Screen I: weather + lifetime energy. Geometry follows Weather.dc.html (480x480).
#include "screens.h"
#include "ui_common.h"
#include "icons.h"
#include "powerwall.h"
#include "secrets.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>

namespace {

constexpr int HOURS = 6;
constexpr int PAD_X = 14;
constexpr int INNER_W = 432 - 2 * PAD_X;                 // 404
constexpr int HOUR_GAP = 4;
constexpr int HOUR_W = (INNER_W - HOUR_GAP * (HOURS - 1)) / HOURS; // 64
constexpr int LIFE_GAP = 8;
constexpr int LIFE_W = (INNER_W - LIFE_GAP * 2) / 3;     // 129
constexpr int COND_W = 220;
constexpr uint32_t PW_STALE_MS = 10000;

ui::TopBar topBar;
lv_obj_t *icoBig, *lblTemp, *lblCond, *lblFeels, *lblHiLo;
lv_obj_t *hourCol[HOURS], *hourLbl[HOURS], *hourIco[HOURS], *hourTemp[HOURS];
int hourIconKey[HOURS];
int bigIconKey = -1;
lv_obj_t *lifeCard, *lifeVal[3];

lv_obj_t *plainBox(lv_obj_t *parent, int w, int h, lv_flex_flow_t flow) {
    lv_obj_t *o = ui::box(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_flex_flow(o, flow);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void cardStyle(lv_obj_t *c) {
    lv_obj_set_style_pad_hor(c, PAD_X, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
}

void hourSlot(lv_obj_t *row, int i) {
    lv_obj_t *col = plainBox(row, HOUR_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(col, 6, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    if (i == 0) {
        lv_obj_set_style_radius(col, 10, 0);
        lv_obj_set_style_bg_color(col, lv_color_hex(0x1F2731), 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
    }
    hourCol[i] = col;
    hourLbl[i] = ui::label(col, &lv_font_montserrat_12, i == 0 ? ui::TEXT : ui::MUTED, i == 0 ? "Now" : "--");
    hourIco[i] = iconCreate(col, Icon::Cloud, 30, ui::C_WEATHER, 1.8f);
    hourIconKey[i] = (int)Icon::Cloud;
    hourTemp[i] = ui::label(col, &lv_font_montserrat_16, ui::TEXT, "--");
    if (i == 0) ui::animTextPulse(hourLbl[i], 3000);
}

lv_obj_t *lifeSlot(lv_obj_t *row, const char *head, lv_color_t color, const char *sub, lv_obj_t **val) {
    lv_obj_t *col = plainBox(row, LIFE_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
    ui::label(col, &lv_font_montserrat_12, color, head);
    *val = ui::label(col, &lv_font_montserrat_20, ui::TEXT, "--");
    ui::label(col, &lv_font_montserrat_12, ui::MUTED, sub);
    return col;
}

} // namespace

lv_obj_t *weatherCreate() {
    lv_obj_t *scr = ui::screenCreate();
    topBar = ui::topBarCreate(scr);

    lv_obj_t *cap = ui::caption(scr, ui::MUTED, WEATHER_NAME);
    lv_obj_set_pos(cap, 24, 54);

    // Current conditions row
    lv_obj_t *row = ui::box(scr);
    lv_obj_set_pos(row, 24, 72);
    lv_obj_set_size(row, 432, 78);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    icoBig = iconCreate(row, Icon::Cloud, 72, ui::C_WEATHER, 1.6f);
    lblTemp = ui::label(row, &lv_font_montserrat_48, ui::TEXT, "--");

    lv_obj_t *col = plainBox(row, COND_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 3, 0);
    lblCond = ui::label(col, &lv_font_montserrat_20, ui::TEXT, "--");
    lv_obj_set_width(lblCond, COND_W);
    lv_label_set_long_mode(lblCond, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    lblFeels = ui::label(col, &lv_font_montserrat_14, ui::MUTED, "");
    lblHiLo = ui::label(col, &lv_font_montserrat_14, ui::MUTED, "");

    // Next 10 hours (now + 2 h steps)
    lv_obj_t *fc = ui::card(scr, 24, 172, 432, LV_SIZE_CONTENT);
    cardStyle(fc);
    ui::caption(fc, ui::MUTED, "NEXT 10 HOURS");
    lv_obj_t *hrow = plainBox(fc, INNER_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hrow, HOUR_GAP, 0);
    for (int i = 0; i < HOURS; i++) hourSlot(hrow, i);

    // Lifetime
    lifeCard = ui::card(scr, 24, 336, 432, LV_SIZE_CONTENT);
    cardStyle(lifeCard);
    ui::caption(lifeCard, ui::MUTED, "LIFETIME");
    lv_obj_t *lrow = plainBox(lifeCard, INNER_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(lrow, LIFE_GAP, 0);
    lifeSlot(lrow, "Solar", ui::C_SOLAR, "kWh generated", &lifeVal[0]);
    lifeSlot(lrow, "Home", ui::C_HOME, "kWh used", &lifeVal[1]);
    lifeSlot(lrow, "Grid", ui::C_GRID, "kWh exported", &lifeVal[2]);

    ui::pageDots(scr, 3);
    return scr;
}

void weatherRefresh() {
    ui::topBarUpdate(topBar);

    WeatherData w = weatherData();
    char buf[64];
    const bool wxOk = w.valid && millis() - w.updatedMs <= WEATHER_STALE_MS;

    if (wxOk) {
        snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(w.tempC));
        ui::setText(lblTemp, buf);
        ui::setText(lblCond, weatherText(w.code));
        snprintf(buf, sizeof(buf), "Feels %d\xC2\xB0 / Wind %d km/h", (int)lroundf(w.feelsC), (int)lroundf(w.windKmh));
        ui::setText(lblFeels, buf);
        int n = snprintf(buf, sizeof(buf), "H %d\xC2\xB0 / L %d\xC2\xB0", (int)lroundf(w.hiC), (int)lroundf(w.loC));
        if (w.rainPct >= 0 && n > 0 && n < (int)sizeof(buf))
            snprintf(buf + n, sizeof(buf) - n, " / Rain %d%%", w.rainPct);
        ui::setText(lblHiLo, buf);

        Icon ic = weatherIcon(w.code, w.isDay);
        if ((int)ic != bigIconKey) {
            iconSet(icoBig, ic, ui::C_WEATHER, 1.6f);
            iconAnimate(icoBig, ic);
            bigIconKey = (int)ic;
        }
    } else {
        ui::setText(lblTemp, "--");
        ui::setText(lblCond, "--");
        ui::setText(lblFeels, "Feels -- / Wind --");
        ui::setText(lblHiLo, "H -- / L --");
    }

    for (int i = 0; i < HOURS; i++) {
        const WeatherHour &h = w.hours[i];
        bool ok = wxOk && h.hour >= 0 && h.code >= 0;
        if (i > 0) {
            if (ok) snprintf(buf, sizeof(buf), "%02d:00", h.hour);
            else snprintf(buf, sizeof(buf), "--");
            ui::setText(hourLbl[i], buf);
        }
        if (ok) {
            snprintf(buf, sizeof(buf), "%d\xC2\xB0", (int)lroundf(h.tempC));
            ui::setText(hourTemp[i], buf);
            Icon ic = weatherIcon(h.code, h.isDay);
            if ((int)ic != hourIconKey[i]) {
                iconSet(hourIco[i], ic, ui::C_WEATHER, 1.8f);
                hourIconKey[i] = (int)ic;
            }
        } else {
            ui::setText(hourTemp[i], "--");
        }
    }

    PwData d = powerwallData();
    const bool pwOk = d.valid && millis() - d.updatedMs <= PW_STALE_MS;
    const double wh[3] = {d.solarExportedWh, d.loadImportedWh, d.siteExportedWh};
    for (int i = 0; i < 3; i++) {
        if (pwOk) ui::formatKWh(buf, sizeof(buf), wh[i] / 1000.0);
        else snprintf(buf, sizeof(buf), "--");
        ui::setText(lifeVal[i], buf);
    }
}

void weatherOnShow() {
    for (int i = 0; i < HOURS; i++) ui::animFadeIn(hourCol[i], 200 + 100 * i);
    ui::animFadeIn(lifeCard, 800);
}
