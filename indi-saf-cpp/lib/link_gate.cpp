// link_gate.cpp -- see link_gate.hpp.
#include "link_gate.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <climits>

namespace saf {

std::uint32_t DumpStopTime = 0;
int DumpStopCount = 0;

bool parseDumpValue(const std::string& raw, std::uint32_t& out) {
    size_t b = 0, e = raw.size();
    while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
    const std::string t = raw.substr(b, e - b);

    std::string lower = t;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "on")  { out = 0xFFFFFFFFu; return true; }
    if (lower == "off") { out = 0;           return true; }

    int Y, M, D, h, m, s = 0, used = 0;
    int got = std::sscanf(t.c_str(), "%4d-%2d-%2dT%2d:%2d:%2dZ%n",
                          &Y, &M, &D, &h, &m, &s, &used);
    if (!(got == 6 && used == static_cast<int>(t.size()))) {
        s = 0; used = 0;
        got = std::sscanf(t.c_str(), "%4d-%2d-%2dT%2d:%2dZ%n",
                          &Y, &M, &D, &h, &m, &used);
        if (!(got == 5 && used == static_cast<int>(t.size()))) return false;
    }
    if (t.size() < 17 || t[4] != '-' || t[7] != '-' || t[10] != 'T') return false;
    if (M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 ||
        m < 0 || m > 59 || s < 0 || s > 59) return false;

    std::tm tm{};
    tm.tm_year = Y - 1900; tm.tm_mon = M - 1; tm.tm_mday = D;
    tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = s;
    std::time_t secs = timegm(&tm);
    // timegm normalizes e.g. Feb 31 -> Mar 3; reject such dates.
    if (tm.tm_year != Y - 1900 || tm.tm_mon != M - 1 || tm.tm_mday != D) return false;
    if (secs < 0 || static_cast<unsigned long long>(secs) > 0xFFFFFFFFull) return false;
    out = static_cast<std::uint32_t>(secs);
    return true;
}

bool parseStopCount(const std::string& raw, int& out) {
    size_t b = 0, e = raw.size();
    while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
    const std::string t = raw.substr(b, e - b);
    if (t.empty()) return false;
    char* end = nullptr;
    const double d = std::strtod(t.c_str(), &end);
    if (end != t.c_str() + t.size() || !std::isfinite(d)) return false;
    if (d != std::floor(d) || d < INT_MIN || d > INT_MAX) return false;
    out = static_cast<int>(d);
    return true;
}

bool linkIsUpAt(std::time_t now) {
    return now >= 0 && static_cast<unsigned long long>(now) < DumpStopTime;
}

bool linkIsUpCount(bool testOnly) {
    if (DumpStopCount < 1) return false;
    if (!testOnly) --DumpStopCount;
    return true;
}

bool linkIsUpNow(std::time_t now, bool testOnly) {
    return linkIsUpAt(now) || linkIsUpCount(testOnly);
}

} // namespace saf
