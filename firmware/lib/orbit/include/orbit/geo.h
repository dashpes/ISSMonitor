#pragma once
//
// orbit/geo.h -- pure coordinate math. No Arduino, no globals, no I/O.
//
// Everything in this header is a free function over plain structs. That is
// deliberate: it compiles on your Mac, so `pio test -e native` can prove the
// math is right months before the case is printed.
//
// Implementation: lib/orbit/src/geo.cpp
// Concepts this exercises: namespaces, struct-by-value, const-correctness,
// pass-by-const-reference, and why `double` is not free on an ESP32.
//
#include <cstdint>

namespace orbit {

// Angles are DEGREES at every public boundary. Radians live inside .cpp only.
// Pick one convention and never waver -- mixed units is the #1 bug in this
// kind of code.

struct GeoPoint {
    double lat_deg = 0.0;
    double lon_deg = 0.0;   // -180..+180, east positive
    double alt_km  = 0.0;
};

struct LookAngle {
    double azimuth_deg   = 0.0;  // 0 = true north, clockwise
    double elevation_deg = 0.0;  // negative = below horizon
    double range_km      = 0.0;
};

// Screen-space point for the globe. `visible` is false when the location is
// on the far side of the sphere and must not be drawn.
struct ScreenPoint {
    int  x = 0;
    int  y = 0;
    bool visible = false;
};

constexpr double kEarthRadiusKm = 6371.0088;  // mean (IUGG)
constexpr double kPi            = 3.14159265358979323846;

inline constexpr double deg2rad(double d) { return d * kPi / 180.0; }
inline constexpr double rad2deg(double r) { return r * 180.0 / kPi; }

// Fold any angle into [0, 360). Must work for -720.5 and +1080.5 alike.
double normalizeDeg(double deg);

// Fold longitude into (-180, +180].
double wrapLongitude(double lon_deg);

// Great-circle distance along the surface, kilometres. Use the haversine
// form, not the naive acos form -- acos loses all precision for small
// distances, which is exactly the case you care about during a pass.
double groundDistanceKm(const GeoPoint& a, const GeoPoint& b);

// Where to point: azimuth/elevation/range from observer to satellite.
// Spherical Earth is fine here -- you are aiming human eyes, not a dish.
LookAngle lookAngle(const GeoPoint& observer, const GeoPoint& satellite);

// Orthographic projection for the globe screen: put `p` on a sphere of radius
// `radius_px` centred at (cx, cy), as seen from directly above `view_center`.
// Sets visible = false for the far hemisphere.
ScreenPoint projectOrthographic(const GeoPoint& p,
                                const GeoPoint& view_center,
                                int cx, int cy, int radius_px);

// Compass point for a heading: "N", "NNE", "NE", ... 16-point rose.
// Returns a pointer to a static string literal -- no allocation, safe to
// hand straight to the display driver.
const char* compassPoint(double azimuth_deg);

// Unit conversions for the UNITS toggle.
inline constexpr double kmToMiles(double km)      { return km * 0.621371; }
inline constexpr double kmhToMph(double kmh)      { return kmh * 0.621371; }

}  // namespace orbit
