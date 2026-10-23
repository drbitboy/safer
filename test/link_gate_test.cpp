// link_gate_test.cpp -- parseDumpValue(), parseStopCount() and LinkGate.
#include "link_gate.hpp"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main() {
    using namespace saf;
    LinkGate g;
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

    g.setStopTime(0);
    check(!g.isUpAt(1000), "8a initial/Off: down");
    g.setStopTime(1000);
    check(g.isUpAt(999) && !g.isUpAt(1000) && !g.isUpAt(1001),
          "8b up while now < stop; down at and after stop");
    g.setStopTime(0xFFFFFFFFu);
    check(g.isUpAt(1792701000), "8c On: up");

    {
        LinkGate fresh;
        check(fresh.stopTime() == 0 && fresh.stopCount() == 0 &&
                  !fresh.isUpAt(1000) && !fresh.isUpTestOnly(),
              "0 a new LinkGate starts with time 0, count 0, link down");
    }

    // --- stop_count ---
    int n = -99;
    check(parseStopCount("5", n) && n == 5, "9a '5' -> 5");
    check(parseStopCount(" -2 ", n) && n == -2, "9b ' -2 ' -> -2");
    check(parseStopCount("3.0", n) && n == 3, "9c '3.0' -> 3 (integral value)");
    n = 42;
    check(!parseStopCount("2.5", n) && n == 42, "9d 2.5 rejected, out untouched");
    check(!parseStopCount("abc", n) && !parseStopCount("", n) && !parseStopCount("4x", n),
          "9e junk/empty rejected");
    check(!parseStopCount("3000000000", n), "9f beyond int range rejected");

    g.setStopCount(0);
    check(!g.isUpCount(false) && g.stopCount() == 0, "10a count 0 -> false, unchanged");
    g.setStopCount(-3);
    check(!g.isUpCount(false) && g.stopCount() == -3, "10b negative -> false, unchanged");
    g.setStopCount(2);
    check(g.isUpCount(false) && g.stopCount() == 1, "10c 2 -> true, now 1");
    check(g.isUpCount(false) && g.stopCount() == 0, "10d 1 -> true, now 0");
    check(!g.isUpCount(false) && g.stopCount() == 0, "10e exhausted -> false");

    g.setStopCount(2);
    check(g.isUpCount(true) && g.isUpCount(true) && g.stopCount() == 2,
          "10f testOnly=true: true, count not decremented");
    g.setStopCount(0);
    check(!g.isUpCount(true) && g.stopCount() == 0, "10g testOnly=true, count 0 -> false");

    // --- combined rule: the || short-circuits ---
    g.setStopTime(2000); g.setStopCount(3);           // window open at now=1000
    check(g.isUpNow(1000, false) && g.stopCount() == 3,
          "11a time window open: true, count not touched (short-circuit)");
    g.setStopTime(1000); g.setStopCount(3);           // window closed at now=1000
    check(g.isUpNow(1000, true) && g.stopCount() == 3,
          "11b window closed, testOnly: true, count not spent");
    check(g.isUpNow(1000, false) && g.stopCount() == 2,
          "11c window closed: true, spends one count");
    g.setStopCount(0);
    check(!g.isUpNow(1000, false) && g.stopCount() == 0,
          "11d window closed, count 0: false");

    // --- the real-clock wrappers ---
    g.setStopTime(0xFFFFFFFFu); g.setStopCount(2);
    check(g.isUp() && g.stopCount() == 2, "12a On: g.isUp() true, count untouched");
    g.setStopTime(0);
    check(g.isUpTestOnly() && g.isUpTestOnly() && g.stopCount() == 2,
          "12b Off, count 2: g.isUpTestOnly() true and spends nothing");
    check(g.isUp() && g.stopCount() == 1, "12c g.isUp() spends one count");
    check(g.isUp(false) && !g.isUp() && g.stopCount() == 0,
          "12d spends the last, then false");
    check(!g.isUpTestOnly(), "12e exhausted: g.isUpTestOnly() false");

    std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
