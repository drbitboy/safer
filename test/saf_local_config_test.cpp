// saf_local_config_test.cpp
//
// Ad hoc smoke test for saf_local_config.hpp/.cpp (Config defaults and
// command-line parsing; there is no config file). Deliberately has NO dependency on liblilxml,
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
    //     field, even though the required-ness is no longer the normal case ---
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
        Args args({"saf_local", "-i", "/tmp/in", "-o", "/tmp/out", "-P", "7625",
                    "-H", "10.0.0.5", "-p", "500", "-l", "50", "-r", "2000", "-v"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(pr.ok && !pr.helpRequested);
        assert(cfg.inboundDir == "/tmp/in");
        assert(cfg.outboundDir == "/tmp/out");
        assert(cfg.indiserverPort == 7625);
        assert(cfg.indiserverHost == "10.0.0.5");
        assert(cfg.loopPauseMs == 500);
        assert(cfg.mailboxLimit == 50);
        assert(cfg.reconnectDelayMs == 2000);
        assert(cfg.verbose == 1);
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
        Args args({"saf_local", "-P", "not-a-number"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(!pr.ok);
        assert(!pr.helpRequested);
        assert(!pr.error.empty());
        std::printf("Test 4 (bad --Port): PASS\n");
    }

    // --- Test 5: -P/--Port range and -p/--loopPause validation; defaults ---
    {
        Config d;
        assert(d.indiserverPort == 7624 && d.loopPauseMs == 0);
        Config a;
        Args argsA({"saf_local", "--Port", "9000", "--loopPause", "250"});
        assert(parseArgs(argsA.argc(), argsA.data(), a).ok);
        assert(a.indiserverPort == 9000 && a.loopPauseMs == 250);
        for (const char* bad : {"0", "65536", "-1", "x"}) {
            Config b;
            Args argsB({"saf_local", "-P", bad});
            assert(!parseArgs(argsB.argc(), argsB.data(), b).ok);
        }
        for (const char* bad : {"-1", "x", "1.5"}) {
            Config c;
            Args argsC({"saf_local", "-p", bad});
            assert(!parseArgs(argsC.argc(), argsC.data(), c).ok);
        }
        Config z;
        Args argsZ({"saf_local", "-p", "0"});
        assert(parseArgs(argsZ.argc(), argsZ.data(), z).ok && z.loopPauseMs == 0);
        std::printf("Test 5 (-P/--Port, -p/--loopPause): PASS\n");
    }

    // --- Test 6: the removed options are rejected: -c/--config, -t ---
    {
        for (const char* bad : {"-c", "--config", "-t", "--select-timeout-ms"}) {
            Config cfg;
            Args args({"saf_local", bad, "x"});
            ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
            assert(!pr.ok && !pr.helpRequested);
        }
        std::printf("Test 6 (-c/--config and -t no longer exist): PASS\n");
    }

    // --- Test 9: link dirs default from outbound-dir / inbound-dir; flags override ---
    {
        Config cfg;
        assert(cfg.outboundLinkDirResolved() == "./outbound/link");
        assert(cfg.inboundLinkDirResolved() == "./inbound/link");
        Args args({"saf_local", "-o", "/x/out", "-i", "/x/in"});
        ParseResult pr = parseArgs(args.argc(), args.data(), cfg);
        assert(pr.ok);
        assert(cfg.outboundLinkDirResolved() == "/x/out/link");
        assert(cfg.inboundLinkDirResolved() == "/x/in/link");
        Config cfg2;
        Args args2({"saf_local", "-o", "/x/out", "--outbound-link-dir", "/y/olink",
                    "--inbound-link-dir", "/y/ilink"});
        pr = parseArgs(args2.argc(), args2.data(), cfg2);
        assert(pr.ok);
        assert(cfg2.outboundLinkDirResolved() == "/y/olink");
        assert(cfg2.inboundLinkDirResolved() == "/y/ilink");
        Config cfg3;
        Args args3({"saf_local", "-L", "/z/olink"});
        assert(parseArgs(args3.argc(), args3.data(), cfg3).ok);
        assert(cfg3.outboundLinkDirResolved() == "/z/olink");
        std::printf("Test 9 (link dirs default and override): PASS\n");
    }

    // --- Test 10: -vv, -v -v, cap at 2, INI verbose=2 ---
    {
        Config a;
        Args argsA({"saf_local", "-vv"});
        assert(parseArgs(argsA.argc(), argsA.data(), a).ok && a.verbose == 2);
        Config b;
        Args argsB({"saf_local", "-v", "-v"});
        assert(parseArgs(argsB.argc(), argsB.data(), b).ok && b.verbose == 2);
        Config c;
        Args argsC({"saf_local", "-v", "--verbose", "-v"});
        assert(parseArgs(argsC.argc(), argsC.data(), c).ok && c.verbose == 2);
        std::printf("Test 10 (-vv, -v -v, cap): PASS\n");
    }

    // --- Test 11: --name / -n / INI name; default is saf_local; empty rejected ---
    {
        Config d;
        assert(d.name == "saf_local");
        Config a;
        Args argsA({"saf_local", "-n", "ground"});
        assert(parseArgs(argsA.argc(), argsA.data(), a).ok && a.name == "ground");
        Config b;
        Args argsB({"saf_local", "--name", "space-1"});
        assert(parseArgs(argsB.argc(), argsB.data(), b).ok && b.name == "space-1");
        Config empty;
        empty.name = "";
        std::string err;
        assert(!validateConfig(empty, err) && err.find("name") != std::string::npos);
        std::printf("Test 11 (-n/--name, default, empty rejected): PASS\n");
    }

    std::printf("All tests passed.\n");
    return 0;
}
