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
    
    const double d = 2.0 * kEarthRadiusKm * std::asin(std::sqrt(h));
   
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

const char* compassPoint(double azimuth_deg) {
    static constexpr const char* kPoints[16] = {
    "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW" };

    const double a = normalizeDeg(azimuth_deg);
    const int idx = int((a / 22.5) + 0.5) % 16;

    return kPoints[idx];
}

}  // namespace orbit
