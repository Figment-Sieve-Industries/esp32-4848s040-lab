// Powerwall 3 local LAN API client (customer-password "basic" access, firmware 26.x) + Open-Meteo.
//   POST /api/login/Basic               -> {"token": ...}   (sent back as Authorization: Bearer)
//   GET  /api/meters/aggregates         -> site / battery / load / solar meters
//   GET  /api/system_status/soe         -> {"percentage": ..}
//   GET  /api/system_status/grid_status -> {"grid_status": "SystemGridConnected" | ..., "grid_services_active": ..}
// 401/403 means the token expired or is missing, so log in again (with back-off on failure).
//
// One network task on core 0 (LVGL runs on core 1 and never touches the network). Each pass polls
// the gateway over its long-lived HTTPS connection, then runs at most one due background job
// (weather, sky.cpp, space.cpp, enso.cpp), each with its own client and a pinned CA. Being one task
// means only one TLS session exists at a time: the gateway connection is closed before a job's.
#include "powerwall.h"
#include "enso.h"
#include "psram_json.h"
#include "sky.h"
#include "space.h"
#include "ca_certs.h"
#include "secrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t READ_TIMEOUT_MS = 3000;
constexpr uint32_t HANDSHAKE_TIMEOUT_S = 8; // default is 120 s, which would freeze polling
constexpr uint32_t WEATHER_HANDSHAKE_TIMEOUT_S = 5;
constexpr uint32_t WEATHER_READ_TIMEOUT_MS = 5000;
constexpr uint32_t WEATHER_PERIOD_MS = 15 * 60 * 1000UL;
constexpr uint32_t SKY_PERIOD_MS = 30 * 60 * 1000UL;   // aurora + Kp
constexpr uint32_t SKY_RETRY_MS = 5 * 60 * 1000UL;     // after a failed sky fetch
constexpr uint32_t SUN_IMAGE_PERIOD_MS = 60 * 60 * 1000UL;
constexpr uint32_t SUN_IMAGE_RETRY_MS = 10 * 60 * 1000UL;
constexpr uint32_t ENSO_PERIOD_MS = 12 * 60 * 60 * 1000UL; // CPC updates weekly/monthly
constexpr uint32_t ENSO_RETRY_MS = 30 * 60 * 1000UL;

WiFiMulti s_wifiMulti;
// Protects s_data / s_weather / s_net. Created at static init: the SD task reads powerwallData()
// before powerwallStart() runs.
SemaphoreHandle_t s_lock = xSemaphoreCreateMutex();
PwData s_data;
WeatherData s_weather;
NetStatus s_net;
uint32_t s_pollMs;
TaskHandle_t s_netTask;

// Gateway connection (network task only)
WiFiClientSecure s_gwClient;
HTTPClient s_gwHttp;
String s_token;
int s_loginFailures = 0;
uint32_t s_nextLoginMs = 0;
bool s_backingOff = false; // s_nextLoginMs is only meaningful while set (millis() wraps after ~49 days)

void setError(const char *msg) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_data.error, msg, sizeof(s_data.error));
    xSemaphoreGive(s_lock);
}

// Keeps a more specific message that is already showing
void setErrorIfNone(const char *msg) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_data.error[0]) strlcpy(s_data.error, msg, sizeof(s_data.error));
    xSemaphoreGive(s_lock);
}

void setHttpError(const char *what, int code) {
    char msg[48];
    if (code < 0) snprintf(msg, sizeof(msg), "%s: %s", what, HTTPClient::errorToString(code).c_str());
    else snprintf(msg, sizeof(msg), "%s HTTP %d", what, code);
    setError(msg);
}

String gwUrl(const char *path) { return String("https://") + PW_HOST + path; }

// Starts a request on the shared keep-alive connection
// http10: HTTP/1.0 request -> the gateway sends a plain (non-chunked) body and closes the
// connection. Used for the chunked /aggregates response: HTTPClient's chunk reader on a
// kept-alive TLS connection could stall in a non-yielding read loop (watchdog reset).
void gwBegin(const char *path, bool http10 = false) {
    s_gwHttp.begin(s_gwClient, gwUrl(path));
    s_gwHttp.useHTTP10(http10);
    s_gwHttp.setReuse(!http10);
    s_gwHttp.setConnectTimeout(CONNECT_TIMEOUT_MS);
    s_gwHttp.setTimeout(READ_TIMEOUT_MS);
}

bool login() {
    if (s_backingOff) {
        if ((int32_t)(millis() - s_nextLoginMs) < 0) {
            setErrorIfNone("login failed - retrying"); // no request made; don't leave the UI without a reason
            return false;
        }
        s_backingOff = false; // back-off elapsed
    }
    gwBegin("/api/login/Basic");
    s_gwHttp.addHeader("Content-Type", "application/json");
    JsonDocument req(psramJson());
    req["username"] = "customer";
    req["password"] = PW_CUSTOMER_PASSWORD;
    req["email"] = "";
    req["clientInfo"]["timezone"] = "Europe/London";
    String body;
    serializeJson(req, body);
    int code = s_gwHttp.POST(body);
    String resp = code == 200 ? s_gwHttp.getString() : String(); // getString() de-chunks
    s_gwHttp.end();

    JsonDocument res(psramJson());
    if (code == 200 && !deserializeJson(res, resp) && res["token"].is<const char *>()) {
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

// GET a path and parse it (optionally through a filter). Returns the HTTP status (<0 = transport error).
int getJson(const char *path, JsonDocument &doc, const JsonDocument *filter = nullptr, bool http10 = false) {
    gwBegin(path, http10);
    s_gwHttp.addHeader("Authorization", "Bearer " + s_token);
    int code = s_gwHttp.GET();
    if (code == 200) {
        // getString() de-chunks the body. The gateway sends larger responses with chunked transfer
        // encoding, and parsing getStream() directly reads the hex chunk-size line as a JSON number.
        String body = s_gwHttp.getString();
        DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                          : deserializeJson(doc, body);
        if (err || !doc.is<JsonObject>()) code = -100;
    }
    s_gwHttp.end(); // keeps the connection open for reuse
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

    // Small fixed-length responses first on the kept-alive connection, then the chunked
    // aggregates as HTTP/1.0 (closes the connection) -> one TLS handshake per poll.
    JsonDocument agg(psramJson()), soe(psramJson()), grid(psramJson());
    int codeSoe = getJson("/api/system_status/soe", soe);
    if (codeSoe == 401 || codeSoe == 403) {
        s_token = "";
        if (!login()) return false;
        codeSoe = getJson("/api/system_status/soe", soe);
    }
    int codeGrid = getJson("/api/system_status/grid_status", grid);
    int code = getJson("/api/meters/aggregates", agg, &aggregatesFilter(), true);
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

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.siteW = n.siteW;
    s_data.batteryW = n.batteryW;
    s_data.loadW = n.loadW;
    s_data.solarW = n.solarW;
    s_data.voltageV = n.voltageV;
    s_data.siteVar = n.siteVar;
    s_data.siteVA = n.siteVA;
    s_data.loadVar = n.loadVar;
    s_data.loadVA = n.loadVA;
    s_data.frequencyHz = n.frequencyHz;
    s_data.solarExportedWh = n.solarExportedWh;
    s_data.loadImportedWh = n.loadImportedWh;
    s_data.siteExportedWh = n.siteExportedWh;
    s_data.siteImportedWh = n.siteImportedWh;
    s_data.battExportedWh = n.battExportedWh;
    s_data.battImportedWh = n.battImportedWh;
    if (haveSoc) {
        s_data.soc = soc;
        s_data.socValid = true;
    }
    if (haveGrid) {
        s_data.gridConnected = gridConnected;
        s_data.gridServicesActive = gridServices;
    }
    s_data.valid = true;
    s_data.updatedMs = millis();
    s_data.error[0] = '\0';
    xSemaphoreGive(s_lock);
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
    WiFiClientSecure client;
    client.setCACert(OPEN_METEO_CA); // public internet: verify the server
    // This runs on the network task, so gateway polling pauses: keep the worst case bounded
    // (connect 3 s + handshake 5 s + read 5 s) instead of the gateway's 8 s handshake / 8 s read.
    client.setHandshakeTimeout(WEATHER_HANDSHAKE_TIMEOUT_S);
    HTTPClient http;
    http.begin(client, url);
    http.setReuse(false);
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(WEATHER_READ_TIMEOUT_MS);
    int code = http.GET();
    String body = code == 200 ? http.getString() : String();
    http.end();
    client.stop();
    if (code != 200) {
        Serial.printf("Weather: %s\n", code < 0 ? HTTPClient::errorToString(code).c_str() : String(code).c_str());
        return false;
    }
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body) || !doc["current"]["weather_code"].is<int>() ||
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
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_weather = w;
    xSemaphoreGive(s_lock);
    Serial.printf("Weather: %.1f C code %d (H %.1f L %.1f, rain %d%%)\n", w.tempC, w.code, w.hiC, w.loC, w.rainPct);
    return true;
}

void updateNetStatus(bool up) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_net.wifiUp = up;
    if (up) {
        strlcpy(s_net.ssid, WiFi.SSID().c_str(), sizeof(s_net.ssid));
        s_net.rssi = WiFi.RSSI(); // NOLINT(bugprone-signed-char-misuse): int8 dBm, sign extension intended
    }
    xSemaphoreGive(s_lock);
}

// One scheduled background job: due when millis() reaches `next`.
struct Job {
    uint32_t next = 0;
    bool due(uint32_t now) const { return (int32_t)(now - next) >= 0; }
    void after(uint32_t now, uint32_t ms) { next = now + ms; }
};

// Run a TLS job with the gateway's session closed first (one TLS session at a time), logging the
// internal-RAM low-water mark it reached.
template <typename F> bool withTls(F fn) {
    s_gwClient.stop();
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t minBefore = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    bool ok = fn();
    Serial.printf("  [tls] job free before %u, min ever %u -> %u\n", (unsigned)before, (unsigned)minBefore,
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    return ok;
}

// Runs at most one due job per call, so the gateway keeps polling in between.
void runOneJob() {
    static Job weather, aurora, kp, moon, swpc, sunImg, enso;
    static uint32_t weatherRetryMs = 60000;
    uint32_t now = millis();
    if (weather.due(now)) {
        bool ok = withTls(fetchWeather);
        // 15 min when OK; on failure retry after 1, 2, 4 ... up to 15 min
        weather.after(millis(), ok ? WEATHER_PERIOD_MS : weatherRetryMs);
        weatherRetryMs = ok ? 60000 : (weatherRetryMs * 2 > WEATHER_PERIOD_MS ? WEATHER_PERIOD_MS : weatherRetryMs * 2);
    } else if (aurora.due(now)) {
        aurora.after(millis(), skyFetchAurora() ? SKY_PERIOD_MS : SKY_RETRY_MS); // plain HTTP
    } else if (kp.due(now)) {
        kp.after(millis(), withTls(skyFetchKp) ? SKY_PERIOD_MS : SKY_RETRY_MS);
    } else if (moon.due(now) && skyMoonNeeded()) {
        // Once per local day; checked again every 30 min (and after midnight)
        moon.after(millis(), withTls(skyFetchMoon) ? SKY_PERIOD_MS : SKY_RETRY_MS);
    } else if (swpc.due(now)) {
        swpc.after(millis(), withTls(spaceFetchSwpc) ? SKY_PERIOD_MS : SKY_RETRY_MS);
    } else if (sunImg.due(now)) {
        sunImg.after(millis(), withTls(spaceFetchSunImage) ? SUN_IMAGE_PERIOD_MS : SUN_IMAGE_RETRY_MS);
    } else if (enso.due(now)) {
        enso.after(millis(), withTls(ensoFetch) ? ENSO_PERIOD_MS : ENSO_RETRY_MS);
    }
}

void netTask(void *) {
    s_gwClient.setInsecure(); // the gateway uses a self-signed certificate (LAN only)
    s_gwClient.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    for (;;) {
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
        pollOnce();
        // Background jobs need NTP time: TLS certificate validity is checked against it
        if (time(nullptr) >= 1700000000) runOneJob();
        uint32_t spent = millis() - start;
        vTaskDelay(pdMS_TO_TICKS(spent < s_pollMs ? s_pollMs - spent : 100));
    }
}

// mbedTLS allocator: blocks of 1 KB and up (record buffers, certificates, big numbers) go to PSRAM,
// small ones stay internal for speed; either falls back to the other. The core's default forces
// everything internal (~40-50 KB per session), which was the internal-RAM low point.
void *tlsCalloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return nullptr;
    const uint32_t ext = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, in = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    bool big = n * size >= 1024;
    void *p = heap_caps_calloc(n, size, big ? ext : in);
    return p ? p : heap_caps_calloc(n, size, big ? in : ext);
}

void tlsFree(void *p) { heap_caps_free(p); }

} // namespace

void powerwallStart(uint32_t pollMs) {
    s_pollMs = pollMs;
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

PwData powerwallData() {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    PwData d = s_data;
    xSemaphoreGive(s_lock);
    return d;
}

WeatherData weatherData() {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    WeatherData w = s_weather;
    xSemaphoreGive(s_lock);
    return w;
}

NetStatus networkStatus() {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    NetStatus n = s_net;
    xSemaphoreGive(s_lock);
    return n;
}

uint32_t netTaskStackFree() { return s_netTask ? uxTaskGetStackHighWaterMark(s_netTask) : 0; }
