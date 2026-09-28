// saf_local_config_test.cpp
//
// Ad hoc smoke test for saf_local_config.hpp/.cpp (Config parsing,
// CLI args, INI files). Deliberately has NO dependency on liblilxml,
// mailbox.hpp, or indi_xml_bridge.hpp -- unlike saf_local.cpp's
// runLoop(), this logic is fully self-contained and links without
// needing the vendored liblilxml source at all. Wired into
// indi-saf-cpp/Makefile:
//
//   cd ../indi-saf-cpp && make test-config
//
// (or `make test` to run this alongside the other two test files).

#include "saf_local_config.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
#include <cstring>

using namespace saf;

// Helper: build a fake argv from a list of strings.
struct Args {
    std::vector<std::string> storage;
    std::vector<char*> argv;
    Args(std::vector<std::string> a) : storage(std::move(a)) {
        for (auto& s : storage) argv.push_back(const_cast<char*>(s.c_str()));
    }
    int argc() { return static_cast<int>(argv.size()); }
    char** data() { return argv.data(); }
};

int main() {
    // --- Test 1: defaults apply and pass validation with no args at all ---
    {
        Config cfg;
        assert(cfg.inboundDir == "./inbound");
        assert(cfg.outboundDir == "./outbound");
        assert(cfg.indiserverPort == 7624);
        std::string err;
        bool ok = validateConfig(cfg, err);
        assert(ok);
        std::printf("Test 1 (defaults apply, pass validation with no args): PASS\n");
    }

    // --- Test 1b: validateConfig still catches an explicitly-emptied
    //     field (e.g. from an unusual config file), even though the
    //     required-ness is no longer the normal case ---
    {
        Config cfg;
        cfg.inboundDir = ""; // simulate something explicitly clearing it
        std::string err;
        bool ok = validateConfig(cfg, err);
        assert(!ok);
        assert(err.find("inbound-dir") != std::string::npos);
        std::printf("Test 1b (validateConfig still catches an explicitly-emptied field): PASS\n");
    }

    // --- Test 2: plain CLI flags set everything ---
    {
        Config cfg;
        Args args({"saf_local", "-i", "/tmp/in", "-o", "/tmp/out", "-p", "7624",
                    "-H", "10.0.0.5", "-t", "500", "-l", "50", "-r", "2000", "-v"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(pr.ok && !pr.helpRequested);
        assert(cfg.inboundDir == "/tmp/in");
        assert(cfg.outboundDir == "/tmp/out");
        assert(cfg.indiserverPort == 7624);
        assert(cfg.indiserverHost == "10.0.0.5");
        assert(cfg.selectTimeoutMs == 500);
        assert(cfg.mailboxLimit == 50);
        assert(cfg.reconnectDelayMs == 2000);
        assert(cfg.verbose == true);
        std::string err;
        assert(validateConfig(cfg, err));
        std::printf("Test 2 (plain CLI flags): PASS\n");
    }

    // --- Test 3: --help ---
    {
        Config cfg;
        Args args({"saf_local", "--help"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(!pr.ok);
        assert(pr.helpRequested);
        std::printf("Test 3 (--help): PASS\n");
    }

    // --- Test 4: bad integer ---
    {
        Config cfg;
        Args args({"saf_local", "-p", "not-a-number"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(!pr.ok);
        assert(!pr.helpRequested);
        assert(!pr.error.empty());
        std::printf("Test 4 (bad --indiserver-port): PASS\n");
    }

    // --- Test 5: INI config file loaded, CLI overrides it ---
    {
        std::string path = "/tmp/saf_local_test.ini";
        {
            std::ofstream f(path);
            f << "# comment\n"
              << "[saf_local]\n"
              << "inbound_dir=/cfg/in\n"
              << "outbound_dir=/cfg/out\n"
              << "indiserver_port=8000\n"
              << "select_timeout_ms=111\n"
              << "verbose=yes\n";
        }
        Config cfg;
        // select-timeout-ms given on CLI should override the config
        // file's 111, regardless of --config appearing BEFORE it.
        Args args({"saf_local", "--config", path, "-t", "999"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(pr.ok);
        assert(cfg.inboundDir == "/cfg/in");       // from config file
        assert(cfg.outboundDir == "/cfg/out");     // from config file
        assert(cfg.indiserverPort == 8000);        // from config file
        assert(cfg.selectTimeoutMs == 999);        // CLI override
        assert(cfg.verbose == true);                // from config file
        std::printf("Test 5 (config file + CLI override): PASS\n");
    }

    // --- Test 6: --config position doesn't matter for override order ---
    {
        std::string path = "/tmp/saf_local_test.ini";
        Config cfg;
        // Same override, but --config comes AFTER -t this time.
        Args args({"saf_local", "-t", "999", "--config", path});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(pr.ok);
        assert(cfg.selectTimeoutMs == 999); // CLI still wins
        std::printf("Test 6 (--config position independence): PASS\n");
    }

    // --- Test 7: config file with a malformed line ---
    {
        std::string path = "/tmp/saf_local_test_bad.ini";
        {
            std::ofstream f(path);
            f << "this line has no equals sign\n";
        }
        Config cfg;
        std::string err;
        bool ok = loadIniConfig(path, cfg, err);
        assert(!ok);
        assert(!err.empty());
        std::printf("Test 7 (malformed config line): PASS\n");
    }

    // --- Test 8: missing config file ---
    {
        Config cfg;
        Args args({"saf_local", "--config", "/tmp/does_not_exist.ini"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(!pr.ok);
        assert(!pr.helpRequested);
        assert(pr.error.find("could not open") != std::string::npos);
        std::printf("Test 8 (missing config file): PASS\n");
    }

    std::printf("All tests passed.\n");
    return 0;
}
