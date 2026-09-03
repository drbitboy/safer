// spool.hpp
//
// Ordered file-spool writer, used by the inbound side only (per current
// design: outbound durability is handled by SQLite, not a spool file;
// 3b was eliminated -- the link itself writes directly into the inbound
// spool directory).
//
// Ordering guarantee: filenames are UTC timestamps at 1-second
// resolution. Any single writer thread targeting one directory must
// wait at least 1.1s after writing a file before writing/naming the
// next, so that lexicographic filename order == write order within
// that directory. This class enforces the wait; callers just call
// write() repeatedly and get correct ordering for free.
//
// Each spool directory must have exactly one writer thread. Inbound
// and outbound use SEPARATE directories (design-doc decision #6), so
// no cross-thread coordination is needed between them.

#pragma once
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace ssf {

class SpoolWriter {
public:
    explicit SpoolWriter(std::string directory);

    // Writes `content` to a new, uniquely-named file in the spool
    // directory. Blocks as needed to satisfy the 1.1s minimum spacing
    // from the previous write() call on this instance. Returns the
    // full path written.
    //
    // NOT safe to call concurrently from multiple threads on the same
    // SpoolWriter instance -- one writer thread per directory is the
    // whole point of the design (see class comment).
    std::string write(const std::string& content);

private:
    std::string dir_;
    std::chrono::steady_clock::time_point lastWriteMonotonic_{};
    bool hasWrittenBefore_ = false;

    static std::string utcTimestampSeconds();
};

// Watches a spool directory and hands each file's contents to the
// supplied handler in filename (i.e. write-time) order, deleting the
// file only after the handler returns successfully. If the handler
// throws or returns false, the file is left in place for retry on the
// next poll -- this is the durability/retry mechanism for 1b when the
// local indiserver connection is down.
class SpoolReader {
public:
    explicit SpoolReader(std::string directory);

    // handler receives (filepath, content) and returns true on success
    // (file will be unlinked) or false on failure (file stays, retried
    // next call). Processes files in filename order. Returns the number
    // of files successfully processed.
    template <typename Handler>
    int pollOnce(Handler handler);

private:
    std::string dir_;
};

// Template method must be visible at the point of instantiation, hence
// defined here in the header rather than in spool.cpp.
template <typename Handler>
int SpoolReader::pollOnce(Handler handler) {
    namespace fs = std::filesystem;

    std::vector<std::string> names;
    for (const auto& entry : fs::directory_iterator(dir_)) {
        if (entry.is_regular_file()) {
            names.push_back(entry.path().filename().string());
        }
    }
    // Filename order == write order, per the UTC-timestamp + 1.1s-wait
    // scheme enforced by SpoolWriter.
    std::sort(names.begin(), names.end());

    int processed = 0;
    for (const auto& name : names) {
        fs::path full = fs::path(dir_) / name;

        std::ifstream in(full, std::ios::binary);
        if (!in) {
            // File may have been removed by a concurrent process;
            // skip rather than fail the whole poll.
            continue;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string content = ss.str();
        in.close();

        bool ok = false;
        try {
            ok = handler(full.string(), content);
        } catch (...) {
            ok = false; // leave file in place for retry
        }

        if (ok) {
            std::error_code ec;
            fs::remove(full, ec);
            // If remove fails (ec set), the file will simply be
            // reprocessed next poll -- handler must be idempotent,
            // which is naturally true here since it's just
            // re-sending the same already-latest value.
            ++processed;
        }
    }
    return processed;
}

} // namespace ssf
