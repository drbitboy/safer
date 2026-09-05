// mailbox.cpp
#include "mailbox.hpp"
#include "wire_format.hpp"
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdio>

namespace saf {

namespace {
namespace fs = std::filesystem;

bool hasSuffix(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Filename-safety check, separate from wire_format::checkSafe's
// delimiter check: these four fields become directory-entry names, so
// '/' (path separator) and '\0' would corrupt or truncate the path.
// '.' is also rejected here even though the suffix check above is
// dot-position-independent, just to keep filenames unambiguous to a
// human glancing at the directory listing.
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
    return el.msg_type + "__" + el.device + "__" + el.property + "__" + el.element;
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
    const fs::path initPath  = fs::path(dir_) / (base + ".init");
    const fs::path readyPath = fs::path(dir_) / (base + ".ready");

    // Content is the full record, via the same wire format used on
    // the link -- this file IS effectively what would have been read
    // back out of the old SQLite row.
    const std::string content = encodeWireMessage(el);

    writeFileOrThrow(initPath, content);

    // Atomic on POSIX; silently replaces readyPath if it already
    // exists, which is exactly the "latest value for this key wins"
    // behavior upsert() needs -- no separate delete step required.
    std::error_code ec;
    fs::rename(initPath, readyPath, ec);
    if (ec) {
        throw std::runtime_error("FileMailbox: rename '" + initPath.string() +
                                  "' -> '" + readyPath.string() + "' failed: " + ec.message());
    }
}

std::vector<PendingRecord> FileMailbox::peekPending(int limit) {
    struct Entry {
        fs::path path;
        fs::file_time_type mtime;
    };
    std::vector<Entry> entries;

    for (const auto& dirEntry : fs::directory_iterator(dir_)) {
        if (!dirEntry.is_regular_file()) continue;
        if (!hasSuffix(dirEntry.path().filename().string(), ".ready")) continue;
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
    fs::remove(filepath, ec);
    // Deliberately not throwing on failure: if the file is already
    // gone (e.g. removed concurrently, or never existed), the desired
    // end state -- file absent -- already holds. Genuine permission
    // errors etc. are silently ignored here too; callers that need to
    // detect those can check fs::exists(filepath) themselves.
}

long long FileMailbox::pendingCount() {
    long long count = 0;
    for (const auto& dirEntry : fs::directory_iterator(dir_)) {
        if (dirEntry.is_regular_file() &&
            hasSuffix(dirEntry.path().filename().string(), ".ready")) {
            ++count;
        }
    }
    return count;
}

} // namespace saf
