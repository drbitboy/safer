// link_gate.hpp
//
// LinkGate: the "dump window" state that decides whether the external
// link is considered up, i.e. whether saf_local may build an outbound
// archive this lap.
//
// An operator controls it by sending vectors to saf_local's own
// pseudo-device (device name == logName(), default "saf_local"):
//
//   Text   property "dump_time", element "stop_time", value one of
//     "On"                    -> stop time = 2^32-1 (link up "forever")
//     "Off"                   -> stop time = 0      (link down)
//     "YYYY-mm-ddTHH:MM:SSZ"  -> stop time = that UTC time as seconds
//                                since the epoch (":SS" may be omitted)
//
//   The link is up while  now < stop time  (seconds since the epoch),
//   down when now >= stop time. The stop time starts at 0, so the link
//   is DOWN until an operator turns it on.
//
//   Number property "dump_count", element "stop_count", an integer,
//   sets the stop count (initially 0). It matters only while the time
//   window is closed: each isUp() call then returns true and decrements
//   the stop count if it is positive, else returns false (so a value < 1
//   grants nothing). In effect: "allow N more archive cycles even though
//   the time window is closed".
//
// Not thread-safe: use from one thread (saf_local's loop).

#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace saf {

// Parses a dump_time value (surrounding whitespace ignored; "On"/"Off"
// are case-insensitive). Returns false and leaves `out` untouched if the
// text is not one of the three accepted forms or the time is outside
// 1970-01-01T00:00:00Z .. 2106-02-07T06:28:15Z.
bool parseDumpValue(const std::string& text, std::uint32_t& out);

// Parses an integer count (surrounding whitespace ignored). Accepts plain
// decimal integers and decimal numbers with an integral value ("5", "-2",
// "3.0"); rejects anything else or values outside int range.
bool parseStopCount(const std::string& text, int& out);

class LinkGate {
public:
    void setStopTime(std::uint32_t secondsSinceEpoch) { stopTime_ = secondsSinceEpoch; }
    void setStopCount(int count) { stopCount_ = count; }
    std::uint32_t stopTime() const { return stopTime_; }
    int stopCount() const { return stopCount_; }

    // True iff now < stop time. Never changes anything.
    bool isUpAt(std::time_t now) const;

    // If the stop count is >= 1: return true, decrementing it by 1 unless
    // testOnly is true; else return false. Meant to be consulted only
    // when isUpAt() was false (see isUpNow()).
    bool isUpCount(bool testOnly);

    // The combined rule: isUpAt(now) || isUpCount(testOnly). The ||
    // short-circuits, so the stop count is neither read nor changed while
    // the time window is open. Takes `now` so it is testable.
    bool isUpNow(std::time_t now, bool testOnly);

    // isUpNow() with the current time. NOTE: with testOnly == false and
    // the time window closed, this CONSUMES one stop count, so call it
    // only when an archive would actually be made.
    bool isUp(bool testOnly = false);

    // Same answer as isUp() but spends no count: for logging and checks.
    bool isUpTestOnly() { return isUp(true); }

private:
    std::uint32_t stopTime_ = 0;
    int stopCount_ = 0;
};

} // namespace saf
