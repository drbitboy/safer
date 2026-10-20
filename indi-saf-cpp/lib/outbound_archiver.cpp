// outbound_archiver.cpp -- see outbound_archiver.hpp for the protocol.
#include "outbound_archiver.hpp"
#include "miniz.h"
#include "saf_log.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

namespace saf {

namespace fs = std::filesystem;

namespace {

const char* const kReady   = "ready";
const char* const kSending = "sending";

// fsync a file or directory by path. Best effort: returns false on error.
bool fsyncPath(const fs::path& p) {
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return false;
    int rc = ::fsync(fd);
    ::close(fd);
    return rc == 0;
}

// All regular files in dir whose name is a 5-field mailbox name with
// the given state prefix.
std::vector<fs::path> listMailboxFiles(const fs::path& dir, const std::string& state) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (!it->is_regular_file(ec2)) continue;
        if (isMailboxFilename(it->path().filename().string(), state)) {
            out.push_back(it->path());
        }
    }
    return out;
}

// "sending.set.dev.prop.elem" -> "set.dev.prop.elem"
std::string stripState(const std::string& filename) {
    return filename.substr(filename.find('.') + 1);
}

// remove() + level-2 "deleted" log when something was actually deleted.
bool removeLogged(const fs::path& p) {
    std::error_code ec;
    bool removed = fs::remove(p, ec);
    if (removed) logAt(2, "deleted \"" + p.filename().string() + "\"");
    return removed;
}

// rename() + level-2 "renamed" log on success.
void renameLogged(const fs::path& from, const fs::path& to, std::error_code& ec) {
    fs::rename(from, to, ec);
    if (!ec) {
        logAt(2, "renamed \"" + from.filename().string() + "\" to \"" +
                     to.filename().string() + "\"");
    }
}

ArchiveReport fail(const std::string& msg) {
    ArchiveReport r;
    r.result = ArchiveResult::Error;
    r.error = msg;
    return r;
}

} // namespace

bool isMailboxFilename(const std::string& name, const std::string& state) {
    if (name.compare(0, state.size() + 1, state + ".") != 0) return false;
    return std::count(name.begin(), name.end(), '.') == 4;
}

ArchiveReport archiveOutbound(const std::string& outboundDir,
                              const std::string& linkDir) {
    std::error_code ec;
    fs::create_directories(linkDir, ec);
    if (ec) return fail("cannot create " + linkDir + ": " + ec.message());

    const fs::path out(outboundDir), link(linkDir);
    const fs::path initZip = link / "init.zip";
    const fs::path sendingZip = link / "sending.zip";

    // --- Step 0: archive already in flight? ---
    if (fs::exists(sendingZip, ec)) {
        ArchiveReport r;
        r.result = ArchiveResult::Busy;
        for (const auto& orphan : listMailboxFiles(out, kSending)) {
            if (removeLogged(orphan)) ++r.fileCount;
        }
        return r;
    }

    // --- Step 1: stale partial archive from a crashed attempt ---
    removeLogged(initZip); // absent is fine

    // --- Step 2: claim ready.<...> -> sending.<...> ---
    for (const auto& rdy : listMailboxFiles(out, kReady)) {
        fs::path dst = rdy;
        dst.replace_filename(std::string(kSending) + "." + stripState(rdy.filename().string()));
        std::error_code ec2;
        renameLogged(rdy, dst, ec2);
        // On failure leave it for the next cycle.
    }

    std::vector<fs::path> sources = listMailboxFiles(out, kSending);
    if (sources.empty()) return ArchiveReport{};
    std::sort(sources.begin(), sources.end()); // deterministic entry order

    // --- Step 3: write init.zip ---
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_file(&zip, initZip.c_str(), 0)) {
        return fail("cannot create " + initZip.string());
    }
    for (const auto& src : sources) {
        std::string entry = "init." + stripState(src.filename().string());
        if (!mz_zip_writer_add_file(&zip, entry.c_str(), src.c_str(), nullptr, 0,
                                    MZ_DEFAULT_LEVEL)) {
            mz_zip_writer_end(&zip);
            removeLogged(initZip);
            return fail("cannot add " + src.string() + " to archive");
        }
    }
    if (!mz_zip_writer_finalize_archive(&zip)) {
        mz_zip_writer_end(&zip);
        removeLogged(initZip);
        return fail("cannot finalize " + initZip.string());
    }
    mz_zip_writer_end(&zip);
    if (!fsyncPath(initZip)) {
        removeLogged(initZip);
        return fail("fsync " + initZip.string() + ": " + std::strerror(errno));
    }

    logAt(1, "wrote file init.zip");

    // --- Step 4: atomic publish ---
    renameLogged(initZip, sendingZip, ec);
    if (ec) {
        removeLogged(initZip);
        return fail("rename to " + sendingZip.string() + ": " + ec.message());
    }
    fsyncPath(link); // best effort: make the rename itself durable

    // --- Step 5: sources are now safely inside sending.zip ---
    ArchiveReport r;
    r.result = ArchiveResult::Archived;
    for (const auto& src : sources) {
        if (removeLogged(src)) ++r.fileCount;
    }
    return r;
}

} // namespace saf
