#include "io/lightsensor.h"

#include <Arduino.h>

#include "config.h"

namespace lightsensor {

void begin() {
#if HAVE_LDR
    // analogSetPinAttenuation(cfg::PIN_LDR, ADC_11db) gives the full 0-3.3 V
    // range. The ESP32 ADC is famously nonlinear at the extremes -- you do not
    // need accuracy here, only repeatability.
#endif
}

void poll(AppState& s) {
#if HAVE_LDR
    // TODO(daniel): BLOCKED until the GL5539 arrives. Then:
    //   raw = analogRead(cfg::PIN_LDR)
    //   ema = ema + (raw - ema) / 8       <- integer EMA, no floats needed
    //   hysteresis around cfg::LDR_NIGHT_THRESHOLD so the console does not
    //   flicker between palettes when someone walks past the lamp
    (void)s;
#else
    // No sensor wired. Leave s.dim exactly as it is -- console.cpp owns it
    // via the 'd' key. Deliberately NOT setting it to a default here: that
    // would fight the operator, which is the classic sin of a fallback path.
    (void)s;
#endif
}

}  // namespace lightsensor
