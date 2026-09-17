#include "net/netclock.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"

namespace netclock {
namespace {

// A state machine, not a wait loop. The naive version of this file is
//     while (WiFi.status() != WL_CONNECTED) delay(500);
// which freezes all three screens for up to 20 seconds and can trip the
// task watchdog. Every phase below returns immediately.
enum class Phase : uint8_t { Idle, Associating, SyncingTime, Ready, Backoff };

Phase       g_phase = Phase::Idle;
const char* g_ssid  = nullptr;
const char* g_pass  = nullptr;

uint32_t g_phase_started_ms = 0;
uint32_t g_backoff_ms       = 2000;    // doubles on each failure, capped
bool     g_time_ok          = false;

constexpr uint32_t kAssociateTimeoutMs = 20000;
constexpr uint32_t kNtpTimeoutMs       = 30000;
constexpr uint32_t kBackoffMaxMs       = 120000;

// Any timestamp past this means NTP has actually answered; the ESP32 boots
// with its clock at 1970 and `time()` will happily hand you that.
constexpr int64_t kSaneEpoch = 1700000000;   // 2023-11-14

void enter(Phase p) {
    g_phase = p;
    g_phase_started_ms = millis();
}

}  // namespace

void begin(const char* ssid, const char* password) {
    g_ssid = ssid;
    g_pass = password;
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.setSleep(false);          // latency over the handful of mA it saves
    configTzTime(cfg::TZ_STRING, cfg::NTP_1, cfg::NTP_2);
    enter(Phase::Idle);
}

void step(AppState& s) {
    const uint32_t now = millis();

    switch (g_phase) {
        case Phase::Idle:
            s.link = LinkState::WifiConnecting;
            WiFi.begin(g_ssid, g_pass);
            enter(Phase::Associating);
            break;

        case Phase::Associating:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[net] wifi up, ip %s, rssi %d\n",
                              WiFi.localIP().toString().c_str(), WiFi.RSSI());
                s.link = LinkState::WifiUp;
                g_backoff_ms = 2000;          // success resets the backoff
                enter(Phase::SyncingTime);
            } else if (now - g_phase_started_ms >= kAssociateTimeoutMs) {
                Serial.println("[net] wifi association timed out");
                WiFi.disconnect();
                enter(Phase::Backoff);
            }
            break;

        case Phase::SyncingTime: {
            const int64_t t = static_cast<int64_t>(::time(nullptr));
            if (t > kSaneEpoch) {
                Serial.printf("[net] time synced: %lld\n", (long long)t);
                g_time_ok = true;
                s.link = LinkState::TimeSet;
                enter(Phase::Ready);
            } else if (now - g_phase_started_ms >= kNtpTimeoutMs) {
                Serial.println("[net] NTP timed out; retrying");
                enter(Phase::Backoff);
            }
            break;
        }

        case Phase::Ready:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[net] wifi dropped");
                s.link = LinkState::Degraded;
                enter(Phase::Backoff);
            }
            break;

        case Phase::Backoff:
            s.link = LinkState::Degraded;
            if (now - g_phase_started_ms >= g_backoff_ms) {
                g_backoff_ms = (g_backoff_ms * 2 > kBackoffMaxMs) ? kBackoffMaxMs
                                                                  : g_backoff_ms * 2;
                enter(Phase::Idle);
            }
            break;
    }

    // Keep the clock current whenever it is trustworthy. Offline, the ESP32's
    // own crystal keeps counting -- it drifts, but seconds-per-day drift is
    // nothing next to a TLE that is hours stale.
    if (g_time_ok) {
        s.unix_now   = static_cast<int64_t>(::time(nullptr));
        s.time_valid = true;
    }
}

bool timeSynced() { return g_time_ok; }

}  // namespace netclock
