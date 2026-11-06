#pragma once
//
// Left screen. Four modes, cycled by the left encoder (or 1/2 on the console):
//   0 SPEED     big number + 270-degree arc gauge
//   1 ALTITUDE  same treatment
//   2 POSITION  lat/lon and distance from Tustin
//   3 STATUS    link state, TLE age, queued passes
//
// The render loop caches the last values it drew and returns early when
// nothing visible has changed. That early-out is the whole reason three SPI
// screens can share one bus and still feel instant.
//
#include "ui/rotated_frame.h"
#include "ui/screen.h"

class TelemetryScreen : public Screen {
  public:
    explicit TelemetryScreen(RoundPanel& p)
        : Screen(p), _frame(p, cfg::ROT_TELEMETRY_DEG) {}
    void begin(const AppState& s) override;
    void render(const AppState& s) override;
    void onClick(AppState& s) override;
    const char* name() const override { return "TELEMETRY"; }

  private:
    void drawFurniture(const AppState& s);
    void drawGauge(const AppState& s, double value);
    static double deg_to_rad_local(double d);

    RotatedFrame _frame;           // what every draw call below goes through

    double    _last_a = 0.0 / 0.0;    // NaN: nothing drawn yet
    double    _last_b = 0.0 / 0.0;
    uint8_t   _last_mode = 255;
    bool      _last_night = false;
    LinkState _last_link = LinkState::Booting;
};
