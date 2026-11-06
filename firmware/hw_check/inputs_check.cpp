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
// With HAVE_ENCODER, the two knobs report too: one line per detent with a
// running total, and one per click. Turn a knob one click clockwise and the
// total should go up by exactly one. Two per click, or none, is a wiring
// fault (or a KY-040 that is not four quarter-steps per detent); DOWN for
// clockwise means CLK and DT are swapped -- fix that in config.h.
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
// print every differently-named field.
const Watch kWatch[] = {
    {"NIGHT", &Switches::night,          cfg::PIN_SW_NIGHT},
    {"UNITS", &Switches::units_imperial, cfg::PIN_SW_UNITS},
    {"ALERT", &Switches::alert_armed,    cfg::PIN_SW_ALERT},
    {"TRACK", &Switches::track_trail,    cfg::PIN_SW_TRACK},
#if HAVE_AUX_SWITCHES
    {"AUX1",  &Switches::aux1,           cfg::PIN_SW_AUX1},
    {"AUX2",  &Switches::aux2,           cfg::PIN_SW_AUX2},
#endif
};
constexpr int kWatchCount = sizeof(kWatch) / sizeof(kWatch[0]);

bool g_last[kWatchCount];
uint32_t g_changes = 0;

#if HAVE_LDR
// Raw ADC, sampled at the same 100 ms main.cpp uses, and printed only when
// it has moved by more than the step below -- enough to ignore ADC wander,
// small enough that a hand over the sensor shows up at once. Sampling here
// also recreates the main firmware's conditions for the knobs: ADC1 reads
// put an ~80 ns glitch on GPIO 36/39 (ESP32 errata 3.11), which is where
// the left knob lives. The decoder should shrug it off; a left total that
// creeps with nobody touching it means it does not.
constexpr int kLdrPrintStep = 150;
int g_ldr_printed = -1000;

void reportLdr(uint32_t now) {
    static uint32_t t_sample = 0;
    if (now - t_sample < cfg::MS_LDR_SAMPLE) return;
    t_sample = now;

    const int raw = analogRead(cfg::PIN_LDR);
    if (abs(raw - g_ldr_printed) >= kLdrPrintStep) {
        g_ldr_printed = raw;
        Serial.printf("LDR  %4d   (%s)\n", raw,
                      raw >= 4000 ? "PINNED HIGH" :
                      raw <= 50   ? "PINNED LOW"  :
                      raw < cfg::LDR_NIGHT_THRESHOLD ? "dark" : "light");
    }
}
#endif

#if HAVE_ENCODER
const char* const kKnobName[] = {"LEFT ", "RIGHT"};
int32_t g_knob_total[2] = {0, 0};

// Drains what the ISR has counted since the last call. Same two calls
// main.cpp makes, so what you see here is what the screens will get.
void reportKnobs() {
    for (int k = 0; k < 2; ++k) {
        if (const int d = inputs::takeEncoderDelta(k)) {
            g_knob_total[k] += d;
            Serial.printf("KNOB %s %s %+d   total %+ld\n", kKnobName[k],
                          d > 0 ? "CW " : "CCW", d, (long)g_knob_total[k]);
        }
        if (inputs::takeEncoderClick(k)) {
            Serial.printf("KNOB %s click\n", kKnobName[k]);
        }
    }
}
#endif

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
#if HAVE_ENCODER
    Serial.println("Knobs: one line per detent, one per click. Clockwise counts UP.");
#endif
#if HAVE_LDR
    analogSetPinAttenuation(cfg::PIN_LDR, ADC_11db);   // as lightsensor.cpp
    Serial.println("LDR: printed when it moves. Cover it -- the number should fall.");
#endif
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

#if HAVE_ENCODER
        reportKnobs();
#endif
    }

#if HAVE_LDR
    reportLdr(now);
#endif

    static uint32_t t_beat = 0;
    if (now - t_beat >= 5000) {
        t_beat = now;
        printRow("idle");
    }
}
