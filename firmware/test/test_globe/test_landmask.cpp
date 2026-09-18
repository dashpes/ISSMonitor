//
// test_landmask.cpp -- is the generated land mask actually pointing at Earth?
//
// src/ui/land_mask.h holds a cube map of land/sea bits and the lookup that
// reads it. The encoding has six faces and three axes, so there are plenty of
// ways to wire it up that are self-consistent and still wrong -- a swapped
// pair would put Africa in the Pacific and nothing would warn you.
//
// The fix for that is not to re-derive the mapping here. It is to check the
// answers against somewhere you already know. Every expectation below is a
// place you can point at on a globe.
//
#include <unity.h>

#include <cmath>

#include "orbit/geo.h"
#include "ui/land_mask.h"

static bool landAtLatLon(double lat_deg, double lon_deg) {
    const orbit::UnitVec v = orbit::toUnitVector({lat_deg, lon_deg, 0.0});
    return land_mask::at(v.x, v.y, v.z);
}

// Interior points, chosen well away from any coast: a cube texel spans about
// 0.7 degrees, so a place 30 km from the sea proves nothing either way.
void test_known_land(void) {
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon( 23.0,   13.0), "Sahara");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon( -3.0,  -60.0), "Amazon basin");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon(-25.0,  134.0), "central Australia");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon( 65.0,  100.0), "central Siberia");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon( 72.0,  -40.0), "Greenland ice sheet");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon( 32.0,   88.0), "Tibetan plateau");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon( 40.0, -100.0), "Great Plains");
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon(-80.0,    0.0), "Queen Maud Land");
}

void test_known_ocean(void) {
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon(  0.0, -140.0), "central Pacific");
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon( 30.0,  -40.0), "north Atlantic");
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon(-30.0,   80.0), "southern Indian");
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon( 89.0,    0.0), "Arctic Ocean");
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon(-55.0,  -20.0), "south Atlantic");
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon( 15.0,   65.0), "Arabian Sea");
}

// Antarctica is the one that catches seam bugs. Its polygon is closed with an
// artificial edge along lat -90 and down the antimeridian; those edges had to
// be REMOVED for the coastline stroke and had to be KEPT for the fill. If the
// fill used the stroke's data, the even-odd rule sees an odd number of
// crossings on every Antarctic row and fills the sea instead of the continent.
void test_antarctica_is_filled_all_the_way_round(void) {
    for (double lon = -180.0; lon < 180.0; lon += 15.0) {
        TEST_ASSERT_TRUE_MESSAGE(landAtLatLon(-85.0, lon), "Antarctic interior");
    }
}

// The south pole itself is the degenerate case: every meridian meets there.
void test_poles(void) {
    TEST_ASSERT_TRUE_MESSAGE(landAtLatLon(-89.9, 0.0), "south pole is land");
    TEST_ASSERT_FALSE_MESSAGE(landAtLatLon(89.9, 0.0), "north pole is sea ice");
}

// Land should come out near Earth's real share of the surface. This is the
// check that catches a mask that is subtly inside-out or shifted a face over:
// those still look plausible point by point but wreck the total.
void test_land_fraction_is_plausible(void) {
    int land = 0, total = 0;
    // Walk the sphere with equal-area sampling -- stepping latitude uniformly
    // would over-weight the poles and flatter Antarctica.
    for (int a = 0; a < 400; ++a) {
        const double sin_lat = -1.0 + 2.0 * (a + 0.5) / 400.0;
        const double lat = orbit::rad2deg(std::asin(sin_lat));
        for (int b = 0; b < 400; ++b) {
            const double lon = -180.0 + 360.0 * (b + 0.5) / 400.0;
            if (landAtLatLon(lat, lon)) ++land;
            ++total;
        }
    }
    const double fraction = static_cast<double>(land) / total;
    TEST_ASSERT_TRUE_MESSAGE(fraction > 0.24 && fraction < 0.34,
                             "land fraction should be near Earth's 0.29");
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_known_land);
    RUN_TEST(test_known_ocean);
    RUN_TEST(test_antarctica_is_filled_all_the_way_round);
    RUN_TEST(test_poles);
    RUN_TEST(test_land_fraction_is_plausible);
    return UNITY_END();
}
