//
// inputs_check.cpp -- watch your debounce code work.
//
//   pio run -e inputs -t upload -t monitor
//
// Prints ONLY when a debounced value changes, plus a 5-second heartbeat.
// Silence between flips is the proof: a switch that bounces its way to a
// dozen printed lines per flip means the debounce is not doing its job.
//
// The RAW column is the undebounced pin, sampled at the same instant. Flip a
// switch slowly and you may catch RAW and STABLE disagreeing for a moment --
// that gap is the 25 ms your code is waiting out.
//
#include <Arduino.h>

#include "app/state.h"
#include "config.h"
#include "io/inputs.h"

namespace {

AppState g_state;

struct Watch { const char* label; bool Switches::* field; uint8_t pin; };

// `bool Switches::*` is a pointer-to-member: it names a FIELD of Switches
// without naming a particular Switches object. Reads as "which field", and
// you apply it later with `.*`. Niche syntax, but it is what lets one loop
// print four differently-named fields.
const Watch kWatch[] = {
    {"NIGHT", &Switches::night,          cfg::PIN_SW_NIGHT},
    {"UNITS", &Switches::units_imperial, cfg::PIN_SW_UNITS},
    {"ALERT", &Switches::alert_armed,    cfg::PIN_SW_ALERT},
    {"TRACK", &Switches::track_trail,    cfg::PIN_SW_TRACK},
};
constexpr int kWatchCount = sizeof(kWatch) / sizeof(kWatch[0]);

bool g_last[kWatchCount];
uint32_t g_changes = 0;

void printRow(const char* why) {
    Serial.printf("%-8s ", why);
    for (int i = 0; i < kWatchCount; ++i) {
        const bool stable = g_state.sw.*(kWatch[i].field);
        const bool raw    = (digitalRead(kWatch[i].pin) == LOW);
        Serial.printf("%s[%c]%s ", kWatch[i].label,
                      stable ? 'X' : ' ',
                      (raw != stable) ? "*" : " ");   // * = raw disagrees
    }
    Serial.printf("  changes=%lu\n", (unsigned long)g_changes);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=== inputs_check :: debounce watcher ===");
    Serial.println("Flip the toggles. One line per flip means it works.");
    Serial.println("A '*' means the raw pin currently disagrees with the");
    Serial.println("debounced value -- you are seeing the 25 ms settle.");
    Serial.println();

    inputs::begin();
    inputs::poll(g_state);

    for (int i = 0; i < kWatchCount; ++i) {
        g_last[i] = g_state.sw.*(kWatch[i].field);
    }
    printRow("initial");
}

void loop() {
    const uint32_t now = millis();

    static uint32_t t_poll = 0;
    if (now - t_poll >= cfg::MS_INPUT_POLL) {
        t_poll = now;
        inputs::poll(g_state);

        bool changed = false;
        for (int i = 0; i < kWatchCount; ++i) {
            const bool v = g_state.sw.*(kWatch[i].field);
            if (v != g_last[i]) {
                g_last[i] = v;
                changed = true;
                ++g_changes;
            }
        }
        if (changed) printRow("CHANGE");
    }

    static uint32_t t_beat = 0;
    if (now - t_beat >= 5000) {
        t_beat = now;
        printRow("idle");
    }
}
