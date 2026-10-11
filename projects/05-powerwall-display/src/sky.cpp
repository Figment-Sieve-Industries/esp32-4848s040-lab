// Aurora (AuroraWatch UK), Kp (NOAA SWPC) and moonrise/moonset (MET Norway) - see sky.h.
// Called from the network task in powerwall.cpp, which schedules them (one TLS session at a time).
// Results are swapped in under the lock only when a response parses completely.
#include "sky.h"
#include "ca_certs.h"
#include "psram_json.h"
#include "secrets.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t READ_TIMEOUT_MS = 5000;
constexpr uint32_t HANDSHAKE_TIMEOUT_S = 5;
// AuroraWatch UK and MET Norway ask for an identifying User-Agent
constexpr const char *USER_AGENT = "powerwall-display/1.0 (personal ESP32 display; Figment Sieve Industries)";

AuroraData s_aurora;
KpData s_kp;
MoonTimes s_moon;

SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex(); // created on first use (thread-safe static init)
    return m;
}

struct Guard {
    Guard() { xSemaphoreTake(lock(), portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(lock()); }
};

// days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant's days_from_civil)
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

// "2026-10-10T17:00:00+0000", "2026-10-11T08:23+01:00", "2026-10-04T00:00:00" (UTC if no offset).
// Returns 0 if it doesn't parse.
time_t parseIso(const char *s) {
    int y, mo, d, h, mi, sec = 0, n = 0;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d%n", &y, &mo, &d, &h, &mi, &n) != 5) return 0;
    const char *p = s + n;
    if (*p == ':') {
        sec = atoi(p + 1);
        p += 3;
    }
    int off = 0; // seconds east of UTC
    if (*p == '+' || *p == '-') {
        int sign = *p == '-' ? -1 : 1;
        int oh = 0, om = 0;
        if (sscanf(p + 1, "%2d:%2d", &oh, &om) != 2) sscanf(p + 1, "%2d%2d", &oh, &om);
        off = sign * (oh * 3600 + om * 60);
    }
    return (time_t)(daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec - off);
}

AuroraLevel levelFrom(const char *id, size_t n) {
    if (n == 5 && !strncmp(id, "green", 5)) return AuroraLevel::Green;
    if (n == 6 && !strncmp(id, "yellow", 6)) return AuroraLevel::Yellow;
    if (n == 5 && !strncmp(id, "amber", 5)) return AuroraLevel::Amber;
    if (n == 3 && !strncmp(id, "red", 3)) return AuroraLevel::Red;
    return AuroraLevel::Unknown;
}

// Value of attribute `attr` (e.g. "status_id=\"") starting at or after p, up to the closing quote
AuroraLevel levelAttr(const char *p, const char *end) {
    const char *a = strstr(p, "status_id=\"");
    if (!a || a > end) return AuroraLevel::Unknown;
    a += 11;
    const char *q = strchr(a, '"');
    return q ? levelFrom(a, q - a) : AuroraLevel::Unknown;
}

// Text between <tag> and </tag> after p, into out. Returns the position after the close tag.
const char *tagText(const char *p, const char *tag, char *out, size_t n) {
    char open[24], close[24];
    snprintf(open, sizeof(open), "<%s>", tag);
    snprintf(close, sizeof(close), "</%s>", tag);
    const char *a = strstr(p, open);
    if (!a) return nullptr;
    a += strlen(open);
    const char *b = strstr(a, close);
    if (!b) return nullptr;
    size_t len = (size_t)(b - a) < n - 1 ? (size_t)(b - a) : n - 1;
    memcpy(out, a, len);
    out[len] = '\0';
    return b + strlen(close);
}

bool httpGet(WiFiClient &client, const char *url, String &body) { // WiFiClientSecure is one too
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    http.setReuse(false);
    http.setUserAgent(USER_AGENT);
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(READ_TIMEOUT_MS);
    // HTTP/1.0: no chunked transfer encoding (a chunked keep-alive response once stalled getString())
    http.useHTTP10(true);
    int code = http.GET();
    if (code == 200) body = http.getString();
    http.end();
    if (code != 200) {
        Serial.printf("Sky: %s -> %s\n", url, code < 0 ? HTTPClient::errorToString(code).c_str() : String(code).c_str());
        return false;
    }
    return true;
}

} // namespace

const char *auroraLevelName(AuroraLevel l) {
    switch (l) {
    case AuroraLevel::Green: return "No significant activity";
    case AuroraLevel::Yellow: return "Minor geomagnetic activity";
    case AuroraLevel::Amber: return "Amber alert: possible aurora";
    case AuroraLevel::Red: return "Red alert: aurora likely";
    default: return "--";
    }
}

uint32_t auroraLevelColor(AuroraLevel l) {
    switch (l) {
    case AuroraLevel::Green: return 0x33FF33;
    case AuroraLevel::Yellow: return 0xFFFF00;
    case AuroraLevel::Amber: return 0xFF9900;
    case AuroraLevel::Red: return 0xFF0000;
    default: return 0x8B98A5;
    }
}

bool skyFetchAurora() {
    WiFiClient client;
    String status, activity;
    if (!httpGet(client, "http://aurorawatch-api.lancs.ac.uk/0.2/status/current-status.xml", status)) return false;
    client.stop();
    if (!httpGet(client, "http://aurorawatch-api.lancs.ac.uk/0.2/status/alerting-site-activity.xml", activity))
        return false;
    client.stop();

    AuroraData a;
    const char *s = status.c_str();
    const char *site = strstr(s, "<site_status");
    a.level = site ? levelAttr(site, s + status.length()) : AuroraLevel::Unknown;
    char buf[40];
    if (tagText(s, "datetime", buf, sizeof(buf))) a.updatedAt = parseIso(buf);

    const char *p = activity.c_str();
    const char *end = p + activity.length();
    // Lower thresholds: <lower_threshold status_id="yellow">50</lower_threshold>
    for (const char *t = strstr(p, "<lower_threshold"); t; t = strstr(t + 1, "<lower_threshold")) {
        AuroraLevel l = levelAttr(t, end);
        const char *gt = strchr(t, '>');
        if (l != AuroraLevel::Unknown && gt) a.thresholdNt[(int)l] = (float)atof(gt + 1);
    }
    // <activity status_id="green"><datetime>...</datetime><value>17.4</value></activity>, oldest first.
    // Keep the newest 24.
    AuroraData all;
    int total = 0;
    for (const char *t = strstr(p, "<activity "); t; t = strstr(t + 1, "<activity ")) {
        const char *close = strstr(t, "</activity>");
        if (!close) break;
        char dt[40], val[16];
        if (!tagText(t, "datetime", dt, sizeof(dt)) || !tagText(t, "value", val, sizeof(val))) break;
        int i = total % 24; // ring buffer of the newest 24
        all.activityHour[i] = parseIso(dt);
        all.activityNt[i] = (float)atof(val);
        all.activityLevel[i] = levelAttr(t, close);
        total++;
        t = close;
    }
    if (a.level == AuroraLevel::Unknown || total == 0) {
        Serial.println("Sky: AuroraWatch response incomplete");
        return false;
    }
    a.count = total < 24 ? total : 24;
    int first = total < 24 ? 0 : total % 24;
    for (int k = 0; k < a.count; k++) {
        int i = (first + k) % 24;
        a.activityHour[k] = all.activityHour[i];
        a.activityNt[k] = all.activityNt[i];
        a.activityLevel[k] = all.activityLevel[i];
    }
    a.valid = true;
    a.fetchedMs = millis();
    {
        Guard g;
        s_aurora = a;
    }
    Serial.printf("Sky: aurora %s, last hour %.1f nT\n", auroraLevelName(a.level), a.activityNt[a.count - 1]);
    return true;
}

bool skyFetchKp() {
    WiFiClientSecure client;
    client.setCACert(NOAA_CA);
    client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    String body;
    bool ok = httpGet(client, "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json", body);
    client.stop();
    if (!ok) return false;

    JsonDocument filter(psramJson());
    filter[0]["time_tag"] = true;
    filter[0]["kp"] = true;
    filter[0]["observed"] = true;
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter)) || !doc.is<JsonArray>()) {
        Serial.println("Sky: Kp response did not parse");
        return false;
    }
    time_t now = time(nullptr);
    KpData k;
    k.day0 = now - now % 86400;
    k.nowSlot = (int)((now - k.day0) / (3 * 3600));
    time_t nowKpAt = 0;
    int filled = 0;
    for (JsonObject e : doc.as<JsonArray>()) {
        time_t t = parseIso(e["time_tag"] | "");
        if (!t || !e["kp"].is<float>()) continue;
        float kp = e["kp"];
        const char *kind = e["observed"] | "";
        KpKind kk = !strcmp(kind, "observed")    ? KpKind::Observed
                    : !strcmp(kind, "estimated") ? KpKind::Estimated
                                                 : KpKind::Predicted;
        if (kk != KpKind::Predicted && t <= now && t >= nowKpAt) {
            nowKpAt = t;
            k.nowKp = kp;
        }
        if (t < k.day0) continue;
        int slot = (int)((t - k.day0) / (3 * 3600));
        if (slot < 0 || slot >= KP_SLOTS) continue;
        k.kp[slot] = kp;
        k.kind[slot] = kk;
        filled++;
    }
    if (filled == 0) {
        Serial.println("Sky: Kp response has no slots for today");
        return false;
    }
    k.valid = true;
    k.fetchedMs = millis();
    {
        Guard g;
        s_kp = k;
    }
    Serial.printf("Sky: Kp now %.2f, %d slots\n", k.nowKp, filled);
    return true;
}

bool skyMoonNeeded() {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    Guard g;
    return !s_moon.valid || s_moon.year != lt.tm_year || s_moon.yday != lt.tm_yday;
}

bool skyFetchMoon() {
    time_t now = time(nullptr);
    struct tm lt, ut;
    localtime_r(&now, &lt);
    gmtime_r(&now, &ut);
    // Local UTC offset (UK: +00:00 or +01:00), from the difference of the two calendar readings
    int64_t localSec = daysFromCivil(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) * 86400 + lt.tm_hour * 3600 +
                       lt.tm_min * 60;
    int64_t utcSec = daysFromCivil(ut.tm_year + 1900, ut.tm_mon + 1, ut.tm_mday) * 86400 + ut.tm_hour * 3600 +
                     ut.tm_min * 60;
    int offMin = (int)((localSec - utcSec) / 60);
    char url[200];
    // MET Norway: at most 4 decimals of position
    snprintf(url, sizeof(url),
             "https://api.met.no/weatherapi/sunrise/3.0/moon?lat=%.4f&lon=%.4f&date=%04d-%02d-%02d&offset=%c%02d:%02d",
             (double)WEATHER_LAT, (double)WEATHER_LON, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             offMin < 0 ? '-' : '+', abs(offMin) / 60, abs(offMin) % 60);
    WiFiClientSecure client;
    client.setCACert(MET_NO_CA);
    client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    String body;
    bool ok = httpGet(client, url, body);
    client.stop();
    if (!ok) return false;

    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body) || !doc["properties"].is<JsonObject>()) {
        Serial.println("Sky: moon response did not parse");
        return false;
    }
    JsonObject p = doc["properties"];
    MoonTimes m;
    m.year = lt.tm_year;
    m.yday = lt.tm_yday;
    m.rise = parseIso(p["moonrise"]["time"] | ""); // null -> "" -> 0 (no moonrise that day)
    m.set = parseIso(p["moonset"]["time"] | "");
    m.riseAzDeg = p["moonrise"]["azimuth"] | -1.0f;
    m.setAzDeg = p["moonset"]["azimuth"] | -1.0f;
    m.valid = true;
    m.fetchedMs = millis();
    {
        Guard g;
        s_moon = m;
    }
    Serial.printf("Sky: moonrise %s, moonset %s\n", (const char *)(p["moonrise"]["time"] | "--"),
                  (const char *)(p["moonset"]["time"] | "--"));
    return true;
}

AuroraData auroraData() {
    Guard g;
    return s_aurora;
}

KpData kpData() {
    Guard g;
    return s_kp;
}

MoonTimes moonTimes() {
    Guard g;
    return s_moon;
}
