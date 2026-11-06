#include "ui/rotated_frame.h"

#include <Arduino.h>

namespace {

// The sprite's palette slots, dressed up as a Palette. Field order matches
// theme.h, and loadSlots() below fills the slots in the same order.
constexpr Palette kPaletteSlots{0, 1, 2, 3, 4, 5, 6, 7, 8};

}  // namespace

void pushRotated(LGFX_Sprite& frame, RoundPanel& panel, int soft_deg) {
    if (soft_deg == 0) {
        frame.pushSprite(&panel, 0, 0);
        return;
    }
    // WithAA: bilinear, not nearest-neighbour. At 45 degrees nearest-neighbour
    // turns every one-pixel stroke of the 6x8 font into a staircase with gaps
    // in it; the small labels stop being readable. The sprite's pivot is its
    // centre, so pinning that to the panel's centre spins it in place.
    frame.pushRotateZoomWithAA(&panel, cfg::SCREEN_R, cfg::SCREEN_R,
                               static_cast<float>(soft_deg), 1.0f, 1.0f);
}

void RotatedFrame::begin(const char* who) {
    _who = who;
    if (_soft_deg == 0) return;      // quarter turns only: the panel does it all

    _sprite.setColorDepth(lgfx::palette_4bit);
    _active = _sprite.createSprite(cfg::SCREEN_W, cfg::SCREEN_H) != nullptr;
    if (!_active) {
        Serial.printf("[%s] frame alloc FAILED -- drawing direct, %d degrees off\n",
                      who, _soft_deg);
    }
    _night = -1;
}

const Palette& RotatedFrame::pens(bool night) {
    if (!_active) return palette(night);

    if (_night != static_cast<int8_t>(night)) {
        const Palette& c = palette(night);
        const uint16_t colours[] = {c.bg, c.text, c.dim, c.accent, c.warn,
                                    c.grid, c.ocean, c.land, c.land_fill};
        for (size_t i = 0; i < sizeof(colours) / sizeof(colours[0]); ++i) {
            _sprite.setPaletteColor(i, colours[i]);
        }
        _night = static_cast<int8_t>(night);
    }
    return kPaletteSlots;
}

void RotatedFrame::present() {
    if (!_active) return;

    const uint32_t t0 = micros();
    pushRotated(_sprite, _panel, _soft_deg);

    // The one number that decides whether this approach is affordable. Printed
    // once, so it is on the boot log without scrolling the console forever.
    if (!_timed) {
        _timed = true;
        Serial.printf("[%s] rotated push: %lu ms\n", _who,
                      (unsigned long)((micros() - t0) / 1000));
    }
}
