#pragma once
#include <Arduino.h>

// Snapshot of the Powerwall state, written by the network task and read by the UI.
// Sign conventions (Tesla): site > 0 importing from grid, < 0 exporting;
// battery > 0 discharging, < 0 charging; solar/load > 0.
struct PwData {
    float solarW = 0, loadW = 0, siteW = 0, batteryW = 0;
    float soc = 0;           // battery %, as reported by /api/system_status/soe
    bool gridConnected = true;
    bool valid = false;      // at least one full successful poll
    uint32_t updatedMs = 0;  // millis() of the last successful poll
    char error[48] = "";     // last error, empty when OK
};

struct NetStatus {
    bool wifiUp = false;
    char ssid[33] = "";
    int rssi = 0;
};

// Starts Wi-Fi (WiFiMulti over WIFI_NETWORKS) and the polling task on core 0.
void powerwallStart(uint32_t pollMs = 2000);

// Thread-safe copies of the latest state.
PwData powerwallData();
NetStatus networkStatus();
