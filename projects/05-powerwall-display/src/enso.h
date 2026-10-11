#pragma once
// El Nino / ENSO state for the ENSO screen (docs/11-space-weather-and-climate-data.md), from
// NOAA CPC (www.cpc.ncep.noaa.gov, Go Daddy Root CA G2). Static text files fetched with HTTP Range
// tails, plus the whole ENSO discussion page (Range bytes=0-16383), streamed through a tag stripper.
// Fetched by the network task in powerwall.cpp about twice a day. Getter returns a thread-safe copy.
#include <stdint.h>
#include <math.h>
#include <time.h>

enum class EnsoAlert : int8_t { Unknown = -1, None = 0, ElNinoWatch, ElNinoAdvisory, LaNinaWatch, LaNinaAdvisory, FinalAdvisory };

struct EnsoData {
    bool valid = false;
    EnsoAlert alert = EnsoAlert::Unknown;
    char alertText[32] = "";      // as CPC writes it, ASCII only: "El Nino Advisory"
    char synopsis[160] = "";      // first sentence after "Synopsis:", ASCII only (n-tilde -> n)
    // Weekly Nino 3.4 (wksst9120.for, last row)
    float weeklyAnom = NAN;       // degC
    char weeklyDate[12] = "";     // "30SEP2026"
    // Latest 3-month indices
    float oni = NAN, roni = NAN;
    char oniSeason[4] = "";       // "JAS"
    char roniSeason[4] = "";
    // Monthly Nino 3.4 anomaly history (sstoi.indices), oldest first
    static constexpr int MONTHS = 24;
    int monthCount = 0;
    float monthAnom[MONTHS] = {};
    int lastYear = 0, lastMonth = 0; // of the newest monthly row (month 1-12)
    // Probabilities from the discussion ("54% ... SON, 83% ... OND, 70% ... NDJ"); -1 = unknown
    static constexpr int PROBS = 3;
    int probCount = 0;
    int8_t probPct[PROBS] = {-1, -1, -1};
    char probSeason[PROBS][4] = {};
    char probWhat[24] = "";       // what the odds are for, e.g. "strong+" (best effort, may be empty)
    char nextUpdate[16] = "";     // "12 Nov" from "next ENSO diagnostics discussion ... 12 November 2026"
    uint32_t fetchedMs = 0;
};

EnsoData ensoData();

constexpr uint32_t ENSO_STALE_MS = 3UL * 24UL * 60UL * 60UL * 1000UL; // 3 days

// Called only by the network task (one TLS session at a time). One keep-alive connection for all files;
// returns false (data still swapped in) when a part failed or the job's time budget ran out.
bool ensoFetch();
