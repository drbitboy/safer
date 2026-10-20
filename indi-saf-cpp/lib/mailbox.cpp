// mailbox.cpp
#include "mailbox.hpp"
#include "wire_format.hpp"
#include "saf_log.hpp"
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

namespace saf {

namespace {
namespace fs = std::filesystem;

// Filenames are "<state>.<msg_type>.<device>.<property>.<element>"
// where <state> is one of init / ready / sending (a PREFIX, not a
// suffix). Fields never contain '.', so the separator is unambiguous.
const std::string kInit    = "init.";
const std::string kReady   = "ready.";
const std::string kSending = "sending.";

bool hasPrefix(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() &&
           s.compare(0, prefix.size(), prefix) == 0;
}

// Filename-safety check, separate from wire_format::checkSafe's
// delimiter check: these four fields become directory-entry names, so
// '/' (path separator) and '\0' would corrupt or truncate the path.
// '.' is also rejected here because '.' is the field separator in the
// filename (and also delimits the leading state prefix).
void checkFilenameSafe(const std::string& field, const char* fieldName) {
    if (field.find('/') != std::string::npos ||
        field.find('\0') != std::string::npos ||
        field.find('.') != std::string::npos) {
        throw std::runtime_error(
            std::string("FileMailbox: field '") + fieldName +
            "' contains '/', '.', or a null byte, unsafe for a filename: " + field);
    }
}

std::string keyBasename(const MailboxElement& el) {
    checkFilenameSafe(el.msg_type, "msg_type");
    checkFilenameSafe(el.device,   "device");
    checkFilenameSafe(el.property, "property");
    checkFilenameSafe(el.element,  "element");
    return el.msg_type + "." + el.device + "." + el.property + "." + el.element;
}

void writeFileOrThrow(const fs::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("FileMailbox: failed to open for write: " + path.string());
    }
    out << content;
    out.flush();
    if (!out) {
        throw std::runtime_error("FileMailbox: write failed: " + path.string());
    }
}

std::string readFileOrThrow(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("FileMailbox: failed to open for read: " + path.string());
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

FileMailbox::FileMailbox(const std::string& directory) : dir_(directory) {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec) {
        throw std::runtime_error("FileMailbox: failed to create directory '" +
                                  dir_ + "': " + ec.message());
    }
}

void FileMailbox::upsert(const MailboxElement& el) {
    const std::string base = keyBasename(el);
    const fs::path initPath  = fs::path(dir_) / (kInit + base);
    const fs::path readyPath = fs::path(dir_) / (kReady + base);

    // Content is the full record, via the same wire format used on
    // the link -- this file IS effectively what would have been read
    // back out of the old SQLite row.
    const std::string content = encodeWireMessage(el);

    writeFileOrThrow(initPath, content);
    logAt(1, "wrote file " + initPath.filename().string());

    // Atomic on POSIX; silently replaces readyPath if it already
    // exists, which is exactly the "latest value for this key wins"
    // behavior upsert() needs -- no separate delete step required.
    std::error_code ec;
    fs::rename(initPath, readyPath, ec);
    if (ec) {
        throw std::runtime_error("FileMailbox: rename '" + initPath.string() +
                                  "' -> '" + readyPath.string() + "' failed: " + ec.message());
    }
    logAt(2, "renamed \"" + initPath.filename().string() + "\" to \"" +
                 readyPath.filename().string() + "\"");
}

std::vector<PendingRecord> FileMailbox::peekPending(int limit) {
    // Phase 1: claim. Rename every ready.* to sending.*. This is what
    // makes erase() safe -- see class comment in mailbox.hpp. A
    // rename() here is atomic and, if a sending.* already exists for
    // the same key (a leftover from a crash before it was erased),
    // silently overwrites it: the leftover was stale and superseded
    // by this newer value, so discarding it is correct latest-value-
    // wins behavior, not data loss.
    for (const auto& dirEntry : fs::directory_iterator(dir_)) {
        if (!dirEntry.is_regular_file()) continue;
        const std::string fname = dirEntry.path().filename().string();
        if (!hasPrefix(fname, kReady)) continue;

        fs::path sendingPath = dirEntry.path();
        sendingPath.replace_filename(
            kSending + fname.substr(kReady.size()));

        std::error_code ec;
        fs::rename(dirEntry.path(), sendingPath, ec);
        if (!ec) {
            logAt(2, "renamed \"" + fname + "\" to \"" +
                         sendingPath.filename().string() + "\"");
        }
        // Ignore failure here (e.g. removed concurrently by another
        // process in a multi-reader setup, which this design doesn't
        // otherwise assume) -- just leaves it for a future pass.
    }

    // Phase 2: glob sending.* independently of phase 1 above -- this
    // picks up BOTH what was just claimed AND any leftover sending.*
    // file from a previous crash that phase 1 didn't touch (because
    // no fresh ready.* existed for that key). See mailbox.hpp for the
    // two-case argument this relies on.
    struct Entry {
        fs::path path;
        fs::file_time_type mtime;
    };
    std::vector<Entry> entries;

    for (const auto& dirEntry : fs::directory_iterator(dir_)) {
        if (!dirEntry.is_regular_file()) continue;
        if (!hasPrefix(dirEntry.path().filename().string(), kSending)) continue;
        std::error_code ec;
        auto mtime = fs::last_write_time(dirEntry.path(), ec);
        if (ec) continue; // file may have been removed concurrently; skip
        entries.push_back({dirEntry.path(), mtime});
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });

    std::vector<PendingRecord> out;
    out.reserve(static_cast<size_t>(std::min<size_t>(entries.size(), static_cast<size_t>(limit))));
    for (const auto& entry : entries) {
        if (static_cast<int>(out.size()) >= limit) break;
        std::string content;
        try {
            content = readFileOrThrow(entry.path);
        } catch (const std::exception&) {
            // Removed/unreadable between listing and read -- skip,
            // it'll simply not appear in this pass.
            continue;
        }
        PendingRecord rec;
        rec.filepath = entry.path.string();
        try {
            rec.el = decodeWireMessage(content);
        } catch (const std::exception&) {
            // Malformed pending file -- skip rather than crash the
            // drain loop; leaves the bad file in place for manual
            // inspection.
            continue;
        }
        out.push_back(std::move(rec));
    }
    return out;
}

void FileMailbox::erase(const std::string& filepath) {
    std::error_code ec;
    if (fs::remove(filepath, ec)) {
        logAt(2, "deleted \"" + fs::path(filepath).filename().string() + "\"");
    }
    // Safe unconditional delete-by-path: filepath always points at a
    // sending.* file here (see peekPending()'s phase 1), and nothing
    // other than peekPending()'s own claim sweep ever creates or
    // touches a sending.* file. So unlike deleting a ready.* file
    // directly, there's no reader/writer race window on this path --
    // upsert() only ever writes init.* and ready.*, never sending.*.
    //
    // Not throwing on failure: if the file is already gone (e.g.
    // removed concurrently, or never existed), the desired end state
    // -- file absent -- already holds. Genuine permission errors etc.
    // are silently ignored here too; callers that need to detect
    // those can check fs::exists(filepath) themselves.
}

long long FileMailbox::pendingCount() {
    long long count = 0;
    for (const auto& dirEntry : fs::directory_iterator(dir_)) {
        if (!dirEntry.is_regular_file()) continue;
        const std::string fname = dirEntry.path().filename().string();
        if (hasPrefix(fname, kReady) || hasPrefix(fname, kSending)) {
            ++count;
        }
    }
    return count;
}

} // namespace saf
