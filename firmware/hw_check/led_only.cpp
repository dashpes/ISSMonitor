//
// led_only.cpp -- the smallest possible "is any of this working" test.
//
//   pio run -e led -t upload -t monitor
//
// Nothing but the LED. No screens, no Wi-Fi, no libraries. If this works you
// have proven, in one shot:
//   - the toolchain installed and your board setting is right
//   - your USB-C cable carries DATA, not just power (the classic first wall)
//   - upload and the serial monitor both work
//   - your LED, resistor and ground are wired correctly
//
// Wiring (PARTS_LIST section 4):
//
//   GPIO 13 ──[ 150 ohm ]──►|── GND
//                          green LED, LONG leg toward the resistor
//
// If the LED is dark but the serial output is scrolling, the ESP32 is fine
// and the problem is the circuit: check the long leg faces the resistor, and
// check your ground actually reaches the board's GND pin.
//
// If it is lit but very dim, that is expected with 150 ohm on a 3.3 V pin --
// this green LED wants 2.8-3.6 V. Two 150 ohm resistors side by side (75 ohm)
// roughly doubles the current. Do not go below ~47 ohm.
//
#include <Arduino.h>

#include "config.h"

// Arduino core 3.x renamed the LEDC PWM functions. This keeps the file
// working on either, which saves you a confusing first error.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #define LED_ATTACH() ledcAttach(cfg::PIN_LED, 5000, 8)
  #define LED_WRITE(d) ledcWrite(cfg::PIN_LED, (d))
#else
  #define LED_ATTACH() do { ledcSetup(0, 5000, 8); ledcAttachPin(cfg::PIN_LED, 0); } while (0)
  #define LED_WRITE(d) ledcWrite(0, (d))
#endif

void setup() {
    Serial.begin(115200);
    delay(400);  // let USB serial enumerate so you don't miss the banner

    Serial.println();
    Serial.println("=== ISS Monitor :: LED test ===");
    Serial.printf("LED on GPIO %u\n", (unsigned)cfg::PIN_LED);
    Serial.printf("free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
    Serial.println("Phase 1: 5 slow blinks (plain on/off)");

    pinMode(cfg::PIN_LED, OUTPUT);
    for (int i = 0; i < 5; ++i) {
        digitalWrite(cfg::PIN_LED, HIGH);
        Serial.printf("  blink %d ON\n", i + 1);
        delay(400);
        digitalWrite(cfg::PIN_LED, LOW);
        delay(400);
    }

    Serial.println("Phase 2: continuous fade (PWM).");
    Serial.println("If you see this text but no light, it's the circuit, not the chip.");
    LED_ATTACH();
}

void loop() {
    // A triangle wave with no delay() and no floating point -- the same
    // non-blocking shape the real firmware uses everywhere.
    const uint32_t now   = millis();
    const uint32_t phase = now % 2000;                      // 0..1999
    const uint32_t tri   = phase < 1000 ? phase : 2000 - phase;  // 0..1000..0
    LED_WRITE(tri / 4);                                     // 0..250 duty

    static uint32_t last = 0;
    if (now - last >= 1000) {
        last = now;
        Serial.printf("alive %lus  duty=%lu\n",
                      (unsigned long)(now / 1000), (unsigned long)(tri / 4));
    }
}
