#pragma once
#include <Arduino.h>

// Powerwall usable capacity for time-to-full / time-to-empty estimates (the basic LAN API
// doesn't report it). 13.5 kWh per Powerwall 3; for expansion packs add -DPW_CAPACITY_KWH=27.0f
// (etc.) to build_flags in platformio.ini.
#ifndef PW_CAPACITY_KWH
#define PW_CAPACITY_KWH 13.5f
#endif

// Battery % as the Tesla app shows it: the app hides a 5% reserve, (raw - 5) / 0.95
// (same scaling as pypowerwall). Add -DPW_SOC_APP_SCALE=0 to build_flags for the raw value.
#ifndef PW_SOC_APP_SCALE
#define PW_SOC_APP_SCALE 1
#endif

// Gateway data older than this is treated as stale by the UI and by energy.cpp ("--" / dimmed).
// Must tolerate one background job, which pauses gateway polling on the single network task, plus
// the poll interval and the gateway reconnect afterwards.
constexpr uint32_t PW_STALE_MS = 20000;

// Time budget of one background job (net::Options::budgetMs in every fetcher): once spent, the job's
// remaining requests are skipped (retried next period) and each phase of a request in flight is
// clamped to what is left (at least 1 s). A job can still overrun by about one request's phase floors.
constexpr uint32_t PW_JOB_BUDGET_MS = 12000;
static_assert(PW_JOB_BUDGET_MS + 2000 /*poll*/ + 3000 /*overrun + reconnect*/ <= PW_STALE_MS,
              "a background job must not make the live data stale");

// 1 (default): keep the gateway's TLS connection open across polls (HTTP/1.1 keep-alive for /aggregates,
// bodies de-chunked by net::readBody). Soak 2026-10-11, 10 min: 22 TLS connects instead of 275, gateway
// handshake refusals 1 instead of 19, main screen 59 fps instead of ~40; costs ~10 KB of internal RAM
// while the session is held (min free 105 KB). 0: /aggregates is HTTP/1.0, one handshake per poll.
#ifndef PW_GW_KEEPALIVE
#define PW_GW_KEEPALIVE 1
#endif

// Weather older than this is treated as stale by the UI ("--" / dimmed).
constexpr uint32_t WEATHER_STALE_MS = 60UL * 60UL * 1000UL;

// Snapshot of the Powerwall state, written by the network task and read by the UI.
// Sign conventions (Tesla): site > 0 importing from grid, < 0 exporting;
// battery > 0 discharging, < 0 charging; solar/load > 0.
struct PwData {
    float solarW = 0, loadW = 0, siteW = 0, batteryW = 0;
    float soc = 0;              // battery % (app-scaled unless PW_SOC_APP_SCALE is 0)
    bool socValid = false;      // at least one successful /soe read
    bool gridConnected = true;
    bool gridServicesActive = false;
    float siteVar = 0, siteVA = 0;  // grid reactive (var) / apparent (VA) power
    float loadVar = 0, loadVA = 0;  // home reactive / apparent power
    float voltageV = 0;         // site average voltage
    float frequencyHz = 0;      // battery-side frequency (the site meter reports 0)
    // Lifetime energy counters in Wh (never decrease)
    double solarExportedWh = 0; // solar generated
    double loadImportedWh = 0;  // home used
    double siteExportedWh = 0, siteImportedWh = 0;
    double battExportedWh = 0, battImportedWh = 0; // discharged / charged
    bool valid = false;         // at least one full successful poll
    uint32_t updatedMs = 0;     // millis() of the last successful poll
    char error[48] = "";        // last error, empty when OK
};

struct WeatherHour {
    int hour = -1;              // local hour of day
    int code = -1;
    bool isDay = true;
    float tempC = 0;
};

struct WeatherData {
    bool valid = false;
    float tempC = 0, feelsC = 0, windKmh = 0, hiC = 0, loC = 0;
    int rainPct = -1;           // today's max precipitation probability
    int cloudPct = -1;          // current total cloud cover % (-1 unknown)
    int code = -1;              // WMO weather code
    bool isDay = true;
    int sunriseMin = -1;        // today's sunrise, local minutes since midnight (-1 unknown)
    int sunsetMin = -1;         // today's sunset, same
    WeatherHour hours[6];       // now, +2 h ... +10 h
    uint32_t updatedMs = 0;
};
// SD cache record version (cache.h). Bump when a field's meaning or units change; a size change is caught anyway.
constexpr uint16_t WEATHER_CACHE_VERSION = 2;

struct NetStatus {
    bool wifiUp = false;
    char ssid[33] = "";
    int rssi = 0;
};

// Starts Wi-Fi (WiFiMulti over WIFI_NETWORKS) and the network task on core 0: gateway polling plus
// the background jobs (weather, sky, space, ENSO), one TLS session at a time.
void powerwallStart(uint32_t pollMs = 2000);

// Thread-safe copies of the latest state.
PwData powerwallData();
NetStatus networkStatus();
WeatherData weatherData();

// Diagnostics: minimum free stack (bytes) seen so far in the network task.
uint32_t netTaskStackFree();
