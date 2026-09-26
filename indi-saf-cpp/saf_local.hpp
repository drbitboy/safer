// saf_local.hpp
//
// The program that runs 1a and 1b as a single thread, per the design
// worked out in conversation:
//
//   loop:
//     (A)   check the inbound mailbox for pending records
//     (A.1) none pending -> go to (B)
//     (A.2) pending -> recompose + send each to indiserver over the
//           socket, erase each .sending file once fully sent; a
//           partial/EAGAIN write on the socket stops draining for
//           this lap (leaves the rest for next time) rather than
//           busy-looping on it
//     (B)   select() on the indiserver socket's read fd, with a
//           timeout -- this is what paces the loop; there's no sleep()
//     (B.1) timeout, nothing readable -> go to (A)
//     (B.2) readable -> recv(), feed bytes into the persistent LilXML
//           parser, decompose any complete vector messages that
//           result, upsert each into the outbound mailbox -> go to (A)
//
// No select() on the write side: write-readiness is essentially always
// true and isn't the actual gating condition -- whether the inbound
// mailbox has anything pending is what matters, and that's a plain
// directory check in (A), not something select() can watch directly
// (FileMailbox is files, not a fd-bearing object).
//
// 1a and 1b share ONE TCP connection to indiserver, since they're
// both just "the INDI client" talking to the same local server --
// this is one of the practical benefits of combining them into a
// single thread rather than the benefit being purely simplicity.
//
// Config, INI-file parsing, and CLI-argument parsing all live in
// saf_local_config.hpp/.cpp instead of here, specifically so that
// logic has no dependency on liblilxml and can be linked/tested on
// its own -- runLoop() below is the only thing in this file, and it's
// the only thing that actually needs liblilxml, mailbox.hpp, and
// indi_xml_bridge.hpp.

#pragma once
#include "saf_local_config.hpp"

namespace saf {

// Runs the loop described above. Blocks until a shutdown signal
// (SIGINT/SIGTERM) is received or an unrecoverable error occurs.
// Returns an exit code suitable for main() to return directly.
int runLoop(const Config& cfg);

} // namespace saf
