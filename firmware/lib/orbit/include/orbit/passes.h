#pragma once
//
// orbit/passes.h -- "when do I go outside and look up?"
//
// Implementation: lib/orbit/src/passes.cpp
// Concepts: function pointers / std::function-free callbacks, fixed-capacity
// arrays instead of std::vector, and writing a search loop that cannot run
// forever on a device with no OS to rescue it.
//
#include <cstdint>

#include "orbit/geo.h"

namespace orbit {

struct Pass {
    int64_t aos_unix = 0;        // acquisition of signal: crosses the min elevation
    int64_t max_unix = 0;        // moment of highest elevation
    int64_t los_unix = 0;        // loss of signal
    double  max_elevation_deg = 0.0;
    double  aos_azimuth_deg   = 0.0;   // where to start looking
    double  max_azimuth_deg   = 0.0;   // bearing at the high point
    double  los_azimuth_deg   = 0.0;
    bool    sunlit            = false; // ISS lit + observer in darkness == visible
};

// How the finder asks "where was the ISS at time t?". Implemented on the ESP32
// by the Sgp4 wrapper; implemented in the tests by a fake that traces a known
// circle. That substitution is the whole reason this is a parameter and not a
// direct call -- it is how you unit-test orbital code without a satellite.
using SubpointFn = GeoPoint (*)(int64_t unix_time, void* ctx);

// Coarse-then-fine search. Step forward in `coarse_step_s` chunks looking for
// elevation to cross above min_elevation_deg, then bisect to the second.
//
// Writes at most `max_passes` into `out`; returns how many it found.
// MUST stop after `horizon_s` of simulated time no matter what -- an infinite
// loop here means a watchdog reset with three screens frozen mid-draw.
int findPasses(SubpointFn subpoint, void* ctx,
               const GeoPoint& observer,
               int64_t from_unix,
               int32_t horizon_s,
               double  min_elevation_deg,
               int32_t coarse_step_s,
               Pass*   out,
               int     max_passes);

// Is the ISS in sunlight while the observer is in darkness? That combination
// is what makes it actually visible. Cheap solar position is good enough --
// you need the sun's declination to about a degree, not an arcsecond.
bool isVisiblePass(const GeoPoint& satellite,
                   const GeoPoint& observer,
                   int64_t unix_time);

}  // namespace orbit
