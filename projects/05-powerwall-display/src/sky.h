#pragma once
// Sky data for the moon and aurora screens, fetched by the network task (core 0):
// - AuroraWatch UK (Lancaster University): alert level + last 24 h of hourly activity, plain HTTP
// - NOAA SWPC planetary Kp (observed + 3-day forecast), HTTPS
// - MET Norway: moonrise / moonset for the configured location, HTTPS, once per local day
// Aurora and Kp every 30 min. Thread-safe copies; all times are UTC time_t.
// The moon's phase, illumination and next full/new moon are computed on-device (moon_calc.h).
#include <stdint.h>
#include <time.h>

enum class AuroraLevel : int8_t { Unknown = -1, Green = 0, Yellow = 1, Amber = 2, Red = 3 };

// AuroraWatch UK's official names and colours (their API terms: don't change them)
const char *auroraLevelName(AuroraLevel l);  // "No significant activity", "Minor geomagnetic activity",
                                             // "Amber alert: possible aurora", "Red alert: aurora likely", "--"
uint32_t auroraLevelColor(AuroraLevel l);    // 0x33FF33, 0xFFFF00, 0xFF9900, 0xFF0000; 0x8B98A5 unknown

struct AuroraData {
    bool valid = false;
    AuroraLevel level = AuroraLevel::Unknown;  // current status
    float thresholdNt[4] = {0, 50, 100, 200};  // lower thresholds for green..red (from the API)
    int count = 0;                              // hourly entries, oldest first (up to 24)
    float activityNt[24] = {};
    AuroraLevel activityLevel[24] = {};
    time_t activityHour[24] = {};               // start of each hour
    time_t updatedAt = 0;                       // the API's own "updated" time
    uint32_t fetchedMs = 0;                     // millis() of the last successful fetch
};

constexpr int KP_SLOTS = 24; // 3 days x 8 three-hour slots
enum class KpKind : uint8_t { None = 0, Observed, Estimated, Predicted };

struct KpData {
    bool valid = false;
    time_t day0 = 0;          // 00:00 UTC today; slot i covers day0 + i * 3 h
    float kp[KP_SLOTS] = {};  // valid only where kind != None
    KpKind kind[KP_SLOTS] = {};
    int nowSlot = -1;         // slot containing the current time
    float nowKp = -1;         // latest observed/estimated value at or before now (-1 unknown)
    uint32_t fetchedMs = 0;
};

struct MoonTimes {
    bool valid = false;
    int year = 0, yday = -1;     // local date these belong to (tm_year, tm_yday)
    time_t rise = 0, set = 0;    // 0 = no such event on that date
    float riseAzDeg = -1, setAzDeg = -1;
    uint32_t fetchedMs = 0;
};

// SD cache record versions (cache.h). Bump when a field's meaning or units change; a size change is caught anyway.
constexpr uint16_t AURORA_CACHE_VERSION = 1;
constexpr uint16_t KP_CACHE_VERSION = 1;
constexpr uint16_t MOON_CACHE_VERSION = 1;

AuroraData auroraData();
KpData kpData();
MoonTimes moonTimes();

// Data older than this is shown as stale ("--" / dimmed).
constexpr uint32_t SKY_STALE_MS = 2UL * 60UL * 60UL * 1000UL; // 2 h (fetched every 30 min)

// ---- Fetchers, called only by the network task in powerwall.cpp ----
// skyFetchAurora() is plain HTTP; the other two use TLS (one session at a time on that task).
bool skyFetchAurora();
bool skyFetchKp();
bool skyFetchMoon();
bool skyMoonNeeded(); // no moonrise/moonset for today's local date yet
// Restore the last saved record from the SD cache; returns its fetch time (UTC), 0 = nothing restored.
time_t skyRestoreAurora();
time_t skyRestoreKp();
time_t skyRestoreMoon();
