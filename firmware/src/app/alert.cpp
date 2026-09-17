#include "app/alert.h"

#include <Arduino.h>

#include "config.h"

namespace alert {
namespace {

// Breathing curve, 32 steps of a half-sine, precomputed as bytes. Building a
// table instead of calling sinf() every tick is not about speed here -- it is
// the habit that matters when the table is a coastline and the alternative is
// 4,000 trig calls per frame.
constexpr uint8_t kBreath[32] = {
      0,   2,   6,  13,  22,  33,  47,  62,
     79,  97, 116, 136, 156, 175, 194, 211,
    227, 240, 250, 255, 250, 240, 227, 211,
    194, 175, 156, 136, 116,  97,  79,  62,
};

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  #define LED_ATTACH() ledcAttach(cfg::PIN_LED, 5000, 8)
  #define LED_WRITE(d) ledcWrite(cfg::PIN_LED, (d))
#else
  #define LED_ATTACH() do { ledcSetup(0, 5000, 8); ledcAttachPin(cfg::PIN_LED, 0); } while (0)
  #define LED_WRITE(d) ledcWrite(0, (d))
#endif

}  // namespace

void begin() {
    LED_ATTACH();
    LED_WRITE(0);
}

void update(const AppState& s) {
    if (!s.sw.alert_armed) { LED_WRITE(0); return; }

    // Above the horizon right now: solid, no ambiguity.
    if (s.iss_look.elevation_deg > cfg::PASS_MIN_ELEVATION_DEG) {
        LED_WRITE(255);
        return;
    }

    if (s.pass_count == 0 || !s.time_valid) { LED_WRITE(0); return; }

    const int64_t secs = s.passes[0].aos_unix - s.unix_now;
    if (secs <= 0 || secs > static_cast<int64_t>(cfg::ALERT_LEAD_SECONDS)) {
        LED_WRITE(0);
        return;
    }

    // Inside the lead window: breathe, and breathe faster as it approaches.
    // 2.5 s per cycle at the far edge, down to 0.5 s just before AOS.
    const uint32_t period_ms =
        500 + static_cast<uint32_t>(2000.0 * secs / cfg::ALERT_LEAD_SECONDS);
    const uint32_t phase = (millis() % period_ms) * 32 / period_ms;
    LED_WRITE(kBreath[phase & 31]);
}

}  // namespace alert
