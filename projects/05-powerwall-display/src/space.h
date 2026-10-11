#pragma once
// Space weather for the Sun screen (docs/11-space-weather-and-climate-data.md), fetched by the
// network task in powerwall.cpp (core 0). All HTTPS to services.swpc.noaa.gov (Amazon Root CA 1)
// and soho.nascom.nasa.gov (ISRG Root X1). Getters return thread-safe copies.
#include <stdint.h>
#include <time.h>

struct SunData {
    bool valid = false;
    // NOAA scales now (0-5 each), from /products/noaa-scales.json key "0"; -1 = unknown
    int8_t scaleR = -1, scaleS = -1, scaleG = -1;
    int8_t scaleGNext = -1;        // forecast G scale for tomorrow (key "1"), -1 = unknown
    // X-ray flares, from /json/goes/primary/xray-flares-latest.json
    char flareNow[8] = "";         // current X-ray class, e.g. "B7.0"
    char flareMax[8] = "";         // max class of the latest flare, e.g. "C1.1"
    time_t flareMaxAt = 0;         // UTC time of that maximum
    // Flare probabilities for today (%), from /json/solar_probabilities.json row 0; -1 = unknown
    int8_t probC = -1, probM = -1, probX = -1;
    // Solar wind propagated to Earth, last hour, from /products/geospace/propagated-solar-wind-1-hour.json
    float windKms = -1, densityCc = -1, bzNt = 0; // latest values (windKms < 0 = unknown)
    bool bzValid = false;
    static constexpr int WIND_POINTS = 60;
    int windCount = 0;                       // points, oldest first
    int16_t windSpeed[WIND_POINTS] = {};     // km/s
    int8_t windBz[WIND_POINTS] = {};         // nT, rounded and clamped to +-127
    // Daily indices, from /text/daily-solar-indices.txt (last row); -1 = unknown
    int sunspots = -1;
    int f107 = -1;                           // 10.7 cm radio flux (sfu)
    time_t updatedAt = 0;                    // UTC time of the newest data (flare time_tag)
    uint32_t fetchedMs = 0;                  // millis() of the last successful fetch
};

SunData sunData();

// Sun photo (SOHO HMI intensitygram, 512x512 JPEG) decoded, downscaled and tinted into a
// SUN_IMG_PX square RGB565 buffer in PSRAM, with black outside the solar disc. Double-buffered:
// a new image goes to the back buffer and is then swapped in, so the pointer the screen holds
// stays valid until the following fetch (an hour later).
constexpr int SUN_IMG_PX = 200;
// Front buffer (nullptr until the first image). version changes on every new image.
const uint16_t *sunImage(uint32_t &version, time_t &takenAt);

// Data older than this is shown as stale.
constexpr uint32_t SPACE_STALE_MS = 2UL * 60UL * 60UL * 1000UL; // 2 h (fetched every 30 min)

// ---- Fetchers, called only by the network task in powerwall.cpp (one TLS session at a time) ----
bool spaceFetchSwpc();    // the four SWPC JSON files + daily indices, one keep-alive connection; ~20 KB
bool spaceFetchSunImage(); // SOHO JPEG (~70-100 KB into PSRAM) -> decode -> swap
