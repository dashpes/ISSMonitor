#pragma once
//
// net/netclock.h -- Wi-Fi association and NTP, both non-blocking.
//
// Implementation: src/net/netclock.cpp
// Concepts: a state machine instead of a blocking wait. The naive version of
// this is `while (WiFi.status() != WL_CONNECTED) delay(500);` -- and that
// freezes all three screens for up to 20 seconds and can trip the watchdog.
// Write it as a step() you call every loop and never wait inside.
//
#include "app/state.h"

namespace netclock {

void begin(const char* ssid, const char* password);

// Advances the connect / NTP state machine one step. Updates s.link,
// s.unix_now and s.time_valid.
void step(AppState& s);

bool timeSynced();

}  // namespace netclock
