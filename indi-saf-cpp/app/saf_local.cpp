// saf_local.cpp
#include "saf_local.hpp"
#include "mailbox.hpp"
#include "indi_xml_bridge.hpp"
#include "inbound_unpacker.hpp"
#include "outbound_archiver.hpp"
#include "saf_log.hpp"
#include "link_gate.hpp"

extern "C" {
#include "lilxml.h"
}

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <system_error>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

namespace saf {

namespace {

// ---------------------------------------------------------------------
// Shutdown signal handling -- same pattern as the earlier SIGCHLD demo:
// a signal handler that does nothing but set a flag (only
// async-signal-safe operations), with all real work happening in the
// main loop once it observes the flag.
volatile sig_atomic_t g_shutdownRequested = 0;

void shutdownHandler(int /*sig*/) {
    g_shutdownRequested = 1;
}

void installShutdownHandlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = shutdownHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // deliberately NOT SA_RESTART: select() should be
                      // interrupted (EINTR) by these so the loop can
                      // notice g_shutdownRequested promptly rather
                      // than waiting out a full timeout.
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

// Level 1 (-v) and level 2 (-vv) diagnostics. Both go through the
// shared logger (saf_log.hpp), which runLoop() configures from
// cfg.verbose, so library code (mailbox, archiver) logs at the same
// levels.
void logv(const Config&, const std::string& msg) { logAt(1, msg); }
void logv2(const Config&, const std::string& msg) { logAt(2, msg); }


void logErr(const std::string& msg) {
    std::fprintf(stderr, "%s: ERROR: %s\n", logName().c_str(), msg.c_str());
}

// Logs an error only when it differs from the previous one held in
// `last`, so a persistent failure does not repeat on every lap.
void logErrOnce(std::string& last, const std::string& msg) {
    if (msg != last) {
        logErr(msg);
        last = msg;
    }
}

} // namespace

// ---------------------------------------------------------------------
// Socket helpers.

namespace {

// Connects to host:port over TCP, sets the socket non-blocking.
// Returns the fd, or -1 on failure (with a message already logged).
int connectToIndiserver(const Config& cfg) {
    struct addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* results = nullptr;
    std::string portStr = std::to_string(cfg.indiserverPort);
    int rc = getaddrinfo(cfg.indiserverHost.c_str(), portStr.c_str(), &hints, &results);
    if (rc != 0) {
        logErr("getaddrinfo(" + cfg.indiserverHost + "): " + gai_strerror(rc));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* p = results; p != nullptr; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
            break; // success
        }
        close(fd);
        fd = -1;
    }
    freeaddrinfo(results);

    if (fd < 0) {
        logErr("connect to " + cfg.indiserverHost + ":" + portStr + " failed: " +
                std::strerror(errno));
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        logErr("fcntl O_NONBLOCK failed: " + std::string(std::strerror(errno)));
        close(fd);
        return -1;
    }

    return fd;
}

// Sleeps for ms milliseconds using select() (so it's interruptible by
// the shutdown signal handlers, same as the main loop's select()) --
// checked in small increments against g_shutdownRequested rather than
// one uninterruptible block, so a reconnect wait doesn't delay
// shutdown by the full reconnectDelayMs.
void interruptibleSleepMs(int ms) {
    while (ms > 0 && !g_shutdownRequested) {
        int chunk = std::min(ms, 100);
        struct timeval tv;
        tv.tv_sec = chunk / 1000;
        tv.tv_usec = (chunk % 1000) * 1000;
        select(0, nullptr, nullptr, nullptr, &tv); // pure timeout, no fds
        ms -= chunk;
    }
}

// Sent once after each successful connect() to make indiserver stream
// all properties to us. INDI v1.7 specifies the version attribute on
// getProperties as required; it is included here for that reason
// (verify against your indiserver if you want the bare "<getProperties/>").
const char* const kGetPropertiesMsg = "<getProperties version=\"1.7\"/>";

// Sends the whole string on fd, retrying on EINTR and (for the
// non-blocking socket) waiting up to ~1 s total for writability on
// EAGAIN. Returns true if every byte was sent; false on error/timeout
// with errno set.
bool sendAllBlocking(int fd, const std::string& s) {
    size_t total = 0;
    int waitedMs = 0;
    while (total < s.size()) {
        ssize_t n = send(fd, s.data() + total, s.size() - total, MSG_NOSIGNAL);
        if (n > 0) {
            total += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waitedMs >= 1000) { errno = ETIMEDOUT; return false; }
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            struct timeval tv = {0, 100000};
            select(fd + 1, nullptr, &wfds, nullptr, &tv);
            waitedMs += 100;
            continue;
        }
        return false; // n == 0 (unexpected) or hard error
    }
    return true;
}

// Given a vector-message tag like "setNumberVector", splits it into
// msgType ("set") and vecType ("Number"). Returns false for anything
// that isn't a recognized def/set/new + Text/Number/Switch/Light
// combination -- including BLOB vectors (explicitly out of scope per
// the design doc) and non-vector messages (delProperty, message,
// getProperties, enableBLOB), which this bridge doesn't handle.
bool parseVectorTag(const std::string& tag, std::string& msgType, std::string& vecType) {
    static const char* kMsgTypes[] = {"def", "set", "new"};
    static const char* kVecTypes[] = {"Text", "Number", "Switch", "Light"};
    // BLOB deliberately excluded -- out of scope (see design doc).

    for (const char* mt : kMsgTypes) {
        if (tag.rfind(mt, 0) != 0) continue; // doesn't start with mt
        for (const char* vt : kVecTypes) {
            std::string expected = std::string(mt) + vt + "Vector";
            if (tag == expected) {
                msgType = mt;
                vecType = vt;
                return true;
            }
        }
    }
    return false;
}

} // namespace

namespace {

// Handles a vector whose device == logName(): it is a control message for
// this program, never forwarded. Does something only for
//   Text   vector "dump_time",      element "stop_time" (On | Off | UTC time)
//   Number vector "dump_count", element "stop_count" (integer)
// and ignores everything else (logged at -vv).
void handleControlVector(const Config& cfg, LinkGate& gate, XMLEle* root,
                         const std::string& tag) {
    const std::string prop = findXMLAttValu(root, "name");
    auto endsWith = [&](const char* suffix) {
        const size_t n = std::strlen(suffix);
        return tag.size() >= n && tag.compare(tag.size() - n, n, suffix) == 0;
    };
    const bool isDump  = endsWith("TextVector")   && prop == "dump_time";
    const bool isCount = endsWith("NumberVector") && prop == "dump_count";
    if (!isDump && !isCount) {
        logv2(cfg, "ignored control message " + tag + " for " + logName() + "." + prop);
        return;
    }
    const std::string elName = isDump ? "stop_time" : "stop_count";
    const std::string full = logName() + "." + prop + "." + elName;
    bool found = false;
    for (XMLEle* ep = nextXMLEle(root, 1); ep != nullptr; ep = nextXMLEle(root, 0)) {
        if (std::string(findXMLAttValu(ep, "name")) != elName) continue;
        found = true;
        const std::string text = pcdataXMLEle(ep);
        if (isDump) {
            std::uint32_t v;
            if (!parseDumpValue(text, v)) {
                logErr("invalid " + full + " value '" + text +
                       "' (want On, Off, or YYYY-mm-ddTHH:MM:SSZ); stop time unchanged");
                continue;
            }
            gate.setStopTime(v);
            logv(cfg, full + " = '" + text + "' -> stop time " + std::to_string(v) +
                          "; time-based link state is " +
                          (gate.isUpAt(std::time(nullptr)) ? "up" : "down"));
        } else {
            int v;
            if (!parseStopCount(text, v)) {
                logErr("invalid " + full + " value '" + text +
                       "' (want an integer); stop count unchanged");
                continue;
            }
            gate.setStopCount(v);
            logv(cfg, full + " = '" + text + "' -> stop count " + std::to_string(v));
        }
    }
    if (!found) {
        logErr("control message " + tag + " for " + logName() + "." + prop +
               " has no element named '" + elName + "'");
    }
}

} // namespace

int runLoop(const Config& cfg) {
    installShutdownHandlers();
    setLogLevel(cfg.verbose);
    setLogName(cfg.name);

    FileMailbox inboundMailbox(cfg.inboundDir);
    FileMailbox outboundMailbox(cfg.outboundDir);
    LinkGate gate; // decides whether archiving may run (see link_gate.hpp)

    // VERIFY: parseXMLChunk's exact return contract isn't documented
    // in the lilxml.h we have (no doc comment on that declaration).
    // Assumed here, based on common liblilxml usage: returns a
    // NULL-terminated array of complete top-level XMLEle* parsed from
    // the accumulated stream so far (any incomplete trailing fragment
    // is retained internally in the LilXML* context for the next
    // call); caller owns the returned elements and must delXMLEle()
    // each one once done with it. If MagAO-X's fork differs (e.g. a
    // separate count rather than NULL-termination, or different
    // ownership), this loop's (D.2) handling below needs adjusting.
    LilXML* lp = newLilXML();
    if (lp == nullptr) {
        logErr("newLilXML() failed");
        return 1;
    }

    int fd = -1;
    bool needReconnect = true;

    int exitCode = 0;
    const std::string outboundLinkDir = cfg.outboundLinkDirResolved();
    const std::string inboundLinkDir = cfg.inboundLinkDirResolved();
    std::string lastUnpackErr, lastArchiveErr;

    while (!g_shutdownRequested) {
        // --- (A) unpack the archive delivered by the link sender ---
        // First, so ready.* files already waiting in the inbound mailbox
        // are replaced by the (newer) archive contents before step C
        // sends them to indiserver. Local file work only: independent of
        // the indiserver connection and of the link state.
        {
            UnpackReport ur = unpackInbound(cfg.inboundDir, inboundLinkDir);
            switch (ur.result) {
                case UnpackResult::Unpacked:
                    lastUnpackErr.clear();
                    logv(cfg, "unpacked " + std::to_string(ur.fileCount) +
                                  " record(s) from " + inboundLinkDir + "/sending.zip");
                    break;
                case UnpackResult::Rejected:
                    lastUnpackErr.clear();
                    logErr("REJECTED bad archive (" + ur.error + "); moved to " +
                           ur.rejectedPath);
                    break;
                case UnpackResult::Error:
                    logErrOnce(lastUnpackErr, "unpack failed: " + ur.error);
                    break;
                case UnpackResult::NothingToDo:
                    lastUnpackErr.clear();
                    break;
            }
        }

        // --- (B) archive outbound mailbox files for the link sender ---
        // gate.isUp() spends a stop count when the time-based gate is
        // closed, so call it only when this lap would really make an archive:
        // something is pending and no archive is already in flight. Otherwise
        // use the time-based gate alone (no count spent).
        bool archiveNow;
        {
            std::error_code fsec;
            const bool inFlight =
                std::filesystem::exists(std::filesystem::path(outboundLinkDir) / "sending.zip", fsec);
            const bool spendCount = outboundMailbox.pendingCount() > 0 && !inFlight;
            archiveNow = spendCount ? gate.isUp() : gate.isUpAt(std::time(nullptr));
        }
        if (archiveNow) {
            ArchiveReport ar = archiveOutbound(cfg.outboundDir, outboundLinkDir);
            if (ar.result == ArchiveResult::Error) {
                logErrOnce(lastArchiveErr, "archive failed: " + ar.error);
            } else {
                lastArchiveErr.clear();
            }
            if (ar.result == ArchiveResult::Archived) {
                logv(cfg, "archived " + std::to_string(ar.fileCount) +
                              " record(s) into " + outboundLinkDir + "/sending.zip");
            } else if (ar.result == ArchiveResult::Busy && ar.fileCount > 0) {
                logv(cfg, "removed " + std::to_string(ar.fileCount) +
                              " orphaned sending.* file(s) already in the archive");
            }
        }

        if (needReconnect) {
            if (fd >= 0) {
                close(fd);
                fd = -1;
            }
            logv(cfg, "connecting to " + cfg.indiserverHost + ":" +
                          std::to_string(cfg.indiserverPort) + " ...");
            fd = connectToIndiserver(cfg);
            if (fd < 0) {
                interruptibleSleepMs(cfg.reconnectDelayMs);
                continue; // try again (or exit, if shutdown was requested
                          // during the sleep -- checked at top of loop)
            }
            logv(cfg, "connected");

            // Ask indiserver to start sending us def*/set* updates for
            // all devices/properties. Sent once per successful connect
            // (so again after every reconnect). The socket was just
            // created, so this ~30-byte message fits in the send
            // buffer; a short write or error is treated as a dead
            // connection and triggers a reconnect.
            if (!sendAllBlocking(fd, kGetPropertiesMsg)) {
                logErr("send(getProperties) failed: " +
                        std::string(std::strerror(errno)));
                interruptibleSleepMs(cfg.reconnectDelayMs);
                continue; // needReconnect is still true
            }
            logv(cfg, std::string("sent ") + kGetPropertiesMsg);
            needReconnect = false;
        }

        // --- (C) check inbound mailbox for pending records ---
        auto pending = inboundMailbox.peekPending(cfg.mailboxLimit);

        // (C.1) none pending -> falls through to (D) below (the for
        // loop over an empty vector is a no-op).
        // (C.2) pending -> recompose + send each.
        bool writeBlocked = false;
        for (const auto& rec : pending) {
            if (writeBlocked) break; // stop draining this lap once we
                                      // hit EAGAIN; remaining files
                                      // are retried next lap (still
                                      // sending.*, per mailbox.hpp)

            std::string xml;
            try {
                xml = recomposeVectorXml(rec.el);
            } catch (const std::exception& ex) {
                logErr("recompose failed for " + rec.el.msg_type + " " +
                        rec.el.device + "." + rec.el.property + "." +
                        rec.el.element + ": " + ex.what());
                continue; // leave the malformed file for inspection,
                          // move on to the next pending record
            }

            size_t totalSent = 0;
            bool sendFailed = false;
            while (totalSent < xml.size()) {
                ssize_t n = send(fd, xml.data() + totalSent,
                                  xml.size() - totalSent, MSG_NOSIGNAL);
                if (n > 0) {
                    totalSent += static_cast<size_t>(n);
                    continue;
                }
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                    // Socket send buffer is full right now. Per the
                    // design discussion: don't busy-loop on this --
                    // leave the rest of xml unsent, and since
                    // MailboxElement records are small, treat a
                    // partial send as "not sent" rather than trying
                    // to resume mid-message next lap (simpler, and
                    // the file is still sending.* either way so
                    // nothing is lost).
                    writeBlocked = true;
                    sendFailed = true;
                    break;
                }
                if (n < 0 && errno == EINTR) {
                    continue; // retry the send
                }
                // Any other error: treat the connection as gone.
                logErr("send() failed: " + std::string(std::strerror(errno)));
                needReconnect = true;
                sendFailed = true;
                writeBlocked = true; // stop draining this lap too
                break;
            }

            if (!sendFailed) {
                inboundMailbox.erase(rec.filepath);
                logv(cfg, "sent " + rec.el.msg_type + " " + rec.el.device + "." +
                              rec.el.property + "." + rec.el.element);
            }
        }

        if (needReconnect) {
            continue; // skip (D) this lap, top of loop will reconnect
        }

        // --- (D) select() on the indiserver read fd, with timeout ---
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        struct timeval tv;
        tv.tv_sec = cfg.selectTimeoutMs / 1000;
        tv.tv_usec = (cfg.selectTimeoutMs % 1000) * 1000;

        int rc = select(fd + 1, &readfds, nullptr, nullptr, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue; // signal (possibly shutdown) -- loop back to
                          // (A); top-of-loop g_shutdownRequested check
                          // handles actual shutdown
            }
            logErr("select() failed: " + std::string(std::strerror(errno)));
            needReconnect = true;
            continue;
        }
        if (rc == 0) {
            // (D.1) timeout, nothing readable -> back to (A)
            continue;
        }

        // (D.2) readable
        char buf[4096];
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            // parseXMLChunk unconditionally writes to errmsg (it does
            // errmsg[0] = '\0' on entry), so it must not be NULL.
            char errmsg[1024];
            XMLEle** roots = parseXMLChunk(lp, buf, static_cast<int>(n), errmsg);
            if (errmsg[0] != '\0') {
                // Parse error somewhere in this chunk; the parser has
                // reset itself and any complete elements are still
                // returned in roots, so keep going.
                logErr(std::string("XML parse error: ") + errmsg);
            }
            if (roots != nullptr) {
                for (int i = 0; roots[i] != nullptr; ++i) { // VERIFY: NULL-terminated
                    XMLEle* root = roots[i];
                    std::string tag = tagXMLEle(root);
                    std::string msgType, vecType;
                    if (parseVectorTag(tag, msgType, vecType) &&
                        logName() == findXMLAttValu(root, "device")) {
                        handleControlVector(cfg, gate, root, tag);
                    } else if (parseVectorTag(tag, msgType, vecType)) {
                        auto elements = decomposeVector(root, vecType, msgType);
                        if (logLevel() >= 2) {
                            std::string names;
                            for (const auto& el : elements) {
                                if (!names.empty()) names += ",";
                                names += el.element;
                            }
                            logv2(cfg, "received " + tag + " " +
                                           findXMLAttValu(root, "device") + "." +
                                           findXMLAttValu(root, "name") +
                                           ".{" + names + "}");
                        }
                        for (const auto& el : elements) {
                            outboundMailbox.upsert(el);
                        }
                    } else {
                        logv(cfg, "skipped non-vector or unhandled message: " + tag);
                    }
                    delXMLEle(root); // caller owns each element (confirmed in lilxml.c)
                }
                free(roots); // ...and the malloc'd array itself
            }
        } else if (n == 0) {
            logv(cfg, "indiserver closed the connection");
            needReconnect = true;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Spurious wakeup -- nothing to do, loop back to (A).
            } else if (errno == EINTR) {
                // Loop back to (A); shutdown checked at top of loop.
            } else {
                logErr("recv() failed: " + std::string(std::strerror(errno)));
                needReconnect = true;
            }
        }
    }

    logv(cfg, "shutting down");
    if (fd >= 0) close(fd);
    delLilXML(lp);
    return exitCode;
}

} // namespace saf

int main(int argc, char** argv) {
    saf::Config cfg;
    saf::ParseResult pr = saf::parseArgs(argc, argv, cfg);
    // Prefix for startup errors: the configured name if -n/--name (or the
    // INI name) was parsed before the failure, else the default.
    const std::string who = cfg.name.empty() ? "saf_local" : cfg.name;
    if (pr.helpRequested) {
        saf::printUsage(argv[0], stdout);
        return 0;
    }
    if (!pr.ok) {
        std::fprintf(stderr, "%s: %s\n\n", who.c_str(), pr.error.c_str());
        saf::printUsage(argv[0], stderr);
        return 2;
    }

    std::string errOut;
    if (!saf::validateConfig(cfg, errOut)) {
        std::fprintf(stderr, "%s: %s\n\n", who.c_str(), errOut.c_str());
        saf::printUsage(argv[0], stderr);
        return 2;
    }

    return saf::runLoop(cfg);
}
