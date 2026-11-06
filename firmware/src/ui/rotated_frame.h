#pragma once
//
// ui/rotated_frame.h -- a screen drawn off-screen and pushed at an angle.
//
// Implementation: src/ui/rotated_frame.cpp
//
// The two lower screens sit in the panel turned 135 degrees, because that is
// the only way their header tabs fit. Quarter turns are free (see
// panelQuarterTurns in display_config.h); the leftover 45 is not. LovyanGFX
// can only rotate by an arbitrary angle while COPYING an image, so the screen
// has to exist as an image first. That is this class: a full-frame sprite the
// screen draws into, pushed rotated whenever the screen changed something.
//
// Why a 4-bit palette: 240x240 at 16 bpp is 115 kB, and with Wi-Fi and the
// globe's sprite up the largest free block is ~55 kB. At 4 bpp it is 28.8 kB,
// and these screens only ever draw with the six colours in theme.h. The same
// trick as the globe's sprite, at half the size again.
//
// A palette means draw calls take a SLOT NUMBER, not a colour. pens() hands
// back a Palette whose fields ARE slot numbers, so the drawing code keeps
// writing p.text and p.grid and never needs to know which it was given.
//
// When a screen's rotation is whole quarter turns, nothing is allocated and
// target() is the panel itself -- the original direct, partial-redraw path.
//
#include "display_config.h"
#include "theme.h"

// Copy a full-frame sprite onto its panel, turning it by `soft_deg` on the
// way. 0 is a plain push; anything else is resampled, which costs far more.
void pushRotated(LGFX_Sprite& frame, RoundPanel& panel, int soft_deg);

class RotatedFrame {
  public:
    RotatedFrame(RoundPanel& panel, int rotation_deg)
        : _panel(panel), _sprite(&panel), _soft_deg(softwareRotationDeg(rotation_deg)) {}

    // Allocate. Call from the screen's begin(). If memory is short the screen
    // falls back to drawing straight onto the panel -- upright to within the
    // quarter turns, off by the rest -- and says so on serial.
    void begin(const char* who);

    // Where to draw: the sprite, or the panel when there is no sprite.
    LovyanGFX& target() { return _active ? static_cast<LovyanGFX&>(_sprite) : _panel; }

    // What to draw with: slot numbers into the sprite's palette, or real
    // colours when drawing direct. Reloads the palette when `night` flips, so
    // call it with the same night-or-dim value everywhere in one frame.
    const Palette& pens(bool night);

    bool active() const { return _active; }

    // Push the frame to the panel, rotated. Call once at the end of a render
    // that drew something. A no-op when drawing direct -- it is already there.
    void present();

  private:
    RoundPanel& _panel;
    LGFX_Sprite _sprite;
    int         _soft_deg;
    bool        _active = false;
    int8_t      _night = -1;          // palette loaded: 0 day, 1 night, -1 none
    const char* _who = "";
    bool        _timed = false;       // first push reports its cost, once
};
