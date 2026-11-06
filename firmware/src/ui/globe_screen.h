#pragma once
//
// Centre screen. One mode, no knob -- it is the showpiece and should look the
// same from across the room.
//
// Orthographic projection of the coastline, a graticule, the ground track and
// the ISS, composited in a 4-bit palette sprite. See begin() for why.
//
// The land and graticule vertices are generated -- src/ui/globe_data.h, built
// by scripts/gen_globe_data.py. They ship as unit vectors rather than lat/lon
// so that drawing them needs no trigonometry at all; orbit::OrthoView in
// geo.h explains why that matters on this chip.
//
#include "orbit/geo.h"
#include "theme.h"
#include "ui/rotated_frame.h"
#include "ui/screen.h"

// The eight colours the globe draws with. In the sprite they are PALETTE
// INDICES; in the no-sprite fallback they are raw RGB565 straight from
// theme.h. Same eight names either way, so drawInto() never has to know which
// kind it was handed -- see the note in begin() for why the sprite needs a
// palette at all.
struct GlobePens {
    uint16_t bg, ocean, grid, land, land_fill, dim, accent, text, warn;
};

class GlobeScreen : public Screen {
  public:
    explicit GlobeScreen(RoundPanel& p) : Screen(p), _sprite(&p) {}
    void begin(const AppState& s) override;
    void render(const AppState& s) override;
    void onClick(AppState& s) override;
    const char* name() const override { return "GLOBE"; }

  private:
    void drawInto(LovyanGFX* g, const AppState& s, const orbit::OrthoView& view,
                  const GlobePens& p);
    void loadPalette(const Palette& p);
    bool ensureMask(const orbit::OrthoView& view);   // true if it did work
    void rebuildBand(const orbit::OrthoView& view, int band);
    void paintLand(LovyanGFX* g, uint16_t colour) const;
    // Frame accounting. Compiles to nothing unless cfg::kGlobeProfile.
    void profileTick(bool rebuilt, uint32_t t_start, uint32_t t_mask,
                     uint32_t t_draw, uint32_t t_end);

    LGFX_Sprite _sprite;
    bool   _have_sprite = false;
    // Which palette is currently uploaded: 0 day, 1 night, -1 nothing yet.
    int8_t _palette_night = -1;
    double _view_lat = 0.0;
    double _view_lon = 0.0;

    // Screen-space land/sea answer, one bit per pixel: 7,200 bytes.
    //
    // Deciding it costs an inverse projection and a cube-map lookup per pixel
    // of the disc, ~34,000 of them. That is far too much to repeat every
    // frame -- but it does not need repeating. The view tracks the ISS, which
    // drifts across the globe at about 0.075 px/s, so the picture changes by
    // one pixel roughly every thirteen seconds. The mask is rebuilt when the
    // view has actually moved that far and simply blitted the rest of the
    // time. Everything that really moves -- the marker, the trail, the text --
    // is drawn on top afterwards.
    // SCREEN_W is a multiple of 8, so every row starts on a byte boundary and
    // a row can be cleared with one memset.
    static constexpr int kMaskStride = cfg::SCREEN_W / 8;
    static constexpr int kMaskBytes  = kMaskStride * cfg::SCREEN_H;
    uint8_t _mask[kMaskBytes] = {};
    bool    _mask_valid = false;

    // A full rebuild measured 62 ms on the bench -- see README. MS_REDRAW is
    // 100 ms and all three screens draw in the same tick, so doing it in one
    // go overran the frame by itself. It is spread over kMaskBands frames
    // instead: the disc is rebuilt a horizontal band at a time, and the old
    // mask keeps being drawn meanwhile. Over the ~0.8 s that takes, the view
    // drifts about 0.06 px, so nobody can see the seam.
    static constexpr int kMaskBands = 8;
    int _mask_band = kMaskBands;     // == kMaskBands means "no pass in flight"
    // The view the in-flight pass started against. Every band must use it, or
    // the bands disagree with each other and slice the coastline into strips.
    orbit::OrthoView _mask_view{};
    // The view the finished mask belongs to, kept as axes rather than lat/lon
    // so the "has it moved?" test is two dot products and no trigonometry.
    orbit::UnitVec _mask_up{};
    orbit::UnitVec _mask_east{};
};
