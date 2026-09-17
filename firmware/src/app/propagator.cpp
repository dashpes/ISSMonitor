#include "app/propagator.h"

#include <Arduino.h>
#include <Sgp4.h>

#include <cmath>
#include <cstring>

#include "config.h"
#include "orbit/geo.h"

namespace propagator {
namespace {

Sgp4 g_sat;
bool g_ready = false;

const orbit::GeoPoint kObserver{cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG,
                                cfg::OBS_ALT_M / 1000.0};

// Julian Date -> Unix seconds. 2440587.5 is the JD of 1970-01-01T00:00:00Z.
int64_t jdToUnix(double jd) {
    return static_cast<int64_t>((jd - 2440587.5) * 86400.0 + 0.5);
}

// Orbital speed from mean motion, via Kepler's third law.
//
//   T = 86400 / revs_per_day                 seconds per orbit
//   a = cbrt( mu * (T / 2pi)^2 )             semi-major axis, km
//   v = 2 pi a / T                           km/s, circular approximation
//
// The ISS's eccentricity is about 0.0007, so treating the orbit as circular
// is good to well under a percent -- far tighter than a desk toy needs.
// Sanity check: 15.49 rev/day gives a = 6789 km and v = 27,500 km/h.
double speedKmh(double rev_per_day) {
    if (rev_per_day <= 0.0) return 0.0;
    constexpr double kMu = 398600.4418;            // km^3/s^2
    const double period_s = 86400.0 / rev_per_day;
    const double x = period_s / (2.0 * orbit::kPi);
    const double a_km = std::cbrt(kMu * x * x);
    return (2.0 * orbit::kPi * a_km / period_s) * 3600.0;
}

void pushTrail(AppState& s) {
    s.trail[s.trail_head] = s.iss;
    s.trail_head = (s.trail_head + 1) % AppState::kTrailLen;
    if (s.trail_size < AppState::kTrailLen) ++s.trail_size;
}

}  // namespace

bool ready() { return g_ready; }

bool load(const orbit::Tle& tle) {
    if (!tle.valid) return false;

    // Sgp4::init takes NON-const char[130] and writes into them, so it needs
    // its own mutable copies. Handing it tle.line1 directly would not compile,
    // and casting the const away would let a library scribble on our cache.
    char l1[130] = {0};
    char l2[130] = {0};
    std::strncpy(l1, tle.line1, sizeof(l1) - 1);
    std::strncpy(l2, tle.line2, sizeof(l2) - 1);

    g_sat.site(cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG, cfg::OBS_ALT_M);
    g_ready = g_sat.init(tle.name, l1, l2);

    Serial.printf("[prop] load %s -> %s (%.4f rev/day)\n",
                  tle.name, g_ready ? "ok" : "FAILED", g_sat.revpday);
    return g_ready;
}

void update(AppState& s) {
    if (!g_ready || !s.time_valid) return;

    g_sat.findsat(static_cast<unsigned long>(s.unix_now));

    s.iss.lat_deg = g_sat.satLat;
    s.iss.lon_deg = orbit::wrapLongitude(g_sat.satLon);   // library returns 0..360
    s.iss.alt_km  = g_sat.satAlt;

    s.iss_look.azimuth_deg   = orbit::normalizeDeg(g_sat.satAz);
    s.iss_look.elevation_deg = g_sat.satEl;
    s.iss_look.range_km      = g_sat.satDist;

    s.ground_distance_km = orbit::groundDistanceKm(kObserver, s.iss);
    s.speed_kmh          = speedKmh(g_sat.revpday);

    pushTrail(s);
}

int computePasses(AppState& s) {
    if (!g_ready || !s.time_valid) return 0;

    g_sat.initpredpoint(static_cast<unsigned long>(s.unix_now), 0.0);

    int found = 0;
    passinfo p{};
    while (found < AppState::kMaxPasses) {
        // 20 refinement iterations, searching forward, above our minimum.
        if (!g_sat.nextpass(&p, 20, false, cfg::PASS_MIN_ELEVATION_DEG)) break;

        orbit::Pass& out = s.passes[found];
        out.aos_unix          = jdToUnix(p.jdstart);
        out.max_unix          = jdToUnix(p.jdmax);
        out.los_unix          = jdToUnix(p.jdstop);
        out.max_elevation_deg = p.maxelevation;
        out.aos_azimuth_deg   = orbit::normalizeDeg(p.azstart);
        out.max_azimuth_deg   = orbit::normalizeDeg(p.azmax);
        out.los_azimuth_deg   = orbit::normalizeDeg(p.azstop);
        // `lighted` means the ISS is in sunlight while you are in darkness --
        // the combination that makes it actually visible to the naked eye.
        out.sunlit            = (p.vismax == lighted);
        ++found;
    }

    s.pass_count = found;
    return found;
}

}  // namespace propagator
