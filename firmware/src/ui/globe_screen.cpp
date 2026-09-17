#include "ui/globe_screen.h"

#include <cmath>

#include "orbit/geo.h"
#include "theme.h"

namespace {

constexpr int CX = cfg::SCREEN_R;
constexpr int CY = cfg::SCREEN_R;
constexpr int GLOBE_R = 104;

// Ease the view toward the ISS rather than snapping to it, so the planet
// drifts instead of twitching once a second.
constexpr double kSlew = 0.15;

}  // namespace

void GlobeScreen::begin(const AppState& s) {
    _view_lat = 0.0;
    _view_lon = s.iss.lon_deg;

    // A full 240x240 sprite at 16 bpp is 115 kB -- more than a third of the
    // ESP32's usable RAM, and it has to coexist with the Wi-Fi stack. At 8 bpp
    // with a palette it is 57.6 kB, which fits comfortably. This is the memory
    // trade-off the whole project is built around; there is no third option
    // where you get a free double buffer.
    _sprite.setColorDepth(8);
    _have_sprite = _sprite.createSprite(cfg::SCREEN_W, cfg::SCREEN_H) != nullptr;
    if (!_have_sprite) {
        Serial.println("[globe] sprite alloc FAILED -- drawing direct, expect flicker");
    }
    _tft.fillScreen(palette(s.sw.night).bg);
}

void GlobeScreen::drawInto(LovyanGFX* g, const AppState& s) {
    const Palette& p = palette(s.sw.night || s.dim);
    const orbit::GeoPoint view{_view_lat, _view_lon, 0.0};

    g->fillScreen(p.bg);
    g->fillCircle(CX, CY, GLOBE_R, p.ocean);
    g->drawCircle(CX, CY, GLOBE_R, p.grid);

    // Graticule: meridians every 30 degrees, parallels every 30 degrees.
    // Only the near hemisphere is drawn -- projectOrthographic reports which
    // points are around the back, and joining across that boundary is what
    // produces the classic "line slashed across the globe" bug.
    for (int lon = -180; lon < 180; lon += 30) {
        orbit::ScreenPoint prev{};
        for (int lat = -90; lat <= 90; lat += 5) {
            const auto cur = orbit::projectOrthographic(
                {static_cast<double>(lat), static_cast<double>(lon), 0}, view, CX, CY, GLOBE_R);
            if (cur.visible && prev.visible) g->drawLine(prev.x, prev.y, cur.x, cur.y, p.grid);
            prev = cur;
        }
    }
    for (int lat = -60; lat <= 60; lat += 30) {
        orbit::ScreenPoint prev{};
        for (int lon = -180; lon <= 180; lon += 5) {
            const auto cur = orbit::projectOrthographic(
                {static_cast<double>(lat), static_cast<double>(lon), 0}, view, CX, CY, GLOBE_R);
            if (cur.visible && prev.visible) g->drawLine(prev.x, prev.y, cur.x, cur.y, p.grid);
            prev = cur;
        }
    }
    // The equator, brighter.
    {
        orbit::ScreenPoint prev{};
        for (int lon = -180; lon <= 180; lon += 4) {
            const auto cur = orbit::projectOrthographic(
                {0.0, static_cast<double>(lon), 0}, view, CX, CY, GLOBE_R);
            if (cur.visible && prev.visible) g->drawLine(prev.x, prev.y, cur.x, cur.y, p.land);
            prev = cur;
        }
    }

    // Ground track. The ring buffer is walked oldest-first so the trail fades
    // in the right direction.
    if (s.sw.track_trail && s.trail_size > 1) {
        orbit::ScreenPoint prev{};
        for (int k = 0; k < s.trail_size; ++k) {
            const int idx = (s.trail_head - s.trail_size + k + AppState::kTrailLen * 2)
                            % AppState::kTrailLen;
            const auto cur = orbit::projectOrthographic(s.trail[idx], view, CX, CY, GLOBE_R);
            if (cur.visible && prev.visible) g->drawLine(prev.x, prev.y, cur.x, cur.y, p.dim);
            prev = cur;
        }
    }

    // The ISS itself.
    const auto iss = orbit::projectOrthographic(s.iss, view, CX, CY, GLOBE_R);
    if (iss.visible) {
        g->drawFastHLine(iss.x - 7, iss.y, 15, p.accent);
        g->drawFastVLine(iss.x, iss.y - 7, 15, p.accent);
        g->fillCircle(iss.x, iss.y, 3, p.text);
        g->drawCircle(iss.x, iss.y, 6, p.accent);
    }

    // Observer mark.
    const auto home = orbit::projectOrthographic(
        {cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG, 0.0}, view, CX, CY, GLOBE_R);
    if (home.visible) g->fillCircle(home.x, home.y, 2, p.warn);

    g->setTextDatum(middle_center);
    g->setTextColor(p.dim);
    g->setTextSize(1);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%c  %.1f%c",
                  std::fabs(s.iss.lat_deg), s.iss.lat_deg >= 0 ? 'N' : 'S',
                  std::fabs(s.iss.lon_deg), s.iss.lon_deg >= 0 ? 'E' : 'W');
    g->drawString(buf, CX, 226);
}

void GlobeScreen::render(const AppState& s) {
    // Slew toward the ISS. The difference is wrapped, not the endpoints --
    // otherwise crossing the date line spins the planet 358 degrees backwards
    // instead of nudging it 2 degrees forward.
    const double dlon = orbit::wrapLongitude(s.iss.lon_deg - _view_lon);
    _view_lon = orbit::wrapLongitude(_view_lon + dlon * kSlew);
    _view_lat += (s.iss.lat_deg * 0.6 - _view_lat) * kSlew;   // tilt, damped

    if (_have_sprite) {
        drawInto(&_sprite, s);
        _sprite.pushSprite(&_tft, 0, 0);
    } else {
        drawInto(&_tft, s);
    }
}

void GlobeScreen::onClick(AppState& s) { (void)s; }   // one mode, no knob
