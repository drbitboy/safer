// inbound_unpacker.cpp -- see inbound_unpacker.hpp for the protocol.
#include "inbound_unpacker.hpp"
#include "outbound_archiver.hpp" // isMailboxFilename
#include "fs_util.hpp"
#include "miniz.h"
#include "saf_log.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace saf {

namespace fs = std::filesystem;
using fsutil::fsyncPath;
using fsutil::removeLogged;
using fsutil::renameLogged;

namespace {

UnpackReport makeError(const std::string& msg) {
    UnpackReport r;
    r.result = UnpackResult::Error;
    r.error = msg;
    return r;
}

// "init.set.dev.prop.elem" -> "set.dev.prop.elem"
std::string stripState(const std::string& filename) {
    return filename.substr(filename.find('.') + 1);
}

std::string utcStamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm;
    gmtime_r(&now, &tm);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", &tm);
    return buf;
}

// Moves sending.zip to rejected.zip.<stamp>; fills in the report.
UnpackReport rejectArchive(const fs::path& sendingZip, const fs::path& linkDir,
                           const std::string& why) {
    fs::path dst = linkDir / ("rejected.zip." + utcStamp());
    std::error_code ec;
    for (int n = 1; fs::exists(dst, ec); ++n) {
        dst = linkDir / ("rejected.zip." + utcStamp() + "." + std::to_string(n));
    }
    renameLogged(sendingZip, dst, ec);
    if (ec) {
        return makeError("archive is bad (" + why + ") and could not be moved to " +
                         dst.string() + ": " + ec.message());
    }
    fsyncPath(linkDir);
    UnpackReport r;
    r.result = UnpackResult::Rejected;
    r.error = why;
    r.rejectedPath = dst.string();
    return r;
}

} // namespace

UnpackReport unpackInbound(const std::string& inboundDir, const std::string& linkDir) {
    std::error_code ec;
    fs::create_directories(linkDir, ec);
    if (ec) return makeError("cannot create " + linkDir + ": " + ec.message());

    const fs::path in(inboundDir), link(linkDir);
    const fs::path sendingZip = link / "sending.zip";

    // --- Step 0 ---
    if (!fs::exists(sendingZip, ec)) return UnpackReport{};
    fs::create_directories(in, ec);
    if (ec) return makeError("cannot create " + inboundDir + ": " + ec.message());

    // --- Step 1: stale init.<...> from a crashed unpack ---
    for (fs::directory_iterator it(in, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        if (it->is_regular_file(ec2) &&
            isMailboxFilename(it->path().filename().string(), "init")) {
            removeLogged(it->path());
        }
    }

    // --- Step 2: validate before touching anything ---
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, sendingZip.c_str(), 0)) {
        return rejectArchive(sendingZip, link, "not a readable zip file");
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    if (count == 0) {
        mz_zip_reader_end(&zip);
        return rejectArchive(sendingZip, link, "archive has no entries");
    }
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (mz_uint i = 0; i < count; ++i) {
        char buf[1024];
        mz_uint len = mz_zip_reader_get_filename(&zip, i, buf, sizeof(buf));
        std::string name(buf, len > 0 ? len - 1 : 0); // len includes the NUL
        std::string why;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            why = "entry is a directory: " + name;
        } else if (!isMailboxFilename(name, "init") ||
                   name.find('/') != std::string::npos ||
                   name.find('\\') != std::string::npos) {
            why = "invalid entry name: '" + name + "'";
        } else if (!seen.insert(name).second) {
            why = "duplicate entry name: '" + name + "'";
        }
        if (!why.empty()) {
            mz_zip_reader_end(&zip);
            return rejectArchive(sendingZip, link, why);
        }
        names.push_back(name);
    }

    // --- Step 3: extract everything to init.<...> ---
    std::vector<fs::path> extracted;
    auto discardExtracted = [&]() {
        for (const auto& p : extracted) removeLogged(p);
    };
    for (mz_uint i = 0; i < count; ++i) {
        const fs::path dst = in / names[i];
        if (!mz_zip_reader_extract_to_file(&zip, i, dst.c_str(), 0)) {
            extracted.push_back(dst); // may be partial; remove it too
            mz_zip_reader_end(&zip);
            discardExtracted();
            return rejectArchive(sendingZip, link, "cannot extract entry '" + names[i] +
                                                       "' (corrupt data?)");
        }
        extracted.push_back(dst);
        logAt(1, "wrote file " + names[i]);
        fsyncPath(dst);
    }
    mz_zip_reader_end(&zip);

    // --- Step 4: publish all-or-nothing: init.<...> -> ready.<...> ---
    UnpackReport r;
    r.result = UnpackResult::Unpacked;
    for (const auto& src : extracted) {
        const fs::path dst = in / ("ready." + stripState(src.filename().string()));
        std::error_code ec2;
        renameLogged(src, dst, ec2);
        if (ec2) {
            // Not the archive's fault: leave sending.zip so the whole thing
            // is retried. Already-renamed files hold the same values the
            // retry will write again.
            return makeError("rename to " + dst.string() + ": " + ec2.message());
        }
        ++r.fileCount;
    }
    fsyncPath(in);

    // --- Step 5: delete sending.zip last ---
    if (!removeLogged(sendingZip)) {
        return makeError("unpacked " + std::to_string(r.fileCount) +
                         " record(s) but could not delete " + sendingZip.string());
    }
    fsyncPath(link);
    return r;
}

} // namespace saf
