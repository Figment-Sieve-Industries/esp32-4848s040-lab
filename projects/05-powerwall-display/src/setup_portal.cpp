// First-boot setup portal (contract: setup_portal.h). Runs instead of the dashboard, on the Arduino loop task
// (core 1), which owns LVGL, the web server, the Wi-Fi join state machine and the scan cache. Slow work (town
// lookup over TLS, the test-then-save sequence) runs on one short worker task on core 0 and reports through
// Shared state; the page polls GET /api/state. Gateway discovery has its own task (gw_discover.h).
//
// Endpoints (JSON unless noted; request bodies are capped at BODY_MAX bytes, never logged):
//   GET  /                 the page (setup_page.h); a request for any other host name is redirected here
//   GET  /api/state        config (no secrets), STA status, discovery progress, test progress
//   GET  /api/scan         cached Wi-Fi scan (refreshed in the background when older than SCAN_MAX_AGE_MS)
//   GET  /api/zones        IANA names of the time zone table (setup_tz.h)
//   GET  /api/geocode?q=   starts / polls an Open-Meteo lookup: {"state":"running"} until done
//   POST /api/wifi         {ssid,password}: join as STA, AP stays up
//   POST /api/discover     gwdisc::start()
//   POST /api/save         full config: test (Wi-Fi, gateway reachable, ONE login, location) then cfg::save + restart
//   Captive-portal probes (/generate_204, /hotspot-detect.html, /connecttest.txt, ...) and any unknown path: 302
//   to http://192.168.4.1/.
#include "setup_portal.h"
#include "ca_certs.h"
#include "config_store.h"
#include "gw_discover.h"
#include "mem_util.h"
#include "net_http.h"
#include "setup_page.h"
#include "setup_screen.h"
#include "setup_tz.h"
#include "setup_util.h"
#include "shared_state.h"
#include "time_util.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <atomic>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <freertos/queue.h>
#include <mbedtls/platform.h>
#include <new>
#include <stdio.h>
#include <string.h>

namespace {

constexpr uint32_t HOLD_DETECT_MS = 1000;  // a touch must start within this long after setupRequested()
constexpr uint32_t HOLD_NEED_MS = 5000;    // ... and be held this long
constexpr uint32_t HOLD_LOST_MS = 1500;    // no GT911 report at all for this long counts as released
constexpr uint32_t JOIN_TIMEOUT_MS = 20000;
constexpr uint32_t JOIN_DELAY_MS = 400;    // lets the HTTP reply leave before the radio may change channel
constexpr uint32_t SCAN_MAX_AGE_MS = 10000;
constexpr uint32_t CLOCK_WAIT_MS = 15000;  // NTP before the first TLS lookup (certificate dates)
constexpr uint32_t LOGIN_COOLDOWN_MS = 20000; // after a rejected login: no new test before this
constexpr uint32_t RESTART_DELAY_MS = 3000;
constexpr size_t BODY_MAX = 2048;
constexpr size_t GEO_BUF = 16 * 1024;
constexpr int MAX_NETS = 20;
constexpr int MAX_GEO = 5;

const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_MASK(255, 255, 255, 0);

RTC_NOINIT_ATTR uint32_t s_setupNextBoot;  // survives ESP.restart(), random after a power cut
constexpr uint32_t SETUP_NEXT_MAGIC = 0x50545553; // "SUTP"

// ---- mbedTLS allocator (same policy as powerwall.cpp's, which is file-local there and only installed by
// powerwallStart(), which setup mode never calls): blocks of 4 KB and up to PSRAM, small ones internal. ----
void *tlsCalloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return nullptr;
    const uint32_t ext = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, in = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const bool big = n * size >= 4096;
    void *p = heap_caps_calloc(n, size, big ? ext : in);
    return p ? p : heap_caps_calloc(n, size, big ? in : ext);
}
void tlsFree(void *p) { heap_caps_free(p); }

// ---- shared state (main loop <-> worker task) ----

enum class JoinState : uint8_t { Idle, Pending, Connecting, Ok, Failed };
struct Join {
    JoinState state = JoinState::Idle;
    char ssid[33] = "";
    char pass[65] = ""; // kept only to know whether the save test may reuse the current connection
    char reason[48] = "";
    char ip[16] = "";
    int rssi = 0;
    uint32_t atMs = 0;  // Pending: start time; Connecting: started
    uint8_t attempts = 0;
};
Shared<Join> s_join;
std::atomic<int> s_discReason{0}; // last STA disconnect reason (event task -> main loop)

enum class GeoState : uint8_t { Idle, Running, Done, Error };
struct GeoHit {
    char name[48], admin1[48], country[40], cc[3], tz[40];
    float lat, lon;
};
struct Geo {
    GeoState state = GeoState::Idle;
    char q[64] = "";
    GeoHit hit[MAX_GEO] = {};
    int n = 0;
    char error[64] = "";
};
Shared<Geo> s_geo;

enum class TestState : uint8_t { Idle, Running, Failed, Saved };
struct Step {
    setupscreen::Mark mark = setupscreen::Mark::Wait;
    char note[40] = "";
};
struct Test {
    TestState state = TestState::Idle;
    Step step[setupscreen::STEPS];
    char hostLabel[48] = "";
    char wifiLabel[48] = "";
    char error[96] = "";
};
Shared<Test> s_test;
Shared<cfg::Config> s_pending; // the config under test (worker reads it)
std::atomic<uint32_t> s_restartAt{0};
std::atomic<uint32_t> s_loginFailMs{0};

QueueHandle_t s_jobs;
enum Job : uint8_t { JOB_GEOCODE = 1, JOB_SAVE = 2 };

// ---- main-loop-only state ----
struct Net {
    char ssid[33];
    int8_t rssi;
    uint8_t ch;
    uint8_t bssid[6];
    bool secure;
};
// Bigger buffers live in PSRAM, allocated only in setup mode (the dashboard never pays for them)
struct PortalMem {
    setuputil::BodyBuf<BODY_MAX> body;
    Net nets[MAX_NETS];
    setupscreen::View view;
};
PortalMem *s_mem;
int s_netCount = 0;
uint32_t s_scanMs = 0;
bool s_scanning = false;
bool s_ntpStarted = false;
char s_apSsid[16];
WebServer s_server(80);
DNSServer s_dns;

// ---------------------------------------------------------------------------------------------------------------

void copyNet(const char *src, char *dst, size_t cap) { copyStr(dst, cap, src ? src : ""); }

void utf8Copy(char *dst, size_t cap, const char *src) {
    copyStr(dst, cap, src ? src : "");
    // don't leave half a UTF-8 sequence at the end
    size_t n = strlen(dst);
    size_t i = n;
    while (i > 0 && (static_cast<unsigned char>(dst[i - 1]) & 0xC0) == 0x80) i--;
    if (i > 0 && static_cast<unsigned char>(dst[i - 1]) >= 0xC0) {
        const unsigned char lead = static_cast<unsigned char>(dst[i - 1]);
        const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        if (n - (i - 1) < need) dst[i - 1] = '\0';
    }
}

const char *reasonText(int r) {
    switch (r) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_MIC_FAILURE:
        return "wrong password?";
    case WIFI_REASON_NO_AP_FOUND:
        return "network not found (2.4 GHz only)";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "security type not supported";
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_ASSOC_FAIL:
        return "no answer from the router";
    default:
        return nullptr;
    }
}

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_discReason.store(info.wifi_sta_disconnected.reason);
}

// ---- Wi-Fi scan cache ----

void collectScan(int n) {
    s_netCount = 0;
    for (int i = 0; i < n; i++) {
        const String ssid = WiFi.SSID(i);
        if (ssid.isEmpty() || ssid.length() > 32) continue; // hidden
        const int8_t rssi = static_cast<int8_t>(WiFi.RSSI(i));
        int k = 0;
        while (k < s_netCount && strcmp(s_mem->nets[k].ssid, ssid.c_str()) != 0) k++;
        if (k < s_netCount) { // same name: keep the strongest access point
            if (rssi <= s_mem->nets[k].rssi) continue;
        } else if (s_netCount < MAX_NETS) {
            k = s_netCount++;
        } else {
            continue;
        }
        Net &e = s_mem->nets[k];
        copyNet(ssid.c_str(), e.ssid, sizeof(e.ssid));
        e.rssi = rssi;
        e.ch = static_cast<uint8_t>(WiFi.channel(i));
        const uint8_t *b = WiFi.BSSID(i);
        if (b) memcpy(e.bssid, b, 6);
        else memset(e.bssid, 0, 6);
        e.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    // strongest first
    for (int i = 1; i < s_netCount; i++) {
        for (int j = i; j > 0 && s_mem->nets[j].rssi > s_mem->nets[j - 1].rssi; j--) {
            const Net t = s_mem->nets[j];
            s_mem->nets[j] = s_mem->nets[j - 1];
            s_mem->nets[j - 1] = t;
        }
    }
    WiFi.scanDelete();
    s_scanMs = millis();
}

void scanTick() {
    if (!s_scanning) return;
    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return;
    s_scanning = false;
    if (n >= 0) collectScan(n);
    else s_scanMs = millis(); // failed: don't retry at once
}

bool joinBusy() {
    const JoinState st = s_join.get().state;
    return st == JoinState::Pending || st == JoinState::Connecting;
}

void startScanIfOld() {
    if (s_scanning || joinBusy()) return; // a scan during a join breaks the join
    if (s_scanMs && millis() - s_scanMs < SCAN_MAX_AGE_MS) return;
    if (WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING) s_scanning = true;
    else s_scanMs = millis();
}

// ---- Wi-Fi join state machine (main loop) ----

void requestJoin(const char *ssid, const char *pass) {
    s_join.with([&](Join &j) {
        copyStr(j.ssid, sizeof(j.ssid), ssid);
        copyStr(j.pass, sizeof(j.pass), pass);
        j.state = JoinState::Pending;
        j.reason[0] = '\0';
        j.ip[0] = '\0';
        j.atMs = millis();
        j.attempts = 0;
    });
}

void beginJoin(const Join &j) {
    const Net *hint = nullptr;
    for (int i = 0; i < s_netCount; i++) {
        if (strcmp(s_mem->nets[i].ssid, j.ssid) == 0) hint = &s_mem->nets[i];
    }
    s_discReason.store(0);
    // With the channel and BSSID from the scan the STA does not sweep every channel, which would take the AP
    // with it for a few seconds.
    if (hint && hint->ch) WiFi.begin(j.ssid, j.pass[0] ? j.pass : nullptr, hint->ch, hint->bssid, true);
    else WiFi.begin(j.ssid, j.pass[0] ? j.pass : nullptr);
    Serial.printf("[setup] joining '%s'%s (attempt %u)\n", j.ssid, hint ? "" : " (not in scan)", j.attempts + 1);
}

void joinTick() {
    const Join j = s_join.get();
    const uint32_t now = millis();
    switch (j.state) {
    case JoinState::Pending:
        if ((int32_t)(now - j.atMs) < (int32_t)JOIN_DELAY_MS) return; // atMs may lie in the future (rejoin delay)
        if (s_scanning) return; // let a running scan finish first
        beginJoin(j);
        s_join.with([now](Join &x) {
            x.state = JoinState::Connecting;
            x.atMs = now;
            x.attempts++;
        });
        return;
    case JoinState::Connecting: {
        if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
            const int rssi = WiFi.RSSI(); // NOLINT(bugprone-signed-char-misuse): int8 dBm, sign extension intended
            const String ip = WiFi.localIP().toString();
            s_join.with([&](Join &x) {
                x.state = JoinState::Ok;
                x.rssi = rssi;
                copyStr(x.ip, sizeof(x.ip), ip.c_str());
            });
            Serial.printf("[setup] joined '%s', %d dBm, channel %d\n", j.ssid, rssi, (int)WiFi.channel());
            if (!s_ntpStarted) { // certificate dates for the town lookup; the dashboard sets its own TZ later
                s_ntpStarted = true;
                configTzTime("UTC0", "pool.ntp.org", "time.google.com");
            }
            return;
        }
        int reason = s_discReason.exchange(0);
        if (reason == WIFI_REASON_ASSOC_LEAVE) reason = 0; // our own disconnect before the new attempt
        const bool timedOut = now - j.atMs > JOIN_TIMEOUT_MS;
        if (!reason && !timedOut) return;
        if (j.attempts < 2) { // one retry: a handshake timeout is sometimes just a slow router
            Serial.printf("[setup] join attempt failed (reason %d), retrying\n", reason);
            s_join.with([now](Join &x) {
                x.state = JoinState::Pending;
                x.atMs = now;
            });
            WiFi.disconnect(false, false);
            return;
        }
        const char *txt = reasonText(reason);
        char why[48];
        if (txt) copyStr(why, sizeof(why), txt);
        else if (reason) snprintf(why, sizeof(why), "Wi-Fi error %d", reason);
        else copyStr(why, sizeof(why), "no answer (timeout)");
        Serial.printf("[setup] join failed: %s\n", why);
        WiFi.disconnect(false, false);
        s_join.with([&](Join &x) {
            x.state = JoinState::Failed;
            copyStr(x.reason, sizeof(x.reason), why);
        });
        return;
    }
    case JoinState::Ok: {
        int reason = s_discReason.exchange(0);
        if (reason && WiFi.status() != WL_CONNECTED) { // dropped: try again (auto-reconnect is off)
            Serial.printf("[setup] Wi-Fi lost (reason %d), rejoining\n", reason);
            s_join.with([now](Join &x) {
                x.state = JoinState::Pending;
                x.atMs = now + 2000;
                x.attempts = 0;
            });
        }
        return;
    }
    default:
        return;
    }
}

// ---- HTTP helpers ----

void noCache() { s_server.sendHeader("Cache-Control", "no-store"); }

void sendJson(int code, const JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    noCache();
    s_server.send(code, "application/json", out);
}

void sendError(int code, const char *msg) {
    JsonDocument d(psramJson());
    d["error"] = msg;
    sendJson(code, d);
}

void sendOk() {
    JsonDocument d(psramJson());
    d["ok"] = true;
    sendJson(200, d);
}

void redirectHome() {
    s_server.sendHeader("Location", "http://192.168.4.1/", true);
    noCache();
    s_server.send(302, "text/plain", "");
}

// Raw body collector for every POST handler (the web server handles one request at a time)
void rawBody() {
    HTTPRaw &raw = s_server.raw();
    if (raw.status == RAW_START) s_mem->body.reset();
    else if (raw.status == RAW_WRITE) s_mem->body.add(raw.buf, raw.currentSize);
    else if (raw.status == RAW_ABORTED) s_mem->body.overflow = true;
}

// Parses the collected body; sends the error reply and returns false when it can't.
bool parseBody(JsonDocument &doc) {
    if (s_mem->body.overflow) {
        sendError(413, "request too large");
        return false;
    }
    if (deserializeJson(doc, s_mem->body.buf, s_mem->body.len) || !doc.is<JsonObject>()) {
        sendError(400, "bad request");
        return false;
    }
    return true;
}

bool hostIsUs() {
    const String h = s_server.hostHeader();
    return h.isEmpty() || h == "192.168.4.1" || h == "192.168.4.1:80";
}

// ---- handlers ----

void handleRoot() {
    if (!hostIsUs()) {
        redirectHome();
        return;
    }
    noCache();
    s_server.send_P(200, "text/html; charset=utf-8", setuppage::HTML, sizeof(setuppage::HTML) - 1);
}

void handleNotFound() {
    if (s_server.uri().startsWith("/api/")) sendError(404, "not found");
    else redirectHome();
}

const char *stepMarkName(setupscreen::Mark m) {
    switch (m) {
    case setupscreen::Mark::Run: return "run";
    case setupscreen::Mark::Ok: return "ok";
    case setupscreen::Mark::Fail: return "fail";
    default: return "wait";
    }
}

void handleState() {
    JsonDocument d(psramJson());
    d["ap"]["ssid"] = s_apSsid;
    d["ap"]["ip"] = "192.168.4.1";
    d["ap"]["clients"] = WiFi.softAPgetStationNum();

    const Join j = s_join.get();
    static const char *const JOIN_NAMES[] = {"idle", "connecting", "connecting", "ok", "failed"};
    JsonObject sta = d["sta"].to<JsonObject>();
    sta["state"] = JOIN_NAMES[static_cast<int>(j.state)];
    sta["ssid"] = j.ssid;
    sta["rssi"] = j.state == JoinState::Ok ? (int)WiFi.RSSI() : 0;
    sta["ip"] = j.ip;
    sta["reason"] = j.reason;
    d["clock"] = tu::clockValid();

    // Current config from the store, without secrets (built from the struct: the portal needs only these fields)
    JsonObject c = d["config"].to<JsonObject>();
    c["configured"] = cfg::configured();
    if (cfg::configured()) {
        const cfg::Config &k = cfg::get();
        c["ssid"] = k.wifiCount ? k.wifi[0].ssid : "";
        c["wifiPasswordSet"] = k.wifiCount && k.wifi[0].pass[0];
        c["pwHost"] = k.pwHost;
        c["pwPasswordSet"] = k.pwPassword[0] != '\0';
        c["place"] = k.placeName;
        c["lat"] = k.lat;
        c["lon"] = k.lon;
        c["tz"] = k.tz;
        c["auroraUk"] = k.auroraUk;
    }

    const gwdisc::Result g = gwdisc::status();
    JsonObject disc = d["disc"].to<JsonObject>();
    disc["running"] = g.running;
    disc["done"] = g.done;
    disc["scanned"] = g.scanned;
    disc["total"] = g.total;
    disc["count"] = g.count;
    JsonArray hosts = disc["hosts"].to<JsonArray>();
    for (int i = 0; i < g.count && i < gwdisc::MAX_FOUND; i++) hosts.add(g.hosts[i]);
    disc["error"] = g.error;

    const Test t = s_test.get();
    static const char *const TEST_NAMES[] = {"idle", "running", "failed", "saved"};
    JsonObject test = d["test"].to<JsonObject>();
    test["state"] = TEST_NAMES[static_cast<int>(t.state)];
    JsonArray steps = test["steps"].to<JsonArray>();
    for (const Step &s : t.step) {
        JsonObject o = steps.add<JsonObject>();
        o["s"] = stepMarkName(s.mark);
        o["note"] = s.note;
    }
    test["error"] = t.error;
    sendJson(200, d);
}

void handleScan() {
    startScanIfOld();
    JsonDocument d(psramJson());
    d["scanning"] = s_scanning || (s_scanMs == 0);
    JsonArray a = d["networks"].to<JsonArray>();
    for (int i = 0; i < s_netCount; i++) {
        JsonObject o = a.add<JsonObject>();
        o["ssid"] = s_mem->nets[i].ssid;
        o["rssi"] = s_mem->nets[i].rssi;
        o["secure"] = s_mem->nets[i].secure;
    }
    sendJson(200, d);
}

void handleZones() {
    JsonDocument d(psramJson());
    JsonArray a = d["zones"].to<JsonArray>();
    for (const setuptz::Zone &z : setuptz::ZONES) a.add(z.iana);
    sendJson(200, d);
}

bool testRunning() { return s_test.get().state == TestState::Running; }

void handleWifi() {
    JsonDocument d(psramJson());
    if (!parseBody(d)) return;
    const char *ssid = d["ssid"] | "";
    const char *pass = d["password"] | "";
    if (!*ssid || strlen(ssid) > 32) return sendError(400, "Network name must be 1 to 32 characters");
    const size_t pl = strlen(pass);
    if (pl > 64 || (pl > 0 && pl < 8)) return sendError(400, "Wi-Fi passwords are 8 to 64 characters");
    if (testRunning()) return sendError(409, "A test is running");
    requestJoin(ssid, pass);
    sendOk();
}

void handleDiscover() {
    if (s_join.get().state != JoinState::Ok) return sendError(409, "Connect to your Wi-Fi first (step 1)");
    if (gwdisc::status().running) return sendOk();
    if (!gwdisc::start()) {
        const gwdisc::Result g = gwdisc::status();
        return sendError(409, g.error[0] ? g.error : "Search could not start");
    }
    sendOk();
}

void handleGeocode() {
    const String q = s_server.arg("q");
    if (q.length() < 2 || q.length() > 60) return sendError(400, "Type 2 to 60 characters");
    JsonDocument d(psramJson());
    const Geo g = s_geo.get();
    if (g.state == GeoState::Running) {
        if (q != g.q) return sendError(409, "Another lookup is running");
        d["state"] = "running";
        return sendJson(200, d);
    }
    if (q == g.q && g.state == GeoState::Done) {
        d["state"] = "done";
        JsonArray a = d["results"].to<JsonArray>();
        for (int i = 0; i < g.n; i++) {
            const GeoHit &h = g.hit[i];
            JsonObject o = a.add<JsonObject>();
            o["name"] = h.name;
            o["admin1"] = h.admin1;
            o["country"] = h.country;
            o["cc"] = h.cc;
            o["lat"] = h.lat;
            o["lon"] = h.lon;
            o["tz"] = h.tz;
            const char *posix = setuptz::posixFor(h.tz);
            o["posix"] = posix ? posix : "";
        }
        s_geo.with([](Geo &x) { x.q[0] = '\0'; }); // results handed out: the next request looks up again
        return sendJson(200, d);
    }
    if (q == g.q && g.state == GeoState::Error) {
        d["state"] = "error";
        d["error"] = g.error;
        s_geo.with([](Geo &x) { x.q[0] = '\0'; });
        return sendJson(200, d);
    }
    if (s_join.get().state != JoinState::Ok) return sendError(409, "Connect to your Wi-Fi first (step 1)");
    s_geo.with([&](Geo &x) {
        x.state = GeoState::Running;
        copyStr(x.q, sizeof(x.q), q.c_str());
        x.n = 0;
        x.error[0] = '\0';
    });
    const uint8_t job = JOB_GEOCODE;
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        s_geo.with([](Geo &x) { x.state = GeoState::Idle; });
        return sendError(503, "Busy, try again");
    }
    d["state"] = "running";
    sendJson(200, d);
}

bool reject(int code, const char *msg) {
    sendError(code, msg);
    return false;
}

// Builds the config to test from the page's JSON. Empty password fields keep the stored ones (the page never gets
// them back). Sends the error reply and returns false on a validation error.
bool buildConfig(const JsonDocument &d, cfg::Config &c, char *tzName, size_t tzCap) {
    memset(&c, 0, sizeof(c));
    const bool have = cfg::configured();
    const cfg::Config &old = cfg::get();

    const char *ssid = d["ssid"] | "";
    const char *pass = d["password"] | "";
    if (!*ssid || strlen(ssid) > 32) return reject(400, "Step 1: choose your Wi-Fi network");
    copyStr(c.wifi[0].ssid, sizeof(c.wifi[0].ssid), ssid);
    if (!*pass) { // keep what step 1 joined with, else the stored password for this network
        const Join j = s_join.get();
        if (strcmp(j.ssid, ssid) == 0) pass = j.pass;
        for (int i = 0; have && !*pass && i < old.wifiCount && i < cfg::MAX_WIFI; i++)
            if (strcmp(old.wifi[i].ssid, ssid) == 0) pass = old.wifi[i].pass;
    }
    const size_t pl = strlen(pass);
    if (pl > 64 || (pl > 0 && pl < 8)) return reject(400, "Step 1: Wi-Fi passwords are 8 to 64 characters");
    copyStr(c.wifi[0].pass, sizeof(c.wifi[0].pass), pass);
    c.wifiCount = 1;

    const char *host = d["pwHost"] | "";
    if (!setuputil::validHost(host)) return reject(400, "Step 2: enter the gateway address, e.g. 192.168.1.50");
    copyStr(c.pwHost, sizeof(c.pwHost), host);
    const char *pw = d["pwPassword"] | "";
    if (!*pw && have && strcmp(old.pwHost, host) == 0) pw = old.pwPassword;
    if (!*pw) return reject(400, "Step 2: enter the gateway customer password");
    if (strlen(pw) >= sizeof(c.pwPassword)) return reject(400, "Step 2: the gateway password is too long");
    copyStr(c.pwPassword, sizeof(c.pwPassword), pw);

    const float lat = d["lat"] | NAN, lon = d["lon"] | NAN;
    if (!(lat >= -90.0f && lat <= 90.0f) || !(lon >= -180.0f && lon <= 180.0f) || (lat == 0.0f && lon == 0.0f))
        return reject(400, "Step 3: look up your town or enter its latitude and longitude");
    c.lat = lat;
    c.lon = lon;
    setuputil::asciiFold(d["place"] | "", c.placeName, sizeof(c.placeName));
    if (!c.placeName[0]) return reject(400, "Step 3: enter a place name");

    const char *zone = d["tzName"] | "";
    const char *tz = d["tz"] | "";
    copyStr(tzName, tzCap, zone);
    if (*zone) {
        const char *posix = setuptz::posixFor(zone);
        if (!posix) return reject(400, "Step 3: unknown time zone, choose one from the list");
        copyStr(c.tz, sizeof(c.tz), posix);
    } else if (setuputil::printableAscii(tz, sizeof(c.tz) - 1)) {
        copyStr(c.tz, sizeof(c.tz), tz);
    } else {
        return reject(400, "Step 3: choose your time zone");
    }
    c.auroraUk = d["auroraUk"] | false;
    return true;
}

char s_tzName[40]; // IANA name of the config under test (for the login's clientInfo and the screen)

void handleSave() {
    JsonDocument d(psramJson());
    if (!parseBody(d)) return;
    if (testRunning()) return sendError(409, "A test is already running");
    if (s_restartAt.load()) return sendError(409, "Saved, the display is restarting");
    if (gwdisc::status().running) return sendError(409, "Wait for the gateway search to finish");
    const uint32_t lf = s_loginFailMs.load();
    if (lf && millis() - lf < LOGIN_COOLDOWN_MS) {
        char m[96];
        snprintf(m, sizeof(m), "The gateway rejected the password. Check it and try again in %u s",
                 (unsigned)((LOGIN_COOLDOWN_MS - (millis() - lf)) / 1000 + 1));
        return sendError(429, m);
    }
    cfg::Config c;
    char zone[40];
    if (!buildConfig(d, c, zone, sizeof(zone))) return;
    memset(s_mem->body.buf, 0, sizeof(s_mem->body.buf)); // the body held passwords
    s_mem->body.len = 0;
    copyStr(s_tzName, sizeof(s_tzName), zone);
    s_pending.set(c);
    memset(&c, 0, sizeof(c));
    s_test.with([](Test &t) {
        t = Test();
        t.state = TestState::Running;
    });
    const uint8_t job = JOB_SAVE;
    if (xQueueSend(s_jobs, &job, 0) != pdTRUE) {
        s_test.with([](Test &t) { t.state = TestState::Idle; });
        return sendError(503, "Busy, try again");
    }
    sendOk();
}

// ---- worker jobs (core 0) ----

void runGeocode() {
    Geo g = s_geo.get();
    auto fail = [](const char *why) {
        Serial.printf("[setup] town lookup failed: %s\n", why);
        s_geo.with([why](Geo &x) {
            x.state = GeoState::Error;
            copyStr(x.error, sizeof(x.error), why);
        });
    };
    const uint32_t t0 = millis();
    while (!tu::clockValid() && millis() - t0 < CLOCK_WAIT_MS) vTaskDelay(pdMS_TO_TICKS(200));
    if (!tu::clockValid()) return fail("No internet time yet (check the router's internet)");

    char q[200];
    if (!setuputil::urlEncode(g.q, q, sizeof(q))) return fail("Name too long");
    char url[320];
    snprintf(url, sizeof(url), "https://geocoding-api.open-meteo.com/v1/search?name=%s&count=%d&language=en&format=json", q,
             MAX_GEO);
    HeapBuf body;
    if (!body.psram(GEO_BUF)) return fail("Out of memory");
    size_t len = 0;
    {
        net::Options o;
        o.tag = "Setup";
        o.ca = LETS_ENCRYPT_CA; // geocoding-api.open-meteo.com: Let's Encrypt YR2 -> ISRG Root YR (checked 2026-10-12)
        o.budgetMs = 20000;
        net::Session s(o);
        net::Response r;
        const net::Result res = s.get(url, body.as<char>(), body.size, &r);
        if (res != net::Result::Ok) {
            char why[64];
            if (res == net::Result::Http) snprintf(why, sizeof(why), "Lookup failed (HTTP %d)", r.code);
            else snprintf(why, sizeof(why), "Lookup failed (%s)", net::resultName(res));
            return fail(why);
        }
        len = r.len;
    }
    JsonDocument doc(psramJson());
    if (deserializeJson(doc, body.as<const char>(), len)) return fail("Lookup failed (bad answer)");
    g.n = 0;
    for (JsonObjectConst r : doc["results"].as<JsonArrayConst>()) {
        if (g.n >= MAX_GEO) break;
        if (!r["latitude"].is<float>() || !r["longitude"].is<float>()) continue;
        GeoHit &h = g.hit[g.n++];
        utf8Copy(h.name, sizeof(h.name), r["name"] | "");
        utf8Copy(h.admin1, sizeof(h.admin1), r["admin1"] | "");
        utf8Copy(h.country, sizeof(h.country), r["country"] | "");
        copyStr(h.cc, sizeof(h.cc), r["country_code"] | "");
        copyStr(h.tz, sizeof(h.tz), r["timezone"] | "");
        h.lat = r["latitude"].as<float>();
        h.lon = r["longitude"].as<float>();
    }
    Serial.printf("[setup] town lookup: %d result(s)\n", g.n);
    s_geo.with([&g](Geo &x) {
        memcpy(x.hit, g.hit, sizeof(x.hit));
        x.n = g.n;
        x.state = GeoState::Done;
    });
}

void setStep(int i, setupscreen::Mark m, const char *note) {
    s_test.with([&](Test &t) {
        t.step[i].mark = m;
        copyStr(t.step[i].note, sizeof(t.step[i].note), note ? note : "");
    });
}

void failTest(int i, const char *note, const char *error) {
    setStep(i, setupscreen::Mark::Fail, note);
    s_test.with([error](Test &t) {
        t.state = TestState::Failed;
        copyStr(t.error, sizeof(t.error), error);
    });
    Serial.printf("[setup] test failed at step %d: %s\n", i + 1, error);
}

// One POST /api/login/Basic, the same request as powerwall.cpp's login() (username "customer", empty email).
// Returns the HTTP status (200 = token received), or a negative HTTPClient error. The token is discarded.
int gatewayLogin(const cfg::Config &c, const char *tzName) {
    WiFiClientSecure client;
    client.setInsecure(); // self-signed gateway certificate (ADR-37)
    client.setHandshakeTimeout(8);
    HTTPClient http;
    const char *keys[] = {"Transfer-Encoding"};
    http.collectHeaders(keys, 1);
    char url[96];
    snprintf(url, sizeof(url), "https://%s/api/login/Basic", c.pwHost);
    net::noteTlsConnect();
    if (!http.begin(client, url)) return HTTPC_ERROR_CONNECTION_REFUSED;
    http.useHTTP10(true);
    http.setReuse(false);
    http.setConnectTimeout(3000);
    http.setTimeout(5000);
    http.addHeader("Content-Type", "application/json");
    JsonDocument req(psramJson());
    req["username"] = "customer";
    req["password"] = c.pwPassword;
    req["email"] = "";
    req["clientInfo"]["timezone"] = *tzName ? tzName : "Europe/London";
    String bodyOut;
    serializeJson(req, bodyOut);
    int code = http.POST(bodyOut);
    for (size_t i = 0; i < bodyOut.length(); i++) bodyOut.setCharAt(i, ' '); // held the password
    if (code == 200) {
        HeapBuf buf;
        size_t len = 0;
        bool ok = buf.psram(4096) && net::readBody(http, buf.as<char>(), buf.size, len, 5000) == net::Result::Ok;
        JsonDocument res(psramJson());
        ok = ok && !deserializeJson(res, buf.as<const char>(), len) && res["token"].is<const char *>();
        if (buf.p) memset(buf.p, 0, buf.size); // held the token
        if (!ok) code = -100;
    }
    http.end();
    client.stop();
    return code;
}

void runSaveTest() {
    using setupscreen::Mark;
    cfg::Config c = s_pending.get();
    s_test.with([&c](Test &t) {
        snprintf(t.wifiLabel, sizeof(t.wifiLabel), "Wi-Fi \"%s\"", c.wifi[0].ssid);
        snprintf(t.hostLabel, sizeof(t.hostLabel), "Powerwall gateway %s", c.pwHost);
    });
    char note[40];

    // (a) Wi-Fi
    setStep(0, Mark::Run, "joining...");
    const Join cur = s_join.get();
    const bool reuse = cur.state == JoinState::Ok && WiFi.status() == WL_CONNECTED &&
                       strcmp(cur.ssid, c.wifi[0].ssid) == 0 && strcmp(cur.pass, c.wifi[0].pass) == 0;
    if (!reuse) {
        requestJoin(c.wifi[0].ssid, c.wifi[0].pass);
        const uint32_t t0 = millis();
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(200));
            const JoinState st = s_join.get().state;
            if (st == JoinState::Ok || st == JoinState::Failed) break;
            if (millis() - t0 > 2 * JOIN_TIMEOUT_MS + 5000) break;
        }
    }
    const Join j = s_join.get();
    if (j.state != JoinState::Ok) {
        char err[96];
        snprintf(err, sizeof(err), "Wi-Fi: could not join \"%s\": %s", c.wifi[0].ssid, j.reason[0] ? j.reason : "timeout");
        memset(&c, 0, sizeof(c));
        return failTest(0, j.reason[0] ? j.reason : "failed", err);
    }
    snprintf(note, sizeof(note), "%d dBm", (int)WiFi.RSSI());
    setStep(0, Mark::Ok, note);

    // (b) gateway reachable
    setStep(1, Mark::Run, "checking...");
    if (!gwdisc::probe(c.pwHost)) {
        char err[96];
        snprintf(err, sizeof(err), "No Powerwall gateway answered at %s. Check the address (Find it can help).", c.pwHost);
        memset(&c, 0, sizeof(c));
        return failTest(1, "not found", err);
    }
    setStep(1, Mark::Ok, "found");

    // (c) gateway login: exactly one attempt
    setStep(2, Mark::Run, "checking...");
    const int code = gatewayLogin(c, s_tzName);
    if (code != 200) {
        char err[96];
        if (code == 401 || code == 403) {
            s_loginFailMs.store(millis() | 1);
            copyStr(err, sizeof(err), "The gateway rejected the customer password. Check the last 5 characters on the QR sticker.");
            copyStr(note, sizeof(note), "password rejected");
        } else if (code == 429) {
            s_loginFailMs.store(millis() | 1);
            copyStr(err, sizeof(err), "The gateway says too many logins. Wait a few minutes, then try again.");
            copyStr(note, sizeof(note), "too many tries");
        } else if (code > 0) {
            snprintf(err, sizeof(err), "Gateway login failed (HTTP %d).", code);
            snprintf(note, sizeof(note), "HTTP %d", code);
        } else {
            snprintf(err, sizeof(err), "Gateway login failed (%s).",
                     code == -100 ? "bad answer" : HTTPClient::errorToString(code).c_str());
            copyStr(note, sizeof(note), "no answer");
        }
        memset(&c, 0, sizeof(c));
        return failTest(2, note, err);
    }
    setStep(2, Mark::Ok, "signed in");

    // (d) location and time zone (validated when the request came in)
    setStep(3, Mark::Run, "checking...");
    if (!c.tz[0] || !c.placeName[0]) {
        memset(&c, 0, sizeof(c));
        return failTest(3, "missing", "Location or time zone missing.");
    }
    setStep(3, Mark::Ok, s_tzName[0] ? s_tzName : c.tz);

    char why[72] = "";
    if (!cfg::save(c, why, sizeof(why))) {
        char err[96];
        snprintf(err, sizeof(err), "Could not save: %s", why[0] ? why : "unknown error");
        memset(&c, 0, sizeof(c));
        return failTest(3, "not saved", err);
    }
    memset(&c, 0, sizeof(c));
    s_pending.with([](cfg::Config &p) { memset(&p, 0, sizeof(p)); });
    s_test.with([](Test &t) { t.state = TestState::Saved; });
    s_restartAt.store((millis() + RESTART_DELAY_MS) | 1);
    Serial.println("[setup] all steps passed, settings saved; restarting");
}

void workerTask(void *) {
    for (;;) {
        uint8_t job = 0;
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        const size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        if (job == JOB_GEOCODE) runGeocode();
        else if (job == JOB_SAVE) runSaveTest();
        Serial.printf("  [tls] setup job %u: internal free before %u, min ever %u\n", job, (unsigned)before,
                      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
    }
}

// ---- device screen (LVGL thread) ----

void buildView(setupscreen::View &v) {
    using setupscreen::Tone;
    const Test t = s_test.get();
    if (t.state != TestState::Idle) {
        v.mode = setupscreen::Mode::Test;
        static const char *const LABELS[] = {"Wi-Fi", "Powerwall gateway", "Log in to the gateway", "Location and time zone"};
        for (int i = 0; i < setupscreen::STEPS; i++) {
            v.mark[i] = t.step[i].mark;
            copyStr(v.label[i], sizeof(v.label[i]), LABELS[i]);
            copyStr(v.note[i], sizeof(v.note[i]), t.step[i].note);
        }
        if (t.wifiLabel[0]) copyStr(v.label[0], sizeof(v.label[0]), t.wifiLabel);
        if (t.hostLabel[0]) copyStr(v.label[1], sizeof(v.label[1]), t.hostLabel);
        if (t.state == TestState::Running) {
            v.resultTone = Tone::Warn;
            copyStr(v.result, sizeof(v.result), "Testing...");
        } else if (t.state == TestState::Saved) {
            v.resultTone = Tone::Ok;
            copyStr(v.result, sizeof(v.result), "Saved, restarting");
        } else {
            v.resultTone = Tone::Fail;
            snprintf(v.result, sizeof(v.result), "%s Fix it on your phone.", t.error);
        }
        return;
    }
    v.mode = setupscreen::Mode::Join;
    const Join j = s_join.get();
    const gwdisc::Result g = gwdisc::status();
    const GeoState geo = s_geo.get().state;
    if (j.state == JoinState::Pending || j.state == JoinState::Connecting) {
        v.statusTone = Tone::Warn;
        snprintf(v.status, sizeof(v.status), "Joining %s...", j.ssid);
    } else if (j.state == JoinState::Failed) {
        v.statusTone = Tone::Fail;
        snprintf(v.status, sizeof(v.status), "Wi-Fi: %s", j.reason);
    } else if (g.running) {
        v.statusTone = Tone::Warn;
        snprintf(v.status, sizeof(v.status), "Looking for the gateway %d/%d", g.scanned, g.total);
    } else if (geo == GeoState::Running) {
        v.statusTone = Tone::Warn;
        copyStr(v.status, sizeof(v.status), "Looking up your town...");
    } else if (j.state == JoinState::Ok) {
        v.statusTone = Tone::Ok;
        snprintf(v.status, sizeof(v.status), "On %s, %d dBm", j.ssid, (int)WiFi.RSSI());
    } else if (WiFi.softAPgetStationNum() > 0) {
        v.statusTone = Tone::Ok;
        copyStr(v.status, sizeof(v.status), "Phone connected");
    } else {
        v.statusTone = Tone::Warn;
        copyStr(v.status, sizeof(v.status), "Waiting for your phone...");
    }
}

void screenTimerCb(lv_timer_t *) {
    setupscreen::View &v = s_mem->view;
    v = setupscreen::View();
    buildView(v);
    setupscreen::portalUpdate(v);
}

void heartbeat() {
    static uint32_t last;
    if (millis() - last < 10000) return;
    last = millis();
    const Join j = s_join.get();
    Serial.printf("[setup] clients %u | sta %s %s | internal free %u, largest %u, min %u | psram free %u\n",
                  (unsigned)WiFi.softAPgetStationNum(), j.state == JoinState::Ok ? "on" : "off",
                  j.state == JoinState::Ok ? j.ssid : "", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL), (unsigned)ESP.getFreePsram());
}

} // namespace

void setupRequestOnNextBoot() { s_setupNextBoot = SETUP_NEXT_MAGIC; }

bool setupRequested(board4848::Touch *touch) {
    if (s_setupNextBoot == SETUP_NEXT_MAGIC) {
        s_setupNextBoot = 0;
        Serial.println("[setup] requested from Settings");
        return true;
    }
    if (!cfg::configured()) {
        Serial.println("[setup] no configuration: starting the setup portal");
        return true;
    }
    if (!touch) return false;

    // Quick check: is the panel touched right now (held from power-on)?
    board4848::TouchPoint p;
    const uint32_t t0 = millis();
    bool down = false;
    while (millis() - t0 < HOLD_DETECT_MS) {
        const int8_t n = touch->read(&p, 1);
        if (n > 0) {
            down = true;
            break;
        }
        delay(20);
    }
    if (!down) return false;

    // Held: countdown. LVGL's own touch reading is not running here (no lv_timer_handler), only redraws.
    setupscreen::countdownShow();
    const uint32_t start = millis();
    uint32_t lastReport = start;
    bool held = true;
    for (;;) {
        const uint32_t now = millis();
        const int8_t n = touch->read(&p, 1);
        if (n == 0) {
            held = false;
            break;
        }
        if (n > 0) lastReport = now;
        else if (now - lastReport > HOLD_LOST_MS) {
            held = false;
            break;
        }
        if (now - start >= HOLD_NEED_MS) break;
        setupscreen::countdownSet(now - start, HOLD_NEED_MS);
        lv_refr_now(nullptr);
        delay(30);
    }
    setupscreen::countdownClose();
    lv_refr_now(nullptr);
    Serial.printf("[setup] screen held at boot: %s\n", held ? "starting setup" : "released, normal start");
    return held;
}

void setupPortalRun() {
    Serial.println("[setup] setup portal starting");
    mbedtls_platform_set_calloc_free(tlsCalloc, tlsFree); // before any TLS session (see tlsCalloc)

    void *mem = psramAlloc(sizeof(PortalMem), true);
    if (!mem) mem = internalAlloc(sizeof(PortalMem), true); // no PSRAM: should not happen
    s_mem = new (mem) PortalMem();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    setuputil::apSsid(mac, s_apSsid);
    char qr[64];
    if (!setuputil::wifiQrOpen(s_apSsid, qr, sizeof(qr))) copyStr(qr, sizeof(qr), "http://192.168.4.1/");
    setupscreen::portalShow(s_apSsid, qr);
    lv_refr_now(nullptr);

    WiFi.persistent(false); // nothing written to NVS by the Wi-Fi stack; cfg::save is the only write
    WiFi.onEvent(onWifiEvent);
    WiFi.setHostname("powerwall-display");
    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(false); // joins are driven by joinTick (an auto-retry would sweep channels)

    // Scan first, then start the AP on the strongest network's channel: if that is the home network, joining it
    // later doesn't move the AP (the AP must follow the STA's channel, which drops the phone for a moment).
    const int16_t n = WiFi.scanNetworks(false, false);
    if (n >= 0) collectScan(n);
    const uint8_t ch = s_netCount && s_mem->nets[0].ch >= 1 && s_mem->nets[0].ch <= 13 ? s_mem->nets[0].ch : 1;
    WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
    if (!WiFi.softAP(s_apSsid, nullptr, ch, 0, 4)) Serial.println("[setup] soft AP failed to start");
    Serial.printf("[setup] hotspot '%s' (open) on channel %u, %d networks seen\n", s_apSsid, ch, s_netCount);

    s_dns.setErrorReplyCode(DNSReplyCode::NoError);
    s_dns.start(53, "*", AP_IP);

    const char *hdrs[] = {"Host"};
    s_server.collectHeaders(hdrs, 1);
    s_server.on("/", HTTP_GET, handleRoot);
    s_server.on("/api/state", HTTP_GET, handleState);
    s_server.on("/api/scan", HTTP_GET, handleScan);
    s_server.on("/api/zones", HTTP_GET, handleZones);
    s_server.on("/api/geocode", HTTP_GET, handleGeocode);
    s_server.on("/api/wifi", HTTP_POST, handleWifi, rawBody);
    s_server.on("/api/discover", HTTP_POST, handleDiscover, rawBody);
    s_server.on("/api/save", HTTP_POST, handleSave, rawBody);
    // OS captive-portal probes (Android, Apple, Windows, Firefox ...): a redirect makes the phone open the page
    static const char *const PROBES[] = {"/generate_204", "/gen_204", "/hotspot-detect.html", "/library/test/success.html",
                                         "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink", "/canonical.html",
                                         "/success.txt"};
    for (const char *p : PROBES) s_server.on(p, HTTP_GET, redirectHome); // other methods: handleNotFound redirects too
    s_server.onNotFound(handleNotFound);
    s_server.begin();

    s_jobs = xQueueCreate(4, sizeof(uint8_t));
    // Core 0, low priority; TLS (handshake) needs a large stack
    xTaskCreatePinnedToCore(workerTask, "setup_job", 12288, nullptr, 1, nullptr, 0);

    lv_timer_create(screenTimerCb, 250, nullptr);
    Serial.printf("[setup] ready: internal free %u, psram free %u\n", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)ESP.getFreePsram());

    for (;;) {
        s_dns.processNextRequest(); // no-op with the core's async DNSServer, kept for older cores
        s_server.handleClient();
        scanTick();
        joinTick();
        const uint32_t idle = lv_timer_handler();
        heartbeat();
        const uint32_t at = s_restartAt.load();
        if (at && (int32_t)(millis() - at) >= 0) {
            Serial.println("[setup] restarting");
            Serial.flush();
            s_server.stop();
            delay(100);
            ESP.restart();
        }
        delay(idle < 5 ? idle : 5);
    }
}
