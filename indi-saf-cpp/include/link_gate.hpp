// link_gate.hpp
//
// Process-wide "dump window" state that decides whether the external
// link is considered up (see linkIsUp() in saf_local.cpp).
//
// An operator controls it by sending a Text vector to saf_local's own
// pseudo-device (device name == logName(), default "saf_local"):
//
//   property "dump", element "file", value one of
//     "On"                    -> DumpStopTime = 2^32-1 (link up "forever")
//     "Off"                   -> DumpStopTime = 0      (link down)
//     "YYYY-mm-ddTHH:MM:SSZ"  -> DumpStopTime = that UTC time as seconds
//                                since the epoch (":SS" may be omitted)
//
// The link is up while  now < DumpStopTime  (seconds since the epoch),
// down when now >= DumpStopTime. DumpStopTime starts at 0, so the link
// is DOWN until an operator turns it on.

#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace saf {

extern std::uint32_t DumpStopTime;

// Parses a dump value (surrounding whitespace ignored; "On"/"Off" are
// case-insensitive). Returns false and leaves `out` untouched if the text
// is not one of the three accepted forms or the time is outside
// 1970-01-01T00:00:00Z .. 2106-02-07T06:28:15Z.
bool parseDumpValue(const std::string& text, std::uint32_t& out);

// True iff now < DumpStopTime.
bool linkIsUpAt(std::time_t now);

} // namespace saf
