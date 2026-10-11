// Moon phase and sun altitude (see moon_calc.h). Meeus, "Astronomical Algorithms" 2nd ed.
// Pure math with doubles; no allocation. Phase instants ~1 minute, illuminated fraction ~0.1%.
#include "moon_calc.h"

#include <math.h>
#include <stdint.h>

namespace {

const double kPi = 3.14159265358979323846;
const double kSynodic = 29.530588861;  // mean synodic month, days
const double kDeltaT = 69.0;           // TT - UT in seconds, about 2026
const double kUnixJd = 2440587.5;      // JD of 1970-01-01 00:00 UTC
const double kHalfMonthSec = 14.765 * 86400.0;
const double kAuKm = 149597870.7;

double rad(double deg) { return deg * kPi / 180.0; }

double wrap360(double deg) {
    deg = fmod(deg, 360.0);
    return deg < 0 ? deg + 360.0 : deg;
}

// Julian centuries since J2000 (TT) for a UT Julian date.
double centuries(double jdUt) { return (jdUt + kDeltaT / 86400.0 - 2451545.0) / 36525.0; }

// ---- Ch. 49: instants of the principal phases ----
// q counts quarter-steps from the 2000-01-06 new moon: q%4 = 0 new, 1 first quarter, 2 full, 3 last quarter.
// Returns Unix seconds (UTC) as a double.
double phaseUnix(long q) {
    const double k = q / 4.0;
    const int ph = (int)(((q % 4) + 4) % 4);
    const double T = k / 1236.85, T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    double jde = 2451550.09766 + kSynodic * k + 0.00015437 * T2 - 0.000000150 * T3 + 0.00000000073 * T4;
    const double E = 1 - 0.002516 * T - 0.0000074 * T2, E2 = E * E;
    const double M = rad(2.5534 + 29.10535670 * k - 0.0000014 * T2 - 0.00000011 * T3);
    const double Mp = rad(201.5643 + 385.81693528 * k + 0.0107582 * T2 + 0.00001238 * T3 - 0.000000058 * T4);
    const double F = rad(160.7108 + 390.67050284 * k - 0.0016118 * T2 - 0.00000227 * T3 + 0.000000011 * T4);
    const double Om = rad(124.7746 - 1.56375588 * k + 0.0020672 * T2 + 0.00000215 * T3);

    double c;
    if (ph == 0 || ph == 2) {
        const bool f = ph == 2;  // new and full moon differ only in a few coefficients
        c = (f ? -0.40614 : -0.40720) * sin(Mp) + (f ? 0.17302 : 0.17241) * E * sin(M) +
            (f ? 0.01614 : 0.01608) * sin(2 * Mp) + (f ? 0.01043 : 0.01039) * sin(2 * F) +
            (f ? 0.00734 : 0.00739) * E * sin(Mp - M) + (f ? -0.00515 : -0.00514) * E * sin(Mp + M) +
            (f ? 0.00209 : 0.00208) * E2 * sin(2 * M) - 0.00111 * sin(Mp - 2 * F) -
            0.00057 * sin(Mp + 2 * F) + 0.00056 * E * sin(2 * Mp + M) - 0.00042 * sin(3 * Mp) +
            0.00042 * E * sin(M + 2 * F) + 0.00038 * E * sin(M - 2 * F) - 0.00024 * E * sin(2 * Mp - M) -
            0.00017 * sin(Om) - 0.00007 * sin(Mp + 2 * M) + 0.00004 * sin(2 * Mp - 2 * F) +
            0.00004 * sin(3 * M) + 0.00003 * sin(Mp + M - 2 * F) + 0.00003 * sin(2 * Mp + 2 * F) -
            0.00003 * sin(Mp + M + 2 * F) + 0.00003 * sin(Mp - M + 2 * F) - 0.00002 * sin(Mp - M - 2 * F) -
            0.00002 * sin(3 * Mp + M) + 0.00002 * sin(4 * Mp);
    } else {
        c = -0.62801 * sin(Mp) + 0.17172 * E * sin(M) - 0.01183 * E * sin(Mp + M) +
            0.00862 * sin(2 * Mp) + 0.00804 * sin(2 * F) + 0.00454 * E * sin(Mp - M) +
            0.00204 * E2 * sin(2 * M) - 0.00180 * sin(Mp - 2 * F) - 0.00070 * sin(Mp + 2 * F) -
            0.00040 * sin(3 * Mp) - 0.00034 * E * sin(2 * Mp - M) + 0.00032 * E * sin(M + 2 * F) +
            0.00032 * E * sin(M - 2 * F) - 0.00028 * E2 * sin(Mp + 2 * M) + 0.00027 * E * sin(2 * Mp + M) -
            0.00017 * sin(Om) - 0.00005 * sin(Mp - M - 2 * F) + 0.00004 * sin(2 * Mp + 2 * F) -
            0.00004 * sin(Mp + M + 2 * F) + 0.00004 * sin(Mp - 2 * M) + 0.00003 * sin(Mp + M - 2 * F) +
            0.00003 * sin(3 * M) + 0.00002 * sin(2 * Mp - 2 * F) + 0.00002 * sin(Mp - M + 2 * F) -
            0.00002 * sin(3 * Mp + M);
        const double w = 0.00306 - 0.00038 * E * cos(M) + 0.00026 * cos(Mp) - 0.00002 * cos(Mp - M) +
                         0.00002 * cos(Mp + M) + 0.00002 * cos(2 * F);
        c += ph == 1 ? w : -w;
    }

    // Additional planetary corrections: {A at k=0, A per k, amplitude}
    static const double kAdd[14][3] = {
        {299.77, 0.107408, 0.000325}, {251.88, 0.016321, 0.000165}, {251.83, 26.651886, 0.000164},
        {349.42, 36.412478, 0.000126}, {84.66, 18.206239, 0.000110}, {141.74, 53.303771, 0.000062},
        {207.14, 2.453732, 0.000060},  {154.84, 7.306860, 0.000056}, {34.52, 27.261239, 0.000047},
        {207.19, 0.121824, 0.000042},  {291.34, 1.844379, 0.000040}, {161.72, 24.198154, 0.000037},
        {239.56, 25.513099, 0.000035}, {331.55, 3.592518, 0.000023}};
    for (int i = 0; i < 14; i++) {
        double a = kAdd[i][0] + kAdd[i][1] * k;
        if (i == 0) a -= 0.009173 * T2;
        c += kAdd[i][2] * sin(rad(a));
    }
    return (jde + c - kUnixJd) * 86400.0 - kDeltaT;
}

// ---- Ch. 25: low-precision sun ----
struct SunPos {
    double lon;  // apparent longitude, degrees
    double eps;  // true obliquity, degrees
    double distKm;
};

SunPos sunPos(double T) {
    const double L0 = 280.46646 + 36000.76983 * T + 0.0003032 * T * T;
    const double M = rad(357.52911 + 35999.05029 * T - 0.0001537 * T * T);
    const double C = (1.914602 - 0.004817 * T - 0.000014 * T * T) * sin(M) +
                     (0.019993 - 0.000101 * T) * sin(2 * M) + 0.000289 * sin(3 * M);
    const double e = 0.016708634 - 0.000042037 * T - 0.0000001267 * T * T;
    const double nu = M + rad(C);
    const double om = rad(125.04 - 1934.136 * T);
    SunPos s;
    s.lon = wrap360(L0 + C - 0.00569 - 0.00478 * sin(om));
    s.eps = 23.439291 - 0.0130042 * T + 0.00256 * cos(om);
    s.distKm = 1.000001018 * (1 - e * e) / (1 + e * cos(nu)) * kAuKm;
    return s;
}

// September equinox (sun at 180 deg) as Unix seconds: Meeus ch. 27 mean value, refined with the sun's longitude.
double septEquinoxUnix(int year) {
    const double Y = (year - 2000) / 1000.0;
    double jde = 2451810.21715 + 365242.01767 * Y - 0.11575 * Y * Y + 0.00337 * Y * Y * Y +
                 0.00078 * Y * Y * Y * Y;
    for (int i = 0; i < 3; i++) jde += 58.0 * sin(rad(180.0 - sunPos(centuries(jde - kDeltaT / 86400.0)).lon));
    return (jde - kUnixJd) * 86400.0 - kDeltaT;
}

// ---- Ch. 47 (truncated): moon position ----
struct MoonPos {
    double lon, lat;  // degrees
    double distKm;
};

struct LonTerm { int8_t d, m, mp, f; int32_t l, r; };  // l: 1e-6 deg, r: 1e-3 km
const LonTerm kLon[] = {
    {0, 0, 1, 0, 6288774, -20905355}, {2, 0, -1, 0, 1274027, -3699111}, {2, 0, 0, 0, 658314, -2955968},
    {0, 0, 2, 0, 213618, -569925},    {0, 1, 0, 0, -185116, 48888},     {0, 0, 0, 2, -114332, -3149},
    {2, 0, -2, 0, 58793, 246158},     {2, -1, -1, 0, 57066, -152138},   {2, 0, 1, 0, 53322, -170733},
    {2, -1, 0, 0, 45758, -204586},    {0, 1, -1, 0, -40923, -129620},   {1, 0, 0, 0, -34720, 108743},
    {0, 1, 1, 0, -30383, 104755},     {2, 0, 0, -2, 15327, 10321},      {0, 0, 1, 2, -12528, 0},
    {0, 0, 1, -2, 10980, 79661},      {4, 0, -1, 0, 10675, -34782},     {0, 0, 3, 0, 10034, -23210},
    {4, 0, -2, 0, 8548, -21636},      {2, 1, -1, 0, -7888, 24208},      {2, 1, 0, 0, -6766, 30824},
    {1, 0, -1, 0, -5163, -8379},      {1, 1, 0, 0, 4987, -16675},       {2, -1, 1, 0, 4036, -12831},
    {2, 0, 2, 0, 3994, -10445},       {4, 0, 0, 0, 3861, -11650},       {2, 0, -3, 0, 3665, 14403}};

struct LatTerm { int8_t d, m, mp, f; int32_t b; };
const LatTerm kLat[] = {
    {0, 0, 0, 1, 5128122}, {0, 0, 1, 1, 280602}, {0, 0, 1, -1, 277693}, {2, 0, 0, -1, 173237},
    {2, 0, -1, 1, 55413},  {2, 0, -1, -1, 46271}, {2, 0, 0, 1, 32573},  {0, 0, 2, 1, 17198},
    {2, 0, 1, -1, 9266},   {0, 0, 2, -1, 8822},   {2, -1, 0, -1, 8216}, {2, 0, -2, -1, 4324},
    {2, 0, 1, 1, 4200},    {2, 1, 0, -1, -3359}};

MoonPos moonPos(double T) {
    const double T2 = T * T, T3 = T2 * T, T4 = T3 * T;
    const double Lp = 218.3164477 + 481267.88123421 * T - 0.0015786 * T2 + T3 / 538841.0 - T4 / 65194000.0;
    const double D = rad(297.8501921 + 445267.1114034 * T - 0.0018819 * T2 + T3 / 545868.0 - T4 / 113065000.0);
    const double M = rad(357.5291092 + 35999.0502909 * T - 0.0001536 * T2 + T3 / 24490000.0);
    const double Mp = rad(134.9633964 + 477198.8675055 * T + 0.0087414 * T2 + T3 / 69699.0 - T4 / 14712000.0);
    const double F = rad(93.2720950 + 483202.0175233 * T - 0.0036539 * T2 - T3 / 3526000.0 + T4 / 863310000.0);
    const double E = 1 - 0.002516 * T - 0.0000074 * T2;
    const double A1 = rad(119.75 + 131.849 * T), A2 = rad(53.09 + 479264.290 * T), A3 = rad(313.45 + 481266.484 * T);
    const double Lr = rad(Lp);

    double sl = 0, sr = 0, sb = 0;
    for (const LonTerm &t : kLon) {
        const double ef = t.m == 0 ? 1.0 : (t.m == 1 || t.m == -1) ? E : E * E;
        const double arg = t.d * D + t.m * M + t.mp * Mp + t.f * F;
        sl += ef * t.l * sin(arg);
        sr += ef * t.r * cos(arg);
    }
    for (const LatTerm &t : kLat) {
        const double ef = t.m == 0 ? 1.0 : (t.m == 1 || t.m == -1) ? E : E * E;
        sb += ef * t.b * sin(t.d * D + t.m * M + t.mp * Mp + t.f * F);
    }
    sl += 3958 * sin(A1) + 1962 * sin(Lr - F) + 318 * sin(A2);
    sb += -2235 * sin(Lr) + 382 * sin(A3) + 175 * sin(A1 - F) + 175 * sin(A1 + F) + 127 * sin(Lr - Mp) -
          115 * sin(Lr + Mp);

    MoonPos p;
    p.lon = wrap360(Lp + sl / 1e6);
    p.lat = sb / 1e6;
    p.distKm = 385000.56 + sr / 1000.0;
    return p;
}

const char *const kOctantNames[8] = {"New moon",   "Waxing crescent", "First quarter", "Waxing gibbous",
                                     "Full moon",  "Waning gibbous",  "Last quarter",  "Waning crescent"};

const char *const kMonthNames[12] = {"Wolf Moon",   "Snow Moon",  "Worm Moon",   "Pink Moon",
                                     "Flower Moon", "Strawberry Moon", "Buck Moon", "Sturgeon Moon",
                                     "Corn Moon",   "Hunter's Moon", "Beaver Moon", "Cold Moon"};

bool localMonth(double unixSec, int *year, int *mon) {
    const time_t t = (time_t)llround(unixSec);
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) return false;
    *year = tmv.tm_year + 1900;
    *mon = tmv.tm_mon;
    return true;
}

// True if this full moon is the one nearest the September equinox (the Harvest Moon).
bool isHarvest(double fullUnix) {
    int y, m;
    if (!localMonth(fullUnix, &y, &m)) return false;
    return fabs(fullUnix - septEquinoxUnix(y)) <= kHalfMonthSec;
}

// Name of the full moon at quarter-step q (instant fullUnix).
const char *fullMoonName(long q, double fullUnix) {
    const double prev = phaseUnix(q - 4);
    if (isHarvest(fullUnix)) return "Harvest Moon";
    if (isHarvest(prev)) return "Hunter's Moon";
    int y, m, py, pm;
    if (!localMonth(fullUnix, &y, &m)) return "Full Moon";
    if (localMonth(prev, &py, &pm) && py == y && pm == m) return "Blue Moon";
    return kMonthNames[m];
}

}  // namespace

const char *moonOctantName(int octant) { return kOctantNames[octant & 7]; }

MoonPhase moonPhase(time_t utc) {
    MoonPhase r;
    const double t = (double)utc;
    const double jd = t / 86400.0 + kUnixJd;
    long kn = (long)floor((jd - 2451550.09766) / kSynodic);  // new moon number, corrected below
    while (phaseUnix(4 * kn) > t) --kn;
    while (phaseUnix(4 * (kn + 1)) <= t) ++kn;

    double p[7];  // p[j + 1] = instant of quarter-step 4*kn + j, j = -1..5
    for (int j = -1; j <= 5; j++) p[j + 1] = phaseUnix(4 * kn + j);
    r.lastNew = (time_t)llround(p[1]);
    r.nextNew = (time_t)llround(p[5]);
    long qFull = 4 * kn + 2;
    double fullT = p[3];
    if (fullT <= t) {
        qFull += 4;
        fullT = phaseUnix(qFull);
    }
    r.nextFull = (time_t)llround(fullT);
    r.nextFullName = fullMoonName(qFull, fullT);

    r.ageDays = (t - p[1]) / 86400.0;
    r.cycle = (t - p[1]) / (p[5] - p[1]);

    // octant: principal phase within +-1 day, else the interval after the last principal instant
    int last = 0;
    for (int j = 0; j <= 3; j++)
        if (p[j + 1] <= t) last = j;
    r.octant = 2 * last + 1;
    for (int j = -1; j <= 4; j++)
        if (fabs(t - p[j + 1]) <= 86400.0) r.octant = 2 * (int)(((j % 4) + 4) % 4);
    r.name = kOctantNames[r.octant];

    // ch. 48: illuminated fraction from the phase angle
    const double T = centuries(jd);
    const MoonPos mp = moonPos(T);
    const SunPos sp = sunPos(T);
    const double dl = rad(mp.lon - sp.lon);
    const double cosPsi = cos(rad(mp.lat)) * cos(dl);
    const double psi = acos(cosPsi > 1 ? 1 : (cosPsi < -1 ? -1 : cosPsi));
    const double inc = atan2(sp.distKm * sin(psi), mp.distKm - sp.distKm * cosPsi);
    r.illum = (1 + cos(inc)) / 2;
    r.waxing = sin(dl) > 0;
    return r;
}

double sunAltitudeDeg(time_t utc, double latDeg, double lonDeg) {
    const double jd = (double)utc / 86400.0 + kUnixJd;
    const double T = centuries(jd);
    const SunPos s = sunPos(T);
    const double lam = rad(s.lon), eps = rad(s.eps);
    const double ra = atan2(cos(eps) * sin(lam), cos(lam));
    const double dec = asin(sin(eps) * sin(lam));
    // ch. 12: mean sidereal time at Greenwich from the UT Julian date
    const double Tu = (jd - 2451545.0) / 36525.0;
    const double gmst = wrap360(280.46061837 + 360.98564736629 * (jd - 2451545.0) + 0.000387933 * Tu * Tu -
                                Tu * Tu * Tu / 38710000.0);
    const double H = rad(gmst + lonDeg) - ra;
    const double lat = rad(latDeg);
    const double sinAlt = sin(lat) * sin(dec) + cos(lat) * cos(dec) * cos(H);
    return asin(sinAlt) * 180.0 / kPi;
}
