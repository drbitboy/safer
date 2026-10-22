// link_gate_test.cpp -- parseDumpValue() and linkIsUpAt().
#include "link_gate.hpp"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main() {
    using namespace saf;
    std::uint32_t v = 7;

    check(parseDumpValue("On", v) && v == 0xFFFFFFFFu, "1 'On' -> 2^32-1");
    check(parseDumpValue(" off\n", v) && v == 0, "2 ' off\\n' -> 0 (case/whitespace tolerant)");
    check(parseDumpValue("1970-01-01T00:00:10Z", v) && v == 10, "3 epoch+10s");
    check(parseDumpValue("2026-10-22T20:30:00Z", v) && v == 1792701000u, "4 full UTC time");
    check(parseDumpValue("2026-10-22T20:30Z", v) && v == 1792701000u, "5 seconds optional");
    check(parseDumpValue("2106-02-07T06:28:15Z", v) && v == 0xFFFFFFFFu, "6 max representable time");

    v = 99;
    check(!parseDumpValue("2106-02-07T06:28:16Z", v) && v == 99, "7a past 2^32-1 rejected, out untouched");
    check(!parseDumpValue("1969-12-31T23:59:59Z", v), "7b before epoch rejected");
    check(!parseDumpValue("2026-02-31T00:00:00Z", v), "7c Feb 31 rejected");
    check(!parseDumpValue("2026-10-22 20:30:00Z", v), "7d space instead of T rejected");
    check(!parseDumpValue("2026-10-22T20:30:00", v), "7e missing Z rejected");
    check(!parseDumpValue("2026-10-22T25:00:00Z", v), "7f hour 25 rejected");
    check(!parseDumpValue("maybe", v) && !parseDumpValue("", v), "7g junk/empty rejected");

    DumpStopTime = 0;
    check(!linkIsUpAt(1000), "8a initial/Off: down");
    DumpStopTime = 1000;
    check(linkIsUpAt(999) && !linkIsUpAt(1000) && !linkIsUpAt(1001),
          "8b up while now < stop; down at and after stop");
    DumpStopTime = 0xFFFFFFFFu;
    check(linkIsUpAt(1792701000), "8c On: up");

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
