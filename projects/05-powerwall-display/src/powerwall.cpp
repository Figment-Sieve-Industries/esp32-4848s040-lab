// Powerwall 3 local LAN API client (customer-password "basic" access, firmware 26.x) + Open-Meteo.
//   POST /api/login/Basic               -> {"token": ...}   (sent back as Authorization: Bearer)
//   GET  /api/meters/aggregates         -> site / battery / load / solar meters
//   GET  /api/system_status/soe         -> {"percentage": ..}
//   GET  /api/system_status/grid_status -> {"grid_status": "SystemGridConnected" | ..., "grid_services_active": ..}
// 401/403 means the token expired or is missing, so log in again (with back-off on failure).
//
// One network task on core 0 (LVGL runs on core 1 and never touches the network). Each pass polls
// the gateway over its long-lived HTTPS connection, then runs at most one due background job from
// JOBS (weather here; sky.cpp, space.cpp, enso.cpp), each through a net::Session with a pinned CA and
// a time budget. Being one task means only one TLS session exists at a time: the gateway connection
// is closed before a TLS job starts.
// After boot the jobs' last results come back from the SD cache (cache.h) first, so the screens fill at
// once and a job whose data is still inside its refresh window waits for it to end.
#include "powerwall.h"
#include "ca_certs.h"
#include "cache.h"
#include "enso.h"
#include "history.h"
#include "mem_util.h"
#include "net_http.h"
#include "secrets.h"
#include "shared_state.h"
#include "sky.h"
#include "space.h"
#include "time_util.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>
#include <stdarg.h>

volatile uint8_t g_netPhase; // DIAG stutter: 0 idle, 1 gateway poll, 2 background job (read by main.cpp)

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t READ_TIMEOUT_MS = 3000;
constexpr uint32_t HANDSHAKE_TIMEOUT_S = 8; // default is 120 s, which would freeze polling
constexpr uint32_t GW_BODY_DEADLINE_MS = 5000;
constexpr size_t GW_BUF = 16 * 1024;        // gateway bodies (aggregates is the largest, a few KB)
constexpr size_t WEATHER_BUF = 16 * 1024;   // Open-Meteo body is ~3 KB
constexpr uint32_t MIN = 60 * 1000UL;
constexpr const char *WEATHER_REC = "weather"; // SD cache record name (cache.h)
// After boot, wait at most this long (from the network task's start) for the SD card's first mount
// before restoring the cache anyway (no card: nothing to restore)
constexpr uint32_t RESTORE_WAIT_MS = 20000;

WiFiMulti s_wifiMulti;
// Namespace-scope, so the mutexes exist from static init: the SD task reads powerwallData() before
// powerwallStart() runs.
Shared<PwData> s_data;
Shared<WeatherData> s_weather;
Shared<NetStatus> s_net;
uint32_t s_pollMs;
TaskHandle_t s_netTask;

// Gateway connection (network task only)
WiFiClientSecure s_gwClient;
HTTPClient s_gwHttp;
char *s_gwBuf;      // GW_BUF bytes in PSRAM: every gateway body is read into it and parsed in place
String s_token;
int s_loginFailures = 0;
uint32_t s_nextLoginMs = 0;
bool s_backingOff = false; // s_nextLoginMs is only meaningful while set (millis() wraps after ~49 days)

void setError(const char *msg) {
    s_data.with([msg](PwData &d) { strlcpy(d.error, msg, sizeof(d.error)); });
}

// Keeps a more specific message that is already showing
void setErrorIfNone(const char *msg) {
    s_data.with([msg](PwData &d) {
        if (!d.error[0]) strlcpy(d.error, msg, sizeof(d.error));
    });
}

void setHttpError(const char *what, int code) {
    char msg[48];
    if (code < 0) snprintf(msg, sizeof(msg), "%s: %s", what, HTTPClient::errorToString(code).c_str());
    else snprintf(msg, sizeof(msg), "%s HTTP %d", what, code);
    setError(msg);
}

String gwUrl(const char *path) { return String("https://") + PW_HOST + path; }

// Starts a request on the shared gateway connection.
// keepAlive false: HTTP/1.0 request -> the gateway sends a plain (non-chunked) body and closes the
// connection. keepAlive true: HTTP/1.1, the connection stays open; a chunked body is de-chunked by
// net::readBody (HTTPClient's own chunk reader on a kept-alive TLS connection could stall in a
// non-yielding read loop: watchdog reset).
void gwBegin(const char *path, bool keepAlive) {
    if (!s_gwClient.connected()) net::noteTlsConnect(); // this request makes a new TLS connection
    s_gwHttp.begin(s_gwClient, gwUrl(path));
    s_gwHttp.useHTTP10(!keepAlive);
    s_gwHttp.setReuse(keepAlive);
    s_gwHttp.setConnectTimeout(CONNECT_TIMEOUT_MS);
    s_gwHttp.setTimeout(READ_TIMEOUT_MS);
}

// Reads a 200 response's body into s_gwBuf. Returns its length, or -1 (logged by the caller's code).
int gwBody() {
    size_t len = 0;
    return net::readBody(s_gwHttp, s_gwBuf, GW_BUF, len, GW_BODY_DEADLINE_MS) == net::Result::Ok ? (int)len : -1;
}

bool login() {
    if (s_backingOff) {
        if ((int32_t)(millis() - s_nextLoginMs) < 0) {
            setErrorIfNone("login failed - retrying"); // no request made; don't leave the UI without a reason
            return false;
        }
        s_backingOff = false; // back-off elapsed
    }
    gwBegin("/api/login/Basic", true);
    s_gwHttp.addHeader("Content-Type", "application/json");
    JsonDocument req(psramJson());
    req["username"] = "customer";
    req["password"] = PW_CUSTOMER_PASSWORD;
    req["email"] = "";
    req["clientInfo"]["timezone"] = "Europe/London";
    String body;
    serializeJson(req, body);
    int code = s_gwHttp.POST(body);
    const int len = code == 200 ? gwBody() : -1;
    s_gwHttp.end();

    JsonDocument res(psramJson());
    if (code == 200 && len >= 0 && !deserializeJson(res, (const char *)s_gwBuf, (size_t)len) &&
        res["token"].is<const char *>()) {
        s_token = res["token"].as<const char *>();
        s_loginFailures = 0;
        s_backingOff = false;
        Serial.println("Powerwall: logged in");
        return true;
    }
    // Back off 30 s, 60 s, 120 s ... up to 5 min so a wrong password can't trigger a lockout
    s_loginFailures++;
    uint32_t backoff = 30000UL << (s_loginFailures > 4 ? 4 : s_loginFailures - 1);
    if (backoff > 300000UL) backoff = 300000UL;
    s_nextLoginMs = millis() + backoff;
    s_backingOff = true;
    if (code == 401 || code == 403) setError("login rejected - check password");
    else if (code == 200) setError("login: bad response");
    else setHttpError("login", code);
    Serial.printf("Powerwall: login failed (%d), retry in %lu s\n", code, (unsigned long)(backoff / 1000));
    return false;
}

// GET a path and parse it (optionally through a filter). Returns the HTTP status (<0 = transport
// error, -100 = body or JSON incomplete).
int getJson(const char *path, JsonDocument &doc, const JsonDocument *filter, bool keepAlive) {
    gwBegin(path, keepAlive);
    s_gwHttp.addHeader("Authorization", "Bearer " + s_token);
    int code = s_gwHttp.GET();
    if (code == 200) {
        const int len = gwBody();
        // const char *: ArduinoJson copies the strings, so s_gwBuf can be reused for the next body
        const char *body = s_gwBuf;
        if (len < 0) code = -100;
        else if (filter) code = deserializeJson(doc, body, (size_t)len, DeserializationOption::Filter(*filter)) ? -100 : 200;
        else code = deserializeJson(doc, body, (size_t)len) ? -100 : 200;
        if (code == 200 && !doc.is<JsonObject>()) code = -100;
    }
    s_gwHttp.end(); // keeps the connection open for reuse when the request asked for keep-alive
    return code;
}

const JsonDocument &aggregatesFilter() {
    static JsonDocument filter(psramJson());
    if (filter.isNull()) {
        for (const char *m : {"site", "battery", "load", "solar"}) {
            filter[m]["instant_power"] = true;
            filter[m]["energy_exported"] = true;
            filter[m]["energy_imported"] = true;
            filter[m]["instant_average_voltage"] = true;
            filter[m]["instant_reactive_power"] = true;
            filter[m]["instant_apparent_power"] = true;
            filter[m]["frequency"] = true;
        }
    }
    return filter;
}

bool pollOnce() {
    if (s_token.isEmpty() && !login()) return false;

    // Small fixed-length responses first on the kept-alive connection, then the chunked aggregates:
    // HTTP/1.0 by default (closes the connection -> one TLS handshake per poll), or kept alive with
    // PW_GW_KEEPALIVE (no handshake while the gateway keeps the connection open).
    JsonDocument agg(psramJson()), soe(psramJson()), grid(psramJson());
    int codeSoe = getJson("/api/system_status/soe", soe, nullptr, true);
    if (codeSoe == 401 || codeSoe == 403) {
        s_token = "";
        if (!login()) return false;
        codeSoe = getJson("/api/system_status/soe", soe, nullptr, true);
    }
    int codeGrid = getJson("/api/system_status/grid_status", grid, nullptr, true);
    int code = getJson("/api/meters/aggregates", agg, &aggregatesFilter(), PW_GW_KEEPALIVE != 0);
    if (code != 200) {
        setHttpError("aggregates", code);
        s_gwClient.stop(); // start clean next time
        return false;
    }

    // Parse everything into locals first; the shared state is only touched (briefly, under the
    // lock) once the aggregates are known to be complete. A missing meter must not become "0".
    JsonVariantConst root = agg.as<JsonVariantConst>();
    bool complete = true;
    auto req = [&](const char *meter, const char *key) -> double { // required numeric key
        JsonVariantConst v = root[meter][key];
        if (!v.is<double>()) {
            complete = false;
            return 0.0;
        }
        return v.as<double>();
    };
    PwData n; // only the aggregate fields are used
    n.siteW = (float)req("site", "instant_power");
    n.batteryW = (float)req("battery", "instant_power");
    n.loadW = (float)req("load", "instant_power");
    n.solarW = (float)req("solar", "instant_power");
    n.solarExportedWh = req("solar", "energy_exported");
    n.loadImportedWh = req("load", "energy_imported");
    n.siteExportedWh = req("site", "energy_exported");
    n.siteImportedWh = req("site", "energy_imported");
    n.battExportedWh = req("battery", "energy_exported");
    n.battImportedWh = req("battery", "energy_imported");
    // energy.cpp needs solar and home counters > 0 (zero is how it detects a gateway counter reset)
    if (n.solarExportedWh <= 0 || n.loadImportedWh <= 0) complete = false;
    if (!complete) {
        setError("aggregates: incomplete data"); // keep the last good data; it goes stale on screen
        s_gwClient.stop();
        return false;
    }
    // Optional: the gauges show "--" when these are 0
    n.voltageV = root["site"]["instant_average_voltage"] | 0.0f;
    n.siteVar = root["site"]["instant_reactive_power"] | 0.0f;
    n.siteVA = root["site"]["instant_apparent_power"] | 0.0f;
    n.loadVar = root["load"]["instant_reactive_power"] | 0.0f;
    n.loadVA = root["load"]["instant_apparent_power"] | 0.0f;
    n.frequencyHz = root["battery"]["frequency"] | 0.0f;

    bool haveSoc = false, haveGrid = false, gridConnected = true, gridServices = false;
    float soc = 0;
    if (codeSoe == 200 && soe["percentage"].is<float>()) {
        float raw = soe["percentage"].as<float>();
#if PW_SOC_APP_SCALE
        float pct = (raw - 5.0f) / 0.95f; // as the Tesla app shows it (5% reserve hidden)
#else
        float pct = raw;
#endif
        soc = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        haveSoc = true;
    }
    if (codeGrid == 200) {
        const char *gs = grid["grid_status"] | "SystemGridConnected";
        gridConnected = strcmp(gs, "SystemGridConnected") == 0;
        gridServices = grid["grid_services_active"] | false;
        haveGrid = true;
    }

    s_data.with([&](PwData &d) {
        d.siteW = n.siteW;
        d.batteryW = n.batteryW;
        d.loadW = n.loadW;
        d.solarW = n.solarW;
        d.voltageV = n.voltageV;
        d.siteVar = n.siteVar;
        d.siteVA = n.siteVA;
        d.loadVar = n.loadVar;
        d.loadVA = n.loadVA;
        d.frequencyHz = n.frequencyHz;
        d.solarExportedWh = n.solarExportedWh;
        d.loadImportedWh = n.loadImportedWh;
        d.siteExportedWh = n.siteExportedWh;
        d.siteImportedWh = n.siteImportedWh;
        d.battExportedWh = n.battExportedWh;
        d.battImportedWh = n.battImportedWh;
        if (haveSoc) {
            d.soc = soc;
            d.socValid = true;
        }
        if (haveGrid) {
            d.gridConnected = gridConnected;
            d.gridServicesActive = gridServices;
        }
        d.valid = true;
        d.updatedMs = millis();
        d.error[0] = '\0';
    });
    return true;
}

// Open-Meteo current conditions, today's high/low/rain, and the next 10 h. Only replaces the
// stored weather when the response parses completely.
bool fetchWeather() {
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m,cloud_cover"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&hourly=temperature_2m,weather_code,is_day&forecast_hours=11&forecast_days=1&timezone=auto",
             (double)WEATHER_LAT, (double)WEATHER_LON);
    HeapBuf body;
    if (!body.psram(WEATHER_BUF)) {
        Serial.println("Weather: no PSRAM for the body");
        return false;
    }
    size_t len = 0;
    {
        net::Options o;
        o.tag = "Weather";
        o.ca = LETS_ENCRYPT_CA; // public internet: verify the server
        o.budgetMs = PW_JOB_BUDGET_MS;
        net::Session s(o);
        net::Response r;
        if (s.get(url, body.as<char>(), body.size, &r) != net::Result::Ok) return false;
        len = r.len;
    } // TLS session freed before parsing
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body.as<const char>(), len) || !doc["current"]["weather_code"].is<int>() ||
        !doc["current"]["temperature_2m"].is<float>()) {
        Serial.println("Weather: incomplete response");
        return false;
    }
    WeatherData w;
    w.tempC = doc["current"]["temperature_2m"] | 0.0f;
    w.code = doc["current"]["weather_code"] | -1;
    w.isDay = (doc["current"]["is_day"] | 1) == 1;
    w.hiC = doc["daily"]["temperature_2m_max"][0] | w.tempC;
    w.loC = doc["daily"]["temperature_2m_min"][0] | w.tempC;
    w.feelsC = doc["current"]["apparent_temperature"] | w.tempC;
    w.windKmh = doc["current"]["wind_speed_10m"] | 0.0f;
    w.rainPct = doc["daily"]["precipitation_probability_max"][0] | -1;
    w.cloudPct = doc["current"]["cloud_cover"] | -1;
    // Hourly from the current hour: take every 2nd entry -> now, +2 ... +10 h
    JsonArray times = doc["hourly"]["time"];
    for (int i = 0; i < 6; i++) {
        int k = i * 2;
        const char *t = times[k] | ""; // "2026-10-10T13:00"
        if (strlen(t) < 13) break;
        w.hours[i].hour = atoi(t + 11);
        w.hours[i].code = doc["hourly"]["weather_code"][k] | -1;
        w.hours[i].isDay = (doc["hourly"]["is_day"][k] | 1) == 1;
        w.hours[i].tempC = doc["hourly"]["temperature_2m"][k] | 0.0f;
    }
    w.valid = true;
    w.updatedMs = millis();
    s_weather.set(w);
    Serial.printf("Weather: %.1f C code %d (H %.1f L %.1f, rain %d%%)\n", w.tempC, w.code, w.hiC, w.loC, w.rainPct);
    cache::save(WEATHER_REC, WEATHER_CACHE_VERSION, w, time(nullptr));
    return true;
}

time_t restoreWeather() {
    return cache::restoreInto(WEATHER_REC, WEATHER_CACHE_VERSION, s_weather, &WeatherData::updatedMs);
}

void updateNetStatus(bool up) {
    NetStatus n = s_net.get();
    n.wifiUp = up;
    if (up) {
        strlcpy(n.ssid, WiFi.SSID().c_str(), sizeof(n.ssid)); // WiFi calls outside the lock
        n.rssi = WiFi.RSSI(); // NOLINT(bugprone-signed-char-misuse): int8 dBm, sign extension intended
    }
    s_net.set(n);
}

// ---- Background jobs ----------------------------------------------------------------------------
// Highest priority first; at most one job runs per pass, so the gateway keeps polling in between.
struct JobDef {
    const char *name;
    bool (*fn)();
    bool tls;          // closes the gateway connection first and logs the [tls] line
    uint32_t okMs;     // next run after success
    uint32_t retryMs;  // after a failure (first retry when backoff)
    bool backoff;      // retry 1, 2, 4 ... up to okMs
    bool (*needed)();  // nullptr = always; when false the job is skipped this pass (checked every pass)
    time_t (*restore)(); // loads the job's SD cache record (named like the job); its fetch time, 0 = none
};

const JobDef JOBS[] = {
    {"weather", fetchWeather, true, 15 * MIN, 1 * MIN, true, nullptr, restoreWeather},
    {"aurora", skyFetchAurora, false, 30 * MIN, 5 * MIN, false, nullptr, skyRestoreAurora}, // plain HTTP
    {"kp", skyFetchKp, true, 30 * MIN, 5 * MIN, false, nullptr, skyRestoreKp},
    {"moon", skyFetchMoon, true, 30 * MIN, 5 * MIN, false, skyMoonNeeded, skyRestoreMoon}, // once per local day
    {"swpc", spaceFetchSwpc, true, 30 * MIN, 5 * MIN, false, nullptr, spaceRestoreSwpc},
    {"sunimg", spaceFetchSunImage, true, 60 * MIN, 10 * MIN, false, nullptr, spaceRestoreSunImage},
    {"enso", ensoFetch, true, 12 * 60 * MIN, 30 * MIN, false, nullptr, ensoRestore}, // CPC updates weekly/monthly
};
constexpr size_t JOB_COUNT = sizeof(JOBS) / sizeof(JOBS[0]);

struct JobState {
    uint32_t next = 0;     // due when millis() reaches it
    uint32_t retryMs = 0;  // current back-off step (backoff jobs)
};
JobState s_jobs[JOB_COUNT];

// Runs a TLS job with the gateway's session closed first (one TLS session at a time), logging the
// internal-RAM low-water mark it reached.
bool runTls(const JobDef &j) {
    s_gwClient.stop();
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t minBefore = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    bool ok = j.fn();
    Serial.printf("  [tls] %s: job free before %u, min ever %u -> %u\n", j.name, (unsigned)before,
                  (unsigned)minBefore, (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return ok;
}

void runOneJob() {
    const uint32_t now = millis();
    for (size_t i = 0; i < JOB_COUNT; i++) {
        const JobDef &j = JOBS[i];
        JobState &st = s_jobs[i];
        if ((int32_t)(now - st.next) < 0) continue;
        if (j.needed && !j.needed()) continue;
        const bool ok = j.tls ? runTls(j) : j.fn();
        uint32_t wait = ok ? j.okMs : j.retryMs;
        if (j.backoff) {
            if (!st.retryMs) st.retryMs = j.retryMs;
            wait = ok ? j.okMs : st.retryMs;
            st.retryMs = ok ? j.retryMs : (st.retryMs * 2 > j.okMs ? j.okMs : st.retryMs * 2);
        }
        st.next = millis() + wait;
        return;
    }
}

// ---- SD cache restore ---------------------------------------------------------------------------
// Once after boot, before any job runs: the cached records fill the screens, and a job whose data is
// still inside its refresh window is pushed back to when that window ends. Restored as soon as the SD
// card has mounted; ages need the clock, so until it is set the records show as fresh, and they are
// restored once more when it is set, which then schedules the jobs.
enum class Restore : uint8_t { Pending, NoClock, Done };
Restore s_restore = Restore::Pending;
uint32_t s_taskStartMs;

// snprintf onto the end of buf (cap bytes); stops quietly when full
__attribute__((format(printf, 3, 4))) void appendf(char *buf, size_t cap, const char *fmt, ...) {
    const size_t len = strnlen(buf, cap);
    if (len + 1 >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + len, cap - len, fmt, ap);
    va_end(ap);
}

// Restores every job's record; with the clock set, also schedules the jobs from the records' ages.
// Returns how many records were restored. One summary line.
int restoreCache(bool clockOk) {
    if (!historyStatus().mounted) {
        Serial.println("Cache: no SD card, nothing restored");
        return 0;
    }
    const time_t now = time(nullptr);
    char got[320] = "", none[80] = "";
    int restored = 0;
    for (size_t i = 0; i < JOB_COUNT; i++) {
        const JobDef &j = JOBS[i];
        const time_t utc = j.restore ? j.restore() : 0;
        if (!utc) {
            appendf(none, sizeof(none), "%s%s", none[0] ? ", " : "", j.name);
            continue;
        }
        restored++;
        if (!clockOk) continue;
        char age[16];
        cache::fmtAge(age, sizeof(age), (int64_t)(now - utc));
        appendf(got, sizeof(got), "%s%s %s old", got[0] ? "; " : "", j.name, age);
        if (j.needed) continue; // its predicate decides (moon: a record for today means no fetch)
        // Only next is set: retryMs (weather's back-off step) is left alone
        const uint32_t due = cache::dueInMs(utc, now, j.okMs);
        s_jobs[i].next = millis() + due;
        char in[16];
        cache::fmtAge(in, sizeof(in), due / 1000);
        if (due) appendf(got, sizeof(got), ", next in %s", in);
        else appendf(got, sizeof(got), ", due now");
    }
    if (clockOk) {
        Serial.printf("Cache: restored %d/%u%s%s%s%s\n", restored, (unsigned)JOB_COUNT, got[0] ? " - " : "", got,
                      none[0] ? "; none: " : "", none);
    } else {
        Serial.printf("Cache: restored %d/%u, clock not set (shown as fresh until it is)%s%s\n", restored,
                      (unsigned)JOB_COUNT, none[0] ? "; none: " : "", none);
    }
    return restored;
}

// Called every pass: restores the cache once the boot-time SD mount is done (waited for at most
// RESTORE_WAIT_MS), and again once the clock is set. Jobs run only after that (s_restore Done).
void restorePass() {
    if (s_restore == Restore::Done) return;
    const bool clockOk = tu::clockValid();
    if (s_restore == Restore::NoClock) {
        if (!clockOk) return;
    } else {
        // Don't wait for the clock: show the records now (as fresh) and fix the ages once it is set.
        // An ESP.restart() comes back through an RTC watchdog reset here, which clears the clock, so
        // waiting for NTP left the screens empty for ~20 s after every reboot.
        const bool waited = millis() - s_taskStartMs >= RESTORE_WAIT_MS;
        if (!waited && !historyStatus().firstMountDone) return;
    }
    const int restored = restoreCache(clockOk);
    // Restored without the clock: restore again once it is set, for the ages and the schedule
    s_restore = clockOk || restored == 0 ? Restore::Done : Restore::NoClock;
}

void netTask(void *) {
    s_gwClient.setInsecure(); // the gateway uses a self-signed certificate (LAN only)
    s_gwClient.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    s_taskStartMs = millis();
    // The first Wi-Fi connect blocks for seconds: give the SD task its boot mount (~1.5 s) first, so
    // the cache restore below fills the screens straight away
    while (!historyStatus().firstMountDone && millis() - s_taskStartMs < 3000) vTaskDelay(pdMS_TO_TICKS(50));
    for (;;) {
        restorePass(); // before the Wi-Fi check: the cache fills the screens without a network too
        if (s_wifiMulti.run(10000) != WL_CONNECTED) {
            updateNetStatus(false);
            setError("Wi-Fi not connected");
            // Diagnostics: list what the ESP32 can actually see (2.4 GHz only), at most every 30 s
            static uint32_t lastScan = 0;
            if (lastScan == 0 || millis() - lastScan > 30000) {
                lastScan = millis();
                int n = WiFi.scanNetworks();
                Serial.printf("Wi-Fi scan: %d networks visible\n", n);
                for (int i = 0; i < n; i++) {
                    Serial.printf("  '%s' ch%d %d dBm%s\n", WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i),
                                  WiFi.SSID(i).isEmpty() ? " (hidden)" : "");
                }
                WiFi.scanDelete();
            }
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        updateNetStatus(true);

        uint32_t start = millis();
        g_netPhase = 1; // DIAG
        pollOnce();
        g_netPhase = 2;
        // Background jobs need NTP time: TLS certificate validity is checked against it. And the cache
        // restored first, so fresh records aren't fetched again.
        if (s_restore == Restore::Done && tu::clockValid()) runOneJob();
        g_netPhase = 0;
        uint32_t spent = millis() - start;
        vTaskDelay(pdMS_TO_TICKS(spent < s_pollMs ? s_pollMs - spent : 100));
    }
}

// mbedTLS allocator: blocks of 4 KB and up (mainly the two 16 KB record buffers) go to PSRAM; the
// handshake's many small big-number/certificate blocks stay internal. Either falls back to the
// other. The core's default forces everything internal (~40-50 KB per session), which was the
// internal-RAM low point; a 1 KB threshold sent the per-poll handshake maths through PSRAM too,
// and the PSRAM traffic made the RGB panel crackle.
void *tlsCalloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return nullptr;
    const uint32_t ext = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, in = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    bool big = n * size >= 4096;
    void *p = heap_caps_calloc(n, size, big ? ext : in);
    return p ? p : heap_caps_calloc(n, size, big ? in : ext);
}

void tlsFree(void *p) { heap_caps_free(p); }

} // namespace

void powerwallStart(uint32_t pollMs) {
    s_pollMs = pollMs;
    s_gwBuf = static_cast<char *>(psramAlloc(GW_BUF));
    if (!s_gwBuf) s_gwBuf = static_cast<char *>(internalAlloc(GW_BUF)); // no PSRAM: should not happen
    const char *keys[] = {"Transfer-Encoding"}; // net::readBody detects chunked bodies from it
    s_gwHttp.collectHeaders(keys, 1);
    mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree); // before any TLS session exists
    WiFi.persistent(false); // don't write the Wi-Fi config to NVS (internal flash) at runtime
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("powerwall-display");
    for (const WifiCred &w : WIFI_NETWORKS) s_wifiMulti.addAP(w.ssid, w.password);
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    // Core 0 (Arduino loop/LVGL runs on core 1). TLS + JPEG decode need a generous stack.
    // Idle priority (0): network libraries can busy-wait without yielding (e.g. Stream::readBytes);
    // at priority 0 they time-slice with core 0's idle task, so the task watchdog can't fire.
    xTaskCreatePinnedToCore(netTask, "pw_net", 12288, nullptr, tskIDLE_PRIORITY, &s_netTask, 0);
}

PwData powerwallData() { return s_data.get(); }

WeatherData weatherData() { return s_weather.get(); }

NetStatus networkStatus() { return s_net.get(); }

uint32_t netTaskStackFree() { return s_netTask ? uxTaskGetStackHighWaterMark(s_netTask) : 0; }
