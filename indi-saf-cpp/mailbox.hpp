// mailbox.hpp
//
// File-backed mailbox for the INDI store-and-forward bridge. Used on
// BOTH sides now: one instance for the outbound direction (1a's
// writer, 3a's reader/deleter) and a second, separate instance for
// the inbound direction (whatever receives off the link is the
// writer, 1b is the reader/deleter). Nothing in this class is
// direction-specific -- it's just a keyed store of pending records on
// disk. (Formerly `OutboundStore` in outbound_store.hpp/.cpp, back
// when it was SQLite-backed and outbound-only; renamed on the noSQL
// branch once the same file scheme turned out to fit the inbound side
// too. See git history for the SQLite version.)
//
// One file per (msg_type, device, property, element) — latest write
// wins by filename collision:
//
//   <dir>/<msg_type>__<device>__<property>__<element>.init    (write in progress)
//   <dir>/<msg_type>__<device>__<property>__<element>.ready   (durable, ready to read)
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
//     again before it's been consumed overwrites the previous pending
//     file in place -- this is what gives "latest value wins"
//     semantics without any explicit delete-then-write step, exactly
//     mirroring what SQLite's ON CONFLICT DO UPDATE did on the
//     outbound-only, SQLite-backed version of this class.
//
// Note this key includes msg_type: a pending "def" and a pending
// "set" for the same (device, property, element) are two different
// files, not one -- unlike the SQLite version, whose primary key was
// (device, property, element) alone. Confirmed design choice: this
// store does NOT collapse across different msg_types for the same
// element; only same-key repeats collapse.
//
// On the inbound side specifically, this also means: if two DIFFERENT
// keys (e.g. two different elements, or the same element under two
// different msg_types) both arrive before the reader drains them,
// BOTH are applied when drained -- there is no dropped/lost update
// across distinct keys. Only a same-key repeat drops its predecessor
// (the whole point of latest-value-wins). Cross-key delivery ORDER is
// not preserved (readers see `*.ready` files sorted by file mtime,
// which is a reasonable approximation of write order but not a
// guarantee) -- confirmed not a concern for this bridge.
//
// Read/consume path (peekPending): a two-phase claim-then-glob scheme
// that makes erase() race-free without any locking or timeout/reaper
// logic. Each call:
//
//   1. Claim: renames every current `*.ready` to `*.sending`. rename()
//      is atomic, so this is a safe "take ownership" step -- once a
//      file is `*.sending`, upsert() will never touch it again
//      (upsert() only ever creates `*.init`/`*.ready`). If a
//      `*.sending` already exists for that key -- a leftover from a
//      previous run that crashed before erase() ran -- the rename
//      silently overwrites it, which is correct: the leftover was
//      stale, superseded by this newer value, so discarding it is
//      latest-value-wins doing its job, not data loss.
//   2. Glob: lists `*.sending` (independently of what step 1 just
//      claimed), oldest-by-mtime first, reads+parses up to `limit`
//      into MailboxElement, returns each as a PendingRecord.
//
// The caller deletes the returned filepath directly via erase() once
// the record has been fully handled (sent to the link, for outbound;
// recomposed and sent to the local indiserver, for inbound). Because
// erase() only ever operates on a `*.sending` path -- which nothing
// but this class's own claim step ever creates -- there is no
// read-then-delete race with a concurrent upsert() the way there
// would be if erase() deleted a `*.ready` path directly (an earlier,
// pre-claim design on this branch had exactly that race: a writer's
// upsert() landing between a read and the matching erase() would get
// silently deleted before ever being sent).
//
// Crash recovery falls out of this for free, no reaper needed: a
// `*.sending` file left behind by a reader that died mid-handling is
// picked up again by the very next peekPending() call, in one of two
// ways -- either no newer `*.ready` exists for that key, and step 2
// finds the leftover `*.sending` untouched and retries it; or a
// legitimately newer `*.ready` DOES exist, and step 1 overwrites the
// leftover with it, which (per the paragraph above) is correct
// behavior, not a bug. See example_3a_loop.cpp (outbound) and
// example_1b_loop.cpp (inbound) for how callers use this.

#pragma once
#include <string>
#include <vector>
#include <optional>

namespace saf {

struct MailboxElement {
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
// record has been fully handled.
struct PendingRecord {
    std::string filepath;
    MailboxElement el;
};

class FileMailbox {
public:
    // Ensures `directory` exists (creates it if necessary). No schema,
    // no open handle to hold -- every operation just touches files in
    // this directory. Construct one instance per direction (outbound
    // dir, inbound dir) -- see class comment.
    explicit FileMailbox(const std::string& directory);

    FileMailbox(const FileMailbox&) = delete;
    FileMailbox& operator=(const FileMailbox&) = delete;

    // Latest-value-wins write for this exact (msg_type, device,
    // property, element) key: writes to `<key>.init`, then renames to
    // `<key>.ready`, overwriting any previous pending file for the
    // same key. Throws std::runtime_error if any of msg_type, device,
    // property, element contains '/', '.', or a null byte (would
    // corrupt the filename) -- see wire_format::checkSafe, which also
    // guards '/' for this reason.
    void upsert(const MailboxElement& el);

    // Two-phase claim-then-glob: first renames every current
    // `*.ready` to `*.sending` (atomic; silently supersedes any
    // leftover `*.sending` for the same key from a prior crash), then
    // globs `*.sending`, sorted oldest-first by file modification
    // time, and returns up to `limit` of them parsed into
    // MailboxElement. Does NOT delete anything -- the caller is
    // responsible for calling erase() per file once the record has
    // been fully handled. See the class comment above for why this
    // two-phase scheme (rather than reading `*.ready` directly) is
    // what makes erase() race-free.
    std::vector<PendingRecord> peekPending(int limit = 100);

    // Deletes one pending file by the exact path returned from
    // peekPending() (always a `*.sending` path). Safe to call on a
    // path that no longer exists (e.g. concurrently removed) -- this
    // is a no-op in that case, not an error, since the desired end
    // state (file gone) already holds. Safe with respect to a
    // concurrent upsert() too: upsert() never creates or touches a
    // `*.sending` file, only `*.init`/`*.ready`, so there is no
    // read-then-delete race on the path this deletes.
    void erase(const std::string& filepath);

    // Convenience: how many pending records currently exist, counting
    // both `*.ready` (not yet claimed) and `*.sending` (claimed,
    // in-flight or awaiting retry). Useful for logging/telemetry on
    // link-up/link-down transitions.
    long long pendingCount();

private:
    std::string dir_;
};

} // namespace saf
