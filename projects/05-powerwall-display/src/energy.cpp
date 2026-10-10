// Today's energy from lifetime counters, with the midnight baseline in RTC memory.
// RTC_NOINIT_ATTR data survives software resets, crashes and OTA restarts but not a power cut,
// and is never written to flash (no wear).
#include "energy.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <stddef.h>
#include <string.h>

namespace {

constexpr uint32_t MAGIC = 0x50574531; // "PWE1"

struct Baseline {
    uint32_t magic;
    int yday, year;          // local date the baseline belongs to
    time_t since;
    bool partial;
    double solar, load, siteExp, siteImp, battExp, battImp; // counters (Wh) at baseline
    int hour;                // hour the current hourly bin started
    double hourSolarStart;   // solar counter at the start of that hour
    float hourly[24];        // completed hours (kWh)
    uint32_t check;
};

RTC_NOINIT_ATTR Baseline rtc;

uint32_t checksum(const Baseline &b) {
    const uint8_t *p = (const uint8_t *)&b;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(Baseline, check); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

bool rtcValid() { return rtc.magic == MAGIC && rtc.check == checksum(rtc); }
void rtcSeal() { rtc.check = checksum(rtc); }

TodayStats stats;

void startDay(const PwData &d, const struct tm &tm, time_t now, bool partial) {
    memset(&rtc, 0, sizeof(rtc));
    rtc.magic = MAGIC;
    rtc.yday = tm.tm_yday;
    rtc.year = tm.tm_year;
    rtc.since = now;
    rtc.partial = partial;
    rtc.solar = d.solarExportedWh;
    rtc.load = d.loadImportedWh;
    rtc.siteExp = d.siteExportedWh;
    rtc.siteImp = d.siteImportedWh;
    rtc.battExp = d.battExportedWh;
    rtc.battImp = d.battImportedWh;
    rtc.hour = tm.tm_hour;
    rtc.hourSolarStart = d.solarExportedWh;
    rtcSeal();
}

} // namespace

void energyUpdate(const PwData &d) {
    if (!d.valid || d.solarExportedWh <= 0) return;
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 124) return; // clock not synced yet (before 2024)

    if (!rtcValid() || rtc.year != tm.tm_year || rtc.yday != tm.tm_yday) {
        bool afterMidnight = rtcValid(); // a valid baseline from yesterday = clean rollover
        // Partial unless this is a clean midnight rollover (or we happen to start right at midnight)
        bool partial = !(afterMidnight || (tm.tm_hour == 0 && tm.tm_min < 5));
        startDay(d, tm, now, partial);
        Serial.printf("Energy: new baseline (%s)\n", partial ? "partial day" : "midnight");
    }

    // Close finished hour bins
    if (tm.tm_hour != rtc.hour) {
        if (rtc.hour >= 0 && rtc.hour < 24) rtc.hourly[rtc.hour] = (d.solarExportedWh - rtc.hourSolarStart) / 1000.0;
        rtc.hour = tm.tm_hour;
        rtc.hourSolarStart = d.solarExportedWh;
        rtcSeal();
    }

    TodayStats s;
    s.valid = true;
    s.partial = rtc.partial;
    s.since = rtc.since;
    s.solarKWh = (d.solarExportedWh - rtc.solar) / 1000.0;
    s.homeKWh = (d.loadImportedWh - rtc.load) / 1000.0;
    s.exportKWh = (d.siteExportedWh - rtc.siteExp) / 1000.0;
    s.importKWh = (d.siteImportedWh - rtc.siteImp) / 1000.0;
    s.battInKWh = (d.battImportedWh - rtc.battImp) / 1000.0;
    s.battOutKWh = (d.battExportedWh - rtc.battExp) / 1000.0;
    if (s.homeKWh >= 0.05) {
        double pct = 100.0 * (1.0 - s.importKWh / s.homeKWh);
        s.selfPoweredPct = (float)(pct < 0 ? 0 : pct > 100 ? 100 : pct);
    }
    memcpy(s.hourlySolarKWh, rtc.hourly, sizeof(s.hourlySolarKWh));
    s.currentHour = tm.tm_hour;
    s.hourlySolarKWh[tm.tm_hour] = (d.solarExportedWh - rtc.hourSolarStart) / 1000.0; // in progress
    stats = s;
}

TodayStats energyToday() { return stats; }
