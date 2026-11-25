// saf_local.hpp
//
// The program that runs 1a and 1b as a single thread, per the design
// worked out in conversation:
//
//   loop:
//     (A)   unpack <inbound-link-dir>/sending.zip (delivered by the link
//           sender) into the inbound mailbox, replacing any older
//           ready.* files, BEFORE (C) sends them; runs every lap
//     (B)   if the link is up, archive the outbound mailbox's pending
//           files into <outbound-link-dir>/sending.zip for the separate link
//           sender process (see outbound_archiver.hpp); runs every lap,
//           independent of the indiserver connection, and is a cheap
//           no-op while an archive is already in flight
//     (C)   check the inbound mailbox for pending records
//     (C.1) none pending -> go to (D)
//     (C.2) pending -> recompose + send each to indiserver over the
//           socket, erase each sending.* file once fully sent; a
//           partial/EAGAIN write on the socket stops draining for
//           this lap (leaves the rest for next time) rather than
//           busy-looping on it
//     (D)   select() on the indiserver socket's read fd, with a
//           timeout -- this is what paces the loop; there's no sleep()
//     (D.1) timeout, nothing readable -> go to (A)
//     (D.2) readable -> recv(), feed bytes into the persistent LilXML
//           parser, decompose any complete vector messages that
//           result, upsert each into the outbound mailbox -> go to (A)
//
// No select() on the write side: write-readiness is essentially always
// true and isn't the actual gating condition -- whether the inbound
// mailbox has anything pending is what matters, and that's a plain
// directory check in (C), not something select() can watch directly
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
// its own -- safer_init()/safer_pass()/safer_shutdown() below are the only things in this file, and it's
// the only thing that actually needs liblilxml, mailbox.hpp, and
// indi_xml_bridge.hpp.

#pragma once
#include "saf_local_config.hpp"

namespace saf {

// main() is just:
//     safer_init(argc, argv, cfg);
//     while (!safer_done()) safer_pass(cfg);
//     return safer_shutdown();

// Parses and validates the command line, then sets up the loop (signal
// handlers, logging, mailboxes, XML parser). On -h, a bad command line or
// a setup failure it records the exit code and requests shutdown
// so the loop never runs.
void safer_init(int argc, char** argv, Config& cfg);

// One lap of the loop described above (steps A-D).
void safer_pass(const Config& cfg);

// True once a shutdown was requested (SIGINT/SIGTERM, -h, or a startup
// failure); the loop in main() runs while this is false.
bool safer_done();

// Everything after the loop: closes the connection, frees the parser.
// Returns the exit code for main() to return.
int safer_shutdown();

} // namespace saf
