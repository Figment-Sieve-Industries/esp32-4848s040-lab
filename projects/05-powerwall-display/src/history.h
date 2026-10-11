#pragma once
// SD card for project 05: mounting and hot-swap, the per-minute CSV log, the backup of today's energy
// baseline (so a power cut doesn't reset the Today totals), and saved settings.
// All card I/O runs in one low-priority task on core 0 (SdStore, common/lib). UI code (core 1)
// only copies data in and out; nothing here blocks the UI for more than a mutex hand-off, except
// historyLoadBaseline() (one small read, once after boot).
// Nothing is written to the ESP32's internal flash.
#include <Arduino.h>
#include <time.h>

// Start the SD task. Call once in setup(), AFTER board4848::initDisplay() (SD shares the LCD's
// init-bus pins). The card is mounted asynchronously; no card is fine (polled every 2 s).
void historyStart();

// ---- Status (for the settings screen). Thread-safe copy. ----
struct SdStatus {
    bool firstMountDone = false; // the boot-time mount attempt has finished (card or not)
    bool mounted = false;
    char cardType[12] = "";      // "SDHC/SDXC", "SDSC", ...
    uint32_t clockMHz = 0;       // SPI clock the card proved (self-tuned)
    uint64_t totalBytes = 0, usedBytes = 0;
    uint32_t ioErrors = 0;       // failed I/O attempts since boot (retries count)
    uint32_t cardLost = 0;       // times the card was pulled/lost while mounted
    uint32_t clockStepDowns = 0; // times errors made the clock step down
    char logFile[32] = "";       // today's log, e.g. "/powerwall/2026-10-11.csv"
    uint32_t linesToday = 0;     // minute records written today (this boot; 0 after a reboot until the next write)
    uint32_t lastWriteAgeS = UINT32_MAX; // seconds since the last successful log write
    bool baselineSaved = false;  // today's energy baseline is on the card
};
SdStatus historyStatus();

// ---- Energy baseline backup (energy.cpp) ----
// Queue a copy of the baseline record for the SD task to save (power-cut-safe, writeAtomic).
// Cheap: copies into a buffer under a mutex. Call when the baseline changes and every few minutes.
void historySaveBaseline(const void *data, size_t len);
// Blocking read of the saved baseline. False if no card, nothing saved, or len mismatch.
// Call only after historyStatus().firstMountDone (energy.cpp waits for it, up to a limit).
bool historyLoadBaseline(void *data, size_t len);

// ---- Settings (saved on the card; defaults until a card is read) ----
struct AppSettings {
    uint8_t brightness = 255; // backlight duty, B4848_LCD_BL_MIN..255
};
AppSettings settingsGet();
// Applies the backlight immediately and queues a save to the card.
void settingsSetBrightness(uint8_t level);
