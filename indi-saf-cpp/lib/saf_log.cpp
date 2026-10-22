// saf_log.cpp
#include "saf_log.hpp"
#include <cstdio>

namespace saf {

namespace {
int g_level = 0;
std::string g_name = "saf_local";
}

void setLogName(const std::string& name) { g_name = name; }
const std::string& logName() { return g_name; }

void setLogLevel(int level) { g_level = level; }
int logLevel() { return g_level; }

void logAt(int level, const std::string& msg) {
    if (level <= g_level) {
        std::fprintf(stderr, "%s: %s\n", g_name.c_str(), msg.c_str());
    }
}

} // namespace saf
