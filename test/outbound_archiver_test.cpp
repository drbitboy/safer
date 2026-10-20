// outbound_archiver_test.cpp -- tests for archiveOutbound().
// Build and run via `make test-archiver` from indi-saf-cpp/.
#include "outbound_archiver.hpp"
#include "miniz.h"

#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace saf;

static std::string makeTempDir() {
    char tmpl[] = "/tmp/saf_archiver_test_XXXXXX";
    char* d = mkdtemp(tmpl);
    assert(d);
    return d;
}

static void writeFile(const fs::path& p, const std::string& content) {
    std::ofstream f(p, std::ios::binary);
    f << content;
}

static std::string readFile(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// entry name -> content, read back with miniz's reader
static std::map<std::string, std::string> readZip(const fs::path& p) {
    std::map<std::string, std::string> out;
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    bool ok = mz_zip_reader_init_file(&zip, p.c_str(), 0);
    assert(ok);
    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        char name[512];
        mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
        size_t sz = 0;
        void* data = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        assert(data);
        out[name] = std::string(static_cast<char*>(data), sz);
        mz_free(data);
    }
    mz_zip_reader_end(&zip);
    return out;
}

int main() {
    // --- Test 1: nothing to archive ---
    {
        std::string base = makeTempDir();
        fs::create_directories(base + "/out");
        ArchiveReport r = archiveOutbound(base + "/out", base + "/out/link");
        assert(r.result == ArchiveResult::NothingToDo);
        assert(!fs::exists(base + "/out/link/sending.zip"));
        assert(!fs::exists(base + "/out/link/init.zip"));
        fs::remove_all(base);
        std::cout << "Test 1 (nothing to archive): PASS\n";
    }

    // --- Test 2: archive two records; strays left alone ---
    {
        std::string base = makeTempDir();
        std::string out = base + "/out", link = out + "/link";
        fs::create_directories(out);
        writeFile(out + "/ready.set.d.p.a", "AAA");
        writeFile(out + "/ready.def.d.p.b", "BBB");
        writeFile(out + "/ready.zip", "not a record");       // 2 fields: ignored
        writeFile(out + "/sending.zip", "also not a record"); // must NOT be deleted
        writeFile(out + "/init.set.d.p.c", "still being written"); // writer's temp file
        // sending.zip in OUTBOUND is irrelevant; the in-flight check is linkDir's.
        ArchiveReport r = archiveOutbound(out, link);
        assert(r.result == ArchiveResult::Archived);
        assert(r.fileCount == 2);
        assert(fs::exists(link + "/sending.zip"));
        assert(!fs::exists(link + "/init.zip"));
        auto entries = readZip(link + "/sending.zip");
        assert(entries.size() == 2);
        assert(entries.at("init.set.d.p.a") == "AAA");
        assert(entries.at("init.def.d.p.b") == "BBB");
        // sources gone, strays untouched
        assert(!fs::exists(out + "/ready.set.d.p.a"));
        assert(!fs::exists(out + "/sending.set.d.p.a"));
        assert(!fs::exists(out + "/ready.def.d.p.b"));
        assert(!fs::exists(out + "/sending.def.d.p.b"));
        assert(readFile(out + "/ready.zip") == "not a record");
        assert(readFile(out + "/sending.zip") == "also not a record");
        assert(readFile(out + "/init.set.d.p.c") == "still being written");
        fs::remove_all(base);
        std::cout << "Test 2 (archive records, strays untouched): PASS\n";
    }

    // --- Test 3: Busy while sending.zip exists; ready files untouched,
    //     orphaned sending.<5 fields> files removed ---
    {
        std::string base = makeTempDir();
        std::string out = base + "/out", link = out + "/link";
        fs::create_directories(link);
        writeFile(link + "/sending.zip", "in flight");
        writeFile(out + "/ready.set.d.p.new", "NEW");
        writeFile(out + "/sending.set.d.p.old", "ORPHAN");
        ArchiveReport r = archiveOutbound(out, link);
        assert(r.result == ArchiveResult::Busy);
        assert(r.fileCount == 1);
        assert(readFile(link + "/sending.zip") == "in flight");
        assert(readFile(out + "/ready.set.d.p.new") == "NEW"); // not claimed
        assert(!fs::exists(out + "/sending.set.d.p.new"));
        assert(!fs::exists(out + "/sending.set.d.p.old"));     // orphan removed
        // Sender acknowledges and deletes; next call archives the waiting file.
        fs::remove(link + "/sending.zip");
        r = archiveOutbound(out, link);
        assert(r.result == ArchiveResult::Archived && r.fileCount == 1);
        assert(readZip(link + "/sending.zip").at("init.set.d.p.new") == "NEW");
        fs::remove_all(base);
        std::cout << "Test 3 (busy gate, orphan cleanup, resume after delete): PASS\n";
    }

    // --- Test 4: crash recovery: stale init.zip + leftover sending.X
    //     superseded by a newer ready.X for the same key; another leftover
    //     with no newer value is still archived ---
    {
        std::string base = makeTempDir();
        std::string out = base + "/out", link = out + "/link";
        fs::create_directories(link);
        writeFile(link + "/init.zip", "partial garbage");
        writeFile(out + "/sending.set.d.p.k", "STALE");
        writeFile(out + "/ready.set.d.p.k", "FRESH");
        writeFile(out + "/sending.set.d.p.only", "LEFTOVER");
        ArchiveReport r = archiveOutbound(out, link);
        assert(r.result == ArchiveResult::Archived && r.fileCount == 2);
        auto entries = readZip(link + "/sending.zip");
        assert(entries.at("init.set.d.p.k") == "FRESH");
        assert(entries.at("init.set.d.p.only") == "LEFTOVER");
        assert(!fs::exists(link + "/init.zip"));
        fs::remove_all(base);
        std::cout << "Test 4 (crash recovery: stale init.zip, latest wins): PASS\n";
    }

    // --- Test 5: isMailboxFilename ---
    {
        assert(isMailboxFilename("ready.set.d.p.e", "ready"));
        assert(isMailboxFilename("sending.def.Telescope Simulator.p.e", "sending"));
        assert(!isMailboxFilename("ready.zip", "ready"));
        assert(!isMailboxFilename("sending.zip", "sending"));
        assert(!isMailboxFilename("ready.set.d.p.e.extra", "ready"));
        assert(!isMailboxFilename("ready.set.d.p", "ready"));
        assert(!isMailboxFilename("sending.set.d.p.e", "ready"));
        assert(!isMailboxFilename("readyx.set.d.p.e", "ready"));
        std::cout << "Test 5 (isMailboxFilename): PASS\n";
    }
    return 0;
}
