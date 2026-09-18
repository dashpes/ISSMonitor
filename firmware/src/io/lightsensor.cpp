#include "io/lightsensor.h"

#include <Arduino.h>

#include "config.h"

namespace lightsensor {
namespace {

#if HAVE_LDR
// The exponential moving average, held multiplied by 2^LDR_EMA_SHIFT.
//
// That scaling is the whole trick. Written the obvious way --
//   ema += (raw - ema) / 8;
// -- any difference smaller than 8 counts divides to zero, so the average
// stops moving while still short of the signal and sits there forever. Keep
// the accumulator scaled up and the fraction survives between samples:
//   acc += raw - (acc >> 3);   ema = acc >> 3;
// Same arithmetic, no floats, and it actually converges.
int32_t g_acc = -1;             // negative == not seeded yet

uint32_t g_last_sample_ms = 0;
#endif

}  // namespace

void begin() {
#if HAVE_LDR
    // 11 dB attenuation gives the full 0-3.3 V span. The ESP32's ADC is
    // famously nonlinear at both ends of it, which does not matter here:
    // this reading is compared against a threshold you calibrate on the
    // bench, so it needs to be repeatable, not accurate.
    analogSetPinAttenuation(cfg::PIN_LDR, ADC_11db);
    g_acc = -1;
    g_last_sample_ms = millis();
#endif
}

void poll(AppState& s) {
#if HAVE_LDR
    // Called every MS_INPUT_POLL (5 ms) along with the switches, but the
    // light in a room does not change at 200 Hz and the smoothing below is
    // specified in samples. Rate-limit here rather than making main.cpp know
    // that this one subsystem wants a different tick.
    const uint32_t now = millis();
    if (now - g_last_sample_ms < cfg::MS_LDR_SAMPLE) return;
    g_last_sample_ms = now;

    const int32_t raw = analogRead(cfg::PIN_LDR);

    // Seed from the first real reading. Starting the accumulator at zero
    // would make the console spend its first few seconds believing it is
    // pitch dark and then fading up, which looks like a fault.
    if (g_acc < 0) g_acc = raw << cfg::LDR_EMA_SHIFT;
    g_acc += raw - (g_acc >> cfg::LDR_EMA_SHIFT);

    const int32_t ema = g_acc >> cfg::LDR_EMA_SHIFT;
    s.ldr_raw = static_cast<uint16_t>(ema);

    // Brighter room -> the LDR's resistance drops -> HIGHER reading on D34.
    // (3V3 through the GL5539 into D34, 100k from D34 to GND; see
    // PARTS_LIST.md section 4.) So dark is a LOW number, which is the way
    // round lightsensor.h describes.
    //
    // Two thresholds, not one. With a single threshold, a reading sitting on
    // it flips the whole console between palettes on ADC noise alone -- and
    // it will sit on it, because dusk is exactly the condition this is for.
    if (!s.dim && ema < cfg::LDR_NIGHT_THRESHOLD) {
        s.dim = true;
    } else if (s.dim && ema > cfg::LDR_NIGHT_THRESHOLD + cfg::LDR_HYSTERESIS) {
        s.dim = false;
    }
#else
    // No sensor wired. Leave s.dim exactly as it is -- console.cpp owns it
    // via the 'd' key. Deliberately NOT setting it to a default here: that
    // would fight the operator, which is the classic sin of a fallback path.
    (void)s;
#endif
}

}  // namespace lightsensor
