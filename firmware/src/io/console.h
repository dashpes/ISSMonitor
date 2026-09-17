#pragma once
//
// io/console.h -- serial commands standing in for hardware you have not
// wired yet.
//
// A test seam: the encoders being in a shipping container does not block the
// screen work.
//
// Keep it after the parts arrive. Being able to drive the console from a
// keyboard is how you reproduce a bug at 11pm without reaching behind the
// case, and it is how you would script an end-to-end test of this thing.
//
//   1 / 2   left knob  CCW / CW   (cycle the left screen's mode)
//   3 / 4   right knob CCW / CW   (cycle the right screen's mode)
//   n u a t report NIGHT/UNITS/ALERT/TRACK (read-only: the panel toggles
//           own those, inputs::poll commits them from the pins every 5 ms)
//   d       toggle forced dim      (stands in for the LDR)
//   j       jump time forward 60 s (fast-forward to the next pass)
//   ?       print state
//
#include "app/state.h"

namespace console {

void begin();

// Call every loop. Non-blocking: reads whatever bytes are waiting and
// returns. Applies them to `s` exactly as the real hardware would.
void poll(AppState& s);

// Set by 'j'. main.cpp adds this to the real clock, so you can fast-forward
// to a pass instead of waiting for one.
int32_t timeOffsetSeconds();

}  // namespace console
