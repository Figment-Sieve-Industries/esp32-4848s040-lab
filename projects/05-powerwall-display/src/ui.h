#pragma once
#include <lvgl.h>

// Builds the power-flow screen on the active LVGL screen.
void uiCreate();

// Pulls the latest Powerwall/network state and refreshes labels (call from an LVGL timer).
void uiRefresh();
