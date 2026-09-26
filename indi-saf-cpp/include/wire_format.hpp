// wire_format.hpp
//
// Compact-ish TEXT wire format for a single (device, property, element)
// change crossing the link, AND now (on the noSQL branch) doubling as
// the on-disk content of each mailbox.hpp pending file --
// see that header for the file-naming scheme this pairs with.
// Deliberately isolated in its own function/translation-unit so it
// can be swapped for a genuinely compact (e.g. binary/varint-keyed)
// encoding later without touching 3a, 1b, or FileMailbox. Nothing
// outside this file should know or assume anything about the
// on-the-wire byte layout.
//
// Current format: one line per element change, pipe-delimited, in a
// fixed field order. Fields that are optional are emitted empty (but
// the pipe separator is always present, so field count/order is
// stable for parsing).
//
//   msg_type|device|property|element|vec_type|value|vec_state|vec_perm|
//   vec_timeout|vec_ts|vec_label|vec_group|elem_label
//
// This is intentionally NOT full INDI XML -- that verbosity is exactly
// what decomposition + this compact format are avoiding on the
// bandwidth-constrained link. 1b is responsible for turning this back
// into valid INDI vector XML for the local indiserver.

#pragma once
#include <string>
#include "mailbox.hpp"

namespace saf {

// Serializes one MailboxElement to the current wire format.
// Throws std::runtime_error if any field contains the delimiter ('|')
// or a newline, since the current line-oriented format can't escape
// those -- callers (1a) should sanitize/reject such values at
// decompose time rather than relying on this function to sanitize.
std::string encodeWireMessage(const MailboxElement& el);

// Parses one wire-format line back into an MailboxElement-shaped
// struct. Used by 1b when reading a spool file. Throws
// std::runtime_error on malformed input (wrong field count).
MailboxElement decodeWireMessage(const std::string& line);

} // namespace saf
