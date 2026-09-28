// saf_local_config.hpp
//
// Config struct and all argument/INI-file parsing for saf_local.
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
    // empty/zero via a config file.
    std::string inboundDir = "./inbound";
    std::string outboundDir = "./outbound";
    uint16_t indiserverPort = 7624; // INDI's conventional default port

    // Optional, with defaults.
    std::string indiserverHost = "127.0.0.1"; // "local" indiserver
    int selectTimeoutMs = 250;   // (B)'s timeout; also the loop's
                                  // effective pacing when idle
    int mailboxLimit = 100;      // max records drained per (A.2) lap,
                                  // passed through to
                                  // FileMailbox::peekPending()
    int reconnectDelayMs = 1000; // wait between indiserver reconnect
                                  // attempts after a lost connection
    bool verbose = false;        // diagnostic logging to stderr

    // Not exposed as a parameter: SIGINT/SIGTERM handling for clean
    // shutdown is always on (see saf_local.cpp) -- there's no
    // sensible reason to make graceful shutdown itself optional.
};

// Parses an INI-format file: "key=value" lines, optional "[section]"
// headers (recognized but not filtered on -- all keys apply
// regardless of section), "#" or ";" starts a comment, blank lines
// ignored. Only overwrites fields actually present in the file, so
// cfg should already hold whatever defaults you want for anything the
// file doesn't mention. Recognized keys (case-sensitive), matching
// the long option names below with '-' replaced by '_':
//   inbound_dir, outbound_dir, indiserver_host, indiserver_port,
//   select_timeout_ms, mailbox_limit, reconnect_delay_ms, verbose
// Returns false (with errOut set) if the file can't be opened, or if
// a line is neither blank/comment/section nor a valid key=value pair,
// or if indiserver_port/*_ms/mailbox_limit fail to parse as integers,
// or if verbose isn't one of true/false/1/0/yes/no (case-insensitive).
// An unrecognized key is ignored, not an error (forward-compatible
// with a config file that has extra keys for a newer version).
bool loadIniConfig(const std::string& path, Config& cfg, std::string& errOut);

struct ParseResult {
    bool ok = false;
    bool helpRequested = false; // -h/--help was given; caller should
                                 // print usage and exit 0, not treat
                                 // this as an error
    std::string error;          // set when ok is false and
                                 // helpRequested is false
};

// Parses argv. If --config/-c is present anywhere in argv, that file
// is loaded FIRST (via loadIniConfig), regardless of its position
// relative to other flags -- then every other flag actually given on
// the command line overwrites the corresponding field, so CLI always
// wins over the config file regardless of argument order. cfg should
// hold whatever built-in defaults you want for anything neither the
// config file nor the command line sets.
ParseResult parseArgs(int argc, char** argv, Config& cfg);

// Checks that inboundDir, outboundDir, and indiserverPort are all
// set (non-empty / non-zero). All three now have built-in defaults
// (see Config above), so this only fails if something explicitly set
// one of them to empty/0 (e.g. an unusual config file) -- not a
// normal case, but still checked rather than silently passing an
// empty directory or port 0 through to runLoop().
bool validateConfig(const Config& cfg, std::string& errOut);

// Prints usage/help text to the given stream.
void printUsage(const char* argv0, FILE* out);

} // namespace saf
