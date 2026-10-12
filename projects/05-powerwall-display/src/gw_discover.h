#pragma once
// Powerwall gateway discovery on the local /24 (used by the setup portal's "Find it" button).
// Needs the Wi-Fi STA to be connected. One scan at a time, in its own low-priority task on core 0; the portal polls
// status(). Only private (RFC 1918) /24 networks are scanned, never more than 254 hosts.
#include <stdint.h>

namespace gwdisc {

constexpr int MAX_FOUND = 4;

struct Result {
    bool running = false;
    bool done = false;
    int scanned = 0, total = 0;       // hosts probed so far / to probe
    int count = 0;                    // gateways found
    char hosts[MAX_FOUND][16] = {};   // dotted IPv4
    char error[48] = "";              // e.g. "Wi-Fi not connected", "not a private network"
};

// Start a scan of the STA's /24. False if one is already running or the STA is not connected.
bool start();
// Thread-safe copy of the current state.
Result status();
// Check one host: does it look like a Powerwall gateway? Blocking, a few seconds at most. Used by start()'s task and
// by the portal to validate a typed address.
bool probe(const char *host);

} // namespace gwdisc
