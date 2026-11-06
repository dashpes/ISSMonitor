#pragma once
//
// Right screen. Four modes, cycled by the right encoder (or 3/4 on the
// console):
//   0 NEXT PASS   countdown + a compass rose with the pass arc drawn on it
//   1 SKY NOW     polar plot: zenith at the centre, horizon at the rim
//   2 SCHEDULE    the next three passes
//   3 VISIBILITY  sunlit satellite + dark sky, or not
//
#include "ui/rotated_frame.h"
#include "ui/screen.h"

class SkyScreen : public Screen {
  public:
    explicit SkyScreen(RoundPanel& p) : Screen(p), _frame(p, cfg::ROT_SKY_DEG) {}
    void begin(const AppState& s) override;
    void render(const AppState& s) override;
    void onClick(AppState& s) override;
    const char* name() const override { return "SKY"; }

  private:
    void drawCompassRose(const AppState& s, int radius);

    RotatedFrame _frame;           // what every draw call below goes through

    int64_t _last_countdown = -1;
    uint8_t _last_mode = 255;
    bool    _last_night = false;
};
