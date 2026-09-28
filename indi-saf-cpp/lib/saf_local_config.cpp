// saf_local_config.cpp
#include "saf_local_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <getopt.h>
#include <sstream>
#include <vector>

namespace saf {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

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

bool parseBoolField(const std::string& value, const std::string& key,
                     bool& out, std::string& errOut) {
    std::string v = value;
    std::transform(v.begin(), v.end(), v.begin(),
                    [](unsigned char c) { return std::tolower(c); });
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        out = false;
        return true;
    }
    errOut = "config: '" + key + "' is not a valid boolean: '" + value + "'";
    return false;
}

} // namespace

bool loadIniConfig(const std::string& path, Config& cfg, std::string& errOut) {
    std::ifstream in(path);
    if (!in) {
        errOut = "config: could not open '" + path + "'";
        return false;
    }

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;
        if (trimmed[0] == '#' || trimmed[0] == ';') continue;
        if (trimmed.front() == '[' && trimmed.back() == ']') continue; // section header, ignored

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            errOut = "config: " + path + ":" + std::to_string(lineNo) +
                      ": not a key=value line: '" + trimmed + "'";
            return false;
        }
        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));

        if (key == "inbound_dir") {
            cfg.inboundDir = value;
        } else if (key == "outbound_dir") {
            cfg.outboundDir = value;
        } else if (key == "indiserver_host") {
            cfg.indiserverHost = value;
        } else if (key == "indiserver_port") {
            int v;
            if (!parseIntField(value, key, v, errOut)) return false;
            if (v < 1 || v > 65535) {
                errOut = "config: 'indiserver_port' out of range 1-65535: '" + value + "'";
                return false;
            }
            cfg.indiserverPort = static_cast<uint16_t>(v);
        } else if (key == "select_timeout_ms") {
            if (!parseIntField(value, key, cfg.selectTimeoutMs, errOut)) return false;
        } else if (key == "mailbox_limit") {
            if (!parseIntField(value, key, cfg.mailboxLimit, errOut)) return false;
        } else if (key == "reconnect_delay_ms") {
            if (!parseIntField(value, key, cfg.reconnectDelayMs, errOut)) return false;
        } else if (key == "verbose") {
            if (!parseBoolField(value, key, cfg.verbose, errOut)) return false;
        }
        // Unrecognized key: ignored, not an error (see header comment).
    }
    return true;
}

void printUsage(const char* argv0, FILE* out) {
    std::fprintf(out,
        "Usage: %s [options]\n"
        "\n"
        "Runs 1a (indiserver -> outbound mailbox) and 1b (inbound mailbox ->\n"
        "indiserver) as a single select()-paced loop.\n"
        "\n"
        "Options (all optional -- defaults shown):\n"
        "  -i, --inbound-dir DIR       default: ./inbound\n"
        "  -o, --outbound-dir DIR      default: ./outbound\n"
        "  -p, --indiserver-port PORT  default: 7624\n"
        "  -H, --indiserver-host HOST  default: 127.0.0.1\n"
        "  -t, --select-timeout-ms MS  default: 250\n"
        "  -l, --mailbox-limit N       max records drained per lap, default: 100\n"
        "  -r, --reconnect-delay-ms MS default: 1000\n"
        "  -v, --verbose                diagnostic logging to stderr\n"
        "  -c, --config FILE            INI file providing any of the above;\n"
        "                                loaded first, then any flag actually\n"
        "                                given on the command line overrides it\n"
        "                                (regardless of --config's position)\n"
        "  -h, --help                   this message\n",
        argv0);
}

namespace {

const struct option kLongOpts[] = {
    {"inbound-dir",        required_argument, nullptr, 'i'},
    {"outbound-dir",       required_argument, nullptr, 'o'},
    {"indiserver-port",    required_argument, nullptr, 'p'},
    {"indiserver-host",    required_argument, nullptr, 'H'},
    {"select-timeout-ms",  required_argument, nullptr, 't'},
    {"mailbox-limit",      required_argument, nullptr, 'l'},
    {"reconnect-delay-ms", required_argument, nullptr, 'r'},
    {"verbose",            no_argument,       nullptr, 'v'},
    {"config",             required_argument, nullptr, 'c'},
    {"help",               no_argument,       nullptr, 'h'},
    {nullptr, 0, nullptr, 0},
};
const char* kShortOpts = "i:o:p:H:t:l:r:vc:h";

} // namespace

ParseResult parseArgs(int argc, char** argv, Config& cfg) {
    ParseResult result;

    // Pass 1: find --config/-c (last occurrence wins) and load it, so
    // it forms a base layer UNDER whatever the command line says,
    // regardless of where --config appears among the other flags.
    // Manual scan rather than getopt_long, specifically so this pass
    // doesn't consume/require the other flags to be well-formed yet.
    std::string configPath;
    bool haveConfigPath = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) {
                configPath = argv[i + 1];
                haveConfigPath = true;
            }
        } else if (arg.rfind("--config=", 0) == 0) {
            configPath = arg.substr(std::strlen("--config="));
            haveConfigPath = true;
        }
    }
    if (haveConfigPath) {
        std::string errOut;
        if (!loadIniConfig(configPath, cfg, errOut)) {
            result.ok = false;
            result.error = errOut;
            return result;
        }
    }

    // Pass 2: full getopt_long parse. Every flag actually present
    // here overwrites whatever pass 1 set, so CLI always wins.
    optind = 1; // in case argv was already scanned by getopt_long
                // elsewhere in this process; harmless otherwise
    int c;
    while ((c = getopt_long(argc, argv, kShortOpts, kLongOpts, nullptr)) != -1) {
        switch (c) {
            case 'i': cfg.inboundDir = optarg; break;
            case 'o': cfg.outboundDir = optarg; break;
            case 'H': cfg.indiserverHost = optarg; break;
            case 'p': {
                std::string errOut;
                int v;
                if (!parseIntField(optarg, "indiserver-port", v, errOut) ||
                    v < 1 || v > 65535) {
                    result.ok = false;
                    result.error = "invalid --indiserver-port: '" + std::string(optarg) + "'";
                    return result;
                }
                cfg.indiserverPort = static_cast<uint16_t>(v);
                break;
            }
            case 't': {
                std::string errOut;
                if (!parseIntField(optarg, "select-timeout-ms", cfg.selectTimeoutMs, errOut)) {
                    result.ok = false;
                    result.error = errOut;
                    return result;
                }
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
            case 'v': cfg.verbose = true; break;
            case 'c': break; // already handled in pass 1
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
    if (cfg.inboundDir.empty()) missing.push_back("inbound-dir");
    if (cfg.outboundDir.empty()) missing.push_back("outbound-dir");
    if (cfg.indiserverPort == 0) missing.push_back("indiserver-port");
    if (missing.empty()) return true;

    std::ostringstream oss;
    oss << "missing required parameter(s):";
    for (const auto& m : missing) oss << " --" << m;
    errOut = oss.str();
    return false;
}

} // namespace saf
