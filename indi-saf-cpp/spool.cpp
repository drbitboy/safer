// spool.cpp
#include "spool.hpp"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <iomanip>

namespace saf {

namespace fs = std::filesystem;

SpoolWriter::SpoolWriter(std::string directory) : dir_(std::move(directory)) {
    fs::create_directories(dir_);
}

std::string SpoolWriter::utcTimestampSeconds() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t t = system_clock::to_time_t(now);
    std::tm tm_utc{};
    gmtime_r(&t, &tm_utc);
    std::ostringstream oss;
    // e.g. 20260902T153045Z -- sorts lexicographically == chronologically.
    oss << std::put_time(&tm_utc, "%Y%m%dT%H%M%SZ");
    return oss.str();
}

std::string SpoolWriter::write(const std::string& content) {
    if (hasWrittenBefore_) {
        auto elapsed = std::chrono::steady_clock::now() - lastWriteMonotonic_;
        auto minGap = std::chrono::milliseconds(1100);
        if (elapsed < minGap) {
            std::this_thread::sleep_for(minGap - elapsed);
        }
    }

    std::string ts = utcTimestampSeconds();
    fs::path path = fs::path(dir_) / (ts + ".msg");

    // In the rare case two writes land in the same second despite the
    // 1.1s spacing (e.g. after a long pause where the clock/monotonic
    // relationship gets re-checked), disambiguate rather than clobber.
    int suffix = 0;
    while (fs::exists(path)) {
        ++suffix;
        path = fs::path(dir_) / (ts + "-" + std::to_string(suffix) + ".msg");
    }

    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("SpoolWriter: failed to open " + path.string());
        }
        out << content;
    }

    lastWriteMonotonic_ = std::chrono::steady_clock::now();
    hasWrittenBefore_ = true;
    return path.string();
}

SpoolReader::SpoolReader(std::string directory) : dir_(std::move(directory)) {
    fs::create_directories(dir_);
}

} // namespace saf
