#pragma once
// IANA time zone name -> POSIX TZ string, for the setup portal (the geocoder returns an IANA name; newlib's
// tzset() needs the POSIX form). Pure C++ (host-tested in hosttest/test_setup.inc).
//
// POSIX form: STDoffset[DST[offset][,start[/time],end[/time]]]. The offset is the time to ADD to local time to get
// UTC, so the sign is the opposite of the usual "UTC+1": Paris is CET-1. Mm.w.d = month m, week w (5 = last), day d
// (0 = Sunday); /time is the local switch time (default 02:00). Rules as in the tz database (2025b) for the current
// year; zones without DST have no rule part.
//
// Zone abbreviations are letters only (e.g. "SGT-8" rather than tzdata's "<+08>-8"): only the offsets and rules
// matter to the firmware, which never prints %Z, and plain letters avoid relying on newlib's <...> quoting.
// Dublin uses "GMT0IST,M3.5.0/1,M10.5.0" (same clock as tzdata's negative-DST "IST-1GMT0,M10.5.0,M3.5.0/1").
#include <string.h>

namespace setuptz {

struct Zone {
    const char *iana;
    const char *posix;
};

// Sorted by IANA name (the host test checks the order and that every entry parses).
constexpr Zone ZONES[] = {
    {"Africa/Johannesburg", "SAST-2"},
    {"Africa/Lagos", "WAT-1"},
    {"Africa/Nairobi", "EAT-3"},
    {"America/Anchorage", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"America/Argentina/Buenos_Aires", "ART3"},
    {"America/Bogota", "COT5"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Detroit", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Edmonton", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Halifax", "AST4ADT,M3.2.0,M11.1.0"},
    {"America/Lima", "PET5"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Mexico_City", "CST6"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Phoenix", "MST7"},
    {"America/Puerto_Rico", "AST4"},
    {"America/Regina", "CST6"},
    {"America/Sao_Paulo", "BRT3"},
    {"America/St_Johns", "NST3:30NDT,M3.2.0,M11.1.0"},
    {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Winnipeg", "CST6CDT,M3.2.0,M11.1.0"},
    {"Asia/Bangkok", "ICT-7"},
    {"Asia/Dhaka", "BST-6"},
    {"Asia/Dubai", "GST-4"},
    {"Asia/Hong_Kong", "HKT-8"},
    {"Asia/Jakarta", "WIB-7"},
    {"Asia/Karachi", "PKT-5"},
    {"Asia/Kathmandu", "NPT-5:45"},
    {"Asia/Kolkata", "IST-5:30"},
    {"Asia/Kuala_Lumpur", "MYT-8"},
    {"Asia/Manila", "PHT-8"},
    {"Asia/Riyadh", "AST-3"},
    {"Asia/Seoul", "KST-9"},
    {"Asia/Shanghai", "CST-8"},
    {"Asia/Singapore", "SGT-8"},
    {"Asia/Taipei", "CST-8"},
    {"Asia/Tokyo", "JST-9"},
    {"Atlantic/Canary", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Atlantic/Madeira", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Atlantic/Reykjavik", "GMT0"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"},
    {"Australia/Darwin", "ACST-9:30"},
    {"Australia/Hobart", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth", "AWST-8"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Etc/UTC", "UTC0"},
    {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Belgrade", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Bratislava", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Bucharest", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Budapest", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Copenhagen", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Dublin", "GMT0IST,M3.5.0/1,M10.5.0"},
    {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Isle_of_Man", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Istanbul", "TRT-3"},
    {"Europe/Jersey", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Kyiv", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Ljubljana", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Luxembourg", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Malta", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Moscow", "MSK-3"},
    {"Europe/Oslo", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Prague", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Riga", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Sofia", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Stockholm", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Tallinn", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Vienna", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Vilnius", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Warsaw", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zagreb", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/Zurich", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Honolulu", "HST10"},
};
constexpr size_t ZONE_COUNT = sizeof(ZONES) / sizeof(ZONES[0]);

// Old names the geocoder may still return, mapped to their current entry.
constexpr Zone ALIASES[] = {
    {"Asia/Calcutta", "Asia/Kolkata"},
    {"Europe/Kiev", "Europe/Kyiv"},
    {"Etc/GMT", "Etc/UTC"},
    {"UTC", "Etc/UTC"},
    {"GMT", "Etc/UTC"},
    {"Europe/Belfast", "Europe/London"},
    {"Europe/Guernsey", "Europe/London"},
    {"Australia/Canberra", "Australia/Sydney"},
    {"Australia/ACT", "Australia/Sydney"},
    {"Australia/NSW", "Australia/Sydney"},
    {"America/Montreal", "America/Toronto"},
    {"America/Indiana/Indianapolis", "America/New_York"},
    {"America/Kentucky/Louisville", "America/New_York"},
    {"America/Boise", "America/Denver"},
    {"Pacific/Johnston", "Pacific/Honolulu"},
};

// POSIX string for an IANA name (or an alias), nullptr if the zone is not in the table.
inline const char *posixFor(const char *iana) {
    if (!iana || !*iana) return nullptr;
    for (const Zone &a : ALIASES) {
        if (strcmp(a.iana, iana) == 0) {
            iana = a.posix;
            break;
        }
    }
    for (const Zone &z : ZONES) {
        if (strcmp(z.iana, iana) == 0) return z.posix;
    }
    return nullptr;
}

} // namespace setuptz
