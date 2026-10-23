// link_gate.hpp
//
// Process-wide "dump window" state that decides whether the external
// link is considered up (see linkIsUp() in saf_local.cpp).
//
// An operator controls it by sending a Text vector to saf_local's own
// pseudo-device (device name == logName(), default "saf_local"):
//
//   Text   property "dump_time", element "stop_time", value one of
//     "On"                    -> DumpStopTime = 2^32-1 (link up "forever")
//     "Off"                   -> DumpStopTime = 0      (link down)
//     "YYYY-mm-ddTHH:MM:SSZ"  -> DumpStopTime = that UTC time as seconds
//                                since the epoch (":SS" may be omitted)
//
// The link is up while  now < DumpStopTime  (seconds since the epoch),
// down when now >= DumpStopTime. DumpStopTime starts at 0, so the link
// is DOWN until an operator turns it on.
//
//   Number property "dump_count", element "stop_count", an integer,
//   is stored in DumpStopCount (initially 0). It matters only while
//   linkIsUpAt() is false: each linkIsUpCount() call then returns true
//   and decrements DumpStopCount if it is positive, else returns false
//   (so a value < 1 grants nothing). In effect: "allow N more archive
//   cycles even though the time window is closed".

#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace saf {

extern std::uint32_t DumpStopTime;
extern int DumpStopCount;

// Parses a dump value (surrounding whitespace ignored; "On"/"Off" are
// case-insensitive). Returns false and leaves `out` untouched if the text
// is not one of the three accepted forms or the time is outside
// 1970-01-01T00:00:00Z .. 2106-02-07T06:28:15Z.
bool parseDumpValue(const std::string& text, std::uint32_t& out);

// Parses an integer count (surrounding whitespace ignored). Accepts
// plain decimal integers and decimal numbers with an integral value
// ("5", "-2", "3.0"); rejects anything else or values outside int range.
bool parseStopCount(const std::string& text, int& out);

// True iff now < DumpStopTime.
bool linkIsUpAt(std::time_t now);

// If DumpStopCount >= 1: decrement it and return true; else return false.
// Call only when linkIsUpAt() was false (see saf_local.cpp linkIsUp()).
bool linkIsUpCount();

} // namespace saf
