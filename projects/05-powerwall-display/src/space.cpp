// Space weather (NOAA SWPC) and the SOHO HMI sun picture - see space.h and docs/11.
// Called from the network task in powerwall.cpp (core 0), which serialises TLS sessions.
// Results are swapped in under lock() only at the end of each fetch.
#include "space.h"
#include "ca_certs.h"
#include "psram_json.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <src/libs/tjpgd/tjpgd.h> // LVGL's bundled TJpgDec (LV_USE_TJPGD 1; JD_FORMAT 0 = RGB888, no scaling)

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 3000;
constexpr uint32_t READ_TIMEOUT_MS = 5000;
constexpr uint32_t HANDSHAKE_TIMEOUT_S = 5;
constexpr uint32_t BODY_DEADLINE_MS = 8000;   // whole-body deadline for the small SWPC files
constexpr uint32_t IMAGE_DEADLINE_MS = 25000; // whole-body deadline for the JPEG
constexpr const char *USER_AGENT = "powerwall-display/1.0 (personal ESP32 display; Figment Sieve Industries)";

constexpr const char *SWPC = "https://services.swpc.noaa.gov";
constexpr const char *SUN_URL = "https://soho.nascom.nasa.gov/data/realtime/hmi_igr/512/latest.jpg";
constexpr size_t SWPC_BUF = 16 * 1024;     // largest body is solar_probabilities.json (~9 KB)
constexpr size_t JPEG_MAX = 256 * 1024;    // reject anything bigger
constexpr size_t JD_POOL = 8 * 1024;       // TJpgDec workspace (tables + stream buffer + MCU buffers)
constexpr int JD_MAX_DIM = 640;

constexpr int IMG_PIXELS = SUN_IMG_PX * SUN_IMG_PX;

// Solar disc inside the 512x512 HMI quick-look image, measured on 2026-10-11 with Python: the disc
// spans x 19..494 and y 18..493 (radius ~237.5 px) and the caption text sits at y >= 497, outside it.
// As fractions of the image size.
constexpr float DISC_CX = 0.500F;
constexpr float DISC_CY = 0.499F;
constexpr float DISC_R = 0.4635F;

SunData s_sun;
uint16_t *s_img[2] = {nullptr, nullptr};
int s_front = -1;
uint32_t s_version = 0;
time_t s_takenAt = 0;

SemaphoreHandle_t lock() {
    static SemaphoreHandle_t m = xSemaphoreCreateMutex(); // created on first use (thread-safe static init)
    return m;
}

struct Guard {
    Guard() { xSemaphoreTake(lock(), portMAX_DELAY); }
    ~Guard() { xSemaphoreGive(lock()); }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;
};

// malloc'd block freed on scope exit
struct Mem {
    void *p = nullptr;
    Mem() = default;
    Mem(const Mem &) = delete;
    Mem &operator=(const Mem &) = delete;
    ~Mem() {
        if (p) heap_caps_free(p);
    }
    void *psram(size_t n, bool zero = false) {
        p = zero ? heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                 : heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        return p;
    }
    void *internal(size_t n, bool zero = false) {
        p = zero ? heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                 : heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        return p;
    }
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

// SWPC "2026-10-11T10:38:00Z" (UTC) -> time_t, 0 if it doesn't parse
time_t parseIso(const char *s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0;
    if (!s || sscanf(s, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
    return (time_t)(daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + sec);
}

// HTTP date "Sun, 11 Oct 2026 10:56:05 GMT" -> time_t (UTC), 0 if it doesn't parse
time_t parseHttpDate(const char *s) {
    static const char *const MONTHS[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    if (!s || !*s) return 0;
    const char *comma = strchr(s, ',');
    const char *p = comma ? comma + 1 : s;
    int d = 0, y = 0, h = 0, mi = 0, sec = 0;
    char mon[4] = {};
    if (sscanf(p, " %2d %3s %4d %2d:%2d:%2d", &d, mon, &y, &h, &mi, &sec) != 6) return 0;
    for (int m = 0; m < 12; m++) {
        if (strncasecmp(mon, MONTHS[m], 3) == 0)
            return (time_t)(daysFromCivil(y, (unsigned)(m + 1), (unsigned)d) * 86400 + h * 3600 + mi * 60 + sec);
    }
    return 0;
}

void copyStr(char *dst, size_t n, const char *src) {
    snprintf(dst, n, "%.*s", (int)(n - 1), src ? src : "");
}

// ---------------------------------------------------------------------------------------------
// SWPC fetching

enum class Get { Ok, HttpError, NetError };

// GET path on the shared connection into buf (NUL-terminated). The body is read from the stream by
// Content-Length (SWPC sends it); an unknown length (chunked) falls back to getString(), which de-chunks.
Get swpcGet(HTTPClient &http, WiFiClientSecure &client, const char *path, char *buf, size_t cap, size_t &len) {
    char url[96];
    snprintf(url, sizeof(url), "%s%s", SWPC, path);
    len = 0;
    if (!http.begin(client, url)) return Get::NetError;
    http.setReuse(true); // keep-alive: one TLS handshake for all the files
    http.setUserAgent(USER_AGENT);
    http.setConnectTimeout(CONNECT_TIMEOUT_MS);
    http.setTimeout(READ_TIMEOUT_MS);
    const int code = http.GET();
    if (code != 200) {
        Serial.printf("Space: %s -> %s\n", path, code < 0 ? HTTPClient::errorToString(code).c_str() : String(code).c_str());
        http.end();
        if (code < 0) {
            client.stop();
            return Get::NetError;
        }
        return Get::HttpError;
    }
    bool ok = false;
    const int size = http.getSize();
    if (size >= 0) {
        if ((size_t)size < cap) {
            WiFiClient *s = http.getStreamPtr();
            const uint32_t deadline = millis() + BODY_DEADLINE_MS;
            size_t n = 0;
            while (n < (size_t)size && (int32_t)(millis() - deadline) < 0) {
                const int avail = s->available();
                if (avail > 0) {
                    const int r = s->read((uint8_t *)buf + n, (size_t)avail < (size_t)size - n ? (size_t)avail : (size_t)size - n);
                    if (r > 0) n += (size_t)r;
                } else if (!s->connected()) {
                    break;
                } else {
                    delay(2);
                }
            }
            len = n;
            ok = n == (size_t)size;
        }
    } else {
        String body = http.getString();
        if (body.length() < cap) {
            memcpy(buf, body.c_str(), body.length());
            len = body.length();
            ok = len > 0;
        }
    }
    buf[len] = '\0';
    http.end();
    if (!ok) {
        Serial.printf("Space: %s body incomplete (%u of %d B)\n", path, (unsigned)len, size);
        client.stop(); // the connection state is unknown, don't reuse it
        return Get::HttpError;
    }
    return Get::Ok;
}

// NOAA scale value: strings like "0" in the data, null in forecast rows; -1 = unknown
int scaleOf(JsonVariantConst v) {
    if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        if (!s || !*s) return -1;
        const int n = atoi(s);
        return n < 0 ? -1 : (n > 5 ? 5 : n);
    }
    if (v.is<int>()) {
        const int n = v.as<int>();
        return n < 0 ? -1 : (n > 5 ? 5 : n);
    }
    return -1;
}

bool parseScales(const char *buf, size_t len, SunData &d) {
    JsonDocument filter(psramJson());
    for (const char *k : {"R", "S", "G"}) filter["0"][k]["Scale"] = true;
    filter["1"]["G"]["Scale"] = true;
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, buf, len, DeserializationOption::Filter(filter)) || !doc["0"].is<JsonObject>()) {
        Serial.println("Space: scales did not parse");
        return false;
    }
    d.scaleR = (int8_t)scaleOf(doc["0"]["R"]["Scale"]);
    d.scaleS = (int8_t)scaleOf(doc["0"]["S"]["Scale"]);
    d.scaleG = (int8_t)scaleOf(doc["0"]["G"]["Scale"]);
    d.scaleGNext = (int8_t)scaleOf(doc["1"]["G"]["Scale"]);
    return d.scaleR >= 0 || d.scaleS >= 0 || d.scaleG >= 0;
}

bool parseFlares(const char *buf, size_t len, SunData &d) {
    JsonDocument filter(psramJson());
    filter[0]["time_tag"] = true;
    filter[0]["current_class"] = true;
    filter[0]["max_class"] = true;
    filter[0]["max_time"] = true;
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, buf, len, DeserializationOption::Filter(filter)) || !doc[0].is<JsonObject>()) {
        Serial.println("Space: flares did not parse");
        return false;
    }
    JsonVariantConst e = doc[0];
    const char *cur = e["current_class"] | "";
    if (!*cur) return false;
    copyStr(d.flareNow, sizeof(d.flareNow), cur);
    copyStr(d.flareMax, sizeof(d.flareMax), e["max_class"] | "");
    d.flareMaxAt = parseIso(e["max_time"] | "");
    d.updatedAt = parseIso(e["time_tag"] | "");
    return true;
}

int columnOf(JsonArrayConst hdr, const char *name) {
    int i = 0;
    for (JsonVariantConst h : hdr) {
        const char *s = h | "";
        if (!strcmp(s, name)) return i;
        i++;
    }
    return -1;
}

bool numAt(JsonArrayConst row, int col, float &out) {
    if (col < 0) return false;
    JsonVariantConst v = row[col];
    if (!v.is<float>()) return false; // null or string -> no value
    out = v.as<float>();
    return true;
}

// array of arrays, header row first: ["time_tag","speed","density",...,"bz",...]; oldest row first
bool parseWind(const char *buf, size_t len, SunData &d) {
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, buf, len) || !doc.is<JsonArray>() || doc.size() < 2) {
        Serial.println("Space: wind did not parse");
        return false;
    }
    JsonArrayConst rows = doc.as<JsonArrayConst>();
    JsonArrayConst hdr = rows[0];
    const int cSpeed = columnOf(hdr, "speed");
    const int cDens = columnOf(hdr, "density");
    const int cBz = columnOf(hdr, "bz");
    if (cSpeed < 0) {
        Serial.println("Space: wind has no speed column");
        return false;
    }
    float v = 0;
    int total = 0; // rows with a speed
    for (size_t r = 1; r < rows.size(); r++) {
        if (numAt(rows[r], cSpeed, v)) total++;
    }
    if (total == 0) return false;
    int skip = total > SunData::WIND_POINTS ? total - SunData::WIND_POINTS : 0;
    int n = 0;
    float lastSpeed = -1, lastDens = -1, lastBz = 0;
    bool haveBz = false;
    for (size_t r = 1; r < rows.size(); r++) {
        JsonArrayConst row = rows[r];
        float speed = 0, dens = 0, bz = 0;
        const bool hasSpeed = numAt(row, cSpeed, speed);
        if (numAt(row, cDens, dens)) lastDens = dens;
        const bool hasBz = numAt(row, cBz, bz);
        if (hasBz) {
            lastBz = bz;
            haveBz = true;
        }
        if (!hasSpeed) continue;
        lastSpeed = speed;
        if (skip > 0) {
            skip--;
            continue;
        }
        int bzi = hasBz ? (int)lroundf(bz) : 0;
        bzi = bzi < -127 ? -127 : (bzi > 127 ? 127 : bzi);
        d.windSpeed[n] = (int16_t)lroundf(speed);
        d.windBz[n] = (int8_t)bzi;
        n++;
    }
    d.windCount = n;
    d.windKms = lastSpeed;
    d.densityCc = lastDens;
    d.bzNt = lastBz;
    d.bzValid = haveBz;
    return n > 0;
}

// today's row: the one with the newest "date" (the file is newest-first, ~30 rows)
bool parseProbabilities(const char *buf, size_t len, SunData &d) {
    JsonDocument filter(psramJson());
    filter[0]["date"] = true;
    filter[0]["c_class_1_day"] = true;
    filter[0]["m_class_1_day"] = true;
    filter[0]["x_class_1_day"] = true;
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, buf, len, DeserializationOption::Filter(filter)) || !doc.is<JsonArray>()) {
        Serial.println("Space: probabilities did not parse");
        return false;
    }
    JsonVariantConst best;
    const char *bestDate = "";
    for (JsonVariantConst row : doc.as<JsonArrayConst>()) {
        const char *date = row["date"] | "";
        if (best.isNull() || strcmp(date, bestDate) > 0) {
            best = row;
            bestDate = date;
        }
    }
    if (best.isNull()) return false;
    auto pct = [](JsonVariantConst v) -> int8_t {
        if (!v.is<int>()) return -1;
        const int p = v.as<int>();
        return (int8_t)(p < 0 ? -1 : (p > 100 ? 100 : p));
    };
    d.probC = pct(best["c_class_1_day"]);
    d.probM = pct(best["m_class_1_day"]);
    d.probX = pct(best["x_class_1_day"]);
    return d.probC >= 0 || d.probM >= 0 || d.probX >= 0;
}

// daily-solar-indices.txt: ":" and "#" comment lines, then
// "2026 10 10  126  99  690  0 ..." = year month day F10.7 SESC-sunspot-number area ... Take the last row
// that has each value (an unavailable value is -1/-999).
bool parseIndices(const char *buf, size_t len, SunData &d) {
    const char *p = buf;
    const char *end = buf + len;
    int ssn = -1, f107 = -1;
    while (p < end) {
        const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
        const size_t n = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (n > 0 && n < 120 && *p >= '0' && *p <= '9') {
            char line[120];
            memcpy(line, p, n);
            line[n] = '\0';
            int y = 0, mo = 0, dd = 0, flux = 0, sn = 0;
            if (sscanf(line, "%d %d %d %d %d", &y, &mo, &dd, &flux, &sn) == 5 && y > 2000) {
                if (flux > 0) f107 = flux;
                if (sn >= 0) ssn = sn;
            }
        }
        p += n + 1;
    }
    if (ssn < 0 && f107 < 0) {
        Serial.println("Space: indices did not parse");
        return false;
    }
    d.sunspots = ssn;
    d.f107 = f107;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Sun picture

// Warm "sun" palette: brightness 0..255 -> RGB565 (black, deep red, orange, yellow, near white)
struct Stop {
    uint8_t t, r, g, b;
};
constexpr Stop STOPS[] = {{0, 0, 0, 0},       {40, 70, 6, 0},      {110, 190, 50, 0},
                          {180, 255, 130, 10}, {215, 255, 190, 50}, {255, 255, 245, 200}};
constexpr int REF_T = 205; // the typical disc brightness maps here (orange-yellow)

uint16_t rgb565(int r, int g, int b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void buildLut(uint16_t lut[256]) {
    int s = 0;
    for (int t = 0; t < 256; t++) {
        while (s < (int)(sizeof(STOPS) / sizeof(STOPS[0])) - 2 && t > STOPS[s + 1].t) s++;
        const Stop &a = STOPS[s];
        const Stop &b = STOPS[s + 1];
        const int span = b.t - a.t;
        const int k = t - a.t;
        lut[t] = rgb565(a.r + (b.r - a.r) * k / span, a.g + (b.g - a.g) * k / span, a.b + (b.b - a.b) * k / span);
    }
}

// Decoder state (heap, not on the task stack)
struct SunDecode {
    JDEC jd;
    const uint8_t *src;
    size_t len;
    size_t pos;
    uint16_t *sum; // IMG_PIXELS: sum of luminance of the source pixels falling in each output cell
    uint8_t *cnt;  // IMG_PIXELS: number of source pixels in each cell
    uint8_t xmap[JD_MAX_DIM]; // source column -> output column
    int height;
};

size_t jdIn(JDEC *jd, uint8_t *out, size_t n) {
    SunDecode *c = (SunDecode *)jd->device;
    const size_t left = c->len - c->pos;
    if (n > left) n = left;
    if (out) memcpy(out, c->src + c->pos, n);
    c->pos += n;
    return n;
}

int jdOut(JDEC *jd, void *bitmap, JRECT *rect) {
    SunDecode *c = (SunDecode *)jd->device;
    const uint8_t *px = (const uint8_t *)bitmap; // RGB888, rows of (right-left+1) pixels
    const int w = rect->right - rect->left + 1;
    for (int y = rect->top; y <= rect->bottom && y < c->height; y++) {
        const int base = (y * SUN_IMG_PX / c->height) * SUN_IMG_PX;
        for (int x = 0; x < w; x++, px += 3) {
            const int idx = base + c->xmap[rect->left + x];
            c->sum[idx] = (uint16_t)(c->sum[idx] + ((px[0] * 77 + px[1] * 151 + px[2] * 28) >> 8));
            c->cnt[idx]++;
        }
    }
    return 1;
}

bool ensureImageBuffers() {
    if (s_img[0] && s_img[1]) return true;
    for (auto &b : s_img) {
        if (!b) b = (uint16_t *)heap_caps_malloc(IMG_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (s_img[0] && s_img[1]) return true;
    Serial.println("Space: no PSRAM for the sun image");
    return false;
}

// Area-average the decoded picture into dst (SUN_IMG_PX^2 RGB565) and tint it, black outside the disc
void tintImage(const SunDecode *c, uint16_t *dst) {
    const float cx = SUN_IMG_PX * DISC_CX;
    const float cy = SUN_IMG_PX * DISC_CY;
    const float ro = SUN_IMG_PX * DISC_R;
    const float ro2 = ro * ro;
    uint32_t hist[256] = {};
    uint32_t discCells = 0;
    // pass 1: average luminance per cell (0 outside the disc), histogram of the disc
    for (int oy = 0; oy < SUN_IMG_PX; oy++) {
        const float dy = (float)oy + 0.5F - cy;
        for (int ox = 0; ox < SUN_IMG_PX; ox++) {
            const int idx = oy * SUN_IMG_PX + ox;
            const float dx = (float)ox + 0.5F - cx;
            if (dx * dx + dy * dy > ro2 || c->cnt[idx] == 0) {
                dst[idx] = 0;
                continue;
            }
            const uint32_t lum = c->sum[idx] / c->cnt[idx];
            dst[idx] = (uint16_t)lum;
            hist[lum]++;
            discCells++;
        }
    }
    // reference brightness = 95th percentile of the disc, so a dim image (EIT) is stretched too
    uint32_t ref = 255, acc = 0;
    for (uint32_t v = 0; v < 256; v++) {
        acc += hist[v];
        if (acc * 100 >= discCells * 95) {
            ref = v;
            break;
        }
    }
    if (ref < 24) ref = 24;
    uint16_t lut[256];
    buildLut(lut);
    // pass 2: brightness -> colour
    for (int i = 0; i < IMG_PIXELS; i++) {
        const uint32_t v = dst[i];
        if (v == 0) continue;
        uint32_t t = v * REF_T / ref;
        if (t > 255) t = 255;
        dst[i] = lut[t];
    }
}

} // namespace

SunData sunData() {
    Guard g;
    return s_sun;
}

const uint16_t *sunImage(uint32_t &version, time_t &takenAt) {
    Guard g;
    version = s_version;
    takenAt = s_takenAt;
    return s_front >= 0 ? s_img[s_front] : nullptr;
}

bool spaceFetchSwpc() {
    Mem bufMem;
    char *buf = (char *)bufMem.psram(SWPC_BUF);
    if (!buf) {
        Serial.println("Space: no PSRAM for the SWPC buffer");
        return false;
    }
    SunData d = sunData(); // start from the previous values so a failed file keeps its old numbers
    bool gotScales = false, gotFlares = false, gotAny = false, net = true;
    {
        WiFiClientSecure client;
        client.setCACert(NOAA_CA);
        client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
        HTTPClient http;
        size_t len = 0;
        auto get = [&](const char *path) -> bool {
            if (!net) return false; // connection failed earlier: don't wait for more timeouts
            const Get r = swpcGet(http, client, path, buf, SWPC_BUF, len);
            if (r == Get::NetError) net = false;
            return r == Get::Ok;
        };
        if (get("/products/noaa-scales.json")) gotScales = parseScales(buf, len, d);
        if (get("/json/goes/primary/xray-flares-latest.json")) gotFlares = parseFlares(buf, len, d);
        if (get("/products/geospace/propagated-solar-wind-1-hour.json")) gotAny |= parseWind(buf, len, d);
        if (get("/json/solar_probabilities.json")) gotAny |= parseProbabilities(buf, len, d);
        if (get("/text/daily-solar-indices.txt")) gotAny |= parseIndices(buf, len, d);
        client.stop();
    }
    const bool fresh = gotScales && gotFlares;
    if (!fresh && !gotScales && !gotFlares && !gotAny) {
        Serial.println("Space: SWPC fetch failed");
        return false;
    }
    if (fresh) {
        d.valid = true;
        d.fetchedMs = millis();
    }
    {
        Guard g;
        s_sun = d;
    }
    if (!fresh) {
        Serial.printf("Space: partial SWPC fetch (scales %d, flares %d)\n", gotScales, gotFlares);
        return false;
    }
    char wind[24] = "--", bz[16] = "--";
    if (d.windKms >= 0) snprintf(wind, sizeof(wind), "%.0f", (double)d.windKms);
    if (d.bzValid) snprintf(bz, sizeof(bz), "%+.1f", (double)d.bzNt);
    Serial.printf("Space: flare %s (max %s), R%d S%d G%d, wind %s km/s, Bz %s, SSN %d\n", d.flareNow, d.flareMax,
                  d.scaleR, d.scaleS, d.scaleG, wind, bz, d.sunspots);
    return true;
}

bool spaceFetchSunImage() {
    if (!ensureImageBuffers()) return false;

    // ---- download the JPEG into PSRAM ----
    Mem jpgMem;
    uint8_t *jpg = nullptr;
    size_t jpgLen = 0;
    time_t takenAt = 0;
    {
        WiFiClientSecure client;
        client.setCACert(OPEN_METEO_CA); // ISRG Root X1 + YR (Let's Encrypt), same chain as soho.nascom.nasa.gov
        client.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
        HTTPClient http;
        if (!http.begin(client, SUN_URL)) return false;
        http.setReuse(false);
        http.setUserAgent(USER_AGENT);
        http.setConnectTimeout(CONNECT_TIMEOUT_MS);
        http.setTimeout(READ_TIMEOUT_MS);
        http.useHTTP10(true); // no chunked encoding: the body is the JPEG, ended by Content-Length or close
        const char *keys[] = {"Last-Modified"};
        http.collectHeaders(keys, 1);
        const int code = http.GET();
        if (code != 200) {
            Serial.printf("Space: sun image -> %s\n", code < 0 ? HTTPClient::errorToString(code).c_str() : String(code).c_str());
            http.end();
            return false;
        }
        const int size = http.getSize();
        if (size > (int)JPEG_MAX) {
            Serial.printf("Space: sun image too big (%d B)\n", size);
            http.end();
            return false;
        }
        const size_t want = size > 0 ? (size_t)size : JPEG_MAX; // unknown length: read until the server closes
        jpg = (uint8_t *)jpgMem.psram(want);
        if (!jpg) {
            Serial.println("Space: no PSRAM for the sun JPEG");
            http.end();
            return false;
        }
        takenAt = parseHttpDate(http.header("Last-Modified").c_str());
        WiFiClient *s = http.getStreamPtr();
        const uint32_t deadline = millis() + IMAGE_DEADLINE_MS;
        while (jpgLen < want && (int32_t)(millis() - deadline) < 0) {
            const int avail = s->available();
            if (avail > 0) {
                const size_t room = want - jpgLen;
                const int r = s->read(jpg + jpgLen, (size_t)avail < room ? (size_t)avail : room);
                if (r > 0) jpgLen += (size_t)r;
            } else if (!s->connected()) {
                break;
            } else {
                delay(2);
            }
        }
        http.end();
        client.stop(); // free the TLS buffers before decoding
        if (size > 0 ? jpgLen != (size_t)size : jpgLen < 1024) {
            Serial.printf("Space: sun image incomplete (%u of %d B)\n", (unsigned)jpgLen, size);
            return false;
        }
    }
    if (jpgLen < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) {
        Serial.println("Space: sun image is not a JPEG");
        return false;
    }

    // ---- decode and area-average to SUN_IMG_PX ----
    const uint32_t t0 = millis();
    Mem ctxMem, poolMem, accMem;
    SunDecode *c = (SunDecode *)ctxMem.internal(sizeof(SunDecode), true);
    void *pool = poolMem.internal(JD_POOL);
    uint8_t *acc = (uint8_t *)accMem.psram(IMG_PIXELS * (sizeof(uint16_t) + sizeof(uint8_t)), true);
    if (!c || !pool || !acc) {
        Serial.println("Space: no memory to decode the sun image");
        return false;
    }
    c->src = jpg;
    c->len = jpgLen;
    c->sum = (uint16_t *)acc;
    c->cnt = acc + IMG_PIXELS * sizeof(uint16_t);
    JRESULT jr = jd_prepare(&c->jd, jdIn, pool, JD_POOL, c);
    if (jr != JDR_OK) {
        Serial.printf("Space: sun JPEG prepare failed (%d; progressive or unsupported?)\n", (int)jr);
        return false;
    }
    if (c->jd.width < 64 || c->jd.width > JD_MAX_DIM || c->jd.height < 64 || c->jd.height > JD_MAX_DIM) {
        Serial.printf("Space: unexpected sun JPEG size %dx%d\n", c->jd.width, c->jd.height);
        return false;
    }
    c->height = c->jd.height;
    for (int x = 0; x < c->jd.width; x++) c->xmap[x] = (uint8_t)(x * SUN_IMG_PX / c->jd.width);
    jr = jd_decomp(&c->jd, jdOut, 0);
    if (jr != JDR_OK) {
        Serial.printf("Space: sun JPEG decode failed (%d)\n", (int)jr);
        return false;
    }

    int back;
    {
        Guard g;
        back = s_front == 0 ? 1 : 0; // the screen may still be showing the front buffer
    }
    tintImage(c, s_img[back]);
    const uint32_t ms = millis() - t0;
    {
        Guard g;
        s_front = back;
        s_version++;
        s_takenAt = takenAt;
    }
    Serial.printf("Space: sun image %u B, decoded in %lu ms\n", (unsigned)jpgLen, (unsigned long)ms);
    return true;
}
