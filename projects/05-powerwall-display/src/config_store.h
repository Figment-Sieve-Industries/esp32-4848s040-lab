#pragma once
// User configuration for project 05: Wi-Fi, Powerwall gateway, location. Replaces the compile-time secrets.h for
// end users (secrets.h stays as an optional developer default).
//
// Storage (owner decision 2026-10-11): NVS namespace "pwdisp", key "cfg" (one blob), written ONLY when the user
// saves setup (setup portal "Test and save", or the one-time SD import below) and never when nothing changed.
// No periodic or automatic NVS writes. WiFi.persistent(false) stays: the Wi-Fi stack itself still writes nothing.
//
// Load order at boot (cfg::begin):
//   1. NVS blob, if present and valid (magic, version, size, CRC).
//   2. Else /powerwall/config.json on the SD card (import once: parse, validate, save to NVS, rename the file to
//      config.imported.json so a typo'd file isn't re-imported on every boot). Needs the SD task's first mount;
//      cfg::begin waits up to 3 s for it, like the cache restore.
//   3. Else compile-time defaults from secrets.h if it exists (__has_include) - developer builds keep working
//      unchanged and are NOT written to NVS.
//   4. Else unconfigured: main runs the setup portal instead of the dashboard.
#include <stddef.h>
#include <stdint.h>

namespace cfg {

constexpr int MAX_WIFI = 3;

struct WifiNet {
    char ssid[33];   // 32 + NUL (802.11 max)
    char pass[65];   // 64 + NUL (WPA2 PSK hex max)
};

struct Config {
    WifiNet wifi[MAX_WIFI];
    uint8_t wifiCount;          // 1..MAX_WIFI when configured
    char pwHost[64];            // gateway IP or host name, e.g. "192.168.1.50"
    char pwPassword[33];        // gateway "customer" login password (login: username "customer", empty email)
    float lat, lon;             // weather / sun / moon location
    char placeName[24];         // shown on the weather screen (upper-cased by the UI)
    char tz[64];                // POSIX TZ string, e.g. "GMT0BST,M3.5.0/1,M10.5.0"
    bool auroraUk;              // AuroraWatch UK job + UK wording on the aurora screen
};

enum class Source : uint8_t { None, Nvs, SdImport, Secrets };

// Boot: load per the order above. Returns true when a usable config exists (wifiCount >= 1, pwHost set).
bool begin();
bool configured();
Source source();
const Config &get(); // valid after begin(); all strings NUL-terminated

// Validate (lengths, lat/lon range, tz non-empty, at least one SSID) and save to NVS. Writes only if different from
// what is stored. Returns false with a short reason in `why` (ASCII, <= 64 chars) on a validation error.
bool save(const Config &c, char *why, size_t whyLen);

// Forget the stored config (NVS erase of the key). Used by "Re-run setup" (then restart into the portal).
bool clear();

// JSON helpers shared by the SD import and the portal (ArduinoJson, PSRAM documents). Passwords are never
// included by toJson unless includeSecrets (the portal's GET never includes them; it shows "set" / "not set").
bool fromJson(const char *json, size_t len, Config &out, char *why, size_t whyLen);
size_t toJson(const Config &c, bool includeSecrets, char *buf, size_t len);

} // namespace cfg
