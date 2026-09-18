//
// test_geo.cpp -- runs on your Mac: `pio test -e native`
//
// These are the acceptance criteria for lib/orbit/src/geo.cpp. Expected
// values were generated from an independent Python implementation, so if
// your C++ agrees with these you can trust it.
//
// Every one of these fails right now. That is correct. Make them pass one
// at a time, and do not touch the ESP32 until they are all green.
//
#include <unity.h>

#include <cmath>

#include "orbit/geo.h"

using namespace orbit;

static void assertClose(double expected, double actual, double tol) {
    TEST_ASSERT_TRUE_MESSAGE(std::fabs(expected - actual) <= tol,
                             "value outside tolerance");
}

// --------------------------------------------------------------------------
// normalizeDeg / wrapLongitude
// --------------------------------------------------------------------------
void test_normalize_basic(void) {
    assertClose(10.0,  normalizeDeg(10.0),   1e-9);
    assertClose(350.0, normalizeDeg(-10.0),  1e-9);
    assertClose(0.0,   normalizeDeg(360.0),  1e-9);
    assertClose(0.0,   normalizeDeg(720.0),  1e-9);
    assertClose(359.5, normalizeDeg(-720.5), 1e-9);
    assertClose(0.5,   normalizeDeg(1080.5), 1e-9);
}

void test_wrap_longitude(void) {
    assertClose(0.0,    wrapLongitude(0.0),    1e-9);
    assertClose(180.0,  wrapLongitude(180.0),  1e-9);
    assertClose(-179.0, wrapLongitude(181.0),  1e-9);
    assertClose(179.0,  wrapLongitude(-181.0), 1e-9);
    assertClose(-117.826, wrapLongitude(242.174), 1e-6);
}

// --------------------------------------------------------------------------
// groundDistanceKm  (reference: haversine, R = 6371.0088 km)
// --------------------------------------------------------------------------
void test_distance_same_point(void) {
    GeoPoint t{33.746, -117.826, 0.0};
    assertClose(0.0, groundDistanceKm(t, t), 1e-9);
}

void test_distance_known_pairs(void) {
    GeoPoint tustin{33.746, -117.826, 0.0};
    GeoPoint la{34.0522, -118.2437, 0.0};
    GeoPoint nyc{40.7128, -74.0060, 0.0};

    assertClose(51.434,    groundDistanceKm(tustin, la),  0.01);
    assertClose(3914.596,  groundDistanceKm(tustin, nyc), 0.10);
}

void test_distance_degenerate(void) {
    // One degree of longitude at the equator.
    assertClose(111.195, groundDistanceKm({0, 0, 0}, {0, 1, 0}), 0.01);
    // Antipodal: half the circumference.
    assertClose(20015.114, groundDistanceKm({0, 0, 0}, {0, 180, 0}), 0.10);
}

void test_distance_is_symmetric(void) {
    GeoPoint a{33.746, -117.826, 0.0};
    GeoPoint b{40.7128, -74.0060, 0.0};
    const double ab = groundDistanceKm(a, b);
    TEST_ASSERT_TRUE_MESSAGE(ab > 1000.0, "guard: must not trivially pass on a stub");
    assertClose(ab, groundDistanceKm(b, a), 1e-9);
}

// --------------------------------------------------------------------------
// lookAngle
// --------------------------------------------------------------------------
void test_look_directly_overhead(void) {
    GeoPoint obs{33.746, -117.826, 0.035};
    GeoPoint sat{33.746, -117.826, 420.0};
    LookAngle l = lookAngle(obs, sat);

    assertClose(90.0,    l.elevation_deg, 1e-6);
    assertClose(419.965, l.range_km,      1e-3);
    // Azimuth is undefined straight overhead -- deliberately not asserted.
}

void test_look_due_north(void) {
    GeoPoint obs{33.746, -117.826, 0.035};
    GeoPoint sat{38.746, -117.826, 420.0};   // 5 degrees north, same meridian
    LookAngle l = lookAngle(obs, sat);

    assertClose(0.0,     normalizeDeg(l.azimuth_deg + 180.0) - 180.0, 1e-6);
    assertClose(33.659,  l.elevation_deg, 1e-3);
    assertClose(711.090, l.range_km,      1e-2);
}

void test_look_due_east(void) {
    GeoPoint obs{0.0, 0.0, 0.0};
    GeoPoint sat{0.0, 10.0, 420.0};
    LookAngle l = lookAngle(obs, sat);

    assertClose(90.0,     l.azimuth_deg,   1e-6);
    assertClose(15.039,   l.elevation_deg, 1e-3);
    assertClose(1221.066, l.range_km,      1e-2);
}

void test_look_below_horizon(void) {
    // Other side of the planet: elevation must come back negative, not NaN
    // and not clamped to zero. The pass finder depends on the sign.
    GeoPoint obs{33.746, -117.826, 0.035};
    GeoPoint sat{-33.746, 62.174, 420.0};
    TEST_ASSERT_TRUE(lookAngle(obs, sat).elevation_deg < 0.0);
}

// --------------------------------------------------------------------------
// projectOrthographic
// --------------------------------------------------------------------------
void test_projection_center(void) {
    GeoPoint c{33.746, -117.826, 0.0};
    ScreenPoint p = projectOrthographic(c, c, 120, 120, 110);
    TEST_ASSERT_TRUE(p.visible);
    TEST_ASSERT_INT_WITHIN(1, 120, p.x);
    TEST_ASSERT_INT_WITHIN(1, 120, p.y);
}

void test_projection_north_pole(void) {
    // Viewed from (0,0) the north pole sits at the top of the disc.
    ScreenPoint p = projectOrthographic({90, 0, 0}, {0, 0, 0}, 120, 120, 110);
    TEST_ASSERT_TRUE(p.visible);
    TEST_ASSERT_INT_WITHIN(1, 120, p.x);
    TEST_ASSERT_INT_WITHIN(1, 10, p.y);   // cy - radius
}

void test_projection_far_side_hidden(void) {
    // Guard against a stub passing this by returning visible=false always:
    // the near side must be visible in the same breath.
    TEST_ASSERT_TRUE(projectOrthographic({0, 0, 0}, {0, 0, 0}, 120, 120, 110).visible);
    ScreenPoint p = projectOrthographic({0, 180, 0}, {0, 0, 0}, 120, 120, 110);
    TEST_ASSERT_FALSE(p.visible);
}

void test_projection_east_limb(void) {
    // 45 E of centre, on the equator: right of centre, same row.
    ScreenPoint p = projectOrthographic({0, 45, 0}, {0, 0, 0}, 120, 120, 110);
    TEST_ASSERT_TRUE(p.visible);
    TEST_ASSERT_INT_WITHIN(1, 198, p.x);
    TEST_ASSERT_INT_WITHIN(1, 120, p.y);
}

// --------------------------------------------------------------------------
// compassPoint
// --------------------------------------------------------------------------
void test_compass(void) {
    TEST_ASSERT_EQUAL_STRING("N",   compassPoint(0.0));
    TEST_ASSERT_EQUAL_STRING("N",   compassPoint(349.0));   // rounds up to N
    TEST_ASSERT_EQUAL_STRING("NNE", compassPoint(22.5));
    TEST_ASSERT_EQUAL_STRING("E",   compassPoint(90.0));
    TEST_ASSERT_EQUAL_STRING("SSW", compassPoint(202.5));
    TEST_ASSERT_EQUAL_STRING("W",   compassPoint(270.0));
    TEST_ASSERT_EQUAL_STRING("NW",  compassPoint(315.0));
    TEST_ASSERT_EQUAL_STRING("N",   compassPoint(360.0));
}

// --------------------------------------------------------------------------
// OrthoView -- the fast path the globe actually draws with
// --------------------------------------------------------------------------
void test_unit_vector_known_directions(void) {
    const UnitVec origin = toUnitVector({0.0, 0.0, 0.0});
    assertClose(1.0, origin.x, 1e-6);
    assertClose(0.0, origin.y, 1e-6);
    assertClose(0.0, origin.z, 1e-6);

    const UnitVec east = toUnitVector({0.0, 90.0, 0.0});
    assertClose(0.0, east.x, 1e-6);
    assertClose(1.0, east.y, 1e-6);
    assertClose(0.0, east.z, 1e-6);

    const UnitVec pole = toUnitVector({90.0, 0.0, 0.0});
    assertClose(0.0, pole.x, 1e-6);
    assertClose(0.0, pole.y, 1e-6);
    assertClose(1.0, pole.z, 1e-6);

    // Altitude must not change a direction, whatever the ISS is doing.
    const UnitVec high = toUnitVector({33.746, -117.826, 420.0});
    const UnitVec low  = toUnitVector({33.746, -117.826, 0.0});
    assertClose(low.x, high.x, 1e-6);
    assertClose(low.y, high.y, 1e-6);
    assertClose(low.z, high.z, 1e-6);
}

void test_unit_vectors_are_unit_length(void) {
    for (double lat = -90.0; lat <= 90.0; lat += 15.0) {
        for (double lon = -180.0; lon <= 180.0; lon += 15.0) {
            const UnitVec v = toUnitVector({lat, lon, 0.0});
            const double len = std::sqrt(double(v.x) * v.x +
                                         double(v.y) * v.y +
                                         double(v.z) * v.z);
            assertClose(1.0, len, 1e-6);
        }
    }
}

void test_ortho_view_center_and_far_side(void) {
    const GeoPoint center{33.746, -117.826, 0.0};
    const OrthoView view = makeOrthoView(center, 120, 120, 104);

    // Looking straight down the middle: dead centre of the disc.
    const ScreenPoint mid = projectUnit(view, toUnitVector(center));
    TEST_ASSERT_TRUE(mid.visible);
    TEST_ASSERT_EQUAL_INT(120, mid.x);
    TEST_ASSERT_EQUAL_INT(120, mid.y);

    // The antipode is around the back and must be culled.
    const GeoPoint anti{-center.lat_deg, center.lon_deg + 180.0, 0.0};
    TEST_ASSERT_FALSE(projectUnit(view, toUnitVector(anti)).visible);
}

// The whole reason projectUnit() exists is speed, so the thing worth testing
// is that it did not buy that speed with a different answer. Sweep view
// centres and points across the sphere and hold the two implementations
// against each other.
void test_ortho_view_matches_projection(void) {
    for (double lat0 = -80.0; lat0 <= 80.0; lat0 += 40.0) {
        for (double lon0 = -180.0; lon0 < 180.0; lon0 += 45.0) {
            const GeoPoint center{lat0, lon0, 0.0};
            const OrthoView view = makeOrthoView(center, 120, 120, 104);

            for (double lat = -90.0; lat <= 90.0; lat += 10.0) {
                for (double lon = -180.0; lon < 180.0; lon += 10.0) {
                    const GeoPoint p{lat, lon, 0.0};
                    const ScreenPoint slow =
                        projectOrthographic(p, center, 120, 120, 104);
                    const ScreenPoint fast = projectUnit(view, toUnitVector(p));

                    // Single precision costs at most a pixel of rounding.
                    TEST_ASSERT_INT_WITHIN(1, slow.x, fast.x);
                    TEST_ASSERT_INT_WITHIN(1, slow.y, fast.y);

                    // Visibility must agree except within a hair of the limb,
                    // where cos_c is ~0 and either sign is defensible.
                    const double dx = slow.x - 120.0;
                    const double dy = slow.y - 120.0;
                    const bool near_limb = std::fabs(std::sqrt(dx * dx + dy * dy) - 104.0) < 1.0;
                    if (!near_limb) {
                        TEST_ASSERT_EQUAL_INT(slow.visible ? 1 : 0,
                                              fast.visible ? 1 : 0);
                    }
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
// unprojectUnit -- the fill path, which runs the projection backwards
// --------------------------------------------------------------------------
void test_unproject_off_disc(void) {
    const OrthoView view = makeOrthoView({0.0, 0.0, 0.0}, 120, 120, 104);

    // Corners of a 240x240 screen are well outside a 104 px disc.
    TEST_ASSERT_FALSE(unprojectUnit(view, 0, 0).on_globe);
    TEST_ASSERT_FALSE(unprojectUnit(view, 239, 239).on_globe);
    // Just outside the limb, and just inside it.
    TEST_ASSERT_FALSE(unprojectUnit(view, 120 + 105, 120).on_globe);
    TEST_ASSERT_TRUE(unprojectUnit(view, 120 + 103, 120).on_globe);
}

void test_unproject_centre_is_view_axis(void) {
    const GeoPoint centre{33.746, -117.826, 0.0};
    const OrthoView view = makeOrthoView(centre, 120, 120, 104);

    // The middle of the disc looks straight down the view axis, so it must
    // come back as the direction to the view centre itself.
    const SpherePoint mid = unprojectUnit(view, 120, 120);
    TEST_ASSERT_TRUE(mid.on_globe);
    const UnitVec want = toUnitVector(centre);
    assertClose(want.x, mid.dir.x, 1e-5);
    assertClose(want.y, mid.dir.y, 1e-5);
    assertClose(want.z, mid.dir.z, 1e-5);
}

void test_unproject_results_are_unit_length(void) {
    const OrthoView view = makeOrthoView({10.0, 25.0, 0.0}, 120, 120, 104);
    for (int y = 16; y <= 224; ++y) {
        for (int x = 16; x <= 224; ++x) {
            const SpherePoint p = unprojectUnit(view, x, y);
            if (!p.on_globe) continue;
            const double len = std::sqrt(double(p.dir.x) * p.dir.x +
                                         double(p.dir.y) * p.dir.y +
                                         double(p.dir.z) * p.dir.z);
            assertClose(1.0, len, 1e-5);
        }
    }
}

// The real contract: unproject a pixel, project the answer, get the pixel
// back. Sweep whole discs, because a sign error in one of the three rows
// still round-trips along the axes and only shows up off-centre.
void test_unproject_round_trips(void) {
    for (double lat0 = -60.0; lat0 <= 60.0; lat0 += 30.0) {
        for (double lon0 = -150.0; lon0 <= 150.0; lon0 += 75.0) {
            const OrthoView view = makeOrthoView({lat0, lon0, 0.0}, 120, 120, 104);
            for (int y = 16; y <= 224; ++y) {
                for (int x = 16; x <= 224; ++x) {
                    const SpherePoint p = unprojectUnit(view, x, y);
                    if (!p.on_globe) continue;

                    // Exactly on the limb the round trip lands on u == 0,
                    // where a half-ULP decides the sign of `visible`. That is
                    // inherent, not a bug, so the outermost ring is excluded
                    // rather than asserted either way.
                    const double dx = x - 120.0, dy = y - 120.0;
                    if (std::sqrt(dx * dx + dy * dy) > 103.0) continue;

                    const ScreenPoint back = projectUnit(view, p.dir);
                    TEST_ASSERT_TRUE(back.visible);
                    // Near the limb a whole pixel of screen spans a large
                    // angle, so allow one pixel of rounding either way.
                    TEST_ASSERT_INT_WITHIN(1, x, back.x);
                    TEST_ASSERT_INT_WITHIN(1, y, back.y);
                }
            }
        }
    }
}

// --------------------------------------------------------------------------
void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_normalize_basic);
    RUN_TEST(test_wrap_longitude);
    RUN_TEST(test_distance_same_point);
    RUN_TEST(test_distance_known_pairs);
    RUN_TEST(test_distance_degenerate);
    RUN_TEST(test_distance_is_symmetric);
    RUN_TEST(test_look_directly_overhead);
    RUN_TEST(test_look_due_north);
    RUN_TEST(test_look_due_east);
    RUN_TEST(test_look_below_horizon);
    RUN_TEST(test_projection_center);
    RUN_TEST(test_projection_north_pole);
    RUN_TEST(test_projection_far_side_hidden);
    RUN_TEST(test_projection_east_limb);
    RUN_TEST(test_unit_vector_known_directions);
    RUN_TEST(test_unit_vectors_are_unit_length);
    RUN_TEST(test_ortho_view_center_and_far_side);
    RUN_TEST(test_ortho_view_matches_projection);
    RUN_TEST(test_unproject_off_disc);
    RUN_TEST(test_unproject_centre_is_view_axis);
    RUN_TEST(test_unproject_results_are_unit_length);
    RUN_TEST(test_unproject_round_trips);
    RUN_TEST(test_compass);
    return UNITY_END();
}
