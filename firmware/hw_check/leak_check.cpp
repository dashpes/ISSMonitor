//
// leak_check.cpp -- is NIGHT (D32) leaking into the knob on D35/D4?
//
//   pio run -e leak -t upload -t monitor
//
// On the bench, NIGHT flickered on and off in step with that knob's
// detents without anyone touching the switch. A hard short is ruled out --
// with NIGHT held ON the knob still counted -- which leaves a PARTIAL one:
// flux residue or a solder whisker, tens of kilohms, between D32 and the pin
// beside it on the header. A beep test cannot see that.
//
// This can. The knob's CLK and DT idle HIGH through the KY-040's 10k
// pull-ups. Drive D32 LOW, read CLK and DT on the ADC, and any leak shows up
// as a sag: a leak of R against the 10k pull-up divides 3.3 V down to
// 3.3 * R / (R + 10k). No sag, no leak.
//
// Only ever drives D32 LOW. Driving it HIGH with the NIGHT switch ON would
// short the pin to ground through the switch.
//
#include <Arduino.h>

#include "config.h"

namespace {

struct Sense {
    const char* label;
    uint8_t     pin;
    bool        has_10k_pullup;   // only then does the kOhm estimate mean anything
};

// D35 and D34 are ADC1; D4 is ADC2, which is fine with no Wi-Fi running.
// D34 is the LDR, on D35's OTHER side on the header. It has been stuck at
// full scale; if it sags here too, its high reading is leaking in from the
// same contamination rather than coming from the light sensor.
const Sense kSense[] = {
    {"left knob CLK  (D35)", cfg::PIN_ENC_L_CLK, true},
    {"left knob DT   (D4) ", cfg::PIN_ENC_L_DT,  true},
    {"LDR            (D34)", cfg::PIN_LDR,       false},
};

constexpr float kPullupOhms = 10000.0f;   // on the KY-040 breakout

int readAvg(uint8_t pin) {
    int32_t sum = 0;
    for (int i = 0; i < 16; ++i) sum += analogRead(pin);
    return static_cast<int>(sum / 16);
}

void runOnce() {
    // NIGHT ON already grounds D32 through the switch, so there would be no
    // difference to measure -- and a leak would already be dragging the
    // baseline down. Refuse rather than print a false "no leak".
    pinMode(cfg::PIN_SW_NIGHT, INPUT_PULLUP);
    delay(2);
    if (digitalRead(cfg::PIN_SW_NIGHT) == LOW) {
        Serial.println("NIGHT switch is ON -- turn it OFF for this test.");
        return;
    }

    for (const Sense& s : kSense) {
        pinMode(cfg::PIN_SW_NIGHT, INPUT);          // hi-Z: no influence
        delay(2);
        const int base = readAvg(s.pin);

        pinMode(cfg::PIN_SW_NIGHT, OUTPUT);
        digitalWrite(cfg::PIN_SW_NIGHT, LOW);
        delay(2);
        const int loaded = readAvg(s.pin);

        pinMode(cfg::PIN_SW_NIGHT, INPUT_PULLUP);   // back to a switch input

        Serial.printf("  D32 low -> %s : %4d -> %4d   ", s.label, base, loaded);
        if (base < 3500 && s.has_10k_pullup) {
            Serial.println("(idle is not HIGH -- let go of the knob, or no pull-up)");
        } else if (loaded < 100) {
            Serial.println("HARD SHORT to D32");
        } else if (base - loaded > 150 && !s.has_10k_pullup) {
            Serial.println("SAGS -- connected to D32 through a leak");
        } else if (base - loaded > 150) {
            // The ADC is not linear near the top, so this is a rough figure.
            const float r = static_cast<float>(loaded) / base;
            Serial.printf("LEAK, roughly %.0f kOhm\n", kPullupOhms * r / (1.0f - r) / 1000.0f);
        } else {
            Serial.println("no leak");
        }
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=== leak_check :: D32 (NIGHT) vs the knob on D35/D4 ===");
    Serial.println("Hands off the left knob. NIGHT switch OFF.");
    analogSetAttenuation(ADC_11db);
}

void loop() {
    static uint32_t last = 0;
    if (millis() - last >= 2000) {
        last = millis();
        Serial.println("--");
        runOnce();
    }
}
