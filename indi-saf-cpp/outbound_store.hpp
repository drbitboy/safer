// outbound_store.hpp
//
// File-backed outbound mailbox for the INDI store-and-forward bridge.
// Replaces the earlier SQLite-backed version (see git history / the
// `develop` branch) with plain files in a directory. No SQLite
// dependency; no external library beyond <filesystem>.
//
// One file per (msg_type, device, property, element) — latest write
// wins by filename collision:
//
//   outbound/<msg_type>__<device>__<property>__<element>.init    (write in progress)
//   outbound/<msg_type>__<device>__<property>__<element>.ready   (durable, ready to send)
//
// where <msg_type> is the INDI message-type prefix the value arrived
// as ("def" | "set" | "new").
//
// Write path (upsert): content is written to the `.init` file, then
// rename()'d to `.ready`. rename() on the same filesystem is atomic
// on POSIX and silently replaces any existing destination file, so:
//   - a reader globbing for `*.ready` never observes a half-written
//     file (it only ever sees the old `.ready`, if any, or the
//     complete new one);
//   - writing the SAME (msg_type, device, property, element) key
//     again before it's been sent overwrites the previous pending
//     file in place -- this is what gives "latest value wins"
//     semantics without any explicit delete-then-write step, exactly
//     mirroring what SQLite's ON CONFLICT DO UPDATE did.
//
// Note this key includes msg_type: a pending "def" and a pending
// "set" for the same (device, property, element) are two different
// files, not one -- unlike the SQLite version, whose primary key was
// (device, property, element) alone. Accepted tradeoff per design
// discussion: this store does NOT collapse across different msg_types
// for the same element; only same-key repeats collapse.
//
// Read/send path (3a): globs the directory for `*.ready` files,
// oldest (by file mtime) first, reads+parses each into an
// OutboundElement, and the caller (3a's drain loop) deletes the file
// directly by path once the link has accepted the send. See
// example_3a_loop.cpp.

#pragma once
#include <string>
#include <vector>
#include <optional>

namespace saf {

struct OutboundElement {
    std::string msg_type;      // "def" | "set" | "new"
    std::string device;
    std::string property;
    std::string element;
    std::string vec_type;      // "Text" | "Number" | "Switch" | "Light"
    std::string value;

    std::optional<std::string> vec_state;
    std::optional<std::string> vec_perm;
    std::optional<std::string> vec_timeout;
    std::optional<std::string> vec_ts;
    std::optional<std::string> vec_label;
    std::optional<std::string> vec_group;
    std::optional<std::string> elem_label;
};

// One pending file, paired with its parsed content. peekPending()
// returns these; the caller passes .filepath back to erase() once the
// send for that record has succeeded.
struct PendingRecord {
    std::string filepath;
    OutboundElement el;
};

class OutboundStore {
public:
    // Ensures `directory` exists (creates it if necessary). No schema,
    // no open handle to hold -- every operation just touches files in
    // this directory.
    explicit OutboundStore(const std::string& directory);

    OutboundStore(const OutboundStore&) = delete;
    OutboundStore& operator=(const OutboundStore&) = delete;

    // Latest-value-wins write for this exact (msg_type, device,
    // property, element) key: writes to `<key>.init`, then renames to
    // `<key>.ready`, overwriting any previous pending file for the
    // same key. Throws std::runtime_error if any of msg_type, device,
    // property, element contains '/' or a null byte (would corrupt
    // the filename) -- see wire_format::checkSafe, which now also
    // guards against '/' for this reason.
    void upsert(const OutboundElement& el);

    // Globs the directory for `*.ready` files, sorted oldest-first by
    // file modification time, and returns up to `limit` of them
    // parsed into OutboundElement. Does NOT delete anything -- the
    // caller (3a) is responsible for calling erase() per file once
    // the corresponding send has succeeded.
    std::vector<PendingRecord> peekPending(int limit = 100);

    // Deletes one pending file by the exact path returned from
    // peekPending(). Safe to call on a path that no longer exists
    // (e.g. concurrently removed) -- this is a no-op in that case,
    // not an error, since the desired end state (file gone) already
    // holds.
    void erase(const std::string& filepath);

    // Convenience: how many `*.ready` files are currently pending.
    // Useful for logging/telemetry on link-up/link-down transitions.
    long long pendingCount();

private:
    std::string dir_;
};

} // namespace saf
