// inbound_unpacker_test.cpp -- tests for unpackInbound().
// Build and run via `make test-unpacker` from indi-saf-cpp/.
#include "inbound_unpacker.hpp"
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
#include <vector>

namespace fs = std::filesystem;
using namespace saf;

static std::string makeTempDir() {
    char tmpl[] = "/tmp/saf_unpacker_test_XXXXXX";
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

// Builds a zip at `path`; level 0 (stored) so a data byte can be corrupted
// to trigger a CRC failure.
static void makeZip(const fs::path& path, const std::vector<std::pair<std::string, std::string>>& entries) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    bool ok = mz_zip_writer_init_file(&zip, path.c_str(), 0);
    assert(ok);
    for (const auto& e : entries) {
        ok = mz_zip_writer_add_mem(&zip, e.first.c_str(), e.second.data(), e.second.size(),
                                   MZ_NO_COMPRESSION);
        assert(ok);
    }
    ok = mz_zip_writer_finalize_archive(&zip);
    assert(ok);
    mz_zip_writer_end(&zip);
}

static std::vector<std::string> listDir(const fs::path& d, const std::string& prefix = "") {
    std::vector<std::string> out;
    for (const auto& e : fs::directory_iterator(d)) {
        std::string n = e.path().filename().string();
        if (n.compare(0, prefix.size(), prefix) == 0) out.push_back(n);
    }
    return out;
}

int main() {
    // --- Test 1: no sending.zip -> NothingToDo (link dir created) ---
    {
        std::string base = makeTempDir();
        std::string in = base + "/in", link = in + "/link";
        UnpackReport r = unpackInbound(in, link);
        assert(r.result == UnpackResult::NothingToDo);
        assert(fs::is_directory(link));
        fs::remove_all(base);
        std::cout << "Test 1 (nothing to do, link dir created): PASS\n";
    }

    // --- Test 2: normal unpack; existing stale ready.X replaced; unrelated
    //     files untouched; sending.zip deleted last ---
    {
        std::string base = makeTempDir();
        std::string in = base + "/in", link = in + "/link";
        fs::create_directories(link);
        writeFile(in + "/ready.set.d.p.a", "STALE");
        writeFile(in + "/ready.zip", "unrelated");
        makeZip(link + "/sending.zip", {{"init.set.d.p.a", "FRESH-A"},
                                         {"init.def.d.p.b", "B"}});
        UnpackReport r = unpackInbound(in, link);
        assert(r.result == UnpackResult::Unpacked && r.fileCount == 2);
        assert(readFile(in + "/ready.set.d.p.a") == "FRESH-A");
        assert(readFile(in + "/ready.def.d.p.b") == "B");
        assert(readFile(in + "/ready.zip") == "unrelated");
        assert(listDir(in, "init.").empty());
        assert(!fs::exists(link + "/sending.zip"));
        fs::remove_all(base);
        std::cout << "Test 2 (unpack, overwrite stale ready.*, strays untouched): PASS\n";
    }

    // --- Test 3: invalid entry names -> Rejected, nothing published ---
    {
        const std::vector<std::string> bad = {
            "foo", "ready.set.d.p.e", "init.zip", "init.set.d.p.e.extra",
            "init.set.d/../p.e", "init.a.b.c/d"};
        for (const auto& badName : bad) {
            std::string base = makeTempDir();
            std::string in = base + "/in", link = in + "/link";
            fs::create_directories(link);
            makeZip(link + "/sending.zip", {{"init.set.d.p.ok", "OK"}, {badName, "X"}});
            UnpackReport r = unpackInbound(in, link);
            assert(r.result == UnpackResult::Rejected);
            assert(r.rejectedPath.find("/rejected.zip.") != std::string::npos);
            assert(fs::exists(r.rejectedPath));
            assert(!fs::exists(link + "/sending.zip"));
            assert(listDir(in, "ready.").empty() && listDir(in, "init.").empty());
            assert(!fs::exists(base + "/ready.set.d.p.e")); // nothing escaped inbound
            fs::remove_all(base);
        }
        std::cout << "Test 3 (invalid entry names rejected, nothing published): PASS\n";
    }

    // --- Test 4: garbage / truncated / empty archives -> Rejected ---
    {
        std::string base = makeTempDir();
        std::string in = base + "/in", link = in + "/link";
        fs::create_directories(link);
        writeFile(link + "/sending.zip", "this is not a zip file at all");
        UnpackReport r = unpackInbound(in, link);
        assert(r.result == UnpackResult::Rejected);

        makeZip(link + "/good.zip", {{"init.set.d.p.a", std::string(2000, 'x')}});
        std::string z = readFile(link + "/good.zip");
        writeFile(link + "/sending.zip", z.substr(0, z.size() / 2)); // truncated
        r = unpackInbound(in, link);
        assert(r.result == UnpackResult::Rejected);

        makeZip(link + "/sending.zip", {}); // zero entries
        r = unpackInbound(in, link);
        assert(r.result == UnpackResult::Rejected);
        assert(r.error.find("no entries") != std::string::npos);

        assert(listDir(link, "rejected.zip.").size() == 3); // distinct names, same second or not
        assert(listDir(in, "ready.").empty());
        fs::remove_all(base);
        std::cout << "Test 4 (garbage, truncated, empty -> rejected, unique names): PASS\n";
    }

    // --- Test 5: CRC failure in the SECOND entry -> all-or-nothing: the
    //     first entry must NOT be published, no init.* left behind ---
    {
        std::string base = makeTempDir();
        std::string in = base + "/in", link = in + "/link";
        fs::create_directories(link);
        const std::string name2 = "init.set.d.p.second";
        makeZip(link + "/sending.zip", {{"init.set.d.p.first", "FIRST-DATA"},
                                         {name2, "SECOND-DATA-XXXXXXXX"}});
        std::string z = readFile(link + "/sending.zip");
        size_t pos = z.find("SECOND-DATA-XXXXXXXX");
        assert(pos != std::string::npos);
        z[pos + 3] ^= 0x01; // flip one stored data byte -> CRC mismatch
        writeFile(link + "/sending.zip", z);
        writeFile(in + "/ready.set.d.p.first", "OLD-FIRST"); // must survive untouched
        UnpackReport r = unpackInbound(in, link);
        assert(r.result == UnpackResult::Rejected);
        assert(readFile(in + "/ready.set.d.p.first") == "OLD-FIRST");
        assert(listDir(in, "init.").empty());
        assert(!fs::exists(in + "/ready.set.d.p.second"));
        fs::remove_all(base);
        std::cout << "Test 5 (CRC failure -> all-or-nothing, nothing published): PASS\n";
    }

    // --- Test 6: crash recovery: stale init.* from an interrupted unpack is
    //     removed; re-running the same archive is idempotent ---
    {
        std::string base = makeTempDir();
        std::string in = base + "/in", link = in + "/link";
        fs::create_directories(link);
        writeFile(in + "/init.set.d.p.leftover", "half-written");
        writeFile(in + "/init.zip", "not a mailbox name; untouched");
        for (int pass = 0; pass < 2; ++pass) {
            makeZip(link + "/sending.zip", {{"init.set.d.p.a", "A"}});
            UnpackReport r = unpackInbound(in, link);
            assert(r.result == UnpackResult::Unpacked && r.fileCount == 1);
            assert(readFile(in + "/ready.set.d.p.a") == "A");
        }
        assert(!fs::exists(in + "/init.set.d.p.leftover"));
        assert(readFile(in + "/init.zip") == "not a mailbox name; untouched");
        fs::remove_all(base);
        std::cout << "Test 6 (stale init.* cleaned, rerun idempotent): PASS\n";
    }

    // --- Test 7: round trip: archiver output unpacks into the other side ---
    {
        std::string base = makeTempDir();
        std::string out = base + "/a/outbound", olink = out + "/link";
        std::string in = base + "/b/inbound", ilink = in + "/link";
        fs::create_directories(out);
        writeFile(out + "/ready.def.Dev.Prop.E1", "wire-1");
        writeFile(out + "/ready.set.Dev.Prop.E1", "wire-2");
        writeFile(out + "/ready.set.Other Dev.Prop.E2", "wire 3"); // spaces are legal
        assert(archiveOutbound(out, olink).result == ArchiveResult::Archived);
        fs::create_directories(ilink);
        fs::rename(olink + "/sending.zip", ilink + "/sending.zip"); // stand-in for the link
        UnpackReport r = unpackInbound(in, ilink);
        assert(r.result == UnpackResult::Unpacked && r.fileCount == 3);
        assert(readFile(in + "/ready.def.Dev.Prop.E1") == "wire-1");
        assert(readFile(in + "/ready.set.Dev.Prop.E1") == "wire-2");
        assert(readFile(in + "/ready.set.Other Dev.Prop.E2") == "wire 3");
        fs::remove_all(base);
        std::cout << "Test 7 (archiver -> unpacker round trip): PASS\n";
    }
    return 0;
}
