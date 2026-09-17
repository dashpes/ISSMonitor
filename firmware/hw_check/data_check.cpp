//
// data_check.cpp -- the whole data chain, on serial, with no displays.
//
//   cp src/secrets_example.h src/secrets.h     (then fill in your Wi-Fi)
//   pio run -e data -t upload -t monitor
//
// Wi-Fi -> NTP -> Celestrak TLE -> SGP4 -> a live position, once a second.
// This is where the project stops being a wiring exercise.
//
// Expect: a cached-TLE line (empty on the very first run), a wifi line, a
// time line, a fetch line, then a telemetry row every second.
//
#include <Arduino.h>

#include <cmath>

#include "app/propagator.h"
#include "app/state.h"
#include "config.h"
#include "net/netclock.h"
#include "net/tle_fetch.h"
#include "orbit/geo.h"
#include "secrets.h"

namespace {

AppState g_state;
bool     g_have_tle   = false;
bool     g_passes_done = false;

const char* linkName(LinkState l) {
    switch (l) {
        case LinkState::Booting:        return "boot";
        case LinkState::WifiConnecting: return "wifi..";
        case LinkState::WifiUp:         return "wifi";
        case LinkState::TimeSet:        return "time";
        case LinkState::TleReady:       return "ready";
        case LinkState::Degraded:       return "DEGRADED";
    }
    return "?";
}

void printTelemetry() {
    const AppState& s = g_state;

    Serial.printf("[%-8s] ", linkName(s.link));

    if (!propagator::ready() || !s.time_valid) {
        Serial.println("waiting for time + element set...");
        return;
    }

    Serial.printf("%7.3f%c %8.3f%c  alt %5.1f km  %6.0f km/h  |  ",
                  std::fabs(s.iss.lat_deg), s.iss.lat_deg >= 0 ? 'N' : 'S',
                  std::fabs(s.iss.lon_deg), s.iss.lon_deg >= 0 ? 'E' : 'W',
                  s.iss.alt_km, s.speed_kmh);

    Serial.printf("az %5.1f %-3s  el %+6.1f  range %6.0f km  dist %6.0f km",
                  s.iss_look.azimuth_deg,
                  orbit::compassPoint(s.iss_look.azimuth_deg),
                  s.iss_look.elevation_deg,
                  s.iss_look.range_km,
                  s.ground_distance_km);

    if (s.iss_look.elevation_deg > cfg::PASS_MIN_ELEVATION_DEG) {
        Serial.print("   *** OVERHEAD NOW ***");
    } else if (s.pass_count > 0) {
        const int64_t secs = s.passes[0].aos_unix - s.unix_now;
        if (secs > 0) {
            Serial.printf("   next in %02lld:%02lld:%02lld el %.0f %s",
                          secs / 3600, (secs % 3600) / 60, secs % 60,
                          s.passes[0].max_elevation_deg,
                          s.passes[0].sunlit ? "(visible)" : "(not lit)");
        }
    }
    Serial.println();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=== ISS Monitor :: data chain check ===");
    Serial.printf("observer: %.3f, %.3f\n", cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG);

    tle_fetch::begin();

    // Try the cached element set first, so there is something to propagate
    // before the network is up. This is what makes the console usable
    // immediately at boot instead of showing an error for 10 seconds.
    if (tle_fetch::loadCached(g_state.tle)) {
        g_have_tle = propagator::load(g_state.tle);
    } else {
        Serial.println("[tle] no cached element set (first run?)");
    }

    tle_fetch::request();
    netclock::begin(WIFI_SSID, WIFI_PASS);
}

void loop() {
    const uint32_t now = millis();

    netclock::step(g_state);

    // A fresh TLE landing means reload the propagator and redo the passes.
    if (tle_fetch::step(g_state.tle)) {
        g_have_tle = propagator::load(g_state.tle);
        g_passes_done = false;
    }

    static uint32_t t_prop = 0;
    if (g_have_tle && g_state.time_valid && now - t_prop >= cfg::MS_PROPAGATE) {
        t_prop = now;
        propagator::update(g_state);
        g_state.link = LinkState::TleReady;

        if (!g_passes_done) {
            const uint32_t t0 = millis();
            const int n = propagator::computePasses(g_state);
            Serial.printf("[prop] %d passes above %.0f deg (%lu ms)  tle age %.1f h\n",
                          n, cfg::PASS_MIN_ELEVATION_DEG,
                          (unsigned long)(millis() - t0),
                          orbit::tleAgeHours(g_state.tle, g_state.unix_now));
            for (int i = 0; i < g_state.pass_count; ++i) {
                const orbit::Pass& p = g_state.passes[i];
                Serial.printf("       #%d  in %5lld min   max el %4.1f   %s -> %s   %s\n",
                              i + 1, (p.aos_unix - g_state.unix_now) / 60,
                              p.max_elevation_deg,
                              orbit::compassPoint(p.aos_azimuth_deg),
                              orbit::compassPoint(p.los_azimuth_deg),
                              p.sunlit ? "VISIBLE" : "not lit");
            }
            g_passes_done = true;
        }

        // Redo the schedule once the first pass has gone by.
        if (g_state.pass_count > 0 && g_state.unix_now > g_state.passes[0].los_unix) {
            g_passes_done = false;
        }
    }

    static uint32_t t_print = 0;
    if (now - t_print >= 1000) {
        t_print = now;
        printTelemetry();
    }
}
