#pragma once
// sd_tests - test suites for sd_store, driven from the touch UI in main.cpp.
// Every test writes under /sdtest only and removes it at the end (the soak file is kept until the next run).
#include <Arduino.h>

namespace sdtests {

// One finished line of output, no trailing newline. main.cpp prints it to serial and to the screen
// log. Lines starting with "PASS", "FAIL", "WARN" or "==" are coloured green / red / amber / white.
using LogFn = void (*)(const char *line);

struct Result {
    uint16_t passed;
    uint16_t failed;
};

// Full functional suite (blocking, a few seconds to ~30 s). Needs a mounted card; logs FAIL and
// returns {0,1} if there is none.
Result runAll(LogFn log);

// Remount at 1, 4, 10, 20 and 25 MHz. At each: mount ok?, 64 KB write+verify, 512 KB write and
// read throughput. Then remounts at the fastest clock that passed, minus one step for margin, and logs it.
void speedSweep(LogFn log);

// Decode and log the card's registers (manufacturer, model, date, rated clock, timings, speed class)
// plus the raw hex, for looking the card up and choosing settings.
void cardReport(LogFn log);

// Soak: appends one CSV record per second to /sdtest/soak.csv while the UI keeps running, reads
// the last record back every 10th write. Survives card removal (counts errors, resumes when back).
void soakStart(LogFn log);
void soakStop(LogFn log);
bool soakRunning();
void soakStep(LogFn log); // call every loop(); does nothing unless running and a second has passed

struct SoakStats {
    uint32_t written;     // records appended OK
    uint32_t verified;    // read-backs that matched
    uint32_t errors;      // failed appends or mismatched read-backs
    uint32_t maxAppendMs; // slowest appendLine()
    uint32_t avgAppendUs; // mean appendLine() time
};
SoakStats soakStats();

} // namespace sdtests
