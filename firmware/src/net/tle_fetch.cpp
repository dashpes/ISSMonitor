#include "net/tle_fetch.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "config.h"

namespace tle_fetch {
namespace {

Preferences g_prefs;
bool     g_want       = false;
uint32_t g_last_try_ms = 0;
constexpr uint32_t kRetryGapMs = 30000;

constexpr char kNvsNamespace[] = "iss";
constexpr char kNvsKey[]       = "tle";

void saveCached(const orbit::Tle& t) {
    if (!g_prefs.begin(kNvsNamespace, /*readOnly=*/false)) return;
    // This only works because Tle is trivially copyable: fixed char arrays,
    // no pointers, no std::string. It was designed that way on purpose --
    // blitting a struct with a pointer in it into flash would store the
    // address, which is meaningless after a reboot.
    g_prefs.putBytes(kNvsKey, &t, sizeof(t));
    g_prefs.end();
}

}  // namespace

void begin() {
    // Nothing to do here yet; kept so the call site reads consistently with
    // the other modules.
}

bool loadCached(orbit::Tle& out) {
    if (!g_prefs.begin(kNvsNamespace, /*readOnly=*/true)) return false;
    orbit::Tle t{};
    const size_t n = g_prefs.getBytes(kNvsKey, &t, sizeof(t));
    g_prefs.end();

    if (n != sizeof(t) || !t.valid) return false;
    out = t;
    Serial.printf("[tle] loaded cached element set: %s\n", out.name);
    return true;
}

void request() { g_want = true; }

bool step(orbit::Tle& out) {
    if (!g_want) return false;
    if (WiFi.status() != WL_CONNECTED) return false;

    const uint32_t now = millis();
    if (g_last_try_ms != 0 && now - g_last_try_ms < kRetryGapMs) return false;
    g_last_try_ms = now;

    // NOTE: HTTPClient is blocking, so this stalls the loop for roughly a
    // second. That is tolerable because it runs at boot and then once every
    // six hours -- you will see the console pause briefly and that is all.
    // If it ever becomes annoying, the ESP32 fix is to run this on core 0 as
    // its own FreeRTOS task rather than to make the HTTP client asynchronous.
    WiFiClientSecure client;
    // Celestrak serves public orbit data. The worst case for a spoofed TLE is
    // a wrong dot on a desk toy, so skipping certificate validation is an
    // acceptable trade here -- but know that you are making it.
    client.setInsecure();
    client.setTimeout(8);

    HTTPClient http;
    if (!http.begin(client, cfg::TLE_URL)) {
        Serial.println("[tle] http begin failed");
        return false;
    }
    http.setTimeout(8000);
    http.setUserAgent("ISSMonitor/1.0");

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[tle] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Fixed buffer, not String. A 3-line TLE is under 220 bytes; anything
    // bigger than this is not a TLE and we would rather truncate it and let
    // the checksum reject it than fragment the heap growing a String.
    char buf[512] = {0};
    const int len = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    size_t n = 0;
    const uint32_t deadline = millis() + 8000;
    while (n + 1 < sizeof(buf) && millis() < deadline) {
        const int avail = stream->available();
        if (avail > 0) {
            const int got = stream->readBytes(buf + n, sizeof(buf) - 1 - n);
            if (got <= 0) break;
            n += static_cast<size_t>(got);
            if (len > 0 && n >= static_cast<size_t>(len)) break;
        } else if (!http.connected()) {
            break;
        } else {
            delay(5);
        }
    }
    buf[n] = '\0';
    http.end();

    orbit::Tle parsed{};
    if (!orbit::parseTle(buf, parsed)) {
        // Checksum or format failure. Keep whatever is already in `out` --
        // a stale element set beats none.
        Serial.println("[tle] parse/checksum failed; keeping cached set");
        return false;
    }

    out = parsed;
    saveCached(parsed);
    g_want = false;
    Serial.printf("[tle] fetched %s (epoch %d day %.4f)\n",
                  out.name, out.epoch_year, out.epoch_day);
    return true;
}

}  // namespace tle_fetch
