#pragma once
// Moon phase and sun altitude, computed on-device (no network). Pure math, no Arduino/LVGL.
// Algorithms: Meeus, "Astronomical Algorithms" 2nd ed. - ch. 49 (phase times), ch. 47/48
// (position, illuminated fraction), ch. 25 (low-precision sun).
#include <time.h>

struct MoonPhase {
    double ageDays = 0;    // days since the last new moon
    double illum = 0;      // illuminated fraction 0..1 (Meeus ch. 48 phase angle)
    bool waxing = true;
    double cycle = 0;      // 0..1 through the synodic month (0 new, 0.5 full)
    int octant = 0;        // 0 new, 1 waxing crescent, 2 first quarter, 3 waxing gibbous,
                           // 4 full, 5 waning gibbous, 6 last quarter, 7 waning crescent
                           // (principal phases: within +-1 day of the exact instant)
    const char *name = ""; // "New moon", "Waxing crescent", "First quarter", ... (ASCII)
    time_t lastNew = 0, nextNew = 0, nextFull = 0; // UTC, accurate to about a minute
    const char *nextFullName = ""; // traditional name of nextFull: "Hunter's Moon", "Harvest Moon",
                                   // "Blue Moon" (2nd full moon in a calendar month), else the month's
                                   // name ("Wolf Moon" ... "Cold Moon"), by UK local month
};

// Phase information at the given UTC time.
MoonPhase moonPhase(time_t utc);

// Sun altitude in degrees (refraction ignored) at the given place; < -12 = nautical dark.
double sunAltitudeDeg(time_t utc, double latDeg, double lonDeg);
