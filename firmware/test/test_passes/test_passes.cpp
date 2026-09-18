//
// test_passes.cpp -- runs on your Mac: `./scripts/run_tests.sh passes`
//
// These are the acceptance criteria for lib/orbit/src/passes.cpp.
//
// The trick that makes any of this testable is SubpointFn. The real satellite
// comes from SGP4, which needs a TLE, which needs the network, and whose
// answers nobody can check by hand. Here it is replaced by a satellite on a
// circle: constant altitude, constant speed, straight along the equator. A
// circle has closed-form look angles, so every expected value below is worked
// out from geometry rather than copied out of the thing being tested.
//
// The oracle is `expectedElevation()`: put the observer, the centre of the
// Earth and the satellite in a plane and read the angle off. That shares no
// code with lookAngle(), which does it in ECEF and rotates into ENU -- so
// when the two agree to a thousandth of a degree, it is because they are both
// right and not because they are both the same mistake.
//
#include <unity.h>

#include <cmath>

#include "orbit/geo.h"
#include "orbit/passes.h"

using namespace orbit;

static void assertClose(double expected, double actual, double tol) {
    TEST_ASSERT_TRUE_MESSAGE(std::fabs(expected - actual) <= tol,
                             "value outside tolerance");
}

// --------------------------------------------------------------------------
// The fake satellite
// --------------------------------------------------------------------------

struct FakeOrbit {
    int64_t epoch;       // when it is at lon0
    double  lon0_deg;
    double  deg_per_s;   // 0 parks it in place -- see the "never sets" test
    double  lat_deg;
    double  alt_km;
};

static GeoPoint fakeSubpoint(int64_t t, void* ctx) {
    const FakeOrbit& o = *static_cast<const FakeOrbit*>(ctx);
    GeoPoint p;
    p.lat_deg = o.lat_deg;
    p.lon_deg = wrapLongitude(o.lon0_deg +
                              o.deg_per_s * static_cast<double>(t - o.epoch));
    p.alt_km  = o.alt_km;
    return p;
}

// A 90-minute circular orbit. Not a real one -- the Earth does not rotate
// underneath it -- which is the point: passes land exactly one period apart
// and the arithmetic below stays checkable on paper.
static constexpr int64_t kEpoch      = 1789603200;   // 2026-09-17T00:00:00Z
static constexpr double  kPeriodS    = 5400.0;
static constexpr double  kDegPerS    = 360.0 / kPeriodS;
static constexpr double  kAltKm      = 400.0;
static constexpr double  kObsLatDeg  = 5.0;          // just north of the track
static constexpr double  kMinElDeg   = 10.0;

static const GeoPoint kObserver{kObsLatDeg, 0.0, 0.0};

static FakeOrbit makeOrbit() {
    // At kEpoch it is at longitude 0, so it culminates exactly then.
    return FakeOrbit{kEpoch, 0.0, kDegPerS, 0.0, kAltKm};
}

// --------------------------------------------------------------------------
// The oracle: plane geometry, no ECEF, no ENU.
// --------------------------------------------------------------------------

// Central angle from the observer to a satellite sitting on the equator
// `dlon` away. cos(theta) = sin(a)sin(b) + cos(a)cos(b)cos(dlon), with the
// satellite's latitude zero.
static double centralAngleDeg(double dlon_deg) {
    const double c = std::cos(deg2rad(kObsLatDeg)) * std::cos(deg2rad(dlon_deg));
    return rad2deg(std::acos(c));
}

// Observer at radius R, satellite at radius R+h, theta between them at the
// centre of the Earth. Drop it all into one plane: the observer is at (0, R),
// the satellite at (r sin t, r cos t), and elevation is the angle the line
// between them makes with the observer's horizontal.
static double expectedElevation(double theta_deg, double alt_km) {
    const double th = deg2rad(theta_deg);
    const double r  = kEarthRadiusKm + alt_km;
    return rad2deg(std::atan2(std::cos(th) - kEarthRadiusKm / r, std::sin(th)));
}

// Initial great-circle bearing from the observer to a satellite `dlon` away
// on the equator. The usual formula, with sin(lat2) = 0 and cos(lat2) = 1
// folded in. Independent of lookAngle's ECEF-to-ENU rotation.
static double expectedBearingDeg(double dlon_deg) {
    const double dl = deg2rad(dlon_deg);
    const double y  =  std::sin(dl);
    const double x  = -std::sin(deg2rad(kObsLatDeg)) * std::cos(dl);
    return normalizeDeg(rad2deg(std::atan2(y, x)));
}

// The half-width of a pass, in degrees of longitude: how far off to the side
// the satellite can be and still be kMinElDeg above the horizon. Bisected
// here, in the test, from the oracle only.
static double passHalfWidthDeg() {
    double lo = 0.0, hi = 90.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (expectedElevation(centralAngleDeg(mid), kAltKm) >= kMinElDeg) lo = mid;
        else                                                              hi = mid;
    }
    return lo;
}

// --------------------------------------------------------------------------
// The oracle agrees with the code it is about to judge. If this one fails,
// nothing below it means anything.
// --------------------------------------------------------------------------
void test_oracle_agrees_with_look_angle(void) {
    FakeOrbit o = makeOrbit();
    // Walk whole seconds and derive the longitude from the time, rather than
    // picking longitudes and rounding to a time. The fake only moves in
    // one-second steps, and half a second is 0.03 degrees of longitude, which
    // is 0.05 degrees of elevation near the horizon -- bigger than the
    // agreement being tested for.
    for (int64_t t = kEpoch - 450; t <= kEpoch + 450; t += 15) {
        const double dlon = kDegPerS * static_cast<double>(t - kEpoch);
        const double mine   = expectedElevation(centralAngleDeg(dlon), kAltKm);
        const double theirs = lookAngle(kObserver, fakeSubpoint(t, &o)).elevation_deg;
        assertClose(mine, theirs, 1e-6);
    }
}

// Hard numbers, worked out separately, so a quietly broken oracle cannot
// agree with quietly broken code.
void test_pass_geometry_matches_hand_calculation(void) {
    assertClose(32.381, expectedElevation(centralAngleDeg(0.0), kAltKm), 0.01);
    assertClose(11.0157, passHalfWidthDeg(), 0.001);
    assertClose(330.47, 2.0 * passHalfWidthDeg() / kDegPerS, 0.05);
}

// --------------------------------------------------------------------------
// findPasses -- the ordinary case
// --------------------------------------------------------------------------
void test_finds_the_obvious_pass(void) {
    FakeOrbit o = makeOrbit();
    Pass p[4]{};

    // A window centred on the culmination at kEpoch.
    const int n = findPasses(fakeSubpoint, &o, kObserver,
                             kEpoch - 2700, 5400, kMinElDeg, 60, p, 4);
    TEST_ASSERT_EQUAL_INT(1, n);

    const double half_s = passHalfWidthDeg() / kDegPerS;
    // Two seconds: one for the bisection landing on either side of the
    // crossing, one for the oracle's own rounding.
    assertClose(static_cast<double>(kEpoch) - half_s,
                static_cast<double>(p[0].aos_unix), 2.0);
    assertClose(static_cast<double>(kEpoch),
                static_cast<double>(p[0].max_unix), 2.0);
    assertClose(static_cast<double>(kEpoch) + half_s,
                static_cast<double>(p[0].los_unix), 2.0);
    assertClose(32.381, p[0].max_elevation_deg, 0.05);
}

// Rising in the west, setting in the east, highest due south -- because the
// observer is north of a ground track that runs west to east. Get a sign
// wrong anywhere in the chain and this is the test that says so.
//
// Note what the oracle says the answer is: 246 and 114, not 270 and 90. At
// AOS the satellite is both west of the observer AND 5 degrees south of it,
// and 11 degrees of longitude out on the equator is a long way round the
// compass from due west. Guessing "it rises due west" would have been wrong
// by 24 degrees, which is the entire reason for computing it.
void test_pass_azimuths(void) {
    FakeOrbit o = makeOrbit();
    Pass p[2]{};
    TEST_ASSERT_EQUAL_INT(1, findPasses(fakeSubpoint, &o, kObserver,
                                        kEpoch - 2700, 5400, kMinElDeg, 60, p, 2));
    // Ask the oracle about the second the search actually reported, not the
    // second the pass ideally starts. AOS lands on a whole second and the
    // true crossing does not, and near the horizon that last fraction of a
    // second is a tenth of a degree of azimuth -- which would otherwise show
    // up here as a fault in code that has none.
    const double aos_dlon = kDegPerS * static_cast<double>(p[0].aos_unix - kEpoch);
    const double los_dlon = kDegPerS * static_cast<double>(p[0].los_unix - kEpoch);
    assertClose(expectedBearingDeg(aos_dlon), p[0].aos_azimuth_deg, 0.01);
    assertClose(180.0,                        p[0].max_azimuth_deg, 0.01);
    assertClose(expectedBearingDeg(los_dlon), p[0].los_azimuth_deg, 0.01);

    // And the claim in plain language: it comes up in the western half of the
    // compass and goes down in the eastern half.
    TEST_ASSERT_TRUE(p[0].aos_azimuth_deg > 180.0 && p[0].aos_azimuth_deg < 360.0);
    TEST_ASSERT_TRUE(p[0].los_azimuth_deg >   0.0 && p[0].los_azimuth_deg < 180.0);
    // Symmetric about due south to within the second of quantisation above.
    assertClose(360.0, p[0].aos_azimuth_deg + p[0].los_azimuth_deg, 0.3);
}

void test_finds_consecutive_passes(void) {
    FakeOrbit o = makeOrbit();
    Pass p[4]{};
    const int n = findPasses(fakeSubpoint, &o, kObserver,
                             kEpoch - 2700, 4 * 5400, kMinElDeg, 60, p, 4);
    TEST_ASSERT_EQUAL_INT(4, n);

    for (int i = 0; i < 4; ++i) {
        assertClose(static_cast<double>(kEpoch) + i * kPeriodS,
                    static_cast<double>(p[i].max_unix), 2.0);
        assertClose(32.381, p[i].max_elevation_deg, 0.05);
        // Strictly ordered and non-overlapping: aos < max < los < next aos.
        TEST_ASSERT_TRUE(p[i].aos_unix < p[i].max_unix);
        TEST_ASSERT_TRUE(p[i].max_unix < p[i].los_unix);
        if (i > 0) TEST_ASSERT_TRUE(p[i - 1].los_unix < p[i].aos_unix);
    }
}

// --------------------------------------------------------------------------
// The bounds. Every one of these is a way the search could run off the end of
// the array or off the end of time.
// --------------------------------------------------------------------------
void test_respects_max_passes(void) {
    FakeOrbit o = makeOrbit();
    Pass p[4]{};
    p[2].max_elevation_deg = -999.0;      // canary: must not be written

    const int n = findPasses(fakeSubpoint, &o, kObserver,
                             kEpoch - 2700, 10 * 5400, kMinElDeg, 60, p, 2);
    TEST_ASSERT_EQUAL_INT(2, n);
    assertClose(-999.0, p[2].max_elevation_deg, 1e-9);
}

void test_respects_horizon(void) {
    FakeOrbit o = makeOrbit();
    Pass p[8]{};

    // Two periods of horizon, room for eight passes: the horizon is what has
    // to stop it, not the array.
    const int64_t from = kEpoch - 2700;
    const int32_t horizon = 2 * 5400;
    const int n = findPasses(fakeSubpoint, &o, kObserver,
                             from, horizon, kMinElDeg, 60, p, 8);
    TEST_ASSERT_EQUAL_INT(2, n);
    for (int i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE(p[i].aos_unix >= from);
        TEST_ASSERT_TRUE(p[i].aos_unix < from + horizon);
    }
}

void test_never_visible_returns_zero(void) {
    FakeOrbit o = makeOrbit();
    Pass p[4]{};
    // From 89 N an equatorial satellite sits 43 degrees below the horizon and
    // stays there. Three full orbits, nothing found, and no hanging.
    const GeoPoint pole{89.0, 0.0, 0.0};
    TEST_ASSERT_EQUAL_INT(0, findPasses(fakeSubpoint, &o, pole,
                                        kEpoch, 3 * 5400, kMinElDeg, 60, p, 4));
}

void test_rejects_nonsense_arguments(void) {
    FakeOrbit o = makeOrbit();
    Pass p[4]{};
    TEST_ASSERT_EQUAL_INT(0, findPasses(nullptr, &o, kObserver, kEpoch, 5400,
                                        kMinElDeg, 60, p, 4));
    TEST_ASSERT_EQUAL_INT(0, findPasses(fakeSubpoint, &o, kObserver, kEpoch, 5400,
                                        kMinElDeg, 60, nullptr, 4));
    TEST_ASSERT_EQUAL_INT(0, findPasses(fakeSubpoint, &o, kObserver, kEpoch, 5400,
                                        kMinElDeg, 60, p, 0));
    TEST_ASSERT_EQUAL_INT(0, findPasses(fakeSubpoint, &o, kObserver, kEpoch, 0,
                                        kMinElDeg, 60, p, 4));
}

// A step of zero would leave `t` where it is forever. It must be replaced,
// not obeyed -- this test only terminates if that happens.
void test_zero_step_does_not_hang(void) {
    FakeOrbit o = makeOrbit();
    Pass p[2]{};
    TEST_ASSERT_EQUAL_INT(1, findPasses(fakeSubpoint, &o, kObserver,
                                        kEpoch - 2700, 5400, kMinElDeg, 0, p, 2));
    assertClose(static_cast<double>(kEpoch),
                static_cast<double>(p[0].max_unix), 2.0);
}

// A satellite that never sets -- parked overhead, deg_per_s = 0 -- is the
// case that turns "walk forward until it sets" into an infinite loop. The
// pass has to be capped and handed back, not chased.
void test_satellite_that_never_sets_is_capped(void) {
    FakeOrbit parked{kEpoch, 0.0, 0.0, 0.0, kAltKm};
    Pass p[2]{};
    const int n = findPasses(fakeSubpoint, &parked, kObserver,
                             kEpoch, 10 * 86400, kMinElDeg, 60, p, 2);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT64(kEpoch, p[0].aos_unix);
    TEST_ASSERT_TRUE(p[0].los_unix - p[0].aos_unix <= 3600);
}

// Started in the middle of a pass. The AOS is in the past and this only
// searches forward, so it reports "now" -- but it must still report it.
void test_pass_already_in_progress(void) {
    FakeOrbit o = makeOrbit();
    Pass p[2]{};
    // Starting exactly at culmination. One full period of horizon reaches
    // the NEXT pass as well, which is the point: finding the one in progress
    // must not cost you the one after it.
    const int n = findPasses(fakeSubpoint, &o, kObserver,
                             kEpoch, 5400, kMinElDeg, 60, p, 2);
    TEST_ASSERT_EQUAL_INT(2, n);

    const double half_s = passHalfWidthDeg() / kDegPerS;
    TEST_ASSERT_EQUAL_INT64(kEpoch, p[0].aos_unix);      // clamped to "now"
    assertClose(static_cast<double>(kEpoch) + half_s,
                static_cast<double>(p[0].los_unix), 2.0);
    // The peak is behind us, so it lands on the first second searched.
    TEST_ASSERT_EQUAL_INT64(kEpoch, p[0].max_unix);

    assertClose(static_cast<double>(kEpoch) + kPeriodS,
                static_cast<double>(p[1].max_unix), 2.0);
}

// --------------------------------------------------------------------------
// The coarse step. passes.h claims 60 s is safe for the ISS and 300 s is not;
// this is the test that earns that sentence instead of asserting it.
//
// A pass 330 s long is either caught or stepped clean over depending on where
// the grid happens to fall, so sweep every phase of the grid rather than
// trusting one lucky alignment.
// --------------------------------------------------------------------------
void test_sixty_second_step_never_misses_a_pass(void) {
    FakeOrbit o = makeOrbit();
    Pass p[2]{};
    for (int64_t phase = 0; phase < 60; ++phase) {
        const int n = findPasses(fakeSubpoint, &o, kObserver,
                                 kEpoch - 2700 + phase, 5400, kMinElDeg, 60, p, 2);
        TEST_ASSERT_EQUAL_INT(1, n);
        assertClose(static_cast<double>(kEpoch),
                    static_cast<double>(p[0].max_unix), 2.0);
    }
}

void test_a_step_longer_than_the_pass_misses_passes(void) {
    FakeOrbit o = makeOrbit();
    Pass p[2]{};
    int missed = 0;
    for (int64_t phase = 0; phase < 60; ++phase) {
        if (findPasses(fakeSubpoint, &o, kObserver,
                       kEpoch - 2700 + phase, 5400, kMinElDeg, 600, p, 2) == 0) {
            ++missed;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(missed > 0,
                             "a 600 s step should step straight over some passes");
}

// --------------------------------------------------------------------------
// The sun. Checked against facts about the solar system rather than against
// another copy of the same formula -- the declination at the solstices, the
// rate the Earth turns, and the size of the equation of time.
// --------------------------------------------------------------------------
void test_subsolar_declination_at_the_solstices(void) {
    // 2026-06-21 09:00Z and 2026-12-21 21:00Z, either side of the solstices.
    assertClose( 23.44, subsolarPoint(1782032400).lat_deg, 0.15);
    assertClose(-23.44, subsolarPoint(1797886800).lat_deg, 0.15);
}

void test_subsolar_declination_at_the_equinox(void) {
    // 2026-03-20 12:00Z, a couple of hours before the March equinox.
    TEST_ASSERT_TRUE(std::fabs(subsolarPoint(1774008000).lat_deg) < 0.5);
}

void test_subsolar_latitude_never_leaves_the_tropics(void) {
    // Two years, one day at a time. Nothing may put the sun over Reykjavik.
    for (int64_t d = 0; d < 730; ++d) {
        const double lat = subsolarPoint(kEpoch + d * 86400).lat_deg;
        TEST_ASSERT_TRUE(std::fabs(lat) <= 23.5);
    }
}

void test_subsolar_longitude_marches_west_at_fifteen_degrees_an_hour(void) {
    for (int64_t h = 0; h < 24; ++h) {
        const int64_t t = kEpoch + h * 3600;
        const double a = subsolarPoint(t).lon_deg;
        const double b = subsolarPoint(t + 3600).lon_deg;
        assertClose(-15.0, wrapLongitude(b - a), 0.05);
    }
}

void test_subsolar_longitude_is_near_greenwich_at_noon_utc(void) {
    // The equation of time -- the sun running up to 16 minutes ahead of or
    // behind the clock -- is bounded at four degrees. Anything further out
    // means the sidereal rate or the epoch is wrong.
    for (int64_t d = 0; d < 365; ++d) {
        const int64_t noon = kEpoch + d * 86400 + 12 * 3600;
        TEST_ASSERT_TRUE(std::fabs(subsolarPoint(noon).lon_deg) < 4.5);
    }
}

void test_sun_is_overhead_at_the_subsolar_point(void) {
    for (int64_t h = 0; h < 48; h += 3) {
        const int64_t t = kEpoch + h * 3600;
        assertClose(90.0, sunElevationDeg(subsolarPoint(t), t), 1e-6);
        // ...and straight down at the antipode.
        const GeoPoint s = subsolarPoint(t);
        // Looser at the antipode on purpose: the haversine's asin(sqrt(h))
        // goes flat as h approaches 1, so half the mantissa is gone by the
        // time two points are on opposite sides of the planet. A hundredth
        // of an arcsecond is not a problem, but pretending it is not there
        // would be.
        const GeoPoint anti{-s.lat_deg, wrapLongitude(s.lon_deg + 180.0), 0.0};
        assertClose(-90.0, sunElevationDeg(anti, t), 1e-4);
    }
}

// --------------------------------------------------------------------------
// isVisiblePass -- the two halves, tested one at a time.
// --------------------------------------------------------------------------

// A point on the equator exactly `theta` degrees from the subsolar point, so
// the sun there is at elevation 90 - theta. cos(theta) = cos(dec) cos(dlon)
// when the point is on the equator, which solves for dlon directly.
static GeoPoint equatorPointAtSunAngle(int64_t t, double theta_deg) {
    const GeoPoint sun = subsolarPoint(t);
    const double cos_dlon = std::cos(deg2rad(theta_deg)) / std::cos(deg2rad(sun.lat_deg));
    const double dlon = rad2deg(std::acos(cos_dlon));
    return GeoPoint{0.0, wrapLongitude(sun.lon_deg + dlon), 0.0};
}

void test_daylight_is_never_visible(void) {
    const int64_t t = kEpoch + 7 * 3600;
    const GeoPoint sun = subsolarPoint(t);
    const GeoPoint noon{sun.lat_deg, sun.lon_deg, 0.0};     // sun straight up
    const GeoPoint overhead{sun.lat_deg, sun.lon_deg, kAltKm};
    TEST_ASSERT_FALSE(isVisiblePass(overhead, noon, t));
}

void test_local_midnight_satellite_is_in_the_earths_shadow(void) {
    const int64_t t = kEpoch + 7 * 3600;
    const GeoPoint sun = subsolarPoint(t);
    const GeoPoint midnight{-sun.lat_deg, wrapLongitude(sun.lon_deg + 180.0), 0.0};
    const GeoPoint overhead{midnight.lat_deg, midnight.lon_deg, kAltKm};
    // Dark enough, certainly -- the sun is 90 degrees down. But the ISS is
    // in the shadow too, so there is nothing to see.
    assertClose(-90.0, sunElevationDeg(midnight, t), 1e-6);
    TEST_ASSERT_FALSE(isVisiblePass(overhead, midnight, t));
}

void test_twilight_overhead_is_visible(void) {
    const int64_t t = kEpoch + 7 * 3600;
    const GeoPoint obs = equatorPointAtSunAngle(t, 100.0);  // sun 10 deg down
    assertClose(-10.0, sunElevationDeg(obs, t), 0.01);

    const GeoPoint overhead{obs.lat_deg, obs.lon_deg, kAltKm};
    TEST_ASSERT_TRUE(isVisiblePass(overhead, obs, t));
}

// Same sky, same observer, same second -- only the altitude changes. At
// 400 km it clears the Earth's shadow; at 50 km it does not. If this one
// passes for the wrong reason the one above passes too, which is why they
// come in a pair.
void test_shadow_depends_on_altitude(void) {
    const int64_t t = kEpoch + 7 * 3600;
    const GeoPoint obs = equatorPointAtSunAngle(t, 100.0);
    TEST_ASSERT_TRUE (isVisiblePass(GeoPoint{obs.lat_deg, obs.lon_deg, 400.0}, obs, t));
    TEST_ASSERT_FALSE(isVisiblePass(GeoPoint{obs.lat_deg, obs.lon_deg,  50.0}, obs, t));
}

// Civil twilight is the line. Just inside it the sky is still too bright.
void test_civil_twilight_is_the_threshold(void) {
    const int64_t t = kEpoch + 7 * 3600;
    const GeoPoint too_bright = equatorPointAtSunAngle(t, 93.0);   // sun -3
    const GeoPoint dark_enough = equatorPointAtSunAngle(t, 97.0);  // sun -7
    TEST_ASSERT_FALSE(isVisiblePass(
        GeoPoint{too_bright.lat_deg, too_bright.lon_deg, kAltKm}, too_bright, t));
    TEST_ASSERT_TRUE(isVisiblePass(
        GeoPoint{dark_enough.lat_deg, dark_enough.lon_deg, kAltKm}, dark_enough, t));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_oracle_agrees_with_look_angle);
    RUN_TEST(test_pass_geometry_matches_hand_calculation);
    RUN_TEST(test_finds_the_obvious_pass);
    RUN_TEST(test_pass_azimuths);
    RUN_TEST(test_finds_consecutive_passes);
    RUN_TEST(test_respects_max_passes);
    RUN_TEST(test_respects_horizon);
    RUN_TEST(test_never_visible_returns_zero);
    RUN_TEST(test_rejects_nonsense_arguments);
    RUN_TEST(test_zero_step_does_not_hang);
    RUN_TEST(test_satellite_that_never_sets_is_capped);
    RUN_TEST(test_pass_already_in_progress);
    RUN_TEST(test_sixty_second_step_never_misses_a_pass);
    RUN_TEST(test_a_step_longer_than_the_pass_misses_passes);
    RUN_TEST(test_subsolar_declination_at_the_solstices);
    RUN_TEST(test_subsolar_declination_at_the_equinox);
    RUN_TEST(test_subsolar_latitude_never_leaves_the_tropics);
    RUN_TEST(test_subsolar_longitude_marches_west_at_fifteen_degrees_an_hour);
    RUN_TEST(test_subsolar_longitude_is_near_greenwich_at_noon_utc);
    RUN_TEST(test_sun_is_overhead_at_the_subsolar_point);
    RUN_TEST(test_daylight_is_never_visible);
    RUN_TEST(test_local_midnight_satellite_is_in_the_earths_shadow);
    RUN_TEST(test_twilight_overhead_is_visible);
    RUN_TEST(test_shadow_depends_on_altitude);
    RUN_TEST(test_civil_twilight_is_the_threshold);
    return UNITY_END();
}
