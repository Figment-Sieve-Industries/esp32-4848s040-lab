#pragma once
#include <Arduino.h>

// Powerwall 3 usable capacity, used for time-to-full / time-to-empty estimates
// (the basic LAN API doesn't report it).
constexpr float PW_CAPACITY_KWH = 13.5f;

// Snapshot of the Powerwall state, written by the network task and read by the UI.
// Sign conventions (Tesla): site > 0 importing from grid, < 0 exporting;
// battery > 0 discharging, < 0 charging; solar/load > 0.
struct PwData {
    float solarW = 0, loadW = 0, siteW = 0, batteryW = 0;
    float soc = 0;              // battery %, as reported by /api/system_status/soe
    bool gridConnected = true;
    bool gridServicesActive = false;
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
    int code = -1;              // WMO weather code
    bool isDay = true;
    WeatherHour hours[6];       // now, +2 h ... +10 h
    uint32_t updatedMs = 0;
};

struct NetStatus {
    bool wifiUp = false;
    char ssid[33] = "";
    int rssi = 0;
};

// Starts Wi-Fi (WiFiMulti over WIFI_NETWORKS) and the polling task on core 0.
// The same task fetches Open-Meteo weather every 15 min (one TLS session at a time).
void powerwallStart(uint32_t pollMs = 2000);

// Thread-safe copies of the latest state.
PwData powerwallData();
NetStatus networkStatus();
WeatherData weatherData();
