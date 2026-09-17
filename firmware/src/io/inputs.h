#pragma once
//
// io/inputs.h -- six toggles + one rotary encoder, debounced.
//
// Implementation: src/io/inputs.cpp
// This is the single best C++/embedded lesson in the project. Concepts:
// interrupt service routines, `volatile`, IRAM_ATTR, why you may not call
// Serial.print() or allocate inside an ISR, and the difference between a
// LEVEL you sample and an EVENT you must not miss.
//
// Design rule: the ISR does nothing but update two volatile ints. All
// interpretation happens in poll(), on the main loop, where it is safe.
//
#include <cstdint>

#include "app/state.h"

namespace inputs {

void begin();

// Call every cfg::MS_INPUT_POLL from loop(). Debounces the toggles and folds
// encoder movement into `s`. Never blocks.
void poll(AppState& s);

// Detents since the last call, then cleared. Negative = counter-clockwise.
// `which` is 0 for the left knob, 1 for the right.
int takeEncoderDelta(int which);

// True exactly once per physical press.
bool takeEncoderClick(int which);

}  // namespace inputs
