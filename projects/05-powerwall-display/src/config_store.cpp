// User configuration store (see config_store.h for the contract and load order). Validation and the JSON mapping
// live in config_validate.h (host-tested); this file adds NVS, the SD import and the secrets.h fallback.
#include "config_store.h"
#include "config_validate.h"
#include "history.h"
#include "mem_util.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <sd_store.h>

#if __has_include("secrets.h")
#include "secrets.h"
#define CFG_HAVE_SECRETS 1
#endif

namespace cfg {
namespace {

constexpr const char *NVS_NS = "pwdisp";
constexpr const char *NVS_KEY = "cfg";
constexpr uint32_t BLOB_MAGIC = 0x50574346; // "PWCF"
constexpr uint16_t BLOB_VERSION = 1;        // bump when Config's layout changes (old blobs are then ignored)
constexpr const char *SD_FILE = "/powerwall/config.json";
constexpr const char *SD_DONE = "/powerwall/config.imported.json";
constexpr uint32_t SD_WAIT_MS = 3000;
constexpr size_t SD_MAX_BYTES = 6144;

struct Blob {
    uint32_t magic;
    uint16_t version;
    uint16_t size; // sizeof(Config)
    uint32_t crc;  // CRC-32 of the canonical Config bytes
    Config cfg;
};
static_assert(sizeof(Blob) < 1900, "keep the NVS blob small");

Config s_cfg;
Source s_src = Source::None;
Blob s_blob; // scratch (begin/save run on the UI thread only)

void makeBlob(const Config &canon, Blob &b) {
    memset(&b, 0, sizeof b);
    b.magic = BLOB_MAGIC;
    b.version = BLOB_VERSION;
    b.size = sizeof(Config);
    memcpy(&b.cfg, &canon, sizeof canon); // byte copy: keeps the zeroed padding of the canonical form
    b.crc = cfgv::crc32(&b.cfg, sizeof(Config));
}

bool loadNvs(Config &out) {
    Preferences p;
    if (!p.begin(NVS_NS, true)) return false; // namespace never written
    const size_t n = p.getBytesLength(NVS_KEY);
    bool ok = n == sizeof(Blob) && p.getBytes(NVS_KEY, &s_blob, sizeof(Blob)) == sizeof(Blob);
    p.end();
    if (!ok) return false;
    if (s_blob.magic != BLOB_MAGIC || s_blob.version != BLOB_VERSION || s_blob.size != sizeof(Config) ||
        s_blob.crc != cfgv::crc32(&s_blob.cfg, sizeof(Config))) {
        Serial.println("config: stored blob rejected (magic/version/CRC)");
        return false;
    }
    char why[65];
    cfgv::canonical(s_blob.cfg, out);
    if (!cfgv::validate(out, why, sizeof why)) {
        Serial.printf("config: stored blob invalid: %s\n", why);
        return false;
    }
    return true;
}

// One-time import of /powerwall/config.json. Card I/O on the UI thread, but only once at boot before the first
// screen is shown and for one small file (same reasoning as energy.cpp's baseline restore).
bool importSd(Config &out) {
    const uint32_t t0 = millis();
    while (!historyStatus().firstMountDone && millis() - t0 < SD_WAIT_MS) delay(20);
    if (!sdcard::mounted()) return false;
    bool exists;
    {
        sdcard::Lock lk;
        exists = SD.exists(SD_FILE);
    }
    if (!exists) return false;
    HeapBuf buf;
    if (!buf.psram(SD_MAX_BYTES + 1, true)) return false;
    size_t len = 0;
    if (!sdcard::readFile(SD_FILE, buf.p, SD_MAX_BYTES, &len)) {
        Serial.println("config: cannot read " + String(SD_FILE));
        return false;
    }
    char why[65];
    Config c;
    if (len >= SD_MAX_BYTES) {
        Serial.println("config: config.json ignored: file too large");
        return false;
    }
    if (!cfgv::parseJson(buf.as<const char>(), len, c, why, sizeof why)) {
        Serial.printf("config: config.json ignored: %s\n", why); // reason only, never field values
        return false;
    }
    if (!save(c, why, sizeof why)) {
        Serial.printf("config: config.json valid but not saved: %s\n", why);
        out = s_cfg; // save() didn't touch it; fall through to use c for this boot
        cfgv::canonical(c, out);
        return true;
    }
    {
        sdcard::Lock lk;
        if (SD.exists(SD_DONE)) SD.remove(SD_DONE); // FatFs can't rename over an existing file
        if (!SD.rename(SD_FILE, SD_DONE)) Serial.println("config: imported, but renaming config.json failed");
    }
    out = s_cfg;
    Serial.println("config: imported config.json from the SD card");
    return true;
}

bool loadSecrets(Config &out) {
#ifdef CFG_HAVE_SECRETS
    memset(&out, 0, sizeof out);
    int n = 0;
    for (const WifiCred &w : WIFI_NETWORKS) {
        if (n >= MAX_WIFI) {
            Serial.println("config: secrets.h lists more than 3 networks, extras ignored");
            break;
        }
        copyStr(out.wifi[n].ssid, sizeof out.wifi[n].ssid, w.ssid);
        copyStr(out.wifi[n].pass, sizeof out.wifi[n].pass, w.password);
        n++;
    }
    out.wifiCount = static_cast<uint8_t>(n);
    copyStr(out.pwHost, sizeof out.pwHost, PW_HOST);
    copyStr(out.pwPassword, sizeof out.pwPassword, PW_CUSTOMER_PASSWORD);
    out.lat = static_cast<float>(WEATHER_LAT);
    out.lon = static_cast<float>(WEATHER_LON);
    copyStr(out.placeName, sizeof out.placeName, WEATHER_NAME);
    copyStr(out.tz, sizeof out.tz, TZ_INFO);
    out.auroraUk = true;
    return out.wifiCount >= 1 && out.pwHost[0];
#else
    (void)out;
    return false;
#endif
}

} // namespace

bool begin() {
    memset(&s_cfg, 0, sizeof s_cfg);
    s_src = Source::None;
    Config c;
    if (loadNvs(c)) {
        s_cfg = c;
        s_src = Source::Nvs;
    } else if (importSd(c)) {
        s_cfg = c;
        s_src = Source::SdImport;
    } else if (loadSecrets(c)) {
        s_cfg = c;
        s_src = Source::Secrets;
    }
    static const char *const NAMES[] = {"none (setup needed)", "NVS", "SD import", "secrets.h defaults"};
    Serial.printf("config: source %s\n", NAMES[static_cast<int>(s_src)]);
    return configured();
}

bool configured() { return s_src != Source::None && s_cfg.wifiCount >= 1 && s_cfg.pwHost[0]; }
Source source() { return s_src; }
const Config &get() { return s_cfg; }

bool save(const Config &in, char *why, size_t whyLen) {
    Config canon;
    cfgv::canonical(in, canon);
    if (!cfgv::validate(canon, why, whyLen)) return false;
    makeBlob(canon, s_blob);
    static Blob stored; // not on the stack
    Preferences p;
    if (!p.begin(NVS_NS, false)) return cfgv::fail(why, whyLen, "cannot open storage");
    const bool same = p.getBytesLength(NVS_KEY) == sizeof(Blob) &&
                      p.getBytes(NVS_KEY, &stored, sizeof(Blob)) == sizeof(Blob) &&
                      memcmp(reinterpret_cast<const uint8_t *>(&stored), reinterpret_cast<const uint8_t *>(&s_blob), sizeof(Blob)) == 0;
    bool ok = true;
    if (!same) ok = p.putBytes(NVS_KEY, &s_blob, sizeof(Blob)) == sizeof(Blob); // the only NVS write
    p.end();
    if (!ok) return cfgv::fail(why, whyLen, "storage write failed");
    s_cfg = canon;
    s_src = Source::Nvs;
    return true;
}

bool clear() {
    Preferences p;
    if (!p.begin(NVS_NS, false)) return false;
    const bool ok = !p.isKey(NVS_KEY) || p.remove(NVS_KEY);
    p.end();
    if (ok) {
        memset(&s_cfg, 0, sizeof s_cfg);
        s_src = Source::None;
    }
    return ok;
}

bool fromJson(const char *json, size_t len, Config &out, char *why, size_t whyLen) {
    return cfgv::parseJson(json, len, out, why, whyLen);
}

size_t toJson(const Config &c, bool includeSecrets, char *buf, size_t len) {
    return cfgv::writeJson(c, includeSecrets, buf, len);
}

} // namespace cfg
