#include "ui/globe_screen.h"

#include <cmath>
#include <cstring>

#include "orbit/geo.h"
#include "theme.h"
#include "ui/globe_data.h"
#include "ui/land_mask.h"

namespace {

constexpr int CX = cfg::SCREEN_R;
constexpr int CY = cfg::SCREEN_R;
// If you change this, change GLOBE_R_PX in scripts/gen_globe_data.py to match
// and re-run it. The table's simplification tolerance is in these pixels, so
// a much larger globe would show the corners the script was allowed to cut.
constexpr int GLOBE_R = 104;

// Ease the view toward the ISS rather than snapping to it, so the planet
// drifts instead of twitching once a second.
constexpr double kSlew = 0.15;

// Palette slots. These must match the order loadPalette() uploads them in;
// the names themselves live in GlobePens, in the header.
enum PaletteSlot : uint16_t {
    PAL_BG = 0, PAL_OCEAN, PAL_GRID, PAL_LAND, PAL_LAND_FILL,
    PAL_DIM, PAL_ACCENT, PAL_TEXT, PAL_WARN,
};

constexpr GlobePens kSpritePens{PAL_BG,   PAL_OCEAN,  PAL_GRID, PAL_LAND,
                                PAL_LAND_FILL,
                                PAL_DIM,  PAL_ACCENT, PAL_TEXT, PAL_WARN};

GlobePens directPens(const Palette& p) {
    return {p.bg, p.ocean, p.grid, p.land, p.land_fill,
            p.dim, p.accent, p.text, p.warn};
}

// How far the view may drift before the land mask is stale. One pixel at the
// limb subtends 1/GLOBE_R radians; for an angle that small cos(t) == 1 - t*t/2
// to well past float precision, so this avoids a trig call in a constexpr.
constexpr float kMaskTolRad = 1.0f / GLOBE_R;
constexpr float kMaskCosTol = 1.0f - 0.5f * kMaskTolRad * kMaskTolRad;

float dot(const orbit::UnitVec& a, const orbit::UnitVec& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Frame-cost accounting for cfg::kGlobeProfile. All of this compiles away
// when the flag is off -- what is left is four micros() reads per frame, which
// is around 4 us in a 100 ms tick and not worth a macro to avoid.
struct Profile {
    uint32_t frames = 0;
    uint32_t rebuilds = 0;
    uint32_t mask_us = 0;        // most recent rebuild, not an average
    uint32_t draw_us_sum = 0;
    uint32_t draw_us_max = 0;
    uint32_t push_us_sum = 0;
    uint32_t frame_us_max = 0;
    uint32_t last_report_ms = 0;
    // Breakdown of drawInto, because "draw" on its own does not say which of
    // the five things it does is the expensive one.
    uint32_t bg_us = 0, land_us = 0, grat_us = 0, coast_us = 0, over_us = 0;
};
Profile g_prof;

// Draw one of the generated tables from src/ui/globe_data.h.
//
// Two things here are load-bearing, and both are about NOT drawing:
//
//  1. A {0,0,0} vertex is the pen-up marker between strokes. Joining across
//     one runs a line from the tip of Norway to the coast of Chile.
//  2. A segment is drawn only when BOTH ends are on the near hemisphere.
//     Joining across the limb is what produces the classic line slashed
//     straight through the middle of the globe.
//
// Both are the same mistake -- treating a flat array as one continuous path --
// and both look identical when you get them wrong.
void drawStrokes(LovyanGFX* g, const orbit::OrthoView& view,
                 const globe_data::Vertex* verts, int count, uint16_t colour) {
    // Default-constructed means visible == false, which suppresses the join
    // into the first point of a stroke. Same trick the graticule always used.
    orbit::ScreenPoint prev{};

    for (int i = 0; i < count; ++i) {
        const globe_data::Vertex& v = verts[i];
        if (globe_data::isPenUp(v)) {
            prev = orbit::ScreenPoint{};
            continue;
        }

        const orbit::ScreenPoint cur = orbit::projectUnit(
            view, {v.x * globe_data::kInvScale,
                   v.y * globe_data::kInvScale,
                   v.z * globe_data::kInvScale});

        if (cur.visible && prev.visible) {
            g->drawLine(prev.x, prev.y, cur.x, cur.y, colour);
        }
        prev = cur;
    }
}

}  // namespace

void GlobeScreen::begin(const AppState& s) {
    _view_lat = 0.0;
    _view_lon = s.iss.lon_deg;

    // A full 240x240 sprite at 16 bpp is 115 kB -- more than a third of the
    // ESP32's usable RAM, and it has to coexist with the Wi-Fi stack. At 8 bpp
    // it is 57.6 kB, which fits comfortably. This is the memory trade-off the
    // whole project is built around; there is no third option where you get a
    // free double buffer.
    //
    // `palette_8bit` and not plain `8`. Both are one byte per pixel, but plain
    // 8 means rgb332 -- three bits of red, three of green, and TWO of blue.
    // Two bits of blue cannot represent this theme: the ocean is
    // rgb565(10, 34, 58), and 58 does not reach the bottom of the top two
    // bits, so its blue is discarded outright and the Atlantic comes out dark
    // green. Against green coastlines the planet reads as one flat smudge.
    //
    // A palette keeps every colour in theme.h exact. The cost is that draw
    // calls take an index rather than a colour -- hence GlobePens, in the
    // header.
    //
    // And 4 bits of palette, not 8: the globe draws with nine colours, so 16
    // slots is plenty, and it halves the sprite to 28.8 kB. That half is not
    // spare. The two lower screens each need a 28.8 kB frame of their own to
    // be rotated (ui/rotated_frame.h), and with the globe still at 8 bits the
    // HTTPS fetch of the TLE had too little heap left to start TLS at all.
    _sprite.setColorDepth(lgfx::palette_4bit);
    _have_sprite = _sprite.createSprite(cfg::SCREEN_W, cfg::SCREEN_H) != nullptr;
    if (!_have_sprite) {
        Serial.println("[globe] sprite alloc FAILED -- drawing direct, expect flicker");
    }
    _palette_night = -1;   // force the first upload
    _tft.fillScreen(palette(s.sw.night).bg);
}

// Push the eight theme colours into the sprite's lookup table. Cheap, but not
// free, so it runs only when the NIGHT/dim state has actually flipped.
void GlobeScreen::loadPalette(const Palette& p) {
    _sprite.setPaletteColor(PAL_BG,     p.bg);
    _sprite.setPaletteColor(PAL_OCEAN,  p.ocean);
    _sprite.setPaletteColor(PAL_GRID,   p.grid);
    _sprite.setPaletteColor(PAL_LAND,   p.land);
    _sprite.setPaletteColor(PAL_LAND_FILL, p.land_fill);
    _sprite.setPaletteColor(PAL_DIM,    p.dim);
    _sprite.setPaletteColor(PAL_ACCENT, p.accent);
    _sprite.setPaletteColor(PAL_TEXT,   p.text);
    _sprite.setPaletteColor(PAL_WARN,   p.warn);
}

void GlobeScreen::drawInto(LovyanGFX* g, const AppState& s,
                           const orbit::OrthoView& view, const GlobePens& p) {
    const uint32_t t0 = micros();
    g->fillScreen(p.bg);
    g->fillCircle(CX, CY, GLOBE_R, p.ocean);
    g->drawCircle(CX, CY, GLOBE_R, p.grid);

    // Land, from the cached mask. This is what makes the planet legible: an
    // outline is a closed curve, and a closed curve does not say which side of
    // itself is inside. Without a fill the coastline and the graticule are
    // just two sets of lines.
    const uint32_t t1 = micros();
    paintLand(g, p.land_fill);

    // Back to front: graticule, then the equator over it, then the land.
    const uint32_t t2 = micros();
    drawStrokes(g, view, globe_data::kGraticule, globe_data::kGraticuleCount, p.grid);
    const uint32_t t3 = micros();
    drawStrokes(g, view, globe_data::kEquator, globe_data::kEquatorCount, p.land);
    drawStrokes(g, view, globe_data::kCoastline, globe_data::kCoastlineCount, p.land);
    const uint32_t t4 = micros();

    // Ground track. The ring buffer is walked oldest-first so the trail fades
    // in the right direction.
    if (s.sw.track_trail && s.trail_size > 1) {
        orbit::ScreenPoint prev{};
        for (int k = 0; k < s.trail_size; ++k) {
            const int idx = (s.trail_head - s.trail_size + k + AppState::kTrailLen * 2)
                            % AppState::kTrailLen;
            const auto cur = orbit::projectUnit(view, orbit::toUnitVector(s.trail[idx]));
            if (cur.visible && prev.visible) g->drawLine(prev.x, prev.y, cur.x, cur.y, p.dim);
            prev = cur;
        }
    }

    // The ISS itself.
    const auto iss = orbit::projectUnit(view, orbit::toUnitVector(s.iss));
    if (iss.visible) {
        g->drawFastHLine(iss.x - 7, iss.y, 15, p.accent);
        g->drawFastVLine(iss.x, iss.y - 7, 15, p.accent);
        g->fillCircle(iss.x, iss.y, 3, p.text);
        g->drawCircle(iss.x, iss.y, 6, p.accent);
    }

    // Observer mark.
    const auto home = orbit::projectUnit(
        view, orbit::toUnitVector({cfg::OBS_LAT_DEG, cfg::OBS_LON_DEG, 0.0}));
    if (home.visible) g->fillCircle(home.x, home.y, 2, p.warn);

    g->setTextDatum(middle_center);
    g->setTextColor(p.dim);
    g->setTextSize(1);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f%c  %.1f%c",
                  std::fabs(s.iss.lat_deg), s.iss.lat_deg >= 0 ? 'N' : 'S',
                  std::fabs(s.iss.lon_deg), s.iss.lon_deg >= 0 ? 'E' : 'W');
    g->drawString(buf, CX, 226);

    if (cfg::kGlobeProfile) {
        g_prof.bg_us    += t1 - t0;
        g_prof.land_us  += t2 - t1;
        g_prof.grat_us  += t3 - t2;
        g_prof.coast_us += t4 - t3;
        g_prof.over_us  += micros() - t4;
    }
}

void GlobeScreen::render(const AppState& s) {
    // Slew toward the ISS. The difference is wrapped, not the endpoints --
    // otherwise crossing the date line spins the planet 358 degrees backwards
    // instead of nudging it 2 degrees forward.
    const double dlon = orbit::wrapLongitude(s.iss.lon_deg - _view_lon);
    _view_lon = orbit::wrapLongitude(_view_lon + dlon * kSlew);
    _view_lat += (s.iss.lat_deg * 0.6 - _view_lat) * kSlew;   // tilt, damped

    // Six trig calls, once. Every vertex below projects against this with nine
    // multiplies and no trig -- which is the only reason ~2,000 coastline and
    // graticule vertices fit in a frame on a chip with no double-precision FPU.
    const orbit::OrthoView view =
        orbit::makeOrthoView({_view_lat, _view_lon, 0.0}, CX, CY, GLOBE_R);

    const uint32_t t_start = micros();
    const bool rebuilt = ensureMask(view);
    const uint32_t t_mask = micros();

    const bool night = s.sw.night || s.dim;

    if (_have_sprite) {
        if (_palette_night != static_cast<int8_t>(night)) {
            loadPalette(palette(night));
            _palette_night = static_cast<int8_t>(night);
        }
        drawInto(&_sprite, s, view, kSpritePens);
        const uint32_t t_draw = micros();
        pushRotated(_sprite, _tft, softwareRotationDeg(cfg::ROT_GLOBE_DEG));
        profileTick(rebuilt, t_start, t_mask, t_draw, micros());
    } else {
        drawInto(&_tft, s, view, directPens(palette(night)));
        const uint32_t t_draw = micros();
        profileTick(rebuilt, t_start, t_mask, t_draw, t_draw);
    }
}

// Roll one frame into the running totals and report once a second.
//
// What the numbers mean, and what would be bad:
//   mask  -- the ~34,000 inverse projections. Expected to fire about once
//            every 13 s; firing every frame means the staleness test is wrong.
//   draw  -- ocean, land runs, ~2,000 stroke vertices, markers, text.
//   push  -- 240x240x2 bytes over SPI. Fixed cost, ~23 ms at 40 MHz, and
//            already the floor before any of this work was added.
//   frame -- the worst single frame seen, which is the one that has to fit
//            inside cfg::MS_REDRAW alongside the other two screens.
void GlobeScreen::profileTick(bool rebuilt, uint32_t t_start, uint32_t t_mask,
                              uint32_t t_draw, uint32_t t_end) {
    if (!cfg::kGlobeProfile) return;

    ++g_prof.frames;
    if (rebuilt) {
        ++g_prof.rebuilds;
        g_prof.mask_us = t_mask - t_start;
    }
    const uint32_t draw_us = t_draw - t_mask;
    const uint32_t frame_us = t_end - t_start;
    g_prof.draw_us_sum += draw_us;
    if (draw_us > g_prof.draw_us_max) g_prof.draw_us_max = draw_us;
    g_prof.push_us_sum += t_end - t_draw;
    if (frame_us > g_prof.frame_us_max) g_prof.frame_us_max = frame_us;

    const uint32_t now = millis();
    if (now - g_prof.last_report_ms < 1000 || g_prof.frames == 0) return;

    const float n = static_cast<float>(g_prof.frames);
    Serial.printf(
        "[globe] %u fps | mask %u bands (%.1f ms ea) | draw %.1f = bg %.1f + land "
        "%.1f + grat %.1f + coast %.1f + over %.1f | push %.1f | worst %.1f ms "
        "| heap %u\n",
        g_prof.frames, g_prof.rebuilds,
        g_prof.mask_us / 1000.0f,
        g_prof.draw_us_sum / 1000.0f / n,
        g_prof.bg_us / 1000.0f / n,
        g_prof.land_us / 1000.0f / n,
        g_prof.grat_us / 1000.0f / n,
        g_prof.coast_us / 1000.0f / n,
        g_prof.over_us / 1000.0f / n,
        g_prof.push_us_sum / 1000.0f / n,
        g_prof.frame_us_max / 1000.0f,
        ESP.getFreeHeap());

    g_prof = Profile{};
    g_prof.last_report_ms = now;
}

// Rebuild the land mask, but only when the view has actually moved a pixel.
// In steady tracking that is once every ~13 seconds, so the ~34,000 inverse
// projections behind it are amortised to almost nothing per frame.
bool GlobeScreen::ensureMask(const orbit::OrthoView& view) {
    // A pass already in flight always finishes first. Restarting mid-pass on
    // every frame is what would happen during the boot slew, when the view
    // moves fast -- and the mask would then never finish a single pass.
    if (_mask_band >= kMaskBands) {
        if (_mask_valid &&
            dot(view.up, _mask_up) >= kMaskCosTol &&
            dot(view.east, _mask_east) >= kMaskCosTol) {
            return false;
        }
        // Both axes, not just `up`: at a pole `up` is unchanged by a change of
        // longitude while the picture underneath it spins. The globe never
        // tilts that far today, but a test that is only accidentally correct
        // is worse than one that costs an extra three multiplies.
        _mask_view = view;
        _mask_up   = view.up;
        _mask_east = view.east;
        _mask_band = 0;
    }

    rebuildBand(_mask_view, _mask_band);
    ++_mask_band;
    _mask_valid = true;
    return true;
}

// One horizontal slice of the disc: clear its rows, then ask the land mask
// what is under each pixel.
void GlobeScreen::rebuildBand(const orbit::OrthoView& view, int band) {
    const int top = CY - GLOBE_R;
    const int rows = 2 * GLOBE_R + 1;
    const int y0 = top + rows * band / kMaskBands;
    const int y1 = top + rows * (band + 1) / kMaskBands;

    for (int y = y0; y < y1; ++y) {
        std::memset(&_mask[y * kMaskStride], 0, kMaskStride);
        for (int x = CX - GLOBE_R; x <= CX + GLOBE_R; ++x) {
            const orbit::SpherePoint p = orbit::unprojectUnit(view, x, y);
            if (!p.on_globe || !land_mask::at(p.dir.x, p.dir.y, p.dir.z)) continue;
            _mask[y * kMaskStride + (x >> 3)] |= static_cast<uint8_t>(1u << (x & 7));
        }
    }
}

// Blit the mask as horizontal runs. Africa is ~60 px across, so this is a few
// hundred drawFastHLine calls rather than ten thousand setPixel calls -- and
// drawFastHLine on a sprite is a memset of one row segment.
void GlobeScreen::paintLand(LovyanGFX* g, uint16_t colour) const {
    for (int y = CY - GLOBE_R; y <= CY + GLOBE_R; ++y) {
        int run_start = -1;
        // One past the right edge, so a run touching it still gets flushed.
        for (int x = CX - GLOBE_R; x <= CX + GLOBE_R + 1; ++x) {
            bool land = false;
            if (x <= CX + GLOBE_R) {
                land = (_mask[y * kMaskStride + (x >> 3)] >> (x & 7)) & 1u;
            }
            if (land) {
                if (run_start < 0) run_start = x;
            } else if (run_start >= 0) {
                g->drawFastHLine(run_start, y, x - run_start, colour);
                run_start = -1;
            }
        }
    }
}

void GlobeScreen::onClick(AppState& s) { (void)s; }   // one mode, no knob
