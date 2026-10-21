// fs_util.hpp -- small filesystem helpers shared by the archiver and
// the unpacker. Header-only; the level-2 log lines ("renamed"/"deleted")
// are emitted here so every caller logs them the same way.
#pragma once
#include "saf_log.hpp"

#include <fcntl.h>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace saf {
namespace fsutil {

// fsync a file or directory by path. Best effort: returns false on error.
inline bool fsyncPath(const std::filesystem::path& p) {
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return false;
    int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}

// remove() + level-2 "deleted" log when something was actually deleted.
inline bool removeLogged(const std::filesystem::path& p) {
    std::error_code ec;
    bool removed = std::filesystem::remove(p, ec);
    if (removed) logAt(2, "deleted \"" + p.filename().string() + "\"");
    return removed;
}

// rename() + level-2 "renamed" log on success.
inline void renameLogged(const std::filesystem::path& from,
                         const std::filesystem::path& to, std::error_code& ec) {
    std::filesystem::rename(from, to, ec);
    if (!ec) {
        logAt(2, "renamed \"" + from.filename().string() + "\" to \"" +
                     to.filename().string() + "\"");
    }
}

} // namespace fsutil
} // namespace saf
