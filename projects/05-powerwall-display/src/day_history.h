#pragma once
// Per-day energy history for the history screen (day / week / month charts).
//
// Data: one DayRecord per local date (day totals + 24 hourly bins), kept on the SD card in month files
// /powerwall/days/YYYY-MM.bin (31 fixed slots, slot = day of month - 1, written with sdcard::writeAtomic).
// Today's record is built live on the UI thread from the gateway's lifetime counters (dayHistoryUpdate)
// and mirrored in RTC memory; the SD task saves it every hour, every 10 minutes and at midnight.
// Past days are only ever read on the SD task (dayHistorySdTick); the UI never touches the card.
//
// Threads: dayHistoryUpdate / dayHistoryRequest / dayHistoryView run on the LVGL core (core 1).
// dayHistorySdTick runs in history.cpp's SD task (core 0). State is shared under one mutex.
#include <stdint.h>
#include <stddef.h>
#include "energy.h"
#include "powerwall.h"

namespace dayhist {

constexpr uint32_t RECORD_MAGIC = 0x44415931; // "DAY1"
constexpr uint8_t SOC_NONE = 0xFF;

enum DayFlags : uint8_t {
    DAY_PRESENT = 0x01, // the slot holds a record
    DAY_PARTIAL = 0x02, // totals start after midnight (power cut / first boot that day)
};

// One local day. Fixed layout: it is stored raw on the card (zero the whole struct before filling it).
struct DayRecord {
    uint32_t magic;          // RECORD_MAGIC
    int32_t date;            // yyyymmdd, local
    uint8_t flags;           // DayFlags
    uint8_t pad[3];
    float solar, home, exp, imp, battIn, battOut; // day totals, kWh
    float hSolar[24], hHome[24], hImp[24], hExp[24]; // per local hour, kWh (DST: the repeated hour adds into one bin)
    uint8_t soc[24];         // battery % at the end of each hour (last reading in it), SOC_NONE = none
    uint32_t crc;            // crc32 over everything before it
};
static_assert(sizeof(DayRecord) == 448, "on-card layout");

enum class Mode : uint8_t { Day, Week, Month };

constexpr int MAX_SLOTS = 31;

// What the screen draws. Slots are hours (Day: 24) or days (Week: 7, Monday first; Month: 28..31).
struct View {
    uint32_t seq = 0;        // changes whenever anything in the view changes (cheap change test)
    Mode mode = Mode::Day;
    int offset = 0;          // 0 = the current period, -1 = the one before, ...
    bool loading = false;    // past data still being read from the card (draw "--" / empty bars)
    bool noCard = false;     // a past period was asked for but there is no card
    bool canNext = false;    // offset < 0
    char label[24] = "";     // "Today", "Yesterday", "Sat 10 Oct" / "5-11 Oct", "This week" / "October 2026"
    int count = 0;           // number of slots
    int firstDay = 0;        // Week/Month: day of month of slot 0 (Week may cross a month: use dates[])
    int32_t dates[MAX_SLOTS] = {};       // Week/Month: yyyymmdd of each slot; Day: the day's date in dates[0]
    float solar[MAX_SLOTS] = {}, home[MAX_SLOTS] = {}, imp[MAX_SLOTS] = {}, exp[MAX_SLOTS] = {};
    uint8_t soc[24] = {};    // Day only, SOC_NONE where unknown
    bool have[MAX_SLOTS] = {};   // data present for the slot (false: future, before the log started, no card)
    bool future[MAX_SLOTS] = {}; // slot is after now (draw a dim stub)
    int nowSlot = -1;        // slot containing now (current period only), else -1
    bool partial = false;    // Day: the day's totals are partial
    // Period totals over the slots with data (Day: the record's own totals, which include gap energy)
    float totSolar = 0, totHome = 0, totExp = 0, totImp = 0;
    bool haveTotals = false;
};

// UI thread, every refresh (main.cpp calls it right after energyUpdate). Folds the latest reading into
// today's hourly bins and queues saves. Cheap when the reading is not new.
void dayHistoryUpdate(const PwData &d, const TodayStats &t);

// UI thread: ask for a period. Current periods are answered from memory (+ the card for the earlier days
// of a week or month); past ones are read by the SD task. Repeating the same request is free.
void dayHistoryRequest(Mode mode, int offset);

// UI thread: copy of the view for the last request, with today's slot overlaid live. Never blocks on I/O.
void dayHistoryView(View &out);

// SD task only (history.cpp's loop, every 250 ms): saves due records and serves pending reads.
void dayHistorySdTick(bool mounted);

// Diagnostics for the serial "y" command: one line describing records saved / loaded / errors.
void dayHistoryStatusLine(char *buf, size_t n);

} // namespace dayhist
