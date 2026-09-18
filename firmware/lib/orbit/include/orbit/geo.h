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

// --- The same projection, arranged for drawing thousands of points ---------
//
// projectOrthographic() takes a lat/lon, so it must compute four sines and
// cosines for every single point. That is fine for the ISS marker. It is not
// fine for the coastline: `double` trig on an ESP32 is *software* -- the FPU
// is single-precision only -- and the globe draws roughly 2,000 vertices per
// frame inside a 100 ms budget that also has three SPI panel pushes in it.
//
// So the work is split. The trig that depends only on where the camera is
// happens once per frame, in makeOrthoView(). The trig that depends only on
// where a vertex is happens once ever, at build time -- src/ui/globe_data.h
// ships its vertices as unit vectors already. What is left in the inner loop
// is nine multiplies and no trig at all.

// A direction from the centre of the Earth: a point on the unit sphere in
// Earth-centred, Earth-fixed axes. Single-precision on purpose -- the ESP32
// has hardware float, and 240 px of screen does not need more.
struct UnitVec {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// An orthographic projection with the view rotation already worked out.
// The three rows are the observer's own axes written in ECEF: east and north
// span the visible disc, up says which hemisphere a point is on.
struct OrthoView {
    UnitVec east{};
    UnitVec north{};
    UnitVec up{};
    int cx = 0;
    int cy = 0;
    int radius_px = 0;
    // 1/radius_px, precomputed. The Xtensa FPU has no divide instruction --
    // a float division compiles to a call to __divsf3 -- and unprojectUnit()
    // runs once per pixel of the disc, so this must not be computed there.
    float inv_radius = 0.0f;
};

// Direction to a lat/lon. Altitude is ignored -- this is a bearing, not a
// position, and the globe draws everything onto the same sphere.
UnitVec toUnitVector(const GeoPoint& p);

// Build the per-frame rotation. Six trig calls; call it once, not per vertex.
OrthoView makeOrthoView(const GeoPoint& view_center, int cx, int cy, int radius_px);

// Project a point that is already on the unit sphere. Agrees with
// projectOrthographic() to well under a pixel; see test_geo.cpp.
ScreenPoint projectUnit(const OrthoView& view, const UnitVec& p);

// What the globe is showing at a given screen pixel. `on_globe` is false
// outside the disc, where the question has no answer -- that is the whole
// reason this returns a struct rather than a bare vector.
struct SpherePoint {
    UnitVec dir{};
    bool on_globe = false;
};

// The inverse of projectUnit: pixel -> direction. Used to FILL, where the
// question runs the other way round -- not "where does this coastline land on
// screen" but "what is underneath this pixel". An outline cannot answer that;
// a closed curve does not say which side of itself is inside.
SpherePoint unprojectUnit(const OrthoView& view, int x, int y);

// Compass point for a heading: "N", "NNE", "NE", ... 16-point rose.
// Returns a pointer to a static string literal -- no allocation, safe to
// hand straight to the display driver.
const char* compassPoint(double azimuth_deg);

// Unit conversions for the UNITS toggle.
inline constexpr double kmToMiles(double km)      { return km * 0.621371; }
inline constexpr double kmhToMph(double kmh)      { return kmh * 0.621371; }

}  // namespace orbit
