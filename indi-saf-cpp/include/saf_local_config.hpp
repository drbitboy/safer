// saf_local_config.hpp
//
// Config struct and command-line parsing for saf_local.
// Deliberately has NO dependency on liblilxml or mailbox.hpp/
// indi_xml_bridge.hpp -- everything in this file is pure string/int
// parsing, so it can be compiled and linked (and tested) completely
// independently of the actual liblilxml library, which isn't
// available to build against in every environment. See
// saf_local.hpp for the actual runtime loop that DOES need liblilxml.

#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace saf {

struct Config {
    // All fields now have built-in defaults (see below); nothing is
    // strictly required to run saf_local anymore. validateConfig()
    // is kept as a safety net in case a future field needs to be
    // genuinely required, or someone explicitly sets one of these
    // empty/zero on the command line.
    std::string inboundDir = "./inbound";
    std::string outboundDir = "./outbound";
    uint16_t indiserverPort = 7624; // INDI's conventional default port

    // Instance name: the prefix of every log line ("<name>: ..."), so
    // several saf_local instances writing to one terminal or log can be
    // told apart.
    std::string name = "saf_local";

    // Directories for the zip archives, kept separate from the mailbox
    // directories so the mailbox code never sees them. Empty (the
    // default) means "<outboundDir>/link" and "<inboundDir>/link"; use
    // the *Resolved() accessors rather than reading these directly.
    //   outbound link dir: init.zip / sending.zip built by saf_local's
    //                      outbound archiver for the link sender
    //   inbound link dir:  sending.zip delivered by the link sender,
    //                      unpacked by saf_local into inboundDir
    std::string outboundLinkDir;
    std::string inboundLinkDir;
    std::string outboundLinkDirResolved() const {
        return outboundLinkDir.empty() ? outboundDir + "/link" : outboundLinkDir;
    }
    std::string inboundLinkDirResolved() const {
        return inboundLinkDir.empty() ? inboundDir + "/link" : inboundLinkDir;
    }

    // Optional, with defaults.
    std::string indiserverHost = "127.0.0.1"; // "local" indiserver
    unsigned long long loopPauseNs = 0; // pause after each lap, nanoseconds
                                  // (-p/--loopPause), as in MagAOXApp;
                                  // (D)'s select() timeout is fixed at 100 ms
    int mailboxLimit = 100;      // max records drained per (C.2) lap,
                                  // passed through to
                                  // FileMailbox::peekPending()
    int reconnectDelayMs = 1000; // wait between indiserver reconnect
                                  // attempts after a lost connection
    int verbose = 0;             // log level: 0 off, 1 (-v), 2 (-vv);
                                  // see saf_log.hpp

    // Not exposed as a parameter: SIGINT/SIGTERM handling for clean
    // shutdown is always on (see saf_local.cpp) -- there's no
    // sensible reason to make graceful shutdown itself optional.
};

struct ParseResult {
    bool ok = false;
    bool helpRequested = false; // -h/--help was given; caller should
                                 // print usage and exit 0, not treat
                                 // this as an error
    std::string error;          // set when ok is false and
                                 // helpRequested is false
};

// Parses argv; every flag actually given overwrites the corresponding
// field, and cfg keeps whatever built-in default it already held for
// anything not given. There is no config file (the MagAOXApp
// framework supplies one when this becomes a MagAOXApp).
ParseResult parseArgs(int argc, char** argv, Config& cfg);

// Checks that name, inboundDir, outboundDir, and indiserverPort are all
// set (non-empty / non-zero). All have built-in defaults
// (see Config above), so this only fails if something explicitly set
// one of them to empty/0 -- not a
// normal case, but still checked rather than silently passing an
// empty directory or port 0 through to runLoop().
bool validateConfig(const Config& cfg, std::string& errOut);

// Prints usage/help text to the given stream.
void printUsage(const char* argv0, FILE* out);

} // namespace saf
