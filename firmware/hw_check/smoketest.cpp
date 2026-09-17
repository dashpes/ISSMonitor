//
// smoketest.cpp -- prove the wiring, before any firmware exists.
//
//   pio run -e smoketest -t upload -t monitor
//
// A diagnostic you half-trust is worse than none: when a screen stays black
// this program has to be above suspicion, so the only variable left is the
// wiring.
//
// What it does, in order:
//   1. Prints why the chip last reset (watch for BROWNOUT -- that is C1).
//   2. Resets all panels, then brings up each display ALONE: solid colour,
//      its name, a colour-bar strip, a circle that should touch all four
//      edges of the round glass.
//   3. Then drives all three at once so you can see bus contention if the
//      CS wiring is wrong.
//   4. Streams switch states and the LED sweeps brightness continuously.
//
// Expected result with correct wiring:
//   left = red "TELEMETRY", centre = green "GLOBE", right = blue "SKY",
//   each in turn, then all three holding their own image at the same time.
//
#include <Arduino.h>

#include "config.h"
#include "display_config.h"
#include "theme.h"

RoundPanel tft_telemetry(cfg::PIN_CS_TELEMETRY);
RoundPanel tft_globe(cfg::PIN_CS_GLOBE);
RoundPanel tft_sky(cfg::PIN_CS_SKY);

namespace {

struct PanelInfo {
    RoundPanel* tft;
    const char* label;
    uint16_t    colour;
    uint8_t     cs;
};

PanelInfo g_panels[] = {
    {&tft_telemetry, "TELEMETRY", rgb565(220, 40, 40), cfg::PIN_CS_TELEMETRY},
    {&tft_globe,     "GLOBE",     rgb565(40, 200, 90), cfg::PIN_CS_GLOBE},
    {&tft_sky,       "SKY",       rgb565(60, 120, 255), cfg::PIN_CS_SKY},
};
constexpr int kPanelCount = sizeof(g_panels) / sizeof(g_panels[0]);

struct SwitchInfo {
    const char* label;
    uint8_t     pin;
};

// Only the switches you can actually wire today. The encoder is excluded --
// see HAVE_ENCODER in platformio.ini.
SwitchInfo g_switches[] = {
    {"NIGHT", cfg::PIN_SW_NIGHT},
    {"UNITS", cfg::PIN_SW_UNITS},
    {"ALERT", cfg::PIN_SW_ALERT},
    {"TRACK", cfg::PIN_SW_TRACK},
    {"AUX1",  cfg::PIN_SW_AUX1},
    {"AUX2",  cfg::PIN_SW_AUX2},
};
constexpr int kSwitchCount = sizeof(g_switches) / sizeof(g_switches[0]);

const char* resetReasonText(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:  return "POWERON (normal)";
        case ESP_RST_SW:       return "SW reset (normal after upload)";
        case ESP_RST_PANIC:    return "PANIC -- code crashed";
        case ESP_RST_INT_WDT:  return "INTERRUPT WATCHDOG -- blocked in an ISR";
        case ESP_RST_TASK_WDT: return "TASK WATCHDOG -- loop() blocked too long";
        case ESP_RST_BROWNOUT: return "BROWNOUT -- supply sagged. Add C1, try another cable/brick";
        case ESP_RST_DEEPSLEEP:return "deep sleep wake";
        default:               return "other";
    }
}

void drawTestCard(PanelInfo& p) {
    auto& t = *p.tft;
    t.startWrite();
    t.fillScreen(p.colour);

    // A circle that should just touch all four edges. If it is clipped or
    // off-centre, your panel offset_x/offset_y in display_config.h is wrong.
    t.drawCircle(cfg::SCREEN_R, cfg::SCREEN_R, cfg::SCREEN_R - 1, TFT_WHITE);
    t.drawCircle(cfg::SCREEN_R, cfg::SCREEN_R, cfg::SCREEN_R - 2, TFT_WHITE);

    // Crosshair through the centre -- checks rotation and mirroring.
    t.drawFastHLine(0, cfg::SCREEN_R, cfg::SCREEN_W, TFT_BLACK);
    t.drawFastVLine(cfg::SCREEN_R, 0, cfg::SCREEN_H, TFT_BLACK);

    // Colour bars. If red and blue are swapped, flip rgb_order in
    // display_config.h. This is THE most common GC9A01 gotcha.
    const uint16_t bars[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_WHITE, TFT_BLACK};
    for (int i = 0; i < 5; ++i) {
        t.fillRect(70 + i * 20, 150, 18, 40, bars[i]);
    }

    t.setTextColor(TFT_WHITE, p.colour);
    t.setTextDatum(middle_center);
    t.setTextSize(2);
    t.drawString(p.label, cfg::SCREEN_R, 70);
    t.setTextSize(1);
    t.drawString("CS=", cfg::SCREEN_R - 18, 95);
    t.drawNumber(p.cs, cfg::SCREEN_R + 8, 95);
    t.endWrite();
}

void sweepLed(uint32_t now) {
    // Triangle wave, no floating point, no delay().
    const uint32_t phase = now % 2000;
    const uint32_t duty  = phase < 1000 ? phase : (2000 - phase);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(cfg::PIN_LED, duty / 4);
#else
    ledcWrite(0, duty / 4);
#endif
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(400);  // one of the only legitimate delays in this project: let the
                 // USB serial enumerate so you do not miss the banner.

    Serial.println();
    Serial.println("=========================================");
    Serial.println(" ISS Monitor -- hardware smoke test");
    Serial.println("=========================================");
    Serial.printf("reset reason : %s\n", resetReasonText(esp_reset_reason()));
    Serial.printf("free heap    : %u bytes\n", (unsigned)ESP.getFreeHeap());
    Serial.printf("largest block: %u bytes  <-- your sprite budget\n",
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.printf("cpu          : %u MHz\n", (unsigned)getCpuFrequencyMhz());
    Serial.printf("encoder      : %s\n", cfg::kHaveEncoder ? "wired" : "NOT WIRED (skipped)");
    Serial.printf("ldr          : %s\n", cfg::kHaveLdr ? "wired" : "NOT WIRED (skipped)");
    Serial.println();

    // LED first -- it is the cheapest possible "is anything alive" signal.
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(cfg::PIN_LED, 5000, 8);
#else
    ledcSetup(0, 5000, 8);
    ledcAttachPin(cfg::PIN_LED, 0);
#endif

    for (int i = 0; i < kSwitchCount; ++i) {
        pinMode(g_switches[i].pin, INPUT_PULLUP);
    }

    // Deselect every CS and pulse the shared RST -- both before any init().
    // See the comment on prepareDisplayBus() in display_config.h; getting
    // this wrong is what makes one draw land on three screens at once.
    prepareDisplayBus();

    Serial.println("-- bringing up displays one at a time --");
    for (int i = 0; i < kPanelCount; ++i) {
        Serial.printf("   [%d/%d] %-10s CS=%2u ... ",
                      i + 1, kPanelCount, g_panels[i].label, g_panels[i].cs);
        Serial.flush();

        if (!g_panels[i].tft->init()) {
            Serial.println("init FAILED");
            Serial.println("       check: VCC on 3V3 (not VIN), SCL=D18, SDA=D23,");
            Serial.println("              DC=GPIO17, RST=GPIO16, and this panel's CS.");
            continue;
        }
        g_panels[i].tft->setRotation(0);
        g_panels[i].tft->fillScreen(TFT_BLACK);
        Serial.println("init ok");

        // Only this one lit, so a wrong CS is obvious: if two light up
        // together, two panels share a CS line.
        drawTestCard(g_panels[i]);
        delay(2500);
        g_panels[i].tft->fillScreen(TFT_BLACK);
    }

    Serial.println("-- all three together --");
    Serial.println("   each screen must now show ITS OWN card:");
    Serial.println("   left=red TELEMETRY  centre=green GLOBE  right=blue SKY");
    Serial.println("   two screens showing the same card = they share a CS line");
    for (int i = 0; i < kPanelCount; ++i) {
        drawTestCard(g_panels[i]);
        // Tag the phase so you can tell this card apart from the solo one.
        g_panels[i].tft->setTextColor(TFT_WHITE, g_panels[i].colour);
        g_panels[i].tft->setTextDatum(middle_center);
        g_panels[i].tft->setTextSize(1);
        g_panels[i].tft->drawString("ALL 3", cfg::SCREEN_R, 210);
    }

    Serial.println();
    Serial.println("Now streaming switch states. Flip them.");
    Serial.println("(ON == LOW == shown as [X], because of INPUT_PULLUP)");
    Serial.println();
}

void loop() {
    const uint32_t now = millis();
    sweepLed(now);

    static uint32_t last_print = 0;
    if (now - last_print >= 250) {
        last_print = now;

        Serial.print("switches ");
        for (int i = 0; i < kSwitchCount; ++i) {
            const bool on = digitalRead(g_switches[i].pin) == LOW;
            Serial.printf("%s[%c] ", g_switches[i].label, on ? 'X' : ' ');
        }

        if (cfg::kHaveLdr) {
            Serial.printf(" ldr=%4d", analogRead(cfg::PIN_LDR));
        }
        Serial.println();
    }
}
