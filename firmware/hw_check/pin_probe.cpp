//
// pin_probe.cpp -- find a bad display joint with nothing but a multimeter.
//
//   pio run -e probe -t upload -t monitor
//
// For when a screen stays blank and the smoke test cannot say why. Every
// display signal pin is driven with PWM at a DIFFERENT duty cycle, and a
// multimeter on DC volts reads PWM as its average. So each ESP32 pin shows
// up as its own voltage, and one pass with the red probe across the display
// header tells you, for every pin, which ESP32 pin it actually reaches:
//
//   right voltage          -> joint good, wired to the right pin
//   another pin's voltage  -> wired to the wrong ESP32 pin (or two swapped)
//   0 V, or wandering      -> open: cold joint, broken wire, missed pad
//   between two levels     -> bridged to a neighbour
//
// Nothing on the panel is initialised. It sees noise on its bus and ignores
// it; the next real init() resets it.
//
#include <Arduino.h>

#include "config.h"

namespace {

struct Probe {
    const char* esp_label;
    const char* panel_pin;
    uint8_t     gpio;
    float       volts;      // what the meter should read
};

// 0.4 V apart: far enough that any meter tells them apart at a glance, and
// the top one stays clear of a plain 3.3 V rail -- a pin that reads a solid
// 3.3 V is tied to VCC (or pulled up on the module), not driven from here.
const Probe kProbes[] = {
    {"D18",       "SCL",           cfg::PIN_SCK,          0.4f},
    {"D23",       "SDA",           cfg::PIN_MOSI,         0.8f},
    {"TX2 / 17",  "DC",            cfg::PIN_DC,           1.2f},
    {"RX2 / 16",  "RST",           cfg::PIN_RST,          1.6f},
    {"D5",        "CS (left)",     cfg::PIN_CS_TELEMETRY, 2.0f},
    {"D21",       "CS (top)",      cfg::PIN_CS_GLOBE,     2.4f},
    {"D22",       "CS (right)",    cfg::PIN_CS_SKY,       2.8f},
};
constexpr int kProbeCount = sizeof(kProbes) / sizeof(kProbes[0]);

// Fast enough that every meter averages it; slow enough that the edges are
// clean on a 20 cm Dupont lead.
constexpr uint32_t kPwmHz   = 5000;
constexpr uint8_t  kPwmBits = 8;
constexpr float    kVdd     = 3.3f;

void printTable() {
    Serial.println();
    Serial.println("Black probe on the DISPLAY's GND pin. Red probe on each pin:");
    Serial.println();
    Serial.println("  display pin    should read   driven from");
    Serial.println("  -----------    -----------   -----------");
    Serial.printf ("  %-12s   %5.1f V       3V3 rail\n", "VCC", kVdd);
    for (int i = 0; i < kProbeCount; ++i) {
        Serial.printf("  %-12s   %5.1f V       %s (GPIO %u)\n",
                      kProbes[i].panel_pin, kProbes[i].volts,
                      kProbes[i].esp_label, kProbes[i].gpio);
    }
    Serial.println();
    Serial.println("  Only ONE of the three CS voltages will be on your screen's CS pin.");
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.println("=== pin_probe :: display wiring by voltage ===");

    for (int i = 0; i < kProbeCount; ++i) {
        const uint32_t duty = static_cast<uint32_t>(
            kProbes[i].volts / kVdd * ((1u << kPwmBits) - 1) + 0.5f);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        ledcAttach(kProbes[i].gpio, kPwmHz, kPwmBits);
        ledcWrite(kProbes[i].gpio, duty);
#else
        ledcSetup(i, kPwmHz, kPwmBits);
        ledcAttachPin(kProbes[i].gpio, i);
        ledcWrite(i, duty);
#endif
    }

    printTable();
}

void loop() {
    // Nothing to do -- the LEDC peripheral holds every duty cycle on its own.
    // Reprint now and then so a monitor opened late still sees the table.
    static uint32_t last = 0;
    if (millis() - last >= 15000) {
        last = millis();
        printTable();
    }
}
