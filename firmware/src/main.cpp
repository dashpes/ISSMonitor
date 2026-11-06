//
// main.cpp -- wiring, and the cooperative scheduler.
//
// One loop, no delay() anywhere after setup(), every subsystem gets a slice
// when its interval is up. If you ever find yourself typing delay() in this
// file, something upstream is designed wrong.
//
// Note the timing form used throughout: `now - last >= interval`, never
// `now >= last + interval`. millis() is a uint32_t that wraps after 49 days;
// the subtraction wraps with it and stays correct, the addition does not.
//
#include <Arduino.h>

#include "app/alert.h"
#include "app/propagator.h"
#include "app/state.h"
#include "config.h"
#include "display_config.h"
#include "io/console.h"
#include "io/inputs.h"
#include "io/lightsensor.h"
#include "net/netclock.h"
#include "net/tle_fetch.h"
#include "orbit/geo.h"
#include "secrets.h"
#include "theme.h"
#include "ui/globe_screen.h"
#include "ui/sky_screen.h"
#include "ui/telemetry_screen.h"

// Definitions of the panels declared extern in display_config.h.
RoundPanel tft_telemetry(cfg::PIN_CS_TELEMETRY);
RoundPanel tft_globe(cfg::PIN_CS_GLOBE);
RoundPanel tft_sky(cfg::PIN_CS_SKY);

static TelemetryScreen s_telemetry(tft_telemetry);
static GlobeScreen     s_globe(tft_globe);
static SkyScreen       s_sky(tft_sky);

// Array of base-class pointers -- the payoff for Screen being abstract.
// Indices line up with SCR_TELEMETRY / SCR_GLOBE / SCR_SKY.
static Screen* s_screens[SCR_COUNT] = {&s_telemetry, &s_globe, &s_sky};

static AppState g_state;
static bool     g_have_tle    = false;
static bool     g_passes_done = false;

static const char* resetReasonText(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return "POWERON";
        case ESP_RST_SW:       return "SW";
        case ESP_RST_PANIC:    return "PANIC";
        case ESP_RST_INT_WDT:  return "INT_WDT (blocked in an ISR)";
        case ESP_RST_TASK_WDT: return "TASK_WDT (loop blocked too long)";
        case ESP_RST_BROWNOUT: return "BROWNOUT -- check C1 / the USB supply";
        default:               return "other";
    }
}

static void splash() {
    for (int i = 0; i < SCR_COUNT; ++i) {
        auto* t = (i == 0) ? &tft_telemetry : (i == 1) ? &tft_globe : &tft_sky;
        t->fillScreen(TFT_BLACK);
        t->setTextDatum(middle_center);
        t->setTextColor(rgb565(255, 176, 40));
        t->setTextSize(1);
        t->drawString("ISS MONITOR", cfg::SCREEN_R, cfg::SCREEN_R - 10);
        t->drawString(s_screens[i]->name(), cfg::SCREEN_R, cfg::SCREEN_R + 10);
    }
}

void setup() {
    Serial.begin(115200);
    delay(400);
    Serial.println();
    Serial.printf("=== ISS Monitor ===  reset: %s  heap: %u\n",
                  resetReasonText(esp_reset_reason()), (unsigned)ESP.getFreeHeap());

    // MUST come first: deselects every CS and pulses the shared RST once.
    // Skip it and one draw lands on all three screens at the same time.
    prepareDisplayBus();

    tft_telemetry.init();
    tft_globe.init();
    tft_sky.init();
    for (int i = 0; i < SCR_COUNT; ++i) {
        auto* t = (i == 0) ? &tft_telemetry : (i == 1) ? &tft_globe : &tft_sky;
        t->setRotation(0);
    }
    splash();

    inputs::begin();
    lightsensor::begin();
    alert::begin();
    console::begin();

    tle_fetch::begin();
    if (tle_fetch::loadCached(g_state.tle)) {
        g_have_tle = propagator::load(g_state.tle);
    }
    tle_fetch::request();
    netclock::begin(WIFI_SSID, WIFI_PASS);

    inputs::poll(g_state);
    for (int i = 0; i < SCR_COUNT; ++i) s_screens[i]->begin(g_state);
}

void loop() {
    const uint32_t now = millis();

    static uint32_t t_input = 0;
    if (now - t_input >= cfg::MS_INPUT_POLL) {
        t_input = now;
        inputs::poll(g_state);
        lightsensor::poll(g_state);
        console::poll(g_state);      // serial stand-in; harmless once knobs exist

#if HAVE_ENCODER
        // Left knob drives the telemetry screen, right knob drives the sky
        // screen. The globe has no knob -- it always shows the same thing.
        // One line per knob event: a human turns these a few times a minute,
        // so the log costs nothing, and "which knob moved which screen, and
        // did it turn or click" is the first question when one misbehaves.
        if (int d = inputs::takeEncoderDelta(0)) {
            int m = g_state.mode[SCR_TELEMETRY] + d;
            const int n = kModeCount[SCR_TELEMETRY];
            g_state.mode[SCR_TELEMETRY] = static_cast<uint8_t>(((m % n) + n) % n);
            Serial.printf("[knob] L %+d -> mode %u\n", d, g_state.mode[SCR_TELEMETRY]);
        }
        if (int d = inputs::takeEncoderDelta(1)) {
            int m = g_state.mode[SCR_SKY] + d;
            const int n = kModeCount[SCR_SKY];
            g_state.mode[SCR_SKY] = static_cast<uint8_t>(((m % n) + n) % n);
            Serial.printf("[knob] R %+d -> mode %u\n", d, g_state.mode[SCR_SKY]);
        }
        if (inputs::takeEncoderClick(0)) {
            s_telemetry.onClick(g_state);
            Serial.printf("[knob] L click -> mode %u\n", g_state.mode[SCR_TELEMETRY]);
        }
        if (inputs::takeEncoderClick(1)) {
            s_sky.onClick(g_state);
            Serial.printf("[knob] R click -> mode %u\n", g_state.mode[SCR_SKY]);
        }
#endif
    }

    netclock::step(g_state);

    static uint32_t t_tle = 0;
    if (t_tle != 0 && now - t_tle >= cfg::MS_TLE_REFRESH) {
        t_tle = now;
        tle_fetch::request();
    }
    if (tle_fetch::step(g_state.tle)) {
        g_have_tle = propagator::load(g_state.tle);
        g_passes_done = false;
        t_tle = now ? now : 1;
    }

    static uint32_t t_prop = 0;
    if (g_have_tle && g_state.time_valid && now - t_prop >= cfg::MS_PROPAGATE) {
        t_prop = now;
        // console 'j' fast-forwards time so you can rehearse a pass without
        // waiting hours for one.
        g_state.unix_now += console::timeOffsetSeconds();
        propagator::update(g_state);
        g_state.link = LinkState::TleReady;

        if (!g_passes_done) {
            propagator::computePasses(g_state);
            g_passes_done = true;
        }
        if (g_state.pass_count > 0 && g_state.unix_now > g_state.passes[0].los_unix) {
            g_passes_done = false;
        }
    }

    static uint32_t t_draw = 0;
    if (now - t_draw >= cfg::MS_REDRAW) {
        t_draw = now;
        for (int i = 0; i < SCR_COUNT; ++i) s_screens[i]->render(g_state);
    }

    alert::update(g_state);
}
