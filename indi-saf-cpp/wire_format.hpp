// wire_format.hpp
//
// Compact-ish TEXT wire format for a single (device, property, element)
// change crossing the link. Deliberately isolated in its own
// function/translation-unit so it can be swapped for a genuinely compact
// (e.g. binary/varint-keyed) encoding later without touching 3a, 1b, or
// the SQLite schema. Nothing outside this file should know or assume
// anything about the on-the-wire byte layout.
//
// Current format: one line per element change, pipe-delimited, in a
// fixed field order. Fields that are optional in the DB are emitted
// empty (but the pipe separator is always present, so field count/order
// is stable for parsing).
//
//   device|property|element|vec_type|value|vec_state|vec_perm|
//   vec_timeout|vec_ts|vec_label|vec_group|elem_label
//
// This is intentionally NOT full INDI XML -- that verbosity is exactly
// what decomposition + this compact format are avoiding on the
// bandwidth-constrained link. 1b is responsible for turning this back
// into valid INDI vector XML for the local indiserver.

#pragma once
#include <string>
#include "outbound_store.hpp"

namespace ssf {

// Serializes one OutboundElement to the current wire format.
// Throws std::runtime_error if any field contains the delimiter ('|')
// or a newline, since the current line-oriented format can't escape
// those -- callers (1a) should sanitize/reject such values at
// decompose time rather than relying on this function to sanitize.
std::string encodeWireMessage(const OutboundElement& el);

// Parses one wire-format line back into an OutboundElement-shaped
// struct. Used by 1b when reading a spool file. Throws
// std::runtime_error on malformed input (wrong field count).
OutboundElement decodeWireMessage(const std::string& line);

} // namespace ssf
