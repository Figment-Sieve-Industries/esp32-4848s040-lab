// Powerwall 3 local LAN API client (customer-password "basic" access, firmware 26.x) + Open-Meteo.
//   POST /api/login/Basic               -> {"token": ...}   (sent back as Authorization: Bearer)
//   GET  /api/meters/aggregates         -> site / battery / load / solar meters
//   GET  /api/system_status/soe         -> {"percentage": ..}
//   GET  /api/system_status/grid_status -> {"grid_status": "SystemGridConnected" | ..., "grid_services_active": ..}
// 401/403 means the token expired or is missing, so log in again (with back-off on failure).
//
// Two tasks on core 0 (LVGL runs on core 1 and never touches the network):
//  - gwTask polls the gateway every s_pollMs over ONE long-lived HTTPS connection (keep-alive).
//  - wxTask fetches weather every 15 min with its own client and a pinned CA.
// s_tlsLock ensures only one TLS session exists at a time (mbedTLS needs ~40-50 KB internal RAM
// per session): the weather task closes the gateway connection before opening its own.
#include "powerwall.h"
#include "ca_certs.h"
#include "secrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiMulti.h>

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t READ_TIMEOUT_MS = 3000;
constexpr uint32_t HANDSHAKE_TIMEOUT_S = 8; // default is 120 s, which would freeze polling
constexpr uint32_t WEATHER_PERIOD_MS = 15 * 60 * 1000UL;

WiFiMulti s_wifiMulti;
SemaphoreHandle_t s_lock;    // protects s_data / s_weather / s_net
SemaphoreHandle_t s_tlsLock; // one TLS session at a time
PwData s_data;
WeatherData s_weather;
NetStatus s_net;
uint32_t s_pollMs;
TaskHandle_t s_gwTask, s_wxTask;

// Gateway connection (gwTask only, or wxTask while holding s_tlsLock to close it)
WiFiClientSecure s_gwClient;
HTTPClient s_gwHttp;
String s_token;
int s_loginFailures = 0;
uint32_t s_nextLoginMs = 0;

void setError(const char *msg) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_data.error, msg, sizeof(s_data.error));
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
    if ((int32_t)(millis() - s_nextLoginMs) < 0) return false; // backing off
    gwBegin("/api/login/Basic");
    s_gwHttp.addHeader("Content-Type", "application/json");
    JsonDocument req;
    req["username"] = "customer";
    req["password"] = PW_CUSTOMER_PASSWORD;
    req["email"] = "";
    req["clientInfo"]["timezone"] = "Europe/London";
    String body;
    serializeJson(req, body);
    int code = s_gwHttp.POST(body);
    String resp = code == 200 ? s_gwHttp.getString() : String(); // getString() de-chunks
    s_gwHttp.end();

    JsonDocument res;
    if (code == 200 && !deserializeJson(res, resp) && res["token"].is<const char *>()) {
        s_token = res["token"].as<const char *>();
        s_loginFailures = 0;
        Serial.println("Powerwall: logged in");
        return true;
    }
    // Back off 30 s, 60 s, 120 s ... up to 5 min so a wrong password can't trigger a lockout
    s_loginFailures++;
    uint32_t backoff = 30000UL << (s_loginFailures > 4 ? 4 : s_loginFailures - 1);
    if (backoff > 300000UL) backoff = 300000UL;
    s_nextLoginMs = millis() + backoff;
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
    static JsonDocument filter;
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
    JsonDocument agg, soe, grid;
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

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.siteW = agg["site"]["instant_power"] | 0.0f;
    s_data.batteryW = agg["battery"]["instant_power"] | 0.0f;
    s_data.loadW = agg["load"]["instant_power"] | 0.0f;
    s_data.solarW = agg["solar"]["instant_power"] | 0.0f;
    s_data.voltageV = agg["site"]["instant_average_voltage"] | 0.0f;
    s_data.siteVar = agg["site"]["instant_reactive_power"] | 0.0f;
    s_data.siteVA = agg["site"]["instant_apparent_power"] | 0.0f;
    s_data.loadVar = agg["load"]["instant_reactive_power"] | 0.0f;
    s_data.loadVA = agg["load"]["instant_apparent_power"] | 0.0f;
    s_data.frequencyHz = agg["battery"]["frequency"] | 0.0f;
    s_data.solarExportedWh = agg["solar"]["energy_exported"] | 0.0;
    s_data.loadImportedWh = agg["load"]["energy_imported"] | 0.0;
    s_data.siteExportedWh = agg["site"]["energy_exported"] | 0.0;
    s_data.siteImportedWh = agg["site"]["energy_imported"] | 0.0;
    s_data.battExportedWh = agg["battery"]["energy_exported"] | 0.0;
    s_data.battImportedWh = agg["battery"]["energy_imported"] | 0.0;
    if (codeSoe == 200 && soe["percentage"].is<float>()) {
        float raw = soe["percentage"].as<float>();
#if PW_SOC_APP_SCALE
        float pct = (raw - 5.0f) / 0.95f; // as the Tesla app shows it (5% reserve hidden)
#else
        float pct = raw;
#endif
        s_data.soc = pct < 0 ? 0 : pct > 100 ? 100 : pct;
        s_data.socValid = true;
    }
    if (codeGrid == 200) {
        const char *gs = grid["grid_status"] | "SystemGridConnected";
        s_data.gridConnected = strcmp(gs, "SystemGridConnected") == 0;
        s_data.gridServicesActive = grid["grid_services_active"] | false;
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
             "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&hourly=temperature_2m,weather_code,is_day&forecast_hours=11&forecast_days=1&timezone=auto",
             (double)WEATHER_LAT, (double)WEATHER_LON);
    WiFiClientSecure client;
    client.setCACert(OPEN_METEO_CA); // public internet: verify the server
    client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    HTTPClient http;
    http.begin(client, url);
    http.setReuse(false);
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(8000);
    int code = http.GET();
    String body = code == 200 ? http.getString() : String();
    http.end();
    client.stop();
    if (code != 200) {
        Serial.printf("Weather: %s\n", code < 0 ? HTTPClient::errorToString(code).c_str() : String(code).c_str());
        return false;
    }
    JsonDocument doc;
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
        s_net.rssi = WiFi.RSSI();
    }
    xSemaphoreGive(s_lock);
}

void gwTask(void *) {
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
        xSemaphoreTake(s_tlsLock, portMAX_DELAY);
        pollOnce();
        xSemaphoreGive(s_tlsLock);
        uint32_t spent = millis() - start;
        vTaskDelay(pdMS_TO_TICKS(spent < s_pollMs ? s_pollMs - spent : 100));
    }
}

void wxTask(void *) {
    uint32_t retryMs = 60000;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED || time(nullptr) < 1700000000) {
            vTaskDelay(pdMS_TO_TICKS(5000)); // wait for Wi-Fi and NTP (TLS cert validity needs the time)
            continue;
        }
        xSemaphoreTake(s_tlsLock, portMAX_DELAY);
        s_gwClient.stop(); // free the gateway's TLS session first (gwTask is blocked on the lock)
        bool ok = fetchWeather();
        xSemaphoreGive(s_tlsLock);
        // 15 min when OK; on failure retry after 1, 2, 4 ... up to 15 min
        uint32_t wait = ok ? WEATHER_PERIOD_MS : retryMs;
        retryMs = ok ? 60000 : (retryMs * 2 > WEATHER_PERIOD_MS ? WEATHER_PERIOD_MS : retryMs * 2);
        vTaskDelay(pdMS_TO_TICKS(wait));
    }
}

} // namespace

void powerwallStart(uint32_t pollMs) {
    s_pollMs = pollMs;
    s_lock = xSemaphoreCreateMutex();
    s_tlsLock = xSemaphoreCreateMutex();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("powerwall-display");
    for (const WifiCred &w : WIFI_NETWORKS) s_wifiMulti.addAP(w.ssid, w.password);
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    // Core 0 (Arduino loop/LVGL runs on core 1). TLS needs generous stacks.
    // Idle priority (0): network libraries can busy-wait without yielding (e.g. Stream::readBytes);
    // at priority 0 they time-slice with core 0's idle task, so the task watchdog can't fire.
    xTaskCreatePinnedToCore(gwTask, "pw_gw", 12288, nullptr, tskIDLE_PRIORITY, &s_gwTask, 0);
    xTaskCreatePinnedToCore(wxTask, "pw_wx", 10240, nullptr, tskIDLE_PRIORITY, &s_wxTask, 0);
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

void netTaskStackFree(uint32_t &gwBytes, uint32_t &wxBytes) {
    gwBytes = s_gwTask ? uxTaskGetStackHighWaterMark(s_gwTask) : 0;
    wxBytes = s_wxTask ? uxTaskGetStackHighWaterMark(s_wxTask) : 0;
}
