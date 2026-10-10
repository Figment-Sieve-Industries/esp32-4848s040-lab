// Powerwall 3 local LAN API client (customer-password "basic" access, firmware 26.x).
//   POST /api/login/Basic           -> {"token": ...}   (sent back as Authorization: Bearer)
//   GET  /api/meters/aggregates     -> site / battery / load / solar instant_power
//   GET  /api/system_status/soe     -> {"percentage": ..}
//   GET  /api/system_status/grid_status -> {"grid_status": "SystemGridConnected" | "SystemIslandedActive" ...}
// 401/403 means the token expired or is missing, so log in again.
#include "powerwall.h"
#include "secrets.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

static WiFiMulti s_wifiMulti;
static SemaphoreHandle_t s_lock;
static PwData s_data;
static WeatherData s_weather;
static NetStatus s_net;
static String s_token;
static uint32_t s_pollMs;

static void setError(const char *msg) {
    xSemaphoreTake(s_lock, portMAX_DELAY);
    strlcpy(s_data.error, msg, sizeof(s_data.error));
    xSemaphoreGive(s_lock);
}

static String url(const char *path) { return String("https://") + PW_HOST + path; }

static bool login(WiFiClientSecure &client) {
    HTTPClient http;
    http.begin(client, url("/api/login/Basic"));
    http.addHeader("Content-Type", "application/json");
    JsonDocument req;
    req["username"] = "customer";
    req["password"] = PW_CUSTOMER_PASSWORD;
    req["email"] = "";
    req["clientInfo"]["timezone"] = "Europe/London";
    String body;
    serializeJson(req, body);
    int code = http.POST(body);
    if (code != 200) {
        http.end();
        char msg[48];
        snprintf(msg, sizeof(msg), "login HTTP %d", code);
        setError(msg);
        return false;
    }
    JsonDocument res;
    DeserializationError err = deserializeJson(res, http.getStream());
    http.end();
    if (err || !res["token"].is<const char *>()) {
        setError("login: bad response");
        return false;
    }
    s_token = res["token"].as<const char *>();
    Serial.println("Powerwall: logged in");
    return true;
}

// GET a path and parse it (optionally through a filter). Returns the HTTP status (or <0 on transport error).
static int getJson(WiFiClientSecure &client, const char *path, JsonDocument &doc, const JsonDocument *filter = nullptr) {
    HTTPClient http;
    http.begin(client, url(path));
    http.setTimeout(5000);
    http.addHeader("Authorization", "Bearer " + s_token);
    int code = http.GET();
    if (code == 200) {
        // getString() de-chunks the body. The gateway sends larger responses with chunked transfer
        // encoding, and parsing getStream() directly reads the hex chunk-size line as a JSON number.
        String body = http.getString();
        DeserializationError err = filter ? deserializeJson(doc, body, DeserializationOption::Filter(*filter))
                                          : deserializeJson(doc, body);
        if (err || !doc.is<JsonObject>()) code = -100;
    }
    http.end();
    return code;
}

static bool pollOnce(WiFiClientSecure &client) {
    if (s_token.isEmpty() && !login(client)) return false;

    JsonDocument filter;
    for (const char *m : {"site", "battery", "load", "solar"}) {
        filter[m]["instant_power"] = true;
        filter[m]["energy_exported"] = true;
        filter[m]["energy_imported"] = true;
        filter[m]["instant_average_voltage"] = true;
        filter[m]["instant_reactive_power"] = true;
        filter[m]["instant_apparent_power"] = true;
        filter[m]["frequency"] = true;
    }

    JsonDocument agg, soe, grid;
    int code = getJson(client, "/api/meters/aggregates", agg, &filter);
    if (code == 401 || code == 403) {
        s_token = "";
        if (!login(client)) return false;
        code = getJson(client, "/api/meters/aggregates", agg, &filter);
    }
    if (code != 200) {
        char msg[48];
        snprintf(msg, sizeof(msg), "aggregates HTTP %d", code);
        setError(msg);
        return false;
    }
    int codeSoe = getJson(client, "/api/system_status/soe", soe);
    int codeGrid = getJson(client, "/api/system_status/grid_status", grid);

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
    if (codeSoe == 200) s_data.soc = soe["percentage"] | s_data.soc;
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

// Open-Meteo current conditions + today's high/low. Returns true on success.
static bool fetchWeather(WiFiClientSecure &client) {
    char path[512];
    snprintf(path, sizeof(path),
             "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,apparent_temperature,weather_code,is_day,wind_speed_10m"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&hourly=temperature_2m,weather_code,is_day&forecast_hours=11&forecast_days=1&timezone=auto",
             (double)WEATHER_LAT, (double)WEATHER_LON);
    HTTPClient http;
    http.begin(client, path);
    http.setTimeout(8000);
    int code = http.GET();
    if (code != 200) {
        http.end();
        Serial.printf("Weather: HTTP %d\n", code);
        return false;
    }
    String body = http.getString();
    http.end();
    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;
    WeatherData w;
    w.tempC = doc["current"]["temperature_2m"] | 0.0f;
    w.code = doc["current"]["weather_code"] | -1;
    w.isDay = (doc["current"]["is_day"] | 1) == 1;
    w.hiC = doc["daily"]["temperature_2m_max"][0] | 0.0f;
    w.loC = doc["daily"]["temperature_2m_min"][0] | 0.0f;
    w.feelsC = doc["current"]["apparent_temperature"] | 0.0f;
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
    w.valid = w.code >= 0;
    w.updatedMs = millis();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_weather = w;
    xSemaphoreGive(s_lock);
    Serial.printf("Weather: %.1f C code %d (H %.1f L %.1f)\n", w.tempC, w.code, w.hiC, w.loC);
    return w.valid;
}

static void netTask(void *) {
    WiFiClientSecure client;
    client.setInsecure(); // gateway uses a self-signed certificate
    for (;;) {
        if (s_wifiMulti.run(10000) != WL_CONNECTED) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_net.wifiUp = false;
            xSemaphoreGive(s_lock);
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
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_net.wifiUp = true;
        strlcpy(s_net.ssid, WiFi.SSID().c_str(), sizeof(s_net.ssid));
        s_net.rssi = WiFi.RSSI();
        xSemaphoreGive(s_lock);

        uint32_t start = millis();
        pollOnce(client);

        // Weather every 15 min (retry after 1 min on failure). Switching host closes the
        // gateway connection first, so only one TLS session exists at a time.
        static uint32_t nextWeather = 0;
        if ((int32_t)(millis() - nextWeather) >= 0) {
            nextWeather = millis() + (fetchWeather(client) ? 15 * 60 * 1000UL : 60 * 1000UL);
        }
        uint32_t spent = millis() - start;
        vTaskDelay(pdMS_TO_TICKS(spent < s_pollMs ? s_pollMs - spent : 100));
    }
}

void powerwallStart(uint32_t pollMs) {
    s_pollMs = pollMs;
    s_lock = xSemaphoreCreateMutex();
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("powerwall-display");
    for (const WifiCred &w : WIFI_NETWORKS) s_wifiMulti.addAP(w.ssid, w.password);
    configTzTime(TZ_INFO, "pool.ntp.org", "time.google.com");
    // Core 0 (Arduino loop/LVGL runs on core 1). TLS needs a generous stack.
    xTaskCreatePinnedToCore(netTask, "pw_net", 12288, nullptr, 1, nullptr, 0);
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
