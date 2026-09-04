// outbound_store.hpp
//
// SQLite-backed outbound mailbox for the INDI store-and-forward bridge.
// One row per (device, property, element) — latest value wins via UPSERT.
// Uses the raw sqlite3 C API directly (no wrapper library) so the only
// external dependency is libsqlite3.
//
// This component is used ONLY on the outbound path:
//   local indiserver -> [1a: decompose] -> UPSERT into this store
//   this store -> [3a: link-facing] -> DELETE row once link accepts send
//
// Schema:
//   CREATE TABLE IF NOT EXISTS outbound (
//       device      TEXT NOT NULL,
//       property    TEXT NOT NULL,
//       element     TEXT NOT NULL,
//       vec_type    TEXT NOT NULL,   -- "Text"|"Number"|"Switch"|"Light"
//       value       TEXT NOT NULL,   -- raw element value, always stored as text
//       vec_state   TEXT,            -- Idle|Ok|Busy|Alert (vector-level)
//       vec_perm    TEXT,            -- ro|wo|rw (vector-level, if applicable)
//       vec_timeout TEXT,            -- vector-level timeout, if present
//       vec_ts      TEXT,            -- vector-level timestamp, if present
//       vec_label   TEXT,            -- vector-level label, if present
//       vec_group   TEXT,            -- vector-level group, if present
//       elem_label  TEXT,            -- element-level label, if present
//       updated_at  TEXT NOT NULL,   -- UTC ISO8601, set on every UPSERT
//       PRIMARY KEY (device, property, element)
//   );
//
// Rationale for storing extra vec_* / elem_* columns on every element row
// (duplicated across elements of the same vector) rather than normalizing
// into a separate "vectors" table: it keeps every row self-contained, so
// 3a can read one row and have everything needed to build a wire message,
// and 1b (recomposing on the far end) never needs a join. Some redundancy
// traded for simplicity — acceptable at this scale per current design.

#pragma once
#include <string>
#include <vector>
#include <optional>
#include <sqlite3.h>

namespace saf {

struct OutboundElement {
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

class OutboundStore {
public:
    // Opens (creating if necessary) the SQLite DB at db_path and ensures
    // the schema exists. Enables WAL mode per the "cheap default" decision
    // even though full concurrency handling is deferred.
    explicit OutboundStore(const std::string& db_path);
    ~OutboundStore();

    OutboundStore(const OutboundStore&) = delete;
    OutboundStore& operator=(const OutboundStore&) = delete;

    // Latest-value-wins write. Safe to call repeatedly for the same key;
    // each call overwrites the prior row (INSERT ... ON CONFLICT DO UPDATE).
    void upsert(const OutboundElement& el);

    // Returns up to `limit` pending rows, oldest updated_at first, for 3a
    // to drain when the link is up. Does not delete anything.
    std::vector<OutboundElement> peekPending(int limit = 100);

    // Deletes one row by key. Called by 3a once the link has accepted
    // the send for that (device, property, element). See design-doc
    // decision #4 and the open question about what "accepted" really
    // guarantees once the link API is finalized.
    void erase(const std::string& device,
               const std::string& property,
               const std::string& element);

    // Convenience: how many rows are currently pending. Useful for
    // logging/telemetry on the link-up/link-down transitions.
    long long pendingCount();

private:
    sqlite3* db_ = nullptr;
    void execOrThrow(const std::string& sql);
};

} // namespace saf
