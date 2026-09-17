#pragma once
//
// net/tle_fetch.h -- download the ISS element set, cache it in NVS.
//
// Implementation: src/net/tle_fetch.cpp
// Concepts: HTTPS on ESP32, streaming a response into a FIXED buffer instead
// of a String (do the String version once, watch the heap fragment, then do
// it properly), and persisting a struct to flash with Preferences.
//
// The cache is what makes the console usable: boot with no Wi-Fi and it still
// shows a position, flagged as stale, instead of an error screen.
//
#include "orbit/tle.h"

namespace tle_fetch {

void begin();

// Loads the cached TLE from NVS. False if nothing stored yet.
bool loadCached(orbit::Tle& out);

// Non-blocking fetch. Call every loop; returns true on the tick it completes.
bool step(orbit::Tle& out);

// Ask for a refresh on the next step().
void request();

}  // namespace tle_fetch
