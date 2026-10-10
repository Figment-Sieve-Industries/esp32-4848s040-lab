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
