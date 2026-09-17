#pragma once
//
// Centre screen. One mode, no knob -- it is the showpiece and should look the
// same from across the room.
//
// Orthographic projection of a graticule, the ground track and the ISS,
// composited in an 8-bit sprite. See begin() for why 8-bit and not 16.
//
#include "ui/screen.h"

class GlobeScreen : public Screen {
  public:
    explicit GlobeScreen(RoundPanel& p) : Screen(p), _sprite(&p) {}
    void begin(const AppState& s) override;
    void render(const AppState& s) override;
    void onClick(AppState& s) override;
    const char* name() const override { return "GLOBE"; }

  private:
    void drawInto(LovyanGFX* g, const AppState& s);

    LGFX_Sprite _sprite;
    bool   _have_sprite = false;
    double _view_lat = 0.0;
    double _view_lon = 0.0;
};
