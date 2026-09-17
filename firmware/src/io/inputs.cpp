#include "io/inputs.h"

#include <Arduino.h>

#include "config.h"

namespace inputs {
namespace {

// ===========================================================================
// PART A -- the toggles.
// ===========================================================================

// One row per wired switch. Adding AUX1/AUX2 later is two more rows here and
// two more cases in commit() -- no new logic. That is what the table buys you
// over six copies of the same if-block.
enum SwitchId : uint8_t {
    SW_NIGHT = 0,
    SW_UNITS,
    SW_ALERT,
    SW_TRACK,
#if HAVE_AUX_SWITCHES
    SW_AUX1,
    SW_AUX2,
#endif
    SW_COUNT
};

struct Debounced {
    uint8_t  pin;
    bool     stable;       // the value we have committed to, already inverted
    bool     candidate;    // what the pin has been reading lately
    uint32_t since_ms;     // millis() when it started reading that
};

Debounced g_sw[SW_COUNT] = {
    {cfg::PIN_SW_NIGHT, false, false, 0},
    {cfg::PIN_SW_UNITS, false, false, 0},
    {cfg::PIN_SW_ALERT, false, false, 0},
    {cfg::PIN_SW_TRACK, false, false, 0},
#if HAVE_AUX_SWITCHES
    {cfg::PIN_SW_AUX1,  false, false, 0},
    {cfg::PIN_SW_AUX2,  false, false, 0},
#endif
};

// Copy the committed values out into AppState. Done here so the debounce loop
// stays generic and knows nothing about what each switch means.
void commit(AppState& s) {
    s.sw.night          = g_sw[SW_NIGHT].stable;
    s.sw.units_imperial = g_sw[SW_UNITS].stable;
    s.sw.alert_armed    = g_sw[SW_ALERT].stable;
    s.sw.track_trail    = g_sw[SW_TRACK].stable;
#if HAVE_AUX_SWITCHES
    s.sw.aux1           = g_sw[SW_AUX1].stable;
    s.sw.aux2           = g_sw[SW_AUX2].stable;
#endif
}

}  // namespace

void begin() {
    for (int i = 0; i < SW_COUNT; ++i) {
        pinMode(g_sw[i].pin, INPUT_PULLUP);
        // Seed from the actual pin so the console does not spend the first
        // 25 ms believing every switch is off.
        const bool on = (digitalRead(g_sw[i].pin) == LOW);
        g_sw[i].stable    = on;
        g_sw[i].candidate = on;
        g_sw[i].since_ms  = millis();
    }

#if HAVE_ENCODER
    // pinMode(cfg::PIN_ENC_SW, INPUT_PULLUP);
    // pinMode(cfg::PIN_ENC_CLK, INPUT);   // GPIO 36/39: no internal pull-up
    // pinMode(cfg::PIN_ENC_DT,  INPUT);
    // attachInterrupt(digitalPinToInterrupt(cfg::PIN_ENC_CLK), onEncoderEdge, CHANGE);
#endif
}

void poll(AppState& s) {
    const uint32_t now = millis();

    for (int i = 0; i < SW_COUNT; ++i) {
        Debounced& d = g_sw[i];          // reference, so you edit the real row
        const bool reading = (digitalRead(d.pin) == LOW);   // PULLUP: ON == LOW

        if (reading != d.candidate){
            d.candidate = reading; // changes the candidate slot to the reading
            d.since_ms = now; // resets the time to when it was switched
        }
        else if (now - d.since_ms >= cfg::MS_DEBOUNCE && d.candidate != d.stable){
            d.stable = d.candidate; //changes the base line to current status after 25ms as defined by MS_DEBOUNCE
        }

    }

    commit(s);
}

// ===========================================================================
// PART B -- rotary encoder. Compiled out until HAVE_ENCODER is 1.
// io/console.h gives you '[' ']' and ENTER over serial meanwhile.
// ===========================================================================

int takeEncoderDelta(int which) {
    (void)which;
    return 0;
}

bool takeEncoderClick(int which) {
    (void)which;
    return false;
}

}  // namespace inputs
