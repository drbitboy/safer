// saf_log.hpp -- tiny process-wide diagnostic logger shared by the app
// and the library code (mailbox, archiver), so file-level events can be
// logged from inside lib/ without threading a Config through every call.
//
// Levels (set from -v / -vv, see saf_local_config.hpp):
//   0  silent (errors are logged separately and always)
//   1  -v     connection events, messages sent, files written, archives made
//   2  -vv    everything in level 1 plus received-message details,
//             file renames and file deletions
//
// Not thread-safe to change the level while other threads log; set it
// once at startup (runLoop does).

#pragma once
#include <string>

namespace saf {

void setLogLevel(int level);
int logLevel();

// Prints "saf_local: <msg>" to stderr iff level <= current log level.
void logAt(int level, const std::string& msg);

} // namespace saf
