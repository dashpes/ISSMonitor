#pragma once
//
// theme.h -- the console's two palettes.
//
// Day mode is amber-on-black CRT. Night mode is deep red only, which keeps
// your dark adaptation intact when you step outside for a pass -- that is
// the real reason the NIGHT toggle exists, not just to be dimmer.
//
// Change the numbers freely; that is what this file is here for.
//
#include <cstdint>

// RGB565. Use the helper rather than hand-computing hex.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Palette {
    uint16_t bg;
    uint16_t text;
    uint16_t dim;
    uint16_t accent;    // gauges, the ISS marker
    uint16_t warn;      // pass imminent
    uint16_t grid;      // globe graticule, dial ticks
    uint16_t ocean;
    uint16_t land;       // the coastline stroke
    uint16_t land_fill;  // the interior; darker, so the coast still reads
};

constexpr Palette kDayPalette{
    /* bg     */ rgb565(  4,   6,  10),
    /* text   */ rgb565(255, 176,  40),
    /* dim    */ rgb565(120,  84,  20),
    /* accent */ rgb565( 80, 220, 255),
    /* warn   */ rgb565(255,  70,  60),
    /* grid   */ rgb565( 30,  48,  64),
    /* ocean  */ rgb565( 10,  34,  58),
    /* land   */ rgb565( 40, 110,  80),
    /* fill   */ rgb565( 16,  60,  40),
};

constexpr Palette kNightPalette{
    /* bg     */ rgb565(  0,   0,   0),
    /* text   */ rgb565(190,  30,  20),
    /* dim    */ rgb565( 90,  12,   8),
    /* accent */ rgb565(150,  24,  16),
    /* warn   */ rgb565(255,  40,  30),
    /* grid   */ rgb565( 45,   6,   4),
    /* ocean  */ rgb565( 20,   2,   2),
    /* land   */ rgb565( 70,  10,   8),
    /* fill   */ rgb565( 40,   6,   4),
};

inline const Palette& palette(bool night) {
    return night ? kNightPalette : kDayPalette;
}
