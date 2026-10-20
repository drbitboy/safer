// saf_log.cpp
#include "saf_log.hpp"
#include <cstdio>

namespace saf {

namespace {
int g_level = 0;
}

void setLogLevel(int level) { g_level = level; }
int logLevel() { return g_level; }

void logAt(int level, const std::string& msg) {
    if (level <= g_level) {
        std::fprintf(stderr, "saf_local: %s\n", msg.c_str());
    }
}

} // namespace saf
