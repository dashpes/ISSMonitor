#pragma once
//
// orbit/tle.h -- parsing and validating a Two-Line Element set.
//
// Implementation: lib/orbit/src/tle.cpp
// Concepts: fixed-size char buffers instead of String (this is THE embedded
// habit to build), std::strtod, checksum arithmetic, and returning success
// as a value rather than throwing.
//
// A TLE from Celestrak looks like:
//   ISS (ZARYA)
//   1 25544U 98067A   ...
//   2 25544  51.6...
// Columns are FIXED WIDTH. Do not split on spaces -- some fields run together.
//
#include <cstdint>

namespace orbit {

// 69 chars + NUL. Fixed size on purpose: this struct lives in a global, gets
// written to flash, and must never heap-allocate.
struct Tle {
    char name[25] = {0};
    char line1[70] = {0};
    char line2[70] = {0};

    // Parsed out of line1 so the UI can say "orbit data 3 h old".
    int    epoch_year = 0;    // full year, e.g. 2026
    double epoch_day  = 0.0;  // fractional day-of-year
    bool   valid      = false;
};

// Celestrak's checksum: sum every digit in the line, counting '-' as 1 and
// ignoring everything else, mod 10. Must equal the last character.
// This is your only defence against a truncated HTTP response.
bool checksumOk(const char* line);

// Parse a 3-line TLE block. `raw` may have \r\n or \n line endings and may
// have trailing junk. Returns false and leaves `out` untouched on any problem.
bool parseTle(const char* raw, Tle& out);

// Age of the element set in hours, given the current UNIX time.
// Above ~72 h the ISS position drifts noticeably; the UI should say so.
double tleAgeHours(const Tle& tle, int64_t unix_now);

}  // namespace orbit
