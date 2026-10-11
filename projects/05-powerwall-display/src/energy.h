#pragma once
#include "powerwall.h"
#include <time.h>

// "Today" energy totals, derived from the Powerwall's lifetime counters minus a snapshot
// taken at local midnight. Kept in RTC memory (survives restarts/crashes) with a backup on the SD
// card (survives power cuts) - nothing is ever written to flash.
struct TodayStats {
    bool valid = false;        // have a baseline and current data
    bool partial = false;      // baseline taken after midnight (e.g. after a power cut)
    time_t since = 0;          // when the baseline was taken (local time_t)
    double solarKWh = 0, homeKWh = 0, exportKWh = 0, importKWh = 0;
    double battInKWh = 0, battOutKWh = 0;   // charged / discharged
    float selfPoweredPct = -1;              // 100 * (1 - import/home), -1 if not enough data
    float hourlySolarKWh[24] = {};          // solar generated in each local hour today
    int currentHour = -1;
};

// Call regularly (e.g. every 500 ms) from the UI thread with the latest data.
void energyUpdate(const PwData &d);
TodayStats energyToday();
