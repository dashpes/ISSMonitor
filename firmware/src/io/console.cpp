#include "io/console.h"

#include <Arduino.h>

#include "config.h"

namespace console {
namespace {

int32_t g_time_offset_s = 0;

const char* screenName(int i) {
    switch (i) {
        case SCR_TELEMETRY: return "TELEMETRY";
        case SCR_GLOBE:     return "GLOBE";
        case SCR_SKY:       return "SKY";
    }
    return "?";
}

// Stands in for turning the knob under that screen.
void cycleMode(AppState& s, int screen, int delta) {
    const int n = kModeCount[screen];
    int m = static_cast<int>(s.mode[screen]) + delta;
    while (m < 0)  m += n;
    while (m >= n) m -= n;
    s.mode[screen] = static_cast<uint8_t>(m);
    Serial.printf("%s mode -> %d/%d\n", screenName(screen), m + 1, n);
}

void printState(const AppState& s) {
    Serial.println("---- state ----");
    Serial.printf("modes   : L=%u  globe=%u  R=%u\n",
                  (unsigned)s.mode[SCR_TELEMETRY], (unsigned)s.mode[SCR_GLOBE],
                  (unsigned)s.mode[SCR_SKY]);
    Serial.printf("switches: night=%d units=%d alert=%d track=%d\n",
                  s.sw.night, s.sw.units_imperial, s.sw.alert_armed, s.sw.track_trail);
    Serial.printf("iss     : %.3f, %.3f  alt %.1f km  %.0f km/h\n",
                  s.iss.lat_deg, s.iss.lon_deg, s.iss.alt_km, s.speed_kmh);
    Serial.printf("look    : az %.1f el %.1f range %.0f km\n",
                  s.iss_look.azimuth_deg, s.iss_look.elevation_deg, s.iss_look.range_km);
    Serial.printf("passes  : %d found\n", s.pass_count);
    Serial.printf("time    : %lld (offset %+d s)\n",
                  (long long)s.unix_now, (int)g_time_offset_s);
    Serial.printf("heap    : %u free, %u largest block\n",
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.println("---------------");
}

}  // namespace

void begin() {
    Serial.println("console: 1/2 = left knob, 3/4 = right knob, '?' = state");
    if (!cfg::kHaveEncoder) {
        Serial.println("NOTE: encoders not wired -- 1/2/3/4 stand in for them.");
    }
    if (!cfg::kHaveLdr) {
        Serial.println("NOTE: LDR not wired -- 'd' stands in for the light sensor.");
    }
}

void poll(AppState& s) {
    while (Serial.available() > 0) {
        switch (Serial.read()) {
            // Two knobs, two pairs of keys.
            case '1': cycleMode(s, SCR_TELEMETRY, -1); break;
            case '2': cycleMode(s, SCR_TELEMETRY, +1); break;
            case '3': cycleMode(s, SCR_SKY,       -1); break;
            case '4': cycleMode(s, SCR_SKY,       +1); break;

            case 'n': s.sw.night = !s.sw.night;
                      Serial.printf("night  = %d\n", s.sw.night); break;
            case 'u': s.sw.units_imperial = !s.sw.units_imperial;
                      Serial.printf("units  = %s\n", s.sw.units_imperial ? "mi" : "km"); break;
            case 'a': s.sw.alert_armed = !s.sw.alert_armed;
                      Serial.printf("alert  = %d\n", s.sw.alert_armed); break;
            case 't': s.sw.track_trail = !s.sw.track_trail;
                      Serial.printf("track  = %d\n", s.sw.track_trail); break;
            case 'd': s.dim = !s.dim;
                      Serial.printf("dim    = %d\n", s.dim); break;

            case 'j': g_time_offset_s += 60;
                      Serial.printf("time offset = %+d s\n", (int)g_time_offset_s); break;
            case 'J': g_time_offset_s = 0;
                      Serial.println("time offset cleared"); break;

            case '?': printState(s); break;
            default: break;
        }
    }
}

int32_t timeOffsetSeconds() { return g_time_offset_s; }

}  // namespace console
