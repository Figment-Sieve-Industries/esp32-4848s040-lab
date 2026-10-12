// STUB (contract placeholder for config_store.h): returns "configured" so the firmware keeps using secrets.h.
#include "config_store.h"
#include <string.h>

namespace cfg {
namespace {
Config s_cfg;
void reason(char *why, size_t n, const char *text) {
    if (why && n) {
        strncpy(why, text, n - 1);
        why[n - 1] = '\0';
    }
}
} // namespace
bool begin() { return true; }
bool configured() { return true; }
Source source() { return Source::Secrets; }
const Config &get() { return s_cfg; }
bool save(const Config &, char *why, size_t n) {
    reason(why, n, "not implemented");
    return false;
}
bool clear() { return false; }
bool fromJson(const char *, size_t, Config &, char *why, size_t n) {
    reason(why, n, "not implemented");
    return false;
}
size_t toJson(const Config &, bool, char *buf, size_t len) {
    if (buf && len) buf[0] = '\0';
    return 0;
}
} // namespace cfg
