// Screen I: weather + lifetime energy. Geometry follows Weather.dc.html (480x480).
#include "screens.h"
#include "shared_state.h"
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

lv_obj_t *s_icoBig, *s_lblTemp, *s_lblCond, *s_lblFeels, *s_lblHiLo;
lv_obj_t *s_hourCol[HOURS], *s_hourLbl[HOURS], *s_hourIco[HOURS], *s_hourTemp[HOURS];
int s_hourIconKey[HOURS];
int s_bigIconKey = -1;
lv_obj_t *s_lifeCard, *s_lifeVal[3];
lv_obj_t *s_sunUp, *s_sunDown; // sunrise / sunset times, right of the place caption
lv_obj_t *s_place; // location caption (ui::placeName: privacy mode shows a placeholder)

// Fixed-size flex container, children aligned to the start
lv_obj_t *plainBox(lv_obj_t *parent, int w, int h, lv_flex_flow_t flow) {
    lv_obj_t *o = ui::flex(parent, flow, 0, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_size(o, w, h);
    return o;
}

void cardStyle(lv_obj_t *c) {
    lv_obj_set_style_pad_hor(c, PAD_X, 0);
    lv_obj_set_style_pad_ver(c, 12, 0);
    lv_obj_set_style_pad_row(c, 10, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
}

void hourSlot(lv_obj_t *row, int i) {
    lv_obj_t *col = plainBox(row, HOUR_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(col, 6, 0);
    lv_obj_set_style_pad_row(col, 6, 0);
    if (i == 0) {
        lv_obj_set_style_radius(col, 10, 0);
        lv_obj_set_style_bg_color(col, ui::CARD_HI, 0);
        lv_obj_set_style_bg_opa(col, LV_OPA_COVER, 0);
    }
    s_hourCol[i] = col;
    s_hourLbl[i] = ui::label(col, &lv_font_montserrat_12, i == 0 ? ui::TEXT : ui::MUTED, i == 0 ? "Now" : "--");
    s_hourIco[i] = iconCreate(col, Icon::Cloud, 30, ui::C_WEATHER, 1.8f);
    s_hourIconKey[i] = (int)Icon::Cloud;
    s_hourTemp[i] = ui::label(col, &lv_font_montserrat_16, ui::TEXT, "--");
    if (i == 0) ui::animTextPulse(s_hourLbl[i], 3000);
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
    lv_obj_t *scr = ui::screenCreate(ui::SCR_WEATHER);

    s_place = ui::caption(scr, ui::MUTED, ui::placeName());
    lv_obj_set_pos(s_place, 24, 54);

    // Sunrise / sunset, right-aligned on the place caption line
    lv_obj_t *sun = ui::flex(scr, LV_FLEX_FLOW_ROW, 4, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(sun, LV_SIZE_CONTENT, 16);
    lv_obj_align(sun, LV_ALIGN_TOP_RIGHT, -24, 52);
    iconCreate(sun, Icon::Sun, 14, ui::C_SOLAR, 1.4f);
    iconCreate(sun, Icon::ArrowUp, 12, ui::MUTED, 1.6f);
    s_sunUp = ui::label(sun, &lv_font_montserrat_14, ui::MUTED, "--:--");
    iconCreate(sun, Icon::ArrowDown, 12, ui::MUTED, 1.6f);
    s_sunDown = ui::label(sun, &lv_font_montserrat_14, ui::MUTED, "--:--");

    // Current conditions row
    lv_obj_t *row = ui::flex(scr, LV_FLEX_FLOW_ROW, 16);
    lv_obj_set_pos(row, 24, 72);
    lv_obj_set_size(row, 432, 78);

    s_icoBig = iconCreate(row, Icon::Cloud, 72, ui::C_WEATHER, 1.6f);
    s_lblTemp = ui::label(row, &lv_font_montserrat_48, ui::TEXT, "--");

    lv_obj_t *col = plainBox(row, COND_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 3, 0);
    s_lblCond = ui::label(col, &lv_font_montserrat_20, ui::TEXT, "--");
    lv_obj_set_width(s_lblCond, COND_W);
    lv_label_set_long_mode(s_lblCond, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    s_lblFeels = ui::label(col, &lv_font_montserrat_14, ui::MUTED, "");
    s_lblHiLo = ui::label(col, &lv_font_montserrat_14, ui::MUTED, "");

    // Next 10 hours (now + 2 h steps)
    lv_obj_t *fc = ui::card(scr, 24, 172, 432, LV_SIZE_CONTENT);
    cardStyle(fc);
    ui::caption(fc, ui::MUTED, "NEXT 10 HOURS");
    lv_obj_t *hrow = plainBox(fc, INNER_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hrow, HOUR_GAP, 0);
    for (int i = 0; i < HOURS; i++) hourSlot(hrow, i);

    // Lifetime
    s_lifeCard = ui::card(scr, 24, 336, 432, LV_SIZE_CONTENT);
    cardStyle(s_lifeCard);
    ui::caption(s_lifeCard, ui::MUTED, "LIFETIME");
    lv_obj_t *lrow = plainBox(s_lifeCard, INNER_W, LV_SIZE_CONTENT, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(lrow, LIFE_GAP, 0);
    lifeSlot(lrow, "Solar", ui::C_SOLAR, "kWh generated", &s_lifeVal[0]);
    lifeSlot(lrow, "Home", ui::C_HOME, "kWh used", &s_lifeVal[1]);
    lifeSlot(lrow, "Grid", ui::C_GRID, "kWh exported", &s_lifeVal[2]);

    return scr;
}

void weatherRefresh() {
    ui::setText(s_place, ui::placeName());
    WeatherData w = weatherData();
    char buf[64];
    const bool wxOk = !isStale(w.valid, w.updatedMs, WEATHER_STALE_MS);

    if (wxOk) {
        ui::setTextF(s_lblTemp, "%d\xC2\xB0", (int)lroundf(w.tempC));
        ui::setText(s_lblCond, weatherText(w.code));
        ui::setTextF(s_lblFeels, "Feels %d\xC2\xB0 / Wind %d km/h", (int)lroundf(w.feelsC), (int)lroundf(w.windKmh));
        int n = snprintf(buf, sizeof(buf), "H %d\xC2\xB0 / L %d\xC2\xB0", (int)lroundf(w.hiC), (int)lroundf(w.loC));
        if (w.rainPct >= 0 && n > 0 && n < (int)sizeof(buf))
            snprintf(buf + n, sizeof(buf) - n, " / Rain %d%%", w.rainPct);
        ui::setText(s_lblHiLo, buf);

        Icon ic = weatherIcon(w.code, w.isDay);
        if ((int)ic != s_bigIconKey) {
            iconSet(s_icoBig, ic, ui::C_WEATHER, 1.6f);
            iconAnimate(s_icoBig, ic);
            s_bigIconKey = (int)ic;
        }
    } else {
        ui::setText(s_lblTemp, "--");
        ui::setText(s_lblCond, "--");
        ui::setText(s_lblFeels, "Feels -- / Wind --");
        ui::setText(s_lblHiLo, "H -- / L --");
    }

    if (wxOk && w.sunriseMin >= 0) ui::setTextF(s_sunUp, "%02d:%02d", w.sunriseMin / 60, w.sunriseMin % 60);
    else ui::setText(s_sunUp, "--:--");
    if (wxOk && w.sunsetMin >= 0) ui::setTextF(s_sunDown, "%02d:%02d", w.sunsetMin / 60, w.sunsetMin % 60);
    else ui::setText(s_sunDown, "--:--");

    for (int i = 0; i < HOURS; i++) {
        const WeatherHour &h = w.hours[i];
        bool ok = wxOk && h.hour >= 0 && h.code >= 0;
        if (i > 0) {
            if (ok) ui::setTextF(s_hourLbl[i], "%02d:00", h.hour);
            else ui::setText(s_hourLbl[i], "--");
        }
        if (ok) {
            ui::setTextF(s_hourTemp[i], "%d\xC2\xB0", (int)lroundf(h.tempC));
            Icon ic = weatherIcon(h.code, h.isDay);
            if ((int)ic != s_hourIconKey[i]) {
                iconSet(s_hourIco[i], ic, ui::C_WEATHER, 1.8f);
                s_hourIconKey[i] = (int)ic;
            }
        } else {
            ui::setText(s_hourTemp[i], "--");
        }
    }

    PwData d = powerwallData();
    const bool pwOk = !isStale(d.valid, d.updatedMs, PW_STALE_MS);
    const double wh[3] = {d.solarExportedWh, d.loadImportedWh, d.siteExportedWh};
    for (int i = 0; i < 3; i++) {
        if (pwOk) ui::formatKWh(buf, sizeof(buf), wh[i] / 1000.0, false);
        else snprintf(buf, sizeof(buf), "--");
        ui::setText(s_lifeVal[i], buf);
    }
}

void weatherOnShow() {
    ui::fadeIn(s_hourCol, HOURS, 200, 100);
    ui::fadeIn(&s_lifeCard, 1, 800, 0);
}
