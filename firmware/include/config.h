#pragma once
//
// config.h -- every pin number and tunable constant in one place.
// Transcribed from PARTS_LIST.md section 2. If you rewire, change it HERE
// and nowhere else. A magic number that appears in two files is a bug
// waiting to happen.
//
// C++ note: `constexpr` means "compute this at compile time". Unlike the
// Arduino habit of `#define PIN_LED 13`, a constexpr has a real type, obeys
// scope, and shows up in the debugger. Prefer it over #define always.
//
#include <cstdint>

// ---- Which hardware is actually wired up right now ----
// Set from platformio.ini. Defaulted here so the headers stand alone.
#ifndef HAVE_ENCODER
#define HAVE_ENCODER 1
#endif
#ifndef HAVE_LDR
#define HAVE_LDR 1
#endif
#ifndef HAVE_AUX_SWITCHES
#define HAVE_AUX_SWITCHES 1
#endif

namespace cfg {

constexpr bool kHaveEncoder = HAVE_ENCODER;
constexpr bool kHaveLdr     = HAVE_LDR;
constexpr bool kHaveAuxSwitches = HAVE_AUX_SWITCHES;

// ---- Shared SPI bus (all three GC9A01 displays) ----
constexpr uint8_t PIN_SCK  = 18;  // "SCL" on the module
constexpr uint8_t PIN_MOSI = 23;  // "SDA" on the module -- SPI, not I2C
constexpr uint8_t PIN_DC   = 17;  // TX2
constexpr uint8_t PIN_RST  = 16;  // RX2
// NOTE: no backlight pin. The Hosyond modules are the 7-pin type (no BLK),
// so the backlight is always on and D4 is free -- which the case pin plan
// spends on Encoder R's DT line. "Night mode" therefore dims by switching to
// the red palette, not by dimming the backlight. If your modules DO have an
// 8th BLK pin, you have a conflict: move Encoder R DT somewhere else.

// ---- Per-display chip select ----
constexpr uint8_t PIN_CS_TELEMETRY = 5;   // left
constexpr uint8_t PIN_CS_GLOBE     = 21;  // center
constexpr uint8_t PIN_CS_SKY       = 22;  // right

constexpr int  SCREEN_W = 240;
constexpr int  SCREEN_H = 240;
constexpr int  SCREEN_R = 120;           // radius, used constantly by the UI
constexpr uint32_t SPI_HZ = 40'000'000;  // start here; drop to 27 MHz if glitchy

// ---- Toggle switches. All INPUT_PULLUP: ON == LOW. ----
constexpr uint8_t PIN_SW_NIGHT = 32;
constexpr uint8_t PIN_SW_UNITS = 33;
constexpr uint8_t PIN_SW_ALERT = 25;
constexpr uint8_t PIN_SW_TRACK = 26;
constexpr uint8_t PIN_SW_AUX1  = 14;
constexpr uint8_t PIN_SW_AUX2  = 15;  // strapping pin; only affects boot-log verbosity

// ---- Rotary encoders (KY-040) -- one under each lower screen ----
// Per case/CASE_NOTES.md. GPIO 34-39 are input-only with no internal pull-up;
// the KY-040 breakout carries its own 10k pull-ups, so that is fine for
// CLK/DT. The push switches are on normal pins and use INPUT_PULLUP.
constexpr uint8_t PIN_ENC_L_CLK = 36;  // VP
constexpr uint8_t PIN_ENC_L_DT  = 39;  // VN
constexpr uint8_t PIN_ENC_L_SW  = 27;

constexpr uint8_t PIN_ENC_R_CLK = 35;  // input-only
constexpr uint8_t PIN_ENC_R_DT  = 4;   // freed by the 7-pin displays
constexpr uint8_t PIN_ENC_R_SW  = 19;

// ---- Analog / output ----
constexpr uint8_t PIN_LDR = 34;  // ADC1, keeps working with Wi-Fi on
constexpr uint8_t PIN_LED = 13;  // green "ISS OVERHEAD"

// ---- Observer: Tustin, CA ----
constexpr double OBS_LAT_DEG =   33.746;
constexpr double OBS_LON_DEG = -117.826;
constexpr double OBS_ALT_M   =   35.0;

// ---- Network ----
constexpr char TLE_URL[] =
    "https://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE";
constexpr char TZ_STRING[] = "PST8PDT,M3.2.0,M11.1.0";
constexpr char NTP_1[] = "pool.ntp.org";
constexpr char NTP_2[] = "time.nist.gov";

// ---- Timing (milliseconds). Everything in this project is non-blocking; ----
// ---- these are "how often", never "how long to sleep". ----
constexpr uint32_t MS_PROPAGATE   = 1000;              // recompute ISS position
constexpr uint32_t MS_REDRAW      = 100;               // UI tick
constexpr uint32_t MS_INPUT_POLL  = 5;                 // switch scan
constexpr uint32_t MS_TLE_REFRESH = 6UL * 3600 * 1000; // 6 h
constexpr uint32_t MS_DEBOUNCE    = 25;

// ---- Behaviour ----
constexpr double   PASS_MIN_ELEVATION_DEG = 10.0;  // below this, not worth looking
constexpr uint32_t ALERT_LEAD_SECONDS     = 300;   // LED starts 5 min before AOS
constexpr int      LDR_NIGHT_THRESHOLD    = 800;   // calibrate this on the bench

}  // namespace cfg
