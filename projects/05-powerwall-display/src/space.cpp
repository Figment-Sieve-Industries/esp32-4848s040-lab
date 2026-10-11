// Space weather (NOAA SWPC) and the SOHO HMI sun picture - see space.h and docs/11.
// Called from the network task in powerwall.cpp (core 0), which serialises TLS sessions.
// Results are swapped in under a lock only at the end of each fetch.
#include "space.h"
#include "ca_certs.h"
#include "mem_util.h"
#include "net_http.h"
#include "powerwall.h"
#include "shared_state.h"
#include "time_util.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <src/libs/tjpgd/tjpgd.h> // LVGL's bundled TJpgDec (LV_USE_TJPGD 1; JD_FORMAT 0 = RGB888, no scaling)

namespace {

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

Shared<SunData> s_sun;
// Sun image double buffer: the front index, version and time change together under s_imgLock
Mutex s_imgLock;
uint16_t *s_img[2] = {nullptr, nullptr};
int s_front = -1;
uint32_t s_version = 0;
time_t s_takenAt = 0;

// ---------------------------------------------------------------------------------------------
// SWPC parsing

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
    d.flareMaxAt = tu::parseIso8601(e["max_time"] | "");
    d.updatedAt = tu::parseIso8601(e["time_tag"] | "");
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

/// Decoder state (heap, not on the task stack). The area average is accumulated in a small band of
// output rows (internal RAM): TJpgDec delivers MCUs in raster order, so once an MCU row starts at
// source row `top`, every output row above top's has all its pixels and is written out (as average
// luminance) to the PSRAM image in one sequential pass.
struct SunDecode {
    JDEC jd;
    const uint8_t *src;
    size_t len;
    size_t pos;
    uint16_t *out;     // IMG_PIXELS: average luminance per output cell (0 = no source pixels)
    uint16_t *bandSum; // bandRows x SUN_IMG_PX: luminance sums of the output rows in progress (ring)
    uint8_t *bandCnt;  // bandRows x SUN_IMG_PX: source pixels per cell
    int bandRows;
    int doneRows;      // output rows < doneRows are final in `out`
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

// Writes output rows [doneRows, upTo) to c->out and clears their band slots
void flushRows(SunDecode *c, int upTo) {
    for (; c->doneRows < upTo; c->doneRows++) {
        const int slot = (c->doneRows % c->bandRows) * SUN_IMG_PX;
        uint16_t *dst = c->out + c->doneRows * SUN_IMG_PX;
        for (int x = 0; x < SUN_IMG_PX; x++) {
            const uint8_t n = c->bandCnt[slot + x];
            dst[x] = n ? (uint16_t)(c->bandSum[slot + x] / n) : 0;
        }
        memset(c->bandSum + slot, 0, SUN_IMG_PX * sizeof(uint16_t));
        memset(c->bandCnt + slot, 0, SUN_IMG_PX);
    }
}

int jdOut(JDEC *jd, void *bitmap, JRECT *rect) {
    SunDecode *c = (SunDecode *)jd->device;
    const uint8_t *px = (const uint8_t *)bitmap; // RGB888, rows of (right-left+1) pixels
    const int w = rect->right - rect->left + 1;
    flushRows(c, rect->top * SUN_IMG_PX / c->height);
    const int last = (rect->bottom < c->height ? rect->bottom : c->height - 1) * SUN_IMG_PX / c->height;
    if (last - c->doneRows >= c->bandRows) return 0; // band too small (can't happen for baseline JPEGs): abort
    for (int y = rect->top; y <= rect->bottom && y < c->height; y++) {
        const int base = ((y * SUN_IMG_PX / c->height) % c->bandRows) * SUN_IMG_PX;
        for (int x = 0; x < w; x++, px += 3) {
            const int idx = base + c->xmap[rect->left + x];
            c->bandSum[idx] = (uint16_t)(c->bandSum[idx] + ((px[0] * 77 + px[1] * 151 + px[2] * 28) >> 8));
            c->bandCnt[idx]++;
        }
    }
    return 1;
}

bool ensureImageBuffers() {
    if (s_img[0] && s_img[1]) return true;
    for (auto &b : s_img) {
        if (!b) b = static_cast<uint16_t *>(psramAlloc(IMG_PIXELS * sizeof(uint16_t)));
    }
    if (s_img[0] && s_img[1]) return true;
    Serial.println("Space: no PSRAM for the sun image");
    return false;
}

// Tint the averaged luminance in img (SUN_IMG_PX^2) into RGB565 in place: black outside the solar
// disc, and SUN_IMG_BG_RGB565 at radius >= SUN_IMG_PX / 2 from the image centre (the round holder's
// corners, so the screen needs no clip_corner layer).
void tintImage(uint16_t *img) {
    const float cx = SUN_IMG_PX * DISC_CX;
    const float cy = SUN_IMG_PX * DISC_CY;
    const float ro = SUN_IMG_PX * DISC_R;
    const float ro2 = ro * ro;
    const float hc = SUN_IMG_PX * 0.5F; // holder circle: centre and radius
    const float hr2 = hc * hc;
    uint32_t hist[256] = {};
    uint32_t discCells = 0;
    // pass 1: mask (0 outside the disc, background in the corners), histogram of the disc
    for (int oy = 0; oy < SUN_IMG_PX; oy++) {
        const float dy = (float)oy + 0.5F - cy;
        const float hy = (float)oy + 0.5F - hc;
        for (int ox = 0; ox < SUN_IMG_PX; ox++) {
            const int idx = oy * SUN_IMG_PX + ox;
            const float dx = (float)ox + 0.5F - cx;
            const float hx = (float)ox + 0.5F - hc;
            if (hx * hx + hy * hy >= hr2) {
                img[idx] = SUN_IMG_BG_RGB565;
                continue;
            }
            if (dx * dx + dy * dy > ro2) {
                img[idx] = 0;
                continue;
            }
            const uint32_t lum = img[idx] > 255 ? 255 : img[idx];
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
    // pass 2: brightness -> colour (the corners are > 255 and stay as they are; 0 stays black)
    for (int i = 0; i < IMG_PIXELS; i++) {
        const uint32_t v = img[i];
        if (v == 0 || v > 255) continue;
        uint32_t t = v * REF_T / ref;
        if (t > 255) t = 255;
        img[i] = lut[t];
    }
}

static_assert(SUN_IMG_BG_RGB565 > 255, "the background marker must not collide with a luminance value");

} // namespace

SunData sunData() { return s_sun.get(); }

const uint16_t *sunImage(uint32_t &version, time_t &takenAt) {
    LockGuard g(s_imgLock);
    version = s_version;
    takenAt = s_takenAt;
    return s_front >= 0 ? s_img[s_front] : nullptr;
}

bool spaceFetchSwpc() {
    HeapBuf bufMem;
    if (!bufMem.psram(SWPC_BUF)) {
        Serial.println("Space: no PSRAM for the SWPC buffer");
        return false;
    }
    char *buf = bufMem.as<char>();
    SunData d = sunData(); // start from the previous values so a failed file keeps its old numbers
    bool gotScales = false, gotFlares = false, gotAny = false;
    {
        net::Options o;
        o.tag = "Space";
        o.ca = NOAA_CA;
        o.keepAlive = true; // one TLS handshake for all the files
        o.budgetMs = PW_JOB_BUDGET_MS;
        net::Session s(o);
        size_t len = 0;
        auto get = [&](const char *path) -> bool {
            if (!s.usable()) return false; // connection failed or budget spent: don't wait for more timeouts
            char url[96];
            snprintf(url, sizeof(url), "%s%s", SWPC, path);
            net::Response r;
            if (s.get(url, buf, SWPC_BUF, &r) != net::Result::Ok) return false;
            len = r.len;
            return true;
        };
        if (get("/products/noaa-scales.json")) gotScales = parseScales(buf, len, d);
        if (get("/json/goes/primary/xray-flares-latest.json")) gotFlares = parseFlares(buf, len, d);
        if (get("/products/geospace/propagated-solar-wind-1-hour.json")) gotAny |= parseWind(buf, len, d);
        if (get("/json/solar_probabilities.json")) gotAny |= parseProbabilities(buf, len, d);
        if (get("/text/daily-solar-indices.txt")) gotAny |= parseIndices(buf, len, d);
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
    s_sun.set(d);
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
    HeapBuf jpgMem;
    if (!jpgMem.psram(JPEG_MAX + 1)) { // + the NUL net::Session::get() appends
        Serial.println("Space: no PSRAM for the sun JPEG");
        return false;
    }
    const uint8_t *jpg = jpgMem.as<const uint8_t>();
    size_t jpgLen = 0;
    time_t takenAt = 0;
    {
        net::Options o;
        o.tag = "Space";
        o.ca = LETS_ENCRYPT_CA; // ISRG Root X1 + YR, the chain of soho.nascom.nasa.gov
        o.budgetMs = PW_JOB_BUDGET_MS;
        o.bodyDeadlineMs = PW_JOB_BUDGET_MS; // clamped to what is left of the budget
        net::Session s(o);                   // HTTP/1.0: the body is the JPEG, ended by Content-Length or close
        net::Request q;
        q.lastModified = true;
        net::Response r;
        if (s.get(SUN_URL, jpgMem.as<char>(), jpgMem.size, &r, q) != net::Result::Ok) return false;
        jpgLen = r.len;
        takenAt = r.lastModified;
        if (r.contentLength < 0 && jpgLen < 1024) {
            Serial.printf("Space: sun image incomplete (%u B)\n", (unsigned)jpgLen);
            return false;
        }
    } // TLS buffers freed before decoding
    if (jpgLen < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) {
        Serial.println("Space: sun image is not a JPEG");
        return false;
    }

    // ---- decode and area-average to SUN_IMG_PX ----
    const uint32_t t0 = millis();
    HeapBuf ctxMem, poolMem, bandMem;
    SunDecode *c = static_cast<SunDecode *>(ctxMem.internal(sizeof(SunDecode), true) ? ctxMem.p : nullptr);
    void *pool = poolMem.internal(JD_POOL) ? poolMem.p : nullptr;
    if (!c || !pool) {
        Serial.println("Space: no memory to decode the sun image");
        return false;
    }
    c->src = jpg;
    c->len = jpgLen;
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
    // Band: the output rows one MCU row (8 or 16 source rows) can touch, plus the row it shares
    c->bandRows = c->jd.msy * 8 * SUN_IMG_PX / c->height + 2;
    const size_t bandBytes = (size_t)c->bandRows * SUN_IMG_PX * (sizeof(uint16_t) + sizeof(uint8_t));
    if (!bandMem.internal(bandBytes, true) && !bandMem.psram(bandBytes, true)) {
        Serial.println("Space: no memory to decode the sun image");
        return false;
    }
    c->bandSum = bandMem.as<uint16_t>();
    c->bandCnt = bandMem.as<uint8_t>() + (size_t)c->bandRows * SUN_IMG_PX * sizeof(uint16_t);

    int back;
    {
        LockGuard g(s_imgLock);
        back = s_front == 0 ? 1 : 0; // the screen may still be showing the front buffer
    }
    c->out = s_img[back];
    jr = jd_decomp(&c->jd, jdOut, 0);
    if (jr != JDR_OK) {
        Serial.printf("Space: sun JPEG decode failed (%d)\n", (int)jr);
        return false;
    }
    flushRows(c, SUN_IMG_PX);
    tintImage(s_img[back]);
    const uint32_t ms = millis() - t0;
    {
        LockGuard g(s_imgLock);
        s_front = back;
        s_version++;
        s_takenAt = takenAt;
    }
    Serial.printf("Space: sun image %u B, decoded in %lu ms\n", (unsigned)jpgLen, (unsigned long)ms);
    return true;
}
