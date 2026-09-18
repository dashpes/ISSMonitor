#include "app/propagator.h"

#include <Arduino.h>
#include <Sgp4.h>

#include <cmath>
#include <cstring>

#include "config.h"
#include "orbit/geo.h"
#include "orbit/passes.h"

namespace propagator {
namespace {

Sgp4 g_sat;
bool g_ready = false;

// Our own copy of the element set currently loaded. Sgp4::init() returns
// false for TWO different reasons -- a genuinely bad TLE, and "this is the
// same set you already gave me" -- and the caller cannot tell them apart.
// Tracking it here means an unchanged refresh is a no-op instead of looking
// like a failure and switching tracking off.
char g_loaded_line1[70] = {0};

const orbit::GeoPoint kObserver{cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG,
                                cfg::OBS_ALT_M / 1000.0};

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

// The seam. orbit::findPasses knows nothing about SGP4 -- it asks "where was
// the satellite at time t" through this function pointer, and that is the
// whole reason pass prediction can be tested on a laptop: test_passes.cpp
// hands it a satellite on a known circle instead and checks the search
// against closed-form geometry. On the ESP32 the answer comes from here.
//
// findsat() recomputes from the element set on every call, so there is no
// hidden state to disturb by asking about times out of order.
orbit::GeoPoint sgp4Subpoint(int64_t unix_time, void* ctx) {
    (void)ctx;                    // the satellite is a file-scope singleton
    g_sat.findsat(static_cast<unsigned long>(unix_time));
    orbit::GeoPoint p;
    p.lat_deg = g_sat.satLat;
    p.lon_deg = orbit::wrapLongitude(g_sat.satLon);   // library returns 0..360
    p.alt_km  = g_sat.satAlt;
    return p;
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

    // Same element set we are already propagating: nothing to do, and calling
    // init() again would return false and look like an error.
    if (g_ready && std::strcmp(g_loaded_line1, tle.line1) == 0) {
        Serial.println("[prop] element set unchanged, keeping current fit");
        return true;
    }

    // Sgp4::init takes NON-const char[130] and writes into them, so it needs
    // its own mutable copies. Handing it tle.line1 directly would not compile,
    // and casting the const away would let a library scribble on our cache.
    char l1[130] = {0};
    char l2[130] = {0};
    std::strncpy(l1, tle.line1, sizeof(l1) - 1);
    std::strncpy(l2, tle.line2, sizeof(l2) - 1);

    g_sat.site(cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG, cfg::OBS_ALT_M);

    if (!g_sat.init(tle.name, l1, l2)) {
        // A bad refresh must not take down a propagator that was working.
        // Whatever we were already tracking keeps running.
        Serial.printf("[prop] load %s REJECTED; keeping previous fit (ready=%d)\n",
                      tle.name, g_ready);
        return g_ready;
    }

    std::strncpy(g_loaded_line1, tle.line1, sizeof(g_loaded_line1) - 1);
    g_loaded_line1[sizeof(g_loaded_line1) - 1] = '\0';
    g_ready = true;

    Serial.printf("[prop] load %s -> ok (%.4f rev/day)\n", tle.name, g_sat.revpday);
    return true;
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

    // Sgp4 has its own nextpass()/initpredpoint() pair and this used to call
    // it. It does not any more, for the same reason init() had to be worked
    // around above: it answers two different questions with one return value,
    // and the 'lighted' flag it reports is the library's own idea of
    // visibility rather than one this project can test. orbit::findPasses is
    // 63 tests' worth of known-good instead, and it runs on the laptop.
    const uint32_t started_ms = millis();

    const int found = orbit::findPasses(sgp4Subpoint, nullptr, kObserver,
                                        s.unix_now,
                                        cfg::PASS_SEARCH_HORIZON_S,
                                        cfg::PASS_MIN_ELEVATION_DEG,
                                        cfg::PASS_COARSE_STEP_S,
                                        s.passes, AppState::kMaxPasses);

    // The search left the satellite parked at whatever time it stopped
    // looking. update() would move it back on the next tick regardless, but
    // leaving it a day and a half in the future in the meantime is the kind
    // of thing that produces one impossible frame and no explanation.
    g_sat.findsat(static_cast<unsigned long>(s.unix_now));

    s.pass_count = found;
    Serial.printf("[prop] %d pass%s in the next %d h, found in %lu ms\n",
                  found, (found == 1) ? "" : "es",
                  (int)(cfg::PASS_SEARCH_HORIZON_S / 3600),
                  (unsigned long)(millis() - started_ms));
    return found;
}

}  // namespace propagator
