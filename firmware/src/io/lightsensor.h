#pragma once
//
// io/lightsensor.h -- LDR on ADC1, smoothed.
//
// Implementation: src/io/lightsensor.cpp
// Small file, one real idea: an exponential moving average, and hysteresis so
// the console does not flicker between day and night palettes when someone
// walks past the lamp.
//
#include "app/state.h"

namespace lightsensor {

void begin();

// Updates s.ldr_raw and s.dim. Apply hysteresis: switch to dim below
// THRESHOLD, back to bright only above THRESHOLD + margin.
void poll(AppState& s);

}  // namespace lightsensor
