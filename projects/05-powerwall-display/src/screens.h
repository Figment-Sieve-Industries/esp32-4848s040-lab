#pragma once
#include <lvgl.h>

// Each screen: create() builds it once at boot and returns the LVGL screen object;
// refresh() updates it from the latest data (called every 500 ms while it is the active
// screen, and once right before it is shown); onShow() replays entry animations.

lv_obj_t *houseCreate();   // power flow (design N, design O battery pill)
void houseRefresh();

lv_obj_t *todayCreate();   // today's energy
void todayRefresh();
void todayOnShow();

lv_obj_t *healthCreate();  // battery & grid
void healthRefresh();
void healthOnShow();

lv_obj_t *weatherCreate(); // weather & lifetime
void weatherRefresh();
void weatherOnShow();

lv_obj_t *moonCreate();     // moon: real photo shaded to the phase, rise/set, next full/new
void moonRefresh();
void moonOnShow();

lv_obj_t *auroraCreate();   // aurora (UK): AuroraWatch level, 24 h activity, Kp, visibility hint
void auroraRefresh();
void auroraOnShow();

lv_obj_t *sunCreate();      // sun: SOHO photo, flare class/odds, R/S/G scales, solar wind, sunspots
void sunRefresh();
void sunOnShow();

lv_obj_t *ensoCreate();     // El Nino: CPC alert, weekly Nino 3.4, ONI/RONI, monthly history, odds
void ensoRefresh();
void ensoOnShow();

lv_obj_t *settingsCreate(); // settings: brightness, SD card status
void settingsRefresh();
void settingsOnShow();

lv_obj_t *aboutCreate();    // about: maker, version, all data/image credits
void aboutRefresh();
