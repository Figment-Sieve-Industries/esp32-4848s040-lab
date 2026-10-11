// Aurora (AuroraWatch UK), Kp (NOAA SWPC) and moonrise/moonset (MET Norway) - see sky.h.
// Called from the network task in powerwall.cpp, which schedules them (one TLS session at a time).
// Each fetch reads its body into a PSRAM buffer through a net::Session (HTTP/1.0, so no chunked
// bodies) and swaps the result in only when the response parses completely.
#include "sky.h"
#include "ca_certs.h"
#include "mem_util.h"
#include "net_http.h"
#include "powerwall.h"
#include "secrets.h"
#include "shared_state.h"
#include "time_util.h"
#include <Arduino.h>
#include <ArduinoJson.h>

namespace {

constexpr size_t BODY_BUF = 32 * 1024; // the largest body (Kp, the AuroraWatch activity XML) is a few KB

Shared<AuroraData> s_aurora;
Shared<KpData> s_kp;
Shared<MoonTimes> s_moon;

// AuroraWatch UK and MET Norway ask for an identifying User-Agent (net::USER_AGENT)
net::Options options(const char *ca) {
    net::Options o;
    o.tag = "Sky";
    o.ca = ca; // nullptr: plain HTTP (AuroraWatch)
    o.budgetMs = PW_JOB_BUDGET_MS;
    return o;
}

bool allocBody(HeapBuf &body) {
    if (body.psram(BODY_BUF)) return true;
    Serial.println("Sky: no PSRAM for the body");
    return false;
}

// One GET into body; false if it failed (logged by net::Session).
bool get(net::Session &s, const char *url, HeapBuf &body, size_t &len) {
    net::Response r;
    if (s.get(url, body.as<char>(), body.size, &r) != net::Result::Ok) return false;
    len = r.len;
    return true;
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
    HeapBuf body;
    if (!allocBody(body)) return false;
    net::Session s(options(nullptr));
    size_t len = 0;
    AuroraData a;
    if (!get(s, "http://aurorawatch-api.lancs.ac.uk/0.2/status/current-status.xml", body, len)) return false;
    const char *st = body.as<const char>();
    const char *site = strstr(st, "<site_status");
    a.level = site ? levelAttr(site, st + len) : AuroraLevel::Unknown;
    char buf[40];
    if (tagText(st, "datetime", buf, sizeof(buf))) a.updatedAt = tu::parseIso8601(buf);

    if (!get(s, "http://aurorawatch-api.lancs.ac.uk/0.2/status/alerting-site-activity.xml", body, len)) return false;
    s.close();
    const char *p = body.as<const char>();
    const char *end = p + len;
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
        all.activityHour[i] = tu::parseIso8601(dt);
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
    s_aurora.set(a);
    Serial.printf("Sky: aurora %s, last hour %.1f nT\n", auroraLevelName(a.level), a.activityNt[a.count - 1]);
    return true;
}

bool skyFetchKp() {
    HeapBuf body;
    if (!allocBody(body)) return false;
    size_t len = 0;
    {
        net::Session s(options(NOAA_CA));
        if (!get(s, "https://services.swpc.noaa.gov/products/noaa-planetary-k-index-forecast.json", body, len))
            return false;
    }

    JsonDocument filter(psramJson());
    filter[0]["time_tag"] = true;
    filter[0]["kp"] = true;
    filter[0]["observed"] = true;
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body.as<const char>(), len, DeserializationOption::Filter(filter)) ||
        !doc.is<JsonArray>()) {
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
        time_t t = tu::parseIso8601(e["time_tag"] | "");
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
    s_kp.set(k);
    Serial.printf("Sky: Kp now %.2f, %d slots\n", k.nowKp, filled);
    return true;
}

bool skyMoonNeeded() {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    const MoonTimes m = s_moon.get();
    return !m.valid || m.year != lt.tm_year || m.yday != lt.tm_yday;
}

bool skyFetchMoon() {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    const int offMin = tu::utcOffsetMinutes(now); // UK: +00:00 or +01:00
    char url[200];
    // MET Norway: at most 4 decimals of position
    snprintf(url, sizeof(url),
             "https://api.met.no/weatherapi/sunrise/3.0/moon?lat=%.4f&lon=%.4f&date=%04d-%02d-%02d&offset=%c%02d:%02d",
             (double)WEATHER_LAT, (double)WEATHER_LON, lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday,
             offMin < 0 ? '-' : '+', abs(offMin) / 60, abs(offMin) % 60);
    HeapBuf body;
    if (!allocBody(body)) return false;
    size_t len = 0;
    {
        net::Session s(options(MET_NO_CA));
        if (!get(s, url, body, len)) return false;
    }

    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body.as<const char>(), len) || !doc["properties"].is<JsonObject>()) {
        Serial.println("Sky: moon response did not parse");
        return false;
    }
    JsonObject p = doc["properties"];
    MoonTimes m;
    m.year = lt.tm_year;
    m.yday = lt.tm_yday;
    m.rise = tu::parseIso8601(p["moonrise"]["time"] | ""); // null -> "" -> 0 (no moonrise that day)
    m.set = tu::parseIso8601(p["moonset"]["time"] | "");
    m.riseAzDeg = p["moonrise"]["azimuth"] | -1.0f;
    m.setAzDeg = p["moonset"]["azimuth"] | -1.0f;
    m.valid = true;
    m.fetchedMs = millis();
    s_moon.set(m);
    Serial.printf("Sky: moonrise %s, moonset %s\n", (const char *)(p["moonrise"]["time"] | "--"),
                  (const char *)(p["moonset"]["time"] | "--"));
    return true;
}

AuroraData auroraData() { return s_aurora.get(); }

KpData kpData() { return s_kp.get(); }

MoonTimes moonTimes() { return s_moon.get(); }
