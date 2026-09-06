// mailbox_smoketest.cpp
//
// Ad hoc smoke test for indi-saf-cpp/mailbox.hpp's FileMailbox, added
// while working through the read-then-erase race fix (claim-then-glob
// via *.ready -> *.sending). Not a full test suite / not wired into
// any build system yet -- just plain assert()s and a main(), built
// and run manually:
//
//   g++ -std=c++17 -I../indi-saf-cpp mailbox_smoketest.cpp \
//       ../indi-saf-cpp/mailbox.cpp ../indi-saf-cpp/wire_format.cpp \
//       -o /tmp/mailbox_smoketest && /tmp/mailbox_smoketest
//
// Uses a fresh mkdtemp()-style temp directory per run so it's safe to
// run concurrently / repeatedly without stepping on a previous run's
// leftovers.

#include "mailbox.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

using namespace saf;
namespace fs = std::filesystem;

namespace {

MailboxElement makeEl(const std::string& msgType, const std::string& value) {
    MailboxElement el;
    el.msg_type = msgType;
    el.device   = "CCD";
    el.property = "EXPOSURE";
    el.element  = "VALUE";
    el.vec_type = "Number";
    el.value    = value;
    return el;
}

std::string makeTempDir() {
    std::string tmpl = "/tmp/mailbox_smoketest_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (mkdtemp(buf.data()) == nullptr) {
        throw std::runtime_error("mkdtemp failed");
    }
    return std::string(buf.data());
}

} // namespace

int main() {
    // --- Test 1: basic upsert -> peekPending -> erase round trip ---
    {
        std::string dir = makeTempDir();
        FileMailbox mb(dir);
        mb.upsert(makeEl("set", "1.0"));
        auto pending = mb.peekPending();
        assert(pending.size() == 1);
        assert(pending[0].el.value == "1.0");
        // Confirm the claim actually renamed .ready -> .sending.
        assert(pending[0].filepath.find(".sending") != std::string::npos);
        mb.erase(pending[0].filepath);
        assert(mb.pendingCount() == 0);
        fs::remove_all(dir);
        std::cout << "Test 1 (basic round trip): PASS\n";
    }

    // --- Test 2: case 1 -- leftover .sending, no newer .ready ---
    // Simulates a reader that crashed after claiming but before erase.
    {
        std::string dir = makeTempDir();
        std::string leftover = dir + "/set__CCD__EXPOSURE__VALUE.sending";
        {
            std::ofstream f(leftover);
            f << "set|CCD|EXPOSURE|VALUE|Number|2.0|||||||";
        }
        FileMailbox mb(dir);
        auto pending = mb.peekPending();
        assert(pending.size() == 1);
        assert(pending[0].el.value == "2.0");
        assert(pending[0].filepath == leftover);
        mb.erase(pending[0].filepath);
        assert(mb.pendingCount() == 0);
        fs::remove_all(dir);
        std::cout << "Test 2 (leftover .sending, no newer .ready): PASS\n";
    }

    // --- Test 3: case 2 -- leftover .sending superseded by newer .ready ---
    {
        std::string dir = makeTempDir();
        std::string leftover = dir + "/set__CCD__EXPOSURE__VALUE.sending";
        {
            std::ofstream f(leftover);
            f << "set|CCD|EXPOSURE|VALUE|Number|2.0|||||||";
        }
        FileMailbox mb(dir);
        mb.upsert(makeEl("set", "3.0"));  // newer value arrives after crash
        auto pending = mb.peekPending();
        assert(pending.size() == 1);
        // The newer value must win; the stale leftover ("2.0") must
        // NOT be what gets returned or sent.
        assert(pending[0].el.value == "3.0");
        mb.erase(pending[0].filepath);
        assert(mb.pendingCount() == 0);
        fs::remove_all(dir);
        std::cout << "Test 3 (leftover .sending superseded by newer .ready): PASS\n";
    }

    // --- Test 4: same-key repeated upsert before drain -> only latest survives ---
    {
        std::string dir = makeTempDir();
        FileMailbox mb(dir);
        mb.upsert(makeEl("set", "4.0"));
        mb.upsert(makeEl("set", "5.0"));  // same key, overwrites in place
        assert(mb.pendingCount() == 1);   // NOT 2 -- same-key collapse
        auto pending = mb.peekPending();
        assert(pending.size() == 1);
        assert(pending[0].el.value == "5.0");  // latest, not "4.0"
        mb.erase(pending[0].filepath);
        fs::remove_all(dir);
        std::cout << "Test 4 (same-key latest-value-wins): PASS\n";
    }

    // --- Test 5: msg_type IS part of the key -- def and set coexist ---
    {
        std::string dir = makeTempDir();
        FileMailbox mb(dir);
        mb.upsert(makeEl("def", "6.0"));
        mb.upsert(makeEl("set", "7.0"));  // different msg_type, same
                                          // device/property/element
        assert(mb.pendingCount() == 2);  // two separate files, not one
        auto pending = mb.peekPending();
        assert(pending.size() == 2);
        for (auto& rec : pending) mb.erase(rec.filepath);
        fs::remove_all(dir);
        std::cout << "Test 5 (msg_type is part of the key): PASS\n";
    }

    std::cout << "All tests passed.\n";
    return 0;
}
