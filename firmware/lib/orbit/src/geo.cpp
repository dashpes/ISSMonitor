#include "orbit/geo.h"

#include <cmath>

namespace orbit {


double normalizeDeg(double deg) {
    //first fmod makes it positive, the second accounts for negative rotation
    return std::fmod(std::fmod(deg, 360.0) + 360.0, 360.0);
}

double wrapLongitude(double lon_deg) {
    double x = std::fmod(lon_deg - 180.0, 360.0);   // shift the window
    if (x <= 0.0) x += 360.0;                       // fold (this +360 is a free lap)
    return x - 180.0;                               // shift BACK
}

double groundDistanceKm(const GeoPoint& a, const GeoPoint& b) {
    const double lat1 = deg2rad(a.lat_deg);
    const double lat2 = deg2rad(b.lat_deg);
    
    const double dlat = lat2 - lat1;
    const double dlon = deg2rad(b.lon_deg - a.lon_deg);
    
    const double sin_dlat = std::sin(dlat / 2.0);
    const double sin_dlon = std::sin(dlon / 2.0);

    const double h = (sin_dlat * sin_dlat) + std::cos(lat1) * std::cos(lat2) * (sin_dlon * sin_dlon);
    
    // h is a sum of squared sines and cannot exceed 1 -- but rounding does not
    // know that. For two points on opposite sides of the planet it lands on
    // exactly 1, and about one such pair in twenty-five overshoots to 1 + one
    // ulp. Measured, not assumed: 17,226 of 400,000 random antipodal pairs.
    //
    // As it happens that is survivable without this clamp, because sqrt of
    // 1 + one ulp rounds back to exactly 1.0 and asin(1.0) is pi/2. The clamp
    // is here so that the function does not depend on that coincidence -- two
    // ulps would be sqrt to just over 1, asin to NaN, and a NaN neither throws
    // nor warns, it just spreads silently into the telemetry readout. One
    // comparison in a function called once a second is a fair price for not
    // having to re-derive that argument on someone else's libm.
    const double d = 2.0 * kEarthRadiusKm * std::asin(std::sqrt(h > 1.0 ? 1.0 : h));
   
    return d;
}

// ---------------------------------------------------------------------------
// Cartesian helpers, file-local. The anonymous namespace means these are
// invisible outside this .cpp -- the C++ way of saying "private helper".
// ---------------------------------------------------------------------------
namespace {

struct Vec3 { double x, y, z; };

// Spherical-Earth ECEF: Earth-Centred, Earth-Fixed. Origin at the planet's
// centre, +z out the north pole, +x through (0 N, 0 E) off west Africa.
// Real geodesy uses the WGS84 ellipsoid; a sphere is plenty for pointing eyes.
Vec3 toEcef(const GeoPoint& p) {
    const double r   = kEarthRadiusKm + p.alt_km;
    const double lat = deg2rad(p.lat_deg);
    const double lon = deg2rad(p.lon_deg);
    return {r * std::cos(lat) * std::cos(lon),
            r * std::cos(lat) * std::sin(lon),
            r * std::sin(lat)};
}

// asin/acos blow up (NaN) if rounding nudges the argument past +-1, which
// happens for real at exactly-overhead. Clamp first, always.
double clampUnit(double v) {
    if (v >  1.0) return  1.0;
    if (v < -1.0) return -1.0;
    return v;
}

}  // namespace

LookAngle lookAngle(const GeoPoint& observer, const GeoPoint& satellite) {
    const Vec3 o = toEcef(observer);
    const Vec3 s = toEcef(satellite);

    // The vector from you to the satellite, still in Earth-centred axes.
    const double dx = s.x - o.x;
    const double dy = s.y - o.y;
    const double dz = s.z - o.z;

    const double range = std::sqrt(dx * dx + dy * dy + dz * dz);

    // Rotate into the observer's LOCAL frame: East / North / Up. This is the
    // heart of the function. Earth-centred axes are useless to someone
    // standing in a driveway; what they need is "left a bit, and up a bit",
    // which is exactly what ENU gives.
    const double lat = deg2rad(observer.lat_deg);
    const double lon = deg2rad(observer.lon_deg);
    const double sin_lat = std::sin(lat), cos_lat = std::cos(lat);
    const double sin_lon = std::sin(lon), cos_lon = std::cos(lon);

    const double east  = -sin_lon * dx + cos_lon * dy;
    const double north = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
    const double up    =  cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz;

    LookAngle out;
    // atan2 returns -180..+180; normalizeDeg folds it to a compass 0..360.
    out.azimuth_deg   = normalizeDeg(rad2deg(std::atan2(east, north)));
    out.elevation_deg = rad2deg(std::asin(clampUnit(up / range)));
    out.range_km      = range;
    return out;
}

ScreenPoint projectOrthographic(const GeoPoint& p,
                                const GeoPoint& view_center,
                                int cx, int cy, int radius_px) {
    const double lat  = deg2rad(p.lat_deg);
    const double lat0 = deg2rad(view_center.lat_deg);
    // The difference is wrapped, not the endpoints -- crossing the date line
    // must be a small step, not a 358-degree lurch.
    const double dlon = deg2rad(wrapLongitude(p.lon_deg - view_center.lon_deg));

    const double sin_lat  = std::sin(lat),  cos_lat  = std::cos(lat);
    const double sin_lat0 = std::sin(lat0), cos_lat0 = std::cos(lat0);
    const double cos_dlon = std::cos(dlon);

    // cos of the angular distance from the centre of the visible disc.
    // Positive -> near side, negative -> around the back of the globe.
    const double cos_c = sin_lat0 * sin_lat + cos_lat0 * cos_lat * cos_dlon;

    ScreenPoint out;
    out.visible = (cos_c >= 0.0);

    const double x = radius_px * cos_lat * std::sin(dlon);
    const double y = radius_px * (cos_lat0 * sin_lat - sin_lat0 * cos_lat * cos_dlon);

    // Screen y grows DOWNWARD, so north (+y in map space) is -y on screen.
    out.x = static_cast<int>(std::lround(cx + x));
    out.y = static_cast<int>(std::lround(cy - y));
    return out;
}

UnitVec toUnitVector(const GeoPoint& p) {
    const double lat = deg2rad(p.lat_deg);
    const double lon = deg2rad(p.lon_deg);
    const double cos_lat = std::cos(lat);
    return {static_cast<float>(cos_lat * std::cos(lon)),
            static_cast<float>(cos_lat * std::sin(lon)),
            static_cast<float>(std::sin(lat))};
}

OrthoView makeOrthoView(const GeoPoint& view_center, int cx, int cy, int radius_px) {
    const double lat0 = deg2rad(view_center.lat_deg);
    const double lon0 = deg2rad(view_center.lon_deg);
    const double sin_lat0 = std::sin(lat0), cos_lat0 = std::cos(lat0);
    const double sin_lon0 = std::sin(lon0), cos_lon0 = std::cos(lon0);

    // This is the same East/North/Up rotation lookAngle() builds by hand.
    // There it aims a pair of eyes; here it aims a camera at the planet. Worth
    // noticing that they are the same three rows -- the second time you meet a
    // change of basis it stops looking like a trick.
    OrthoView v;
    v.east  = {static_cast<float>(-sin_lon0),
               static_cast<float>( cos_lon0),
               0.0f};
    v.north = {static_cast<float>(-sin_lat0 * cos_lon0),
               static_cast<float>(-sin_lat0 * sin_lon0),
               static_cast<float>( cos_lat0)};
    v.up    = {static_cast<float>( cos_lat0 * cos_lon0),
               static_cast<float>( cos_lat0 * sin_lon0),
               static_cast<float>( sin_lat0)};
    v.cx = cx;
    v.cy = cy;
    v.radius_px = radius_px;
    v.inv_radius = radius_px ? 1.0f / static_cast<float>(radius_px) : 0.0f;
    return v;
}

ScreenPoint projectUnit(const OrthoView& view, const UnitVec& p) {
    // Nine multiplies and six adds. No trig, no doubles, no division.
    const float e = view.east.x  * p.x + view.east.y  * p.y + view.east.z  * p.z;
    const float n = view.north.x * p.x + view.north.y * p.y + view.north.z * p.z;
    const float u = view.up.x    * p.x + view.up.y    * p.y + view.up.z    * p.z;

    ScreenPoint out;
    // `up` is the cosine of the angular distance from the centre of the disc.
    // Positive is the near side; negative is around the back and must not be
    // drawn, or the coastline folds through the planet.
    out.visible = (u >= 0.0f);

    const float r = static_cast<float>(view.radius_px);
    out.x = static_cast<int>(std::lround(view.cx + r * e));
    out.y = static_cast<int>(std::lround(view.cy - r * n));  // screen y grows DOWN
    return out;
}

SpherePoint unprojectUnit(const OrthoView& view, int x, int y) {
    // Where the pixel sits on the visible disc, in units of one radius.
    const float e = (x - view.cx) * view.inv_radius;
    const float n = (view.cy - y) * view.inv_radius;   // screen y grows DOWN
    const float d2 = e * e + n * n;

    SpherePoint out;
    if (d2 > 1.0f) return out;    // off the disc; there is no sphere here
    out.on_globe = true;

    // An orthographic view is a bundle of parallel rays, so the ray through
    // this pixel meets the unit sphere where e^2 + n^2 + u^2 == 1. Two roots;
    // the positive one is the hemisphere facing you. Taking the negative root
    // would draw the far side of the planet, mirrored.
    const float u = std::sqrt(1.0f - d2);

    // Same three rows as projectUnit, applied the other way: there they
    // read a direction off the ECEF axes, here they build one from the
    // observer's. A rotation matrix is its own inverse when transposed, which
    // is why no matrix arithmetic is needed to turn one into the other.
    out.dir = {e * view.east.x + n * view.north.x + u * view.up.x,
               e * view.east.y + n * view.north.y + u * view.up.y,
               e * view.east.z + n * view.north.z + u * view.up.z};
    return out;
}

const char* compassPoint(double azimuth_deg) {
    static constexpr const char* kPoints[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };

    const double a = normalizeDeg(azimuth_deg);
    const int idx = int((a / 22.5) + 0.5) % 16;

    return kPoints[idx];
}

}  // namespace orbit
