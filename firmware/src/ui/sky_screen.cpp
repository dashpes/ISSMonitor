#include "ui/sky_screen.h"

#include <cmath>
#include <cstdio>

#include "orbit/geo.h"
#include "theme.h"

namespace {

constexpr int CX = cfg::SCREEN_R;
constexpr int CY = cfg::SCREEN_R;

const char* modeName(uint8_t m) {
    switch (m) {
        case 0: return "NEXT PASS";
        case 1: return "SKY NOW";
        case 2: return "SCHEDULE";
        case 3: return "VISIBILITY";
    }
    return "";
}

void fmtHMS(int64_t secs, char* out, size_t cap) {
    if (secs < 0) secs = 0;
    std::snprintf(out, cap, "%02lld:%02lld:%02lld",
                  secs / 3600, (secs % 3600) / 60, secs % 60);
}

// Polar sky plot: zenith at the centre, horizon at the rim. An elevation of
// 90 maps to r=0, an elevation of 0 maps to r=R. Azimuth 0 (north) is up, and
// angles run clockwise -- which is the opposite of how atan2 thinks, hence
// the minus sign.
void azElToXY(double az_deg, double el_deg, int radius, int& x, int& y) {
    if (el_deg < 0.0) el_deg = 0.0;
    const double r = radius * (1.0 - el_deg / 90.0);
    const double a = orbit::deg2rad(az_deg - 90.0);
    x = CX + static_cast<int>(r * std::cos(a));
    y = CY + static_cast<int>(r * std::sin(a));
}

}  // namespace

void SkyScreen::begin(const AppState& s) {
    _frame.begin(name());
    const bool night = s.sw.night || s.dim;
    _tft.fillScreen(palette(night).bg);   // the panel's corners, outside the frame
    _frame.target().fillScreen(_frame.pens(night).bg);
    _last_mode = 255;
}

void SkyScreen::drawCompassRose(const AppState& s, int radius) {
    LovyanGFX& gfx = _frame.target();
    const Palette& p = _frame.pens(s.sw.night || s.dim);
    gfx.drawCircle(CX, CY, radius, p.grid);
    gfx.drawCircle(CX, CY, radius / 2, p.grid);

    static const char* kCardinals[4] = {"N", "E", "S", "W"};
    gfx.setTextDatum(middle_center);
    gfx.setTextSize(1);
    gfx.setTextColor(p.dim, p.bg);
    for (int i = 0; i < 4; ++i) {
        int x, y;
        azElToXY(i * 90.0, 0.0, radius + 12, x, y);
        gfx.drawString(kCardinals[i], x, y);
    }
}

void SkyScreen::render(const AppState& s) {
    const uint8_t mode = s.mode[SCR_SKY];
    const bool night = s.sw.night || s.dim;
    LovyanGFX& gfx = _frame.target();
    const Palette& p = _frame.pens(night);

    const bool repaint = (mode != _last_mode) || (night != _last_night);
    if (repaint) {
        _last_mode = mode;
        _last_night = night;
        _last_countdown = -1;
        gfx.fillScreen(p.bg);
        gfx.drawCircle(CX, CY, cfg::SCREEN_R - 1, p.grid);
        gfx.setTextDatum(middle_center);
        gfx.setTextColor(p.dim, p.bg);
        gfx.setTextSize(1);
        gfx.drawString(modeName(mode), CX, 20);
    }

    const bool overhead = s.iss_look.elevation_deg > cfg::PASS_MIN_ELEVATION_DEG;
    const bool have_pass = s.pass_count > 0 && s.time_valid;

    switch (mode) {
        case 0: {   // NEXT PASS -- countdown, then a rose showing where to look
            const int64_t secs = have_pass ? (s.passes[0].aos_unix - s.unix_now) : -1;
            if (secs == _last_countdown && !repaint) return;
            _last_countdown = secs;

            if (repaint) drawCompassRose(s, 66);

            char buf[24];
            gfx.setTextDatum(middle_center);
            gfx.setTextSize(2);
            gfx.setTextColor(overhead ? p.warn : p.text, p.bg);
            if (overhead)        std::snprintf(buf, sizeof(buf), " OVERHEAD ");
            else if (have_pass)  fmtHMS(secs, buf, sizeof(buf));
            else                 std::snprintf(buf, sizeof(buf), " --:--:-- ");
            gfx.drawString(buf, CX, 48);

            if (have_pass && repaint) {
                // Arc across the rose from where it rises to where it sets.
                const orbit::Pass& pass = s.passes[0];
                int x0, y0, x1, y1;
                azElToXY(pass.aos_azimuth_deg, 0, 66, x0, y0);
                azElToXY(pass.max_azimuth_deg, pass.max_elevation_deg, 66, x1, y1);
                gfx.drawLine(x0, y0, x1, y1, p.accent);
                azElToXY(pass.los_azimuth_deg, 0, 66, x0, y0);
                gfx.drawLine(x1, y1, x0, y0, p.accent);
                gfx.fillCircle(x1, y1, 3, p.text);

                char sub[40];
                std::snprintf(sub, sizeof(sub), "MAX %.0f  %s>%s  %s",
                              pass.max_elevation_deg,
                              orbit::compassPoint(pass.aos_azimuth_deg),
                              orbit::compassPoint(pass.los_azimuth_deg),
                              pass.sunlit ? "VISIBLE" : "NOT LIT");
                gfx.setTextSize(1);
                gfx.setTextColor(pass.sunlit ? p.accent : p.dim, p.bg);
                gfx.drawString(sub, CX, 214);
            }
            break;
        }

        case 1: {   // SKY NOW -- polar plot of the live position
            static int prev_x = -1, prev_y = -1;
            int x, y;
            azElToXY(s.iss_look.azimuth_deg, s.iss_look.elevation_deg, 88, x, y);
            if (x == prev_x && y == prev_y && !repaint) return;

            if (repaint) drawCompassRose(s, 88);
            if (prev_x >= 0) gfx.fillCircle(prev_x, prev_y, 5, p.bg);   // erase
            if (repaint) drawCompassRose(s, 88);

            prev_x = x; prev_y = y;
            if (s.iss_look.elevation_deg >= 0.0) {
                gfx.fillCircle(x, y, 4, p.text);
                gfx.drawCircle(x, y, 7, p.accent);
            }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "AZ %3.0f %-3s  EL %+3.0f ",
                          s.iss_look.azimuth_deg,
                          orbit::compassPoint(s.iss_look.azimuth_deg),
                          s.iss_look.elevation_deg);
            gfx.setTextDatum(middle_center);
            gfx.setTextSize(1);
            gfx.setTextColor(s.iss_look.elevation_deg >= 0 ? p.text : p.dim, p.bg);
            gfx.drawString(buf, CX, 214);
            break;
        }

        case 2: {   // SCHEDULE
            const int64_t secs = have_pass ? (s.passes[0].aos_unix - s.unix_now) : -1;
            if (secs / 60 == _last_countdown && !repaint) return;
            _last_countdown = secs / 60;

            gfx.setTextDatum(middle_center);
            gfx.setTextSize(1);
            for (int i = 0; i < 3; ++i) {
                char row[56];
                if (i < s.pass_count) {
                    const orbit::Pass& pass = s.passes[i];
                    const int64_t mins = (pass.aos_unix - s.unix_now) / 60;
                    std::snprintf(row, sizeof(row), "%lldh%02lldm  %2.0f deg  %-3s  %s",
                                  mins / 60, mins % 60, pass.max_elevation_deg,
                                  orbit::compassPoint(pass.aos_azimuth_deg),
                                  pass.sunlit ? "VIS" : "   ");
                    gfx.setTextColor(pass.sunlit ? p.accent : p.dim, p.bg);
                } else {
                    std::snprintf(row, sizeof(row), "%-28s", "--");
                    gfx.setTextColor(p.dim, p.bg);
                }
                gfx.drawString("                              ", CX, 90 + i * 26);
                gfx.drawString(row, CX, 90 + i * 26);
            }
            break;
        }

        default: {  // VISIBILITY
            if (!repaint && _last_countdown == (have_pass ? s.passes[0].aos_unix : 0)) return;
            _last_countdown = have_pass ? s.passes[0].aos_unix : 0;

            const char* verdict = !have_pass        ? "NO DATA"
                                : s.passes[0].sunlit ? "VISIBLE"
                                                     : "NOT LIT";
            gfx.setTextDatum(middle_center);
            gfx.setTextSize(2);
            gfx.setTextColor(have_pass && s.passes[0].sunlit ? p.accent : p.dim, p.bg);
            gfx.drawString("          ", CX, CY - 10);
            gfx.drawString(verdict, CX, CY - 10);

            gfx.setTextSize(1);
            gfx.setTextColor(p.dim, p.bg);
            gfx.drawString("SUNLIT SAT + DARK SKY", CX, CY + 24);
            break;
        }
    }

    _frame.present();
}

void SkyScreen::onClick(AppState& s) {
    s.mode[SCR_SKY] = static_cast<uint8_t>((s.mode[SCR_SKY] + 1) % kModeCount[SCR_SKY]);
}
