#include "orbit/tle.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace orbit {
namespace {

// Copy the next line into buf (NUL-terminated), return a pointer past the
// line ending, or nullptr when there is nothing left. Handles \n, \r\n and \r.
const char* nextLine(const char* p, char* buf, size_t cap) {
    if (p == nullptr || *p == '\0') return nullptr;
    size_t n = 0;
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        if (n + 1 < cap) buf[n++] = *p;
        ++p;
    }
    buf[n] = '\0';
    while (*p == '\r' || *p == '\n') ++p;
    return p;
}

void rstrip(char* s) {
    size_t n = std::strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

// Unix seconds at 00:00 UTC on 1 January of `year`.
int64_t unixAtYearStart(int year) {
    int64_t days = 0;
    for (int y = 1970; y < year; ++y) days += isLeap(y) ? 366 : 365;
    return days * 86400;
}

}  // namespace

bool checksumOk(const char* line) {
    if (line == nullptr) return false;
    if (std::strlen(line) < 69) return false;

    // Sum every digit in columns 1-68. A minus sign counts as 1; everything
    // else counts as 0. Column 69 holds the expected total, mod 10.
    int sum = 0;
    for (int i = 0; i < 68; ++i) {
        const char c = line[i];
        if (c >= '0' && c <= '9')      sum += c - '0';
        else if (c == '-')             sum += 1;
    }

    const char given = line[68];
    if (given < '0' || given > '9') return false;
    return (sum % 10) == (given - '0');
}

bool parseTle(const char* raw, Tle& out) {
    if (raw == nullptr) return false;

    char a[96] = {0}, b[96] = {0}, c[96] = {0};
    const char* p = raw;

    // Skip any leading blank lines, then take three.
    do { p = nextLine(p, a, sizeof(a)); } while (p != nullptr && a[0] == '\0');
    if (p == nullptr) return false;
    p = nextLine(p, b, sizeof(b));
    if (p == nullptr) return false;
    nextLine(p, c, sizeof(c));

    rstrip(a); rstrip(b); rstrip(c);

    // Celestrak's TLE format leads with a name line, but bare 2-line sets
    // exist too. If the first line is already element line 1, shuffle up.
    if (a[0] == '1' && a[1] == ' ') {
        std::memmove(c, b, sizeof(c));
        std::memmove(b, a, sizeof(b));
        a[0] = '\0';
    }

    if (b[0] != '1' || c[0] != '2') return false;
    if (!checksumOk(b) || !checksumOk(c)) return false;

    // Columns 19-20 (0-indexed 18) are a two-digit year; 21-32 are the
    // fractional day of year. strncpy does NOT NUL-terminate for you.
    char ybuf[3] = {0};
    std::strncpy(ybuf, b + 18, 2);
    const int yy = std::atoi(ybuf);

    char dbuf[13] = {0};
    std::strncpy(dbuf, b + 20, 12);
    const double doy = std::strtod(dbuf, nullptr);
    if (doy <= 0.0 || doy >= 367.0) return false;

    // Everything validated -- only now do we touch the caller's struct.
    std::strncpy(out.name,  a, sizeof(out.name)  - 1); out.name[sizeof(out.name) - 1]   = '\0';
    std::strncpy(out.line1, b, sizeof(out.line1) - 1); out.line1[sizeof(out.line1) - 1] = '\0';
    std::strncpy(out.line2, c, sizeof(out.line2) - 1); out.line2[sizeof(out.line2) - 1] = '\0';

    // The two-digit year convention: 57-99 means 19xx, 00-56 means 20xx.
    out.epoch_year = (yy >= 57) ? (1900 + yy) : (2000 + yy);
    out.epoch_day  = doy;
    out.valid      = true;
    return true;
}

double tleAgeHours(const Tle& tle, int64_t unix_now) {
    if (!tle.valid) return 0.0;
    const double epoch_unix =
        static_cast<double>(unixAtYearStart(tle.epoch_year)) + (tle.epoch_day - 1.0) * 86400.0;
    return (static_cast<double>(unix_now) - epoch_unix) / 3600.0;
}

}  // namespace orbit
