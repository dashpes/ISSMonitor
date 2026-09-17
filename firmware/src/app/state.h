#pragma once
//
// app/state.h -- the single struct every screen reads and nothing but the
// app layer writes.
//
// The shape of this struct is the architectural decision; the rest of the
// firmware falls out of it. One owner, many readers:
// screens get a `const AppState&` and physically cannot corrupt it. That is
// const-correctness doing real work, not ceremony.
//
#include <cstdint>

#include "orbit/geo.h"
#include "orbit/passes.h"
#include "orbit/tle.h"

enum class LinkState : uint8_t { Booting, WifiConnecting, WifiUp, TimeSet, TleReady, Degraded };

// Screen identity. The TOP screen has no knob -- it always shows the globe,
// because it is the showpiece and should look the same from across the room.
// The two lower screens each own an encoder.
enum Screen_ : uint8_t { SCR_TELEMETRY = 0, SCR_GLOBE = 1, SCR_SKY = 2, SCR_COUNT = 3 };

// Mode counts per screen, used to wrap the encoder.
constexpr uint8_t kModeCount[SCR_COUNT] = {4, 1, 4};

struct Switches {
    bool night = false;
    bool units_imperial = false;
    bool alert_armed = false;
    bool track_trail = true;
    bool aux1 = false;
    bool aux2 = false;
};

struct AppState {
    LinkState link = LinkState::Booting;
    Switches  sw{};
    // Per-screen sub-mode. Left knob drives mode[SCR_TELEMETRY], right knob
    // drives mode[SCR_SKY]. The globe has one mode and no knob.
    uint8_t   mode[SCR_COUNT] = {0, 0, 0};

    int64_t  unix_now = 0;
    bool     time_valid = false;

    orbit::Tle      tle{};
    orbit::GeoPoint iss{};           // current subpoint
    double   speed_kmh = 0.0;
    double   ground_distance_km = 0.0;
    orbit::LookAngle iss_look{};     // from Tustin, right now

    static constexpr int kMaxPasses = 4;
    orbit::Pass passes[kMaxPasses]{};
    int         pass_count = 0;

    // Ground track history for the TRACK toggle. Fixed capacity, ring buffer:
    // no heap, no fragmentation, bounded worst case. 90 points at 30 s spacing
    // is about 45 minutes of trail, roughly half an orbit.
    static constexpr int kTrailLen = 90;
    orbit::GeoPoint trail[kTrailLen]{};
    int   trail_head = 0;
    int   trail_size = 0;

    uint16_t ldr_raw = 0;
    bool     dim = false;
};
