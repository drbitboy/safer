// outbound_store.cpp
#include "outbound_store.hpp"
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace saf {

namespace {

std::string nowUtcIso8601() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Small RAII wrapper around sqlite3_stmt to guarantee finalize() on
// every exit path, including exceptions.
struct StmtGuard {
    sqlite3_stmt* stmt = nullptr;
    ~StmtGuard() { if (stmt) sqlite3_finalize(stmt); }
};

void bindTextOrNull(sqlite3_stmt* stmt, int idx, const std::optional<std::string>& v) {
    if (v.has_value()) {
        sqlite3_bind_text(stmt, idx, v->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, idx);
    }
}

std::optional<std::string> columnTextOrNull(sqlite3_stmt* stmt, int idx) {
    if (sqlite3_column_type(stmt, idx) == SQLITE_NULL) return std::nullopt;
    const unsigned char* txt = sqlite3_column_text(stmt, idx);
    return std::string(reinterpret_cast<const char*>(txt));
}

} // namespace

OutboundStore::OutboundStore(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string msg = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("OutboundStore: failed to open DB: " + msg);
    }

    // WAL mode: cheap default per design-doc decision #3, even though
    // full concurrency handling between writer (1a) and reader/deleter
    // (3a) threads is explicitly deferred.
    execOrThrow("PRAGMA journal_mode=WAL;");
    execOrThrow("PRAGMA synchronous=NORMAL;");

    execOrThrow(R"SQL(
        CREATE TABLE IF NOT EXISTS outbound (
            device      TEXT NOT NULL,
            property    TEXT NOT NULL,
            element     TEXT NOT NULL,
            vec_type    TEXT NOT NULL,
            value       TEXT NOT NULL,
            vec_state   TEXT,
            vec_perm    TEXT,
            vec_timeout TEXT,
            vec_ts      TEXT,
            vec_label   TEXT,
            vec_group   TEXT,
            elem_label  TEXT,
            updated_at  TEXT NOT NULL,
            PRIMARY KEY (device, property, element)
        );
    )SQL");

    // Index to make "oldest pending first" draining efficient once the
    // table has thousands of rows.
    execOrThrow(
        "CREATE INDEX IF NOT EXISTS idx_outbound_updated_at "
        "ON outbound(updated_at);"
    );
}

OutboundStore::~OutboundStore() {
    if (db_) sqlite3_close(db_);
}

void OutboundStore::execOrThrow(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = errmsg ? errmsg : "unknown sqlite error";
        sqlite3_free(errmsg);
        throw std::runtime_error("OutboundStore: exec failed: " + msg +
                                  " (sql: " + sql + ")");
    }
}

void OutboundStore::upsert(const OutboundElement& el) {
    static const char* sql = R"SQL(
        INSERT INTO outbound
            (device, property, element, vec_type, value,
             vec_state, vec_perm, vec_timeout, vec_ts,
             vec_label, vec_group, elem_label, updated_at)
        VALUES (?,?,?,?,?, ?,?,?,?, ?,?,?, ?)
        ON CONFLICT(device, property, element) DO UPDATE SET
            vec_type    = excluded.vec_type,
            value       = excluded.value,
            vec_state   = excluded.vec_state,
            vec_perm    = excluded.vec_perm,
            vec_timeout = excluded.vec_timeout,
            vec_ts      = excluded.vec_ts,
            vec_label   = excluded.vec_label,
            vec_group   = excluded.vec_group,
            elem_label  = excluded.elem_label,
            updated_at  = excluded.updated_at;
    )SQL";

    StmtGuard g;
    if (sqlite3_prepare_v2(db_, sql, -1, &g.stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("OutboundStore::upsert prepare failed: ") +
                                  sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(g.stmt, 1, el.device.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(g.stmt, 2, el.property.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(g.stmt, 3, el.element.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(g.stmt, 4, el.vec_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(g.stmt, 5, el.value.c_str(), -1, SQLITE_TRANSIENT);
    bindTextOrNull(g.stmt, 6, el.vec_state);
    bindTextOrNull(g.stmt, 7, el.vec_perm);
    bindTextOrNull(g.stmt, 8, el.vec_timeout);
    bindTextOrNull(g.stmt, 9, el.vec_ts);
    bindTextOrNull(g.stmt, 10, el.vec_label);
    bindTextOrNull(g.stmt, 11, el.vec_group);
    bindTextOrNull(g.stmt, 12, el.elem_label);
    std::string ts = nowUtcIso8601();
    sqlite3_bind_text(g.stmt, 13, ts.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(g.stmt) != SQLITE_DONE) {
        throw std::runtime_error(std::string("OutboundStore::upsert step failed: ") +
                                  sqlite3_errmsg(db_));
    }
}

std::vector<OutboundElement> OutboundStore::peekPending(int limit) {
    static const char* sql = R"SQL(
        SELECT device, property, element, vec_type, value,
               vec_state, vec_perm, vec_timeout, vec_ts,
               vec_label, vec_group, elem_label
        FROM outbound
        ORDER BY updated_at ASC
        LIMIT ?;
    )SQL";

    StmtGuard g;
    if (sqlite3_prepare_v2(db_, sql, -1, &g.stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("OutboundStore::peekPending prepare failed: ") +
                                  sqlite3_errmsg(db_));
    }
    sqlite3_bind_int(g.stmt, 1, limit);

    std::vector<OutboundElement> out;
    while (sqlite3_step(g.stmt) == SQLITE_ROW) {
        OutboundElement el;
        el.device   = reinterpret_cast<const char*>(sqlite3_column_text(g.stmt, 0));
        el.property = reinterpret_cast<const char*>(sqlite3_column_text(g.stmt, 1));
        el.element  = reinterpret_cast<const char*>(sqlite3_column_text(g.stmt, 2));
        el.vec_type = reinterpret_cast<const char*>(sqlite3_column_text(g.stmt, 3));
        el.value    = reinterpret_cast<const char*>(sqlite3_column_text(g.stmt, 4));
        el.vec_state   = columnTextOrNull(g.stmt, 5);
        el.vec_perm    = columnTextOrNull(g.stmt, 6);
        el.vec_timeout = columnTextOrNull(g.stmt, 7);
        el.vec_ts      = columnTextOrNull(g.stmt, 8);
        el.vec_label   = columnTextOrNull(g.stmt, 9);
        el.vec_group   = columnTextOrNull(g.stmt, 10);
        el.elem_label  = columnTextOrNull(g.stmt, 11);
        out.push_back(std::move(el));
    }
    return out;
}

void OutboundStore::erase(const std::string& device,
                           const std::string& property,
                           const std::string& element) {
    static const char* sql =
        "DELETE FROM outbound WHERE device=? AND property=? AND element=?;";
    StmtGuard g;
    if (sqlite3_prepare_v2(db_, sql, -1, &g.stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("OutboundStore::erase prepare failed: ") +
                                  sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(g.stmt, 1, device.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(g.stmt, 2, property.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(g.stmt, 3, element.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(g.stmt) != SQLITE_DONE) {
        throw std::runtime_error(std::string("OutboundStore::erase step failed: ") +
                                  sqlite3_errmsg(db_));
    }
}

long long OutboundStore::pendingCount() {
    static const char* sql = "SELECT COUNT(*) FROM outbound;";
    StmtGuard g;
    if (sqlite3_prepare_v2(db_, sql, -1, &g.stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("OutboundStore::pendingCount prepare failed: ") +
                                  sqlite3_errmsg(db_));
    }
    long long count = 0;
    if (sqlite3_step(g.stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(g.stmt, 0);
    }
    return count;
}

} // namespace saf
