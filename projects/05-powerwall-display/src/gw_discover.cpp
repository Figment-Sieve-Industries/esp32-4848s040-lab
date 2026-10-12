// Powerwall gateway discovery (see gw_discover.h and ADR-38 in docs/guide/04-patterns-and-decisions.md).
//
// Phase 1: TCP connect to port 443 on every other host of the STA's private /24, a few non-blocking sockets at a
//          time (lwip has only ~10 sockets in total), 300 ms per batch.
// Phase 2: for each host with 443 open, one TLS session (setInsecure: the gateway's certificate is self-signed)
//          and up to three unauthenticated GETs, scored by gw_discover_calc.h. Never a login, never a POST.
#include "gw_discover.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <errno.h>
#include <fcntl.h>
#include <lwip/sockets.h>
#include <string.h>

#include "gw_discover_calc.h"
#include "mem_util.h"
#include "net_http.h"
#include "shared_state.h"

namespace gwdisc {
namespace {

constexpr int PORT = 443;
constexpr int BATCH = 5;                  // parallel connects; leaves lwip sockets for the portal and DNS
constexpr uint32_t CONNECT_MS = 300;      // per batch
constexpr uint32_t PROBE_BUDGET_MS = 3000; // probe(): whole call
constexpr uint32_t HTTP_MS = 1200;        // connect / read timeout of one request
constexpr size_t BODY_CAP = 640;          // enough for the status document and error JSON
constexpr uint32_t TASK_STACK = 12288;    // one mbedTLS handshake on the stack side plus HTTPClient

Shared<Result> s_state;
Mutex s_probeMutex; // one TLS session at a time (scan task and portal "check" both end up in probeLocked)
bool s_taskRunning = false; // guarded by s_state's lock via with()

void setError(const char *msg) {
    s_state.with([&](Result &r) {
        r.running = false;
        r.done = true;
        strncpy(r.error, msg, sizeof(r.error) - 1);
        r.error[sizeof(r.error) - 1] = '\0';
        return 0;
    });
}

// Non-blocking connect attempts to port 443 for hosts[0..n); open[i] set for those that accepted.
void connectBatch(const uint32_t *hosts, int n, bool *open) {
    int fds[BATCH];
    for (int i = 0; i < n; i++) {
        open[i] = false;
        fds[i] = -1;
        const int fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0 || fd >= FD_SETSIZE) {
            if (fd >= 0) lwip_close(fd);
            continue;
        }
        lwip_fcntl(fd, F_SETFL, lwip_fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
        sockaddr_in sa = {};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(PORT);
        sa.sin_addr.s_addr = htonl(hosts[i]);
        const int rc = lwip_connect(fd, reinterpret_cast<sockaddr *>(&sa), sizeof(sa));
        if (rc == 0) open[i] = true;
        if (rc == 0 || errno != EINPROGRESS) {
            lwip_close(fd);
            continue;
        }
        fds[i] = fd;
    }
    const uint32_t t0 = millis();
    for (;;) {
        fd_set wr;
        FD_ZERO(&wr);
        int maxfd = -1, pending = 0;
        for (int i = 0; i < n; i++) {
            if (fds[i] < 0) continue;
            FD_SET(fds[i], &wr);
            if (fds[i] > maxfd) maxfd = fds[i];
            pending++;
        }
        if (!pending) break;
        const uint32_t el = millis() - t0;
        if (el >= CONNECT_MS) break;
        timeval tv;
        const uint32_t left = CONNECT_MS - el;
        tv.tv_sec = 0;
        tv.tv_usec = static_cast<suseconds_t>(left) * 1000;
        if (lwip_select(maxfd + 1, nullptr, &wr, nullptr, &tv) <= 0) break;
        for (int i = 0; i < n; i++) {
            if (fds[i] < 0 || !FD_ISSET(fds[i], &wr)) continue;
            int err = 0;
            socklen_t len = sizeof(err);
            if (lwip_getsockopt(fds[i], SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) open[i] = true;
            lwip_close(fds[i]);
            fds[i] = -1;
        }
    }
    for (int i = 0; i < n; i++)
        if (fds[i] >= 0) lwip_close(fds[i]);
}

// One GET over the shared session. code <= 0 on failure; body NUL-terminated in buf.
void getInto(HTTPClient &http, WiFiClientSecure &client, const char *host, const char *path, char *buf, Probe &out) {
    buf[0] = '\0';
    out.code = 0;
    out.body = buf;
    if (!http.begin(client, String("https://") + host + path)) return;
    http.setUserAgent(net::USER_AGENT);
    http.setReuse(true);
    http.setConnectTimeout(HTTP_MS);
    http.setTimeout(HTTP_MS);
    const int code = http.GET();
    out.code = code;
    if (code > 0) {
        size_t len = 0;
        net::readBody(http, buf, BODY_CAP, len, HTTP_MS); // partial bodies still fine for fingerprinting
    }
    http.end();
}

bool probeLocked(const char *host, bool verbose) {
    uint32_t ip;
    if (!parseIPv4(host, ip) || !isPrivate(ip)) return false;
    HeapBuf bufs;
    if (!bufs.psram(BODY_CAP * 3)) return false;
    char *b = bufs.as<char>();
    const uint32_t t0 = millis();

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(2);
    net::noteTlsConnect();
    HTTPClient http;
    Probe st, soe, root;
    getInto(http, client, host, "/api/status", b, st);
    if (st.code <= 0) {
        if (verbose) Serial.printf("GwDisc: %s TLS/HTTP failed (%d)\n", host, st.code);
        return false;
    }
    if (!isStatusDoc(st) && millis() - t0 < PROBE_BUDGET_MS - HTTP_MS)
        getInto(http, client, host, "/api/system_status/soe", b + BODY_CAP, soe);
    if (score(st, soe, root) < ACCEPT_SCORE && score(st, soe, root) + 1 >= ACCEPT_SCORE &&
        millis() - t0 < PROBE_BUDGET_MS - HTTP_MS)
        getInto(http, client, host, "/", b + 2 * BODY_CAP, root);
    http.end();
    client.stop();
    const int sc = score(st, soe, root);
    // Codes and flags only: bodies hold the gateway's DIN, which stays out of the log.
    if (verbose)
        Serial.printf("GwDisc: %s status=%d%s soe=%d root=%d%s score=%d %lu ms\n", host, st.code,
                      isStatusDoc(st) ? "(doc)" : (looksJson(st.body) ? "(json)" : ""), soe.code, root.code,
                      mentionsTesla(root) ? "(tesla)" : "", sc, (unsigned long)(millis() - t0));
    return sc >= ACCEPT_SCORE;
}

void scanTask(void *) {
    uint32_t self = 0;
    {
        IPAddress ip = WiFi.localIP();
        self = ((uint32_t)ip[0] << 24) | ((uint32_t)ip[1] << 16) | ((uint32_t)ip[2] << 8) | ip[3];
    }
    uint32_t *hosts = static_cast<uint32_t *>(psramAlloc(254 * sizeof(uint32_t)));
    const int total = hosts ? enumerate24(self, hosts, 254) : 0;
    if (!hosts) setError("out of memory");
    else if (total == 0) setError("not a private network");
    else {
        s_state.with([&](Result &r) {
            r.total = total;
            return 0;
        });
        const uint32_t t0 = millis();
        uint32_t cand[16];
        int nc = 0;
        for (int i = 0; i < total; i += BATCH) {
            const int n = total - i < BATCH ? total - i : BATCH;
            bool open[BATCH];
            connectBatch(hosts + i, n, open);
            for (int k = 0; k < n && nc < 16; k++)
                if (open[k]) cand[nc++] = hosts[i + k];
            s_state.with([&](Result &r) {
                r.scanned = i + n;
                return 0;
            });
            vTaskDelay(1);
        }
        Serial.printf("GwDisc: %d hosts scanned in %lu ms, %d with 443 open\n", total, (unsigned long)(millis() - t0), nc);
        for (int k = 0; k < nc; k++) {
            char h[16];
            formatIPv4(cand[k], h, sizeof(h));
            bool ok;
            {
                LockGuard g(s_probeMutex);
                ok = probeLocked(h, true);
            }
            if (ok)
                s_state.with([&](Result &r) {
                    if (r.count < MAX_FOUND) strncpy(r.hosts[r.count++], h, sizeof(r.hosts[0]) - 1);
                    return 0;
                });
        }
        s_state.with([&](Result &r) {
            r.running = false;
            r.done = true;
            return 0;
        });
        Serial.printf("GwDisc: done, %d found, %lu ms\n", s_state.get().count, (unsigned long)(millis() - t0));
    }
    heap_caps_free(hosts);
    s_state.with([&](Result &) {
        s_taskRunning = false;
        return 0;
    });
    vTaskDelete(nullptr);
}

} // namespace

bool start() {
    if (WiFi.status() != WL_CONNECTED) return false;
    const bool ok = s_state.with([&](Result &r) {
        if (s_taskRunning) return false;
        r = Result();
        r.running = true;
        s_taskRunning = true;
        return true;
    });
    if (!ok) return false;
    if (xTaskCreatePinnedToCore(scanTask, "gw_disc", TASK_STACK, nullptr, tskIDLE_PRIORITY, nullptr, 0) != pdPASS) {
        setError("cannot start scan");
        s_state.with([&](Result &) {
            s_taskRunning = false;
            return 0;
        });
        return false;
    }
    return true;
}

Result status() { return s_state.get(); }

bool probe(const char *host) {
    LockGuard g(s_probeMutex);
    return probeLocked(host, true);
}

} // namespace gwdisc
