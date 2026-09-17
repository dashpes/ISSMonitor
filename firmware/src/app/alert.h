#pragma once
//
// app/alert.h -- the green LED.
//
// Implementation: src/app/alert.cpp
// Deceptively small. It is a state machine (Idle / Armed / Warning / Overhead)
// driven by time-to-next-pass, and the LED pattern differs per state: slow
// pulse while counting down, solid while the ISS is above the horizon.
// Use LEDC PWM for the pulse, not digitalWrite plus delay.
//
#include "app/state.h"

namespace alert {

void begin();
void update(const AppState& s);

}  // namespace alert
