//
// test_tle.cpp -- TLE parsing and checksum validation.
//
// The element set below is a SYNTHETIC FIXTURE. The format and checksums are
// real and correct; the orbital numbers are plausible filler. Never use it to
// point anything at the sky -- it exists to exercise the parser.
//
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "orbit/tle.h"

using namespace orbit;

// Epoch: year 26, day 260.5 -> 2026, day-of-year 260.5
static const char* kGoodLine1 =
    "1 25544U 98067A   26260.50000000  .00016717  00000-0  30777-3 0  9990";
static const char* kGoodLine2 =
    "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49309239431664";

static const char* kGoodBlock =
    "ISS (ZARYA)\r\n"
    "1 25544U 98067A   26260.50000000  .00016717  00000-0  30777-3 0  9990\r\n"
    "2 25544  51.6416 247.4627 0006703 130.5360 325.0288 15.49309239431664\r\n";

// --------------------------------------------------------------------------
// checksumOk
// --------------------------------------------------------------------------
void test_checksum_accepts_valid(void) {
    TEST_ASSERT_TRUE(checksumOk(kGoodLine1));
    TEST_ASSERT_TRUE(checksumOk(kGoodLine2));
}

void test_checksum_rejects_corrupted(void) {
    // Flip one digit in the middle. The checksum must notice.
    char bad[80];
    std::strcpy(bad, kGoodLine1);
    bad[30] = (bad[30] == '7') ? '8' : '7';
    TEST_ASSERT_FALSE(checksumOk(bad));
}

void test_checksum_rejects_truncated(void) {
    // This is the real-world case: the HTTP response got cut short.
    char truncated[80];
    std::strcpy(truncated, kGoodLine1);
    truncated[40] = '\0';
    TEST_ASSERT_FALSE(checksumOk(truncated));
    TEST_ASSERT_FALSE(checksumOk(""));
    TEST_ASSERT_FALSE(checksumOk(nullptr));
}

// --------------------------------------------------------------------------
// parseTle
// --------------------------------------------------------------------------
void test_parse_good_block(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(kGoodBlock, t));
    TEST_ASSERT_TRUE(t.valid);
    TEST_ASSERT_EQUAL_STRING("ISS (ZARYA)", t.name);
    TEST_ASSERT_EQUAL_STRING(kGoodLine1, t.line1);
    TEST_ASSERT_EQUAL_STRING(kGoodLine2, t.line2);
    TEST_ASSERT_EQUAL_INT(2026, t.epoch_year);
    TEST_ASSERT_TRUE(std::fabs(t.epoch_day - 260.5) < 1e-6);
}

void test_parse_accepts_unix_line_endings(void) {
    char block[256];
    std::snprintf(block, sizeof(block), "ISS (ZARYA)\n%s\n%s\n", kGoodLine1, kGoodLine2);
    Tle t;
    TEST_ASSERT_TRUE(parseTle(block, t));
    TEST_ASSERT_EQUAL_INT(2026, t.epoch_year);
}

void test_parse_accepts_bare_two_line_set(void) {
    // No name line -- some sources omit it.
    char block[256];
    std::snprintf(block, sizeof(block), "%s\n%s\n", kGoodLine1, kGoodLine2);
    Tle t;
    TEST_ASSERT_TRUE(parseTle(block, t));
    TEST_ASSERT_EQUAL_STRING(kGoodLine1, t.line1);
}

void test_parse_rejects_garbage(void) {
    Tle t;
    TEST_ASSERT_FALSE(parseTle("<!DOCTYPE html><html><body>404", t));
    TEST_ASSERT_FALSE(parseTle("", t));
    TEST_ASSERT_FALSE(parseTle(nullptr, t));
    // Never claims success on junk.
    TEST_ASSERT_FALSE(t.valid);
}

void test_parse_rejects_bad_checksum(void) {
    char block[256];
    char bad2[80];
    std::strcpy(bad2, kGoodLine2);
    bad2[69] = '\0';
    bad2[68] = (bad2[68] == '4') ? '5' : '4';   // wrong checksum digit
    std::snprintf(block, sizeof(block), "ISS (ZARYA)\n%s\n%s\n", kGoodLine1, bad2);

    Tle t;
    TEST_ASSERT_FALSE(parseTle(block, t));
}

void test_parse_leaves_output_untouched_on_failure(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(kGoodBlock, t));      // good data first
    const int year_before = t.epoch_year;

    TEST_ASSERT_FALSE(parseTle("garbage", t));      // then a failed parse
    // A failed refresh must not destroy the cached element set -- offline,
    // a stale TLE is far better than none.
    TEST_ASSERT_EQUAL_INT(year_before, t.epoch_year);
    TEST_ASSERT_TRUE(t.valid);
}

// --------------------------------------------------------------------------
// tleAgeHours
// --------------------------------------------------------------------------
void test_age_zero_at_epoch(void) {
    Tle t;
    TEST_ASSERT_TRUE(parseTle(kGoodBlock, t));
    // 2026 is not a leap year. Day-of-year 260.5 = 259.5 days after Jan 1.
    const int64_t jan1_2026 = 1767225600;   // 2026-01-01T00:00:00Z
    const int64_t epoch = jan1_2026 + static_cast<int64_t>(259.5 * 86400.0);

    TEST_ASSERT_TRUE(std::fabs(tleAgeHours(t, epoch)) < 0.01);
    TEST_ASSERT_TRUE(std::fabs(tleAgeHours(t, epoch + 3600) - 1.0) < 0.01);
    TEST_ASSERT_TRUE(std::fabs(tleAgeHours(t, epoch + 86400) - 24.0) < 0.01);
}

// --------------------------------------------------------------------------
void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_accepts_valid);
    RUN_TEST(test_checksum_rejects_corrupted);
    RUN_TEST(test_checksum_rejects_truncated);
    RUN_TEST(test_parse_good_block);
    RUN_TEST(test_parse_accepts_unix_line_endings);
    RUN_TEST(test_parse_accepts_bare_two_line_set);
    RUN_TEST(test_parse_rejects_garbage);
    RUN_TEST(test_parse_rejects_bad_checksum);
    RUN_TEST(test_parse_leaves_output_untouched_on_failure);
    RUN_TEST(test_age_zero_at_epoch);
    return UNITY_END();
}
