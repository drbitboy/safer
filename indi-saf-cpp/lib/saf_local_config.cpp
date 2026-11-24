// saf_local_config.cpp
#include "saf_local_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <getopt.h>
#include <sstream>
#include <vector>

namespace saf {

namespace {

bool parseIntField(const std::string& value, const std::string& key,
                    int& out, std::string& errOut) {
    try {
        size_t consumed = 0;
        int v = std::stoi(value, &consumed);
        if (consumed != value.size()) throw std::invalid_argument("trailing junk");
        out = v;
        return true;
    } catch (const std::exception&) {
        errOut = "config: '" + key + "' is not a valid integer: '" + value + "'";
        return false;
    }
}

} // namespace

void printUsage(const char* argv0, FILE* out) {
    std::fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Runs 1a (indiserver -> outbound mailbox) and 1b (inbound mailbox ->\n"
        "indiserver) as a single select()-paced loop.\n"
        "\n"
        "Options (all optional -- defaults shown):\n"
        "  -n, --name NAME             instance name, prefix of every log line;\n"
        "                                default: saf_local\n"
        "  -i, --inbound-dir DIR       default: ./inbound\n"
        "  -o, --outbound-dir DIR      default: ./outbound\n"
        "  -L, --outbound-link-dir DIR zip archives for the link sender;\n"
        "                                default: <outbound-dir>/link\n"
        "      --inbound-link-dir DIR  where the link sender delivers sending.zip;\n"
        "                                default: <inbound-dir>/link\n"
        "  -P, --Port PORT             indiserver port, default: 7624\n"
        "  -H, --indiserver-host HOST  default: 127.0.0.1\n"
        "  -p, --loopPause MS          pause after each lap, milliseconds (>= 0);\n"
        "                                default: 0\n"
        "  -l, --mailbox-limit N       max records drained per lap, default: 100\n"
        "  -r, --reconnect-delay-ms MS default: 1000\n"
        "  -v, --verbose                diagnostic logging to stderr; give twice\n"
        "                                (-vv or -v -v) for more detail\n"
        "  -h, --help                   this message\n",
        argv0);
}

namespace {

const struct option kLongOpts[] = {
    {"name",               required_argument, nullptr, 'n'},
    {"inbound-dir",        required_argument, nullptr, 'i'},
    {"outbound-dir",       required_argument, nullptr, 'o'},
    {"outbound-link-dir",  required_argument, nullptr, 'L'},
    {"inbound-link-dir",   required_argument, nullptr, 256}, // long-only
    {"Port",               required_argument, nullptr, 'P'},
    {"indiserver-host",    required_argument, nullptr, 'H'},
    {"loopPause",          required_argument, nullptr, 'p'},
    {"mailbox-limit",      required_argument, nullptr, 'l'},
    {"reconnect-delay-ms", required_argument, nullptr, 'r'},
    {"verbose",            no_argument,       nullptr, 'v'},
    {"help",               no_argument,       nullptr, 'h'},
    {nullptr, 0, nullptr, 0},
};
const char* kShortOpts = "n:i:o:L:P:p:H:l:r:vh";

} // namespace

ParseResult parseArgs(int argc, char** argv, Config& cfg) {
    ParseResult result;

    // glibc: optind = 0 (not 1) forces a full re-initialization of
    // getopt's internal state, including the position inside a
    // partly-scanned argument left by an earlier call that failed
    // part-way (e.g. on an unknown option).
    optind = 0;
    int c;
    while ((c = getopt_long(argc, argv, kShortOpts, kLongOpts, nullptr)) != -1) {
        switch (c) {
            case 'n': cfg.name = optarg; break;
            case 'i': cfg.inboundDir = optarg; break;
            case 'o': cfg.outboundDir = optarg; break;
            case 'L': cfg.outboundLinkDir = optarg; break;
            case 256: cfg.inboundLinkDir = optarg; break;
            case 'H': cfg.indiserverHost = optarg; break;
            case 'P': {
                std::string errOut;
                int v;
                if (!parseIntField(optarg, "Port", v, errOut) || v < 1 || v > 65535) {
                    result.ok = false;
                    result.error = "invalid --Port: '" + std::string(optarg) + "'";
                    return result;
                }
                cfg.indiserverPort = static_cast<uint16_t>(v);
                break;
            }
            case 'p': {
                std::string errOut;
                int v;
                if (!parseIntField(optarg, "loopPause", v, errOut) || v < 0) {
                    result.ok = false;
                    result.error = "invalid --loopPause (want an integer >= 0 ms): '" +
                                   std::string(optarg) + "'";
                    return result;
                }
                cfg.loopPauseMs = v;
                break;
            }
            case 'l': {
                std::string errOut;
                if (!parseIntField(optarg, "mailbox-limit", cfg.mailboxLimit, errOut)) {
                    result.ok = false;
                    result.error = errOut;
                    return result;
                }
                break;
            }
            case 'r': {
                std::string errOut;
                if (!parseIntField(optarg, "reconnect-delay-ms", cfg.reconnectDelayMs, errOut)) {
                    result.ok = false;
                    result.error = errOut;
                    return result;
                }
                break;
            }
            case 'v': if (cfg.verbose < 2) ++cfg.verbose; break;
            case 'h':
                result.ok = false;
                result.helpRequested = true;
                return result;
            default:
                result.ok = false;
                result.error = "unrecognized or malformed option";
                return result;
        }
    }

    result.ok = true;
    return result;
}

bool validateConfig(const Config& cfg, std::string& errOut) {
    std::vector<std::string> missing;
    if (cfg.name.empty()) missing.push_back("name");
    if (cfg.inboundDir.empty()) missing.push_back("inbound-dir");
    if (cfg.outboundDir.empty()) missing.push_back("outbound-dir");
    if (cfg.indiserverPort == 0) missing.push_back("Port");
    if (missing.empty()) return true;

    std::ostringstream oss;
    oss << "missing required parameter(s):";
    for (const auto& m : missing) oss << " --" << m;
    errOut = oss.str();
    return false;
}

} // namespace saf
