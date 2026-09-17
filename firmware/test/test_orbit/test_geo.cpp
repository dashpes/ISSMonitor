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
    RUN_TEST(test_compass);
    return UNITY_END();
}
