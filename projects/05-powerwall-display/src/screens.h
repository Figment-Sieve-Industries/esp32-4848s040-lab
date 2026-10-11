#pragma once
#include <lvgl.h>

// Each screen: create() builds it once at boot and returns the LVGL screen object;
// refresh() updates it from the latest data (called every 500 ms while it is the active
// screen, and once right before it is shown); onShow() replays entry animations.

lv_obj_t *houseCreate();   // B: power flow
void houseRefresh();

lv_obj_t *todayCreate();   // G: today's energy
void todayRefresh();
void todayOnShow();

lv_obj_t *healthCreate();  // H: battery & grid
void healthRefresh();
void healthOnShow();

lv_obj_t *weatherCreate(); // I: weather & lifetime
void weatherRefresh();
void weatherOnShow();

lv_obj_t *moonCreate();     // moon: real photo shaded to the phase, rise/set, next full/new
void moonRefresh();
void moonOnShow();

lv_obj_t *auroraCreate();   // aurora (UK): AuroraWatch level, 24 h activity, Kp, visibility hint
void auroraRefresh();
void auroraOnShow();

lv_obj_t *settingsCreate(); // settings: brightness, SD card status
void settingsRefresh();
void settingsOnShow();

lv_obj_t *aboutCreate();    // about: maker, version, all data/image credits
void aboutRefresh();
