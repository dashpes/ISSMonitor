#pragma once
//
// ui/screen.h -- the abstract base every screen derives from.
//
// The virtual-dispatch contract for the three screens. Worth reading closely:
//
//   * `virtual ... = 0` makes Screen abstract -- you cannot instantiate it,
//     only derive from it. Each = 0 is a slot every subclass MUST fill.
//   * `override` on the subclass is not decoration. It makes the compiler
//     reject a signature typo that would otherwise silently create a new
//     unrelated method and leave the base's version running. Always use it.
//   * The virtual destructor matters the moment you hold screens through a
//     `Screen*`. Without it, deleting through the base pointer is undefined
//     behaviour. You never delete these (they are globals) but the habit is
//     cheap and its absence is a genuine bug elsewhere.
//   * Each virtual call costs a pointer indirection. On a 240 MHz core doing
//     three of these per frame, that is free. Do not let anyone tell you
//     virtuals are too slow for embedded at this scale.
//
#include "app/state.h"
#include "display_config.h"

class Screen {
  public:
    explicit Screen(RoundPanel& panel) : _tft(panel) {}
    virtual ~Screen() = default;

    // Called once after the panel is initialised. Draw the parts that never
    // change -- the bezel, dial ticks, static labels.
    virtual void begin(const AppState& s) = 0;

    // Called every UI tick. Draw ONLY what changed since last time. Redrawing
    // all 240x240 at 16 bpp means pushing 115 kB over SPI; at 40 MHz that is
    // ~23 ms per screen, and three of those blows your frame budget. Partial
    // redraw is not an optimisation here, it is the requirement.
    virtual void render(const AppState& s) = 0;

    // Encoder click while this screen has focus. Advance the sub-mode.
    virtual void onClick(AppState& s) = 0;

    // Short human name for the boot screen / serial log.
    virtual const char* name() const = 0;

  protected:
    RoundPanel& _tft;   // a reference, not a copy: panels are not copyable
};
