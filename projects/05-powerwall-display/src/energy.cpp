// Today's energy from lifetime counters, with the midnight baseline in RTC memory.
// RTC_NOINIT_ATTR data survives software resets, crashes and OTA restarts but not a power cut,
// and is never written to flash (no wear). A copy goes to the SD card (history.cpp) on every new
// baseline and every 5 minutes; after a power cut it is restored from there.
#include "energy.h"
#include "history.h"
#include <Arduino.h>
#include <esp_attr.h>
#include <stddef.h>
#include <string.h>
#include <type_traits>

namespace {

constexpr uint32_t MAGIC = 0x50574532; // "PWE2" (layout changed: lastSeen added)
constexpr time_t CLEAN_GAP_S = 10 * 60;   // last reading within 10 min before midnight = clean rollover

struct Baseline {
    uint32_t magic;
    int yday, year;          // local date the baseline belongs to
    time_t since;
    bool partial;
    double solar, load, siteExp, siteImp, battExp, battImp; // counters (Wh) at baseline
    int hour;                // hour the current hourly bin started
    double hourSolarStart;   // solar counter at the start of that hour
    float hourly[24];        // completed hours (kWh)
    time_t lastSeen;         // time of the last fresh reading folded in
    double lastSolar, lastLoad, lastSiteExp, lastSiteImp, lastBattExp, lastBattImp; // counters at lastSeen
    uint32_t check;
};

// The checksum below hashes raw bytes, including any padding between members. That is only
// deterministic because startDay() memset()s the whole struct before filling it, and later updates
// assign members only (never copy a Baseline built elsewhere). Keep it trivially copyable/standard
// layout so memset and offsetof stay well defined.
// NOLINTNEXTLINE(misc-redundant-expression): two different traits, not a repeated expression
static_assert(std::is_standard_layout<Baseline>::value && std::is_trivially_copyable<Baseline>::value,
              "Baseline is hashed and zeroed as raw bytes");

RTC_NOINIT_ATTR Baseline rtc;

uint32_t checksum(const Baseline &b) {
    const uint8_t *p = (const uint8_t *)&b;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(Baseline, check); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

bool rtcValid() { return rtc.magic == MAGIC && rtc.check == checksum(rtc); }
void rtcSeal() { rtc.check = checksum(rtc); }

// SD backup of the baseline (history.cpp), so a power cut doesn't turn today into a partial day.
constexpr uint32_t BACKUP_EVERY_MS = 5 * 60 * 1000;
constexpr uint32_t RESTORE_WAIT_MS = 20000; // after boot, wait this long for the card before giving up
bool restoreTried = false;
uint32_t lastBackupMs = 0;

// Called with an invalid RTC copy (power cut). Returns false to mean "wait, the card isn't read yet".
bool tryRestore() {
    if (restoreTried) return true;
    if (!historyStatus().firstMountDone && millis() < RESTORE_WAIT_MS) return false;
    restoreTried = true;
    Baseline b;
    if (historyLoadBaseline(&b, sizeof(b)) && b.magic == MAGIC && b.check == checksum(b)) {
        memcpy(&rtc, &b, sizeof(rtc)); // byte copy: assignment needn't copy the hashed padding
        Serial.println("Energy: baseline restored from SD card");
    }
    return true;
}

void backup(bool now) {
    if (!now && millis() - lastBackupMs < BACKUP_EVERY_MS) return;
    lastBackupMs = millis();
    historySaveBaseline(&rtc, sizeof(rtc));
}

TodayStats stats;
uint32_t statsUpdatedMs = 0;  // d.updatedMs of the reading that produced `stats`
int statsYear = -1, statsYday = -1; // local date `stats` belongs to

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

// Lifetime counters only grow, apart from small reading-to-reading wobble (observed: fractions of
// a Wh). A drop of more than RESET_DROP_WH, or a zero, means the gateway reset them.
constexpr double RESET_DROP_WH = 1000.0;
bool countersSane(const PwData &d) {
    if (d.solarExportedWh <= 0 || d.loadImportedWh <= 0) return false;
    if (!rtcValid() || rtc.lastSeen == 0) return true;
    auto ok = [](double now, double before) { return now > before - RESET_DROP_WH; };
    return ok(d.solarExportedWh, rtc.lastSolar) && ok(d.loadImportedWh, rtc.lastLoad) &&
           ok(d.siteExportedWh, rtc.lastSiteExp) && ok(d.siteImportedWh, rtc.lastSiteImp) &&
           ok(d.battExportedWh, rtc.lastBattExp) && ok(d.battImportedWh, rtc.lastBattImp);
}

void remember(const PwData &d, time_t now) {
    rtc.lastSeen = now;
    rtc.lastSolar = d.solarExportedWh;
    rtc.lastLoad = d.loadImportedWh;
    rtc.lastSiteExp = d.siteExportedWh;
    rtc.lastSiteImp = d.siteImportedWh;
    rtc.lastBattExp = d.battExportedWh;
    rtc.lastBattImp = d.battImportedWh;
}

// Local midnight at the start of the day containing `t`
time_t midnightOf(time_t t) {
    struct tm m;
    localtime_r(&t, &m);
    m.tm_hour = m.tm_min = m.tm_sec = 0;
    m.tm_isdst = -1;
    return mktime(&m);
}

} // namespace

void energyUpdate(const PwData &d) {
    // Only fresh data: frozen counters during an outage would put a whole gap into one baseline/bin
    if (!d.valid || millis() - d.updatedMs > PW_STALE_MS) return;
    if (d.solarExportedWh <= 0 || d.loadImportedWh <= 0) return; // never baseline on zero counters
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 124) return; // clock not synced yet (before 2024)
    if (!rtcValid() && !tryRestore()) return; // power cut: give the SD card a moment first

    bool reset = !countersSane(d);
    bool newBaseline = false;
    if (reset || !rtcValid() || rtc.year != tm.tm_year || rtc.yday != tm.tm_yday) {
        // A clean rollover needs a reading from just before this midnight; otherwise the counters at
        // the first reading of the day include an unknown amount of yesterday -> partial day.
        time_t midnight = midnightOf(now);
        bool clean = !reset && rtcValid() && rtc.lastSeen > 0 && rtc.lastSeen <= midnight &&
                     midnight - rtc.lastSeen <= CLEAN_GAP_S;
        if (clean) {
            // Baseline = the last pre-midnight counters, so energy since midnight isn't lost
            PwData base = d;
            base.solarExportedWh = rtc.lastSolar;
            base.loadImportedWh = rtc.lastLoad;
            base.siteExportedWh = rtc.lastSiteExp;
            base.siteImportedWh = rtc.lastSiteImp;
            base.battExportedWh = rtc.lastBattExp;
            base.battImportedWh = rtc.lastBattImp;
            startDay(base, tm, midnight, false);
        } else {
            bool partial = !(tm.tm_hour == 0 && tm.tm_min < 5 && !reset);
            startDay(d, tm, now, partial);
        }
        Serial.printf("Energy: new baseline (%s)%s\n", clean ? "midnight" : "partial day",
                      reset ? " after counter reset" : "");
        newBaseline = true;
    }
    // Close finished hour bins. After a data gap of more than 30 min, the energy generated during
    // the gap can't be placed in the right hours: the bin is closed with only what was seen before
    // the gap (daily totals still include it).
    bool gap = rtc.lastSeen > 0 && now - rtc.lastSeen > 30 * 60;
    if (tm.tm_hour != rtc.hour) {
        double end = gap ? rtc.lastSolar : d.solarExportedWh;
        if (rtc.hour >= 0 && rtc.hour < 24) rtc.hourly[rtc.hour] = (end - rtc.hourSolarStart) / 1000.0;
        rtc.hour = tm.tm_hour;
        rtc.hourSolarStart = d.solarExportedWh;
    } else if (gap) {
        rtc.hourSolarStart += d.solarExportedWh - rtc.lastSolar; // drop the gap from the current bin
    }
    remember(d, now);
    rtcSeal();
    backup(newBaseline);

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
    statsUpdatedMs = d.updatedMs;
    statsYear = tm.tm_year;
    statsYday = tm.tm_yday;
}

// Totals are only returned while they are fresh and still belong to today's local date;
// otherwise an invalid TodayStats (the screen shows "--") rather than old or yesterday's totals.
TodayStats energyToday() {
    if (!stats.valid || millis() - statsUpdatedMs > PW_STALE_MS) return TodayStats();
    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year != statsYear || tm.tm_yday != statsYday) return TodayStats();
    return stats;
}
