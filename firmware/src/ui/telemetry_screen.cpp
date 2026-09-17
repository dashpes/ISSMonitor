#include "ui/telemetry_screen.h"

#include <cmath>
#include <cstdio>

#include "orbit/geo.h"
#include "theme.h"

namespace {

constexpr int CX = cfg::SCREEN_R;
constexpr int CY = cfg::SCREEN_R;

// The value band is redrawn constantly, so it goes through a sprite: build the
// new frame off-screen, push it in one SPI burst. Drawing straight to the
// panel would show a visible erase-then-redraw flicker at 10 fps.
// 240 x 70 x 16bpp = 33.6 kB. One of these, reused by every mode.
constexpr int BAND_Y = 86;
constexpr int BAND_H = 70;

struct Gauge { double lo, hi; };

const char* modeName(uint8_t m) {
    switch (m) {
        case 0: return "SPEED";
        case 1: return "ALTITUDE";
        case 2: return "POSITION";
        case 3: return "STATUS";
    }
    return "";
}

// Sensible full-scale ranges for the arc. The ISS lives in a narrow band, so
// a 0-based gauge would barely move -- these are zoomed to where it actually is.
Gauge gaugeFor(uint8_t mode, bool imperial) {
    switch (mode) {
        case 0: return imperial ? Gauge{0, 20000} : Gauge{0, 32000};
        case 1: return imperial ? Gauge{180, 300} : Gauge{300, 480};
        default: return Gauge{0, 0};
    }
}

const char* linkText(LinkState l) {
    switch (l) {
        case LinkState::Booting:        return "BOOT";
        case LinkState::WifiConnecting: return "WIFI..";
        case LinkState::WifiUp:         return "WIFI OK";
        case LinkState::TimeSet:        return "TIME OK";
        case LinkState::TleReady:       return "TRACKING";
        case LinkState::Degraded:       return "OFFLINE";
    }
    return "?";
}

}  // namespace

void TelemetryScreen::begin(const AppState& s) {
    _tft.fillScreen(palette(s.sw.night).bg);
    _last_mode = 255;          // force a full repaint on the first render
}

void TelemetryScreen::drawFurniture(const AppState& s) {
    const Palette& p = palette(s.sw.night);
    _tft.startWrite();
    _tft.fillScreen(p.bg);

    // Outer bezel rings.
    _tft.drawCircle(CX, CY, cfg::SCREEN_R - 1, p.grid);
    _tft.drawCircle(CX, CY, cfg::SCREEN_R - 2, p.grid);

    // Mode label across the top.
    _tft.setTextDatum(middle_center);
    _tft.setTextColor(p.dim, p.bg);
    _tft.setTextSize(1);
    _tft.drawString(modeName(s.mode[SCR_TELEMETRY]), CX, 34);

    // Gauge ticks: a 270-degree sweep with the gap at the bottom, so the arc
    // reads like an instrument rather than a pie chart.
    const Gauge g = gaugeFor(s.mode[SCR_TELEMETRY], s.sw.units_imperial);
    if (g.hi > g.lo) {
        for (int i = 0; i <= 8; ++i) {
            const double a = deg_to_rad_local(135.0 + (270.0 * i) / 8.0);
            const int r0 = cfg::SCREEN_R - 8;
            const int r1 = cfg::SCREEN_R - (i % 2 == 0 ? 20 : 14);
            _tft.drawLine(CX + static_cast<int>(r0 * std::cos(a)),
                          CY + static_cast<int>(r0 * std::sin(a)),
                          CX + static_cast<int>(r1 * std::cos(a)),
                          CY + static_cast<int>(r1 * std::sin(a)),
                          p.grid);
        }
    }
    _tft.endWrite();
}

double TelemetryScreen::deg_to_rad_local(double d) {
    return d * 3.14159265358979323846 / 180.0;
}

void TelemetryScreen::drawGauge(const AppState& s, double value) {
    const Palette& p = palette(s.sw.night);
    const Gauge g = gaugeFor(s.mode[SCR_TELEMETRY], s.sw.units_imperial);
    if (g.hi <= g.lo) return;

    double frac = (value - g.lo) / (g.hi - g.lo);
    if (frac < 0.0) frac = 0.0;
    if (frac > 1.0) frac = 1.0;

    // LovyanGFX measures arcs clockwise from 3 o'clock. Our sweep starts at
    // 135 degrees (lower-left) and runs 270 degrees to 405 (lower-right).
    const int r_out = cfg::SCREEN_R - 10;
    const int r_in  = cfg::SCREEN_R - 16;
    _tft.startWrite();
    _tft.fillArc(CX, CY, r_in, r_out, 135, 405, p.grid);              // track
    _tft.fillArc(CX, CY, r_in, r_out, 135, 135 + 270.0 * frac, p.accent);  // filled
    _tft.endWrite();
}

void TelemetryScreen::render(const AppState& s) {
    const uint8_t mode = s.mode[SCR_TELEMETRY];
    const bool night = s.sw.night || s.dim;

    // Full repaint only when something structural changed.
    if (mode != _last_mode || night != _last_night) {
        _last_mode  = mode;
        _last_night = night;
        _last_a = _last_b = NAN;     // force the value band to redraw
        drawFurniture(s);
    }

    const Palette& p = palette(night);

    char big[24]  = {0};
    char unit[16] = {0};
    char sub[32]  = {0};
    double gauge_value = 0.0;
    bool has_gauge = false;

    switch (mode) {
        case 0: {   // SPEED
            const double v = s.sw.units_imperial ? orbit::kmhToMph(s.speed_kmh) : s.speed_kmh;
            if (std::fabs(v - _last_a) < 1.0) return;     // nothing visible changed
            _last_a = v;
            std::snprintf(big,  sizeof(big),  "%.0f", v);
            std::snprintf(unit, sizeof(unit), s.sw.units_imperial ? "MPH" : "KM/H");
            std::snprintf(sub,  sizeof(sub),  "ORBITAL VELOCITY");
            gauge_value = v; has_gauge = true;
            break;
        }
        case 1: {   // ALTITUDE
            const double v = s.sw.units_imperial ? orbit::kmToMiles(s.iss.alt_km) : s.iss.alt_km;
            if (std::fabs(v - _last_a) < 0.1) return;
            _last_a = v;
            std::snprintf(big,  sizeof(big),  "%.1f", v);
            std::snprintf(unit, sizeof(unit), s.sw.units_imperial ? "MILES" : "KM");
            std::snprintf(sub,  sizeof(sub),  "ABOVE SEA LEVEL");
            gauge_value = v; has_gauge = true;
            break;
        }
        case 2: {   // POSITION
            if (std::fabs(s.iss.lat_deg - _last_a) < 0.01 &&
                std::fabs(s.iss.lon_deg - _last_b) < 0.01) return;
            _last_a = s.iss.lat_deg;
            _last_b = s.iss.lon_deg;
            std::snprintf(big, sizeof(big), "%.2f%c", std::fabs(s.iss.lat_deg),
                          s.iss.lat_deg >= 0 ? 'N' : 'S');
            std::snprintf(unit, sizeof(unit), "%.2f%c", std::fabs(s.iss.lon_deg),
                          s.iss.lon_deg >= 0 ? 'E' : 'W');
            const double d = s.sw.units_imperial ? orbit::kmToMiles(s.ground_distance_km)
                                                 : s.ground_distance_km;
            std::snprintf(sub, sizeof(sub), "%.0f %s AWAY", d,
                          s.sw.units_imperial ? "MI" : "KM");
            break;
        }
        default: {  // STATUS
            const double age = orbit::tleAgeHours(s.tle, s.unix_now);
            if (std::fabs(age - _last_a) < 0.02 && s.link == _last_link) return;
            _last_a = age;
            _last_link = s.link;
            std::snprintf(big,  sizeof(big),  "%s", linkText(s.link));
            std::snprintf(unit, sizeof(unit), "TLE %.1f H OLD", age);
            std::snprintf(sub,  sizeof(sub),  "%d PASS%s QUEUED",
                          s.pass_count, s.pass_count == 1 ? "" : "ES");
            break;
        }
    }

    // ---- the value band, built off-screen then pushed in one go ----
    LGFX_Sprite band(&_tft);
    band.setColorDepth(16);
    if (band.createSprite(cfg::SCREEN_W, BAND_H)) {
        band.fillSprite(p.bg);
        band.setTextDatum(middle_center);

        band.setTextColor(p.text);
        band.setTextSize(mode == 3 ? 2 : 3);
        band.drawString(big, cfg::SCREEN_W / 2, 22);

        band.setTextColor(p.accent);
        band.setTextSize(mode == 2 ? 2 : 1);
        band.drawString(unit, cfg::SCREEN_W / 2, 50);

        band.pushSprite(0, BAND_Y);
        band.deleteSprite();
    }

    _tft.setTextDatum(middle_center);
    _tft.setTextColor(p.dim, p.bg);
    _tft.setTextSize(1);
    _tft.drawString("                        ", CX, 176);   // erase
    _tft.drawString(sub, CX, 176);

    if (has_gauge) drawGauge(s, gauge_value);
}

void TelemetryScreen::onClick(AppState& s) {
    s.mode[SCR_TELEMETRY] =
        static_cast<uint8_t>((s.mode[SCR_TELEMETRY] + 1) % kModeCount[SCR_TELEMETRY]);
}
