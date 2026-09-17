#pragma once
//
// app/propagator.h -- the bridge from an element set to "where is it now".
//
// Thin wrapper over Hopperpop's Sgp4. Everything here is glue: the orbital
// mechanics live in the library, the coordinate maths live in lib/orbit, and
// this file only moves numbers between them and AppState.
//
#include "app/state.h"
#include "orbit/tle.h"

namespace propagator {

// Feed a validated element set. Safe to call again when a fresh TLE arrives.
bool load(const orbit::Tle& tle);
bool ready();

// Propagate to s.unix_now and fill in position, look angle, speed, distance.
// Also pushes the current subpoint onto the ground-track ring buffer.
void update(AppState& s);

// Recompute the upcoming passes. Costs tens of milliseconds, so call it when
// the list empties or a new TLE lands -- not every second.
int computePasses(AppState& s);

}  // namespace propagator
