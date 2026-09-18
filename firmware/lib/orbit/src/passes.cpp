#include "orbit/passes.h"

#include <cmath>

namespace orbit {
namespace {

// Unix seconds at J2000.0 -- 2000-01-01 12:00 UTC. The solar formulae below
// are all written as "days since J2000", so this is where they count from.
constexpr int64_t kUnixJ2000 = 946728000;

// How far the sun has to be below the horizon before the sky is dark enough
// to pick the ISS out of it. Astronomers would say -18 (astronomical
// twilight); the ISS at a good pass is magnitude -3, brighter than any star
// and brighter than Venus, so civil twilight is the right threshold here.
constexpr double kCivilTwilightDeg = -6.0;

// Only used when the caller hands in a step of zero or less, which would make
// the search loop never advance.
constexpr int32_t kDefaultCoarseStepS = 60;

// The hard stop. Without it, a satellite that never sets -- a geostationary
// one, or a bug in the caller's subpoint function -- walks the LOS search
// forward forever, and "forever" on a device with no OS means a watchdog
// reset with three screens frozen mid-draw. The longest possible ISS pass,
// straight overhead horizon to horizon, is about ten minutes.
constexpr int64_t kMaxPassDurationS = 3600;

// Everything the search needs, bundled so the helpers below do not each take
// five arguments in the same order and invite a transposition.
struct Search {
    SubpointFn subpoint;
    void*      ctx;
    GeoPoint   observer;
    double     min_el_deg;
    int32_t    step_s;
};

double elevationAt(const Search& s, int64_t t) {
    return lookAngle(s.observer, s.subpoint(t, s.ctx)).elevation_deg;
}

// Angle subtended at the centre of the Earth between two ground points.
// groundDistanceKm is arc length on the surface, so dividing by the radius
// gives the angle back -- no second trig implementation to keep in step.
double centralAngleRad(const GeoPoint& a, const GeoPoint& b) {
    return groundDistanceKm(a, b) / kEarthRadiusKm;
}

// The first second in (lo, hi] where "elevation >= min" stops agreeing with
// what it was at lo. Works for both edges of a pass: hand it a rise and you
// get the first second above, hand it a set and you get the first second
// below.
//
// Precondition: the two ends disagree. The caller has always just established
// that by finding the crossing in the first place.
//
// Terminates because mid is strictly between lo and hi whenever hi - lo >= 2,
// so the interval shrinks every iteration. That is worth checking rather than
// assuming -- a bisection that can stall is the same bug as an unbounded
// loop, just better disguised.
int64_t refineCrossing(const Search& s, int64_t lo, int64_t hi) {
    const bool above_at_lo = elevationAt(s, lo) >= s.min_el_deg;
    while (hi - lo > 1) {
        const int64_t mid = lo + (hi - lo) / 2;      // not (lo+hi)/2: overflow
        if ((elevationAt(s, mid) >= s.min_el_deg) == above_at_lo) lo = mid;
        else                                                      hi = mid;
    }
    return hi;
}

// The second of highest elevation in [lo, hi].
//
// Elevation through a pass has exactly one peak, and that is what lets a
// ternary search work: sample two interior points, and whichever side is
// lower cannot contain the maximum, so throw that third away. Roughly 2
// evaluations per 1.5x reduction -- about 30 for a five-minute window, versus
// 300 for a one-second scan.
int64_t findPeak(const Search& s, int64_t lo, int64_t hi) {
    while (hi - lo > 2) {
        const int64_t third = (hi - lo) / 3;         // >= 1, so lo/hi do move
        const int64_t m1 = lo + third;
        const int64_t m2 = hi - third;
        if (elevationAt(s, m1) < elevationAt(s, m2)) lo = m1;
        else                                         hi = m2;
    }
    int64_t best    = lo;
    double  best_el = elevationAt(s, lo);
    for (int64_t t = lo + 1; t <= hi; ++t) {         // at most three left
        const double el = elevationAt(s, t);
        if (el > best_el) { best_el = el; best = t; }
    }
    return best;
}

// Walk forward from a point known to be above the minimum until the satellite
// sets, then fill in `out`. Returns the LOS time, which is also where the
// caller resumes the coarse scan.
int64_t closePass(const Search& s, int64_t aos, int64_t known_up, Pass& out) {
    const int64_t must_end_by = aos + kMaxPassDurationS;

    int64_t a = known_up;
    int64_t b = a + s.step_s;
    while (elevationAt(s, b) >= s.min_el_deg && b < must_end_by) {
        a = b;
        b += s.step_s;
    }

    // Two ways out of that loop. Either elevation dropped below the minimum
    // somewhere in (a, b] -- the normal case, bisect for the exact second --
    // or we hit the cap, in which case b is the answer we are willing to give.
    const int64_t los = (elevationAt(s, b) < s.min_el_deg)
                            ? refineCrossing(s, a, b)
                            : b;

    const int64_t peak = findPeak(s, aos, los);

    const LookAngle at_aos  = lookAngle(s.observer, s.subpoint(aos,  s.ctx));
    const GeoPoint  sat_max = s.subpoint(peak, s.ctx);
    const LookAngle at_max  = lookAngle(s.observer, sat_max);
    const LookAngle at_los  = lookAngle(s.observer, s.subpoint(los,  s.ctx));

    out.aos_unix          = aos;
    out.max_unix          = peak;
    out.los_unix          = los;
    out.max_elevation_deg = at_max.elevation_deg;
    out.aos_azimuth_deg   = at_aos.azimuth_deg;
    out.max_azimuth_deg   = at_max.azimuth_deg;
    out.los_azimuth_deg   = at_los.azimuth_deg;
    // Judged at the high point: that is when you would actually be looking,
    // and a pass that starts in shadow and emerges is still a pass worth
    // going outside for.
    out.sunlit            = isVisiblePass(sat_max, s.observer, peak);

    return los;
}

}  // namespace

int findPasses(SubpointFn subpoint, void* ctx,
               const GeoPoint& observer,
               int64_t from_unix,
               int32_t horizon_s,
               double  min_elevation_deg,
               int32_t coarse_step_s,
               Pass*   out,
               int     max_passes) {
    if (subpoint == nullptr || out == nullptr) return 0;
    if (max_passes <= 0 || horizon_s <= 0)     return 0;

    Search s;
    s.subpoint   = subpoint;
    s.ctx        = ctx;
    s.observer   = observer;
    s.min_el_deg = min_elevation_deg;
    // A step of zero means `t` never advances and this function never
    // returns. Clamp rather than trust; there is no upper clamp because too
    // LARGE a step is not a hang, it is just a missed pass, and the caller is
    // entitled to that trade. test_passes.cpp measures exactly what it costs.
    s.step_s     = (coarse_step_s > 0) ? coarse_step_s : kDefaultCoarseStepS;

    const int64_t end = from_unix + horizon_s;
    int found = 0;

    int64_t t  = from_unix;
    double  el = elevationAt(s, t);

    // Starting in the middle of a pass. The real AOS is in the past and this
    // function only searches forward, so it is reported as starting now --
    // which is also what you want on the screen: a pass in progress should
    // say "NOW", not a time that has already gone by.
    if (el >= s.min_el_deg) {
        t  = closePass(s, from_unix, from_unix, out[found]);
        ++found;
        el = elevationAt(s, t);
    }

    while (t < end && found < max_passes) {
        const int64_t next    = t + s.step_s;
        const double  next_el = elevationAt(s, next);

        if (el < s.min_el_deg && next_el >= s.min_el_deg) {
            const int64_t aos = refineCrossing(s, t, next);
            // The coarse step can straddle the horizon, so the crossing it
            // just found may be on the far side of it. A pass that has not
            // started yet by the time the caller stopped caring is not a
            // result -- and this is also what makes the horizon exact rather
            // than "within one coarse step".
            if (aos >= end) break;
            const int64_t los = closePass(s, aos, next, out[found]);
            ++found;
            // Resume from the set, not from `next` -- otherwise the same pass
            // is found again on the very next step.
            t  = los;
            el = elevationAt(s, t);
            continue;
        }

        t  = next;
        el = next_el;
    }

    return found;
}

GeoPoint subsolarPoint(int64_t unix_time) {
    // The Astronomical Almanac's low-precision solar coordinates: good to
    // about 0.01 degrees for a century either side of 2000. This decides "is
    // it dark yet" and "is the ISS still in sunlight", and both answers are
    // the same whether the sun's position is known to an arcsecond or to a
    // degree, so the cheap version is not a compromise here.
    const double n = (static_cast<double>(unix_time) - kUnixJ2000) / 86400.0;

    const double mean_lon_deg  = 280.460 + 0.9856474 * n;   // deg
    const double mean_anom_rad = deg2rad(357.528 + 0.9856003 * n);

    // Ecliptic longitude: the mean position plus the equation of centre, which
    // is the correction for the Earth's orbit being an ellipse and not a
    // circle. Two terms is plenty at this precision.
    const double ecl_lon_rad = deg2rad(mean_lon_deg
                                       + 1.915 * std::sin(mean_anom_rad)
                                       + 0.020 * std::sin(2.0 * mean_anom_rad));

    // Tilt of the Earth's axis. The tiny secular term is why the tropics are
    // creeping towards the equator at about half an arcsecond a year.
    const double obliquity_rad = deg2rad(23.439 - 0.0000004 * n);

    const double sin_ecl = std::sin(ecl_lon_rad);
    const double dec_rad = std::asin(std::sin(obliquity_rad) * sin_ecl);
    const double ra_rad  = std::atan2(std::cos(obliquity_rad) * sin_ecl,
                                      std::cos(ecl_lon_rad));

    // Greenwich mean sidereal time: which meridian is currently facing the
    // vernal equinox. The sun's right ascension is measured from that same
    // point, so the difference is a longitude on the rotating Earth. Note the
    // rate -- 360.98565 degrees per day, not 360: a sidereal day is the four
    // minutes shorter one.
    const double gmst_deg = 280.46061837 + 360.98564736629 * n;

    GeoPoint out;
    out.lat_deg = rad2deg(dec_rad);
    out.lon_deg = wrapLongitude(rad2deg(ra_rad) - gmst_deg);
    out.alt_km  = 0.0;
    return out;
}

double sunElevationDeg(const GeoPoint& observer, int64_t unix_time) {
    // Straight up at the subsolar point, on the horizon a quarter turn away.
    // No refraction and no allowance for the sun's half-degree width: both
    // matter at sunrise and neither matters at the -6 degree threshold this
    // exists to test.
    return 90.0 - rad2deg(centralAngleRad(observer, subsolarPoint(unix_time)));
}

bool isVisiblePass(const GeoPoint& satellite,
                   const GeoPoint& observer,
                   int64_t unix_time) {
    const GeoPoint sun = subsolarPoint(unix_time);

    // Half of it: is it dark where you are standing? In daylight the ISS is
    // lit perfectly well and you still cannot see it.
    const double sun_el = 90.0 - rad2deg(centralAngleRad(observer, sun));
    if (sun_el > kCivilTwilightDeg) return false;

    // The other half: is the ISS itself still in sunlight? It is only bright
    // because it reflects the sun, so once it enters the Earth's shadow it
    // vanishes mid-sky -- which is a startling thing to watch happen.
    //
    // Cylindrical shadow: treat the shadow as a cylinder of the Earth's
    // radius stretching away from the sun. `psi` is the angle at the centre
    // of the Earth between the sun's direction and the satellite's.
    const double psi = centralAngleRad(satellite, sun);
    // Sunward hemisphere: nothing between it and the sun.
    if (std::cos(psi) >= 0.0) return true;
    // Anti-sunward: lit only if it is far enough off the axis to clear the
    // planet's own shadow. Ignores the penumbra and the fact that the shadow
    // is really a cone -- both are worth well under a minute at 400 km.
    const double r_km = kEarthRadiusKm + satellite.alt_km;
    return r_km * std::sin(psi) > kEarthRadiusKm;
}

}  // namespace orbit
