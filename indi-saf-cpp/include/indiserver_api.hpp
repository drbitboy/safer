// indiserver_api.hpp
//
// STUBBED interface for 1b's connection to the LOCAL indiserver (the
// socket/FIFO write mentioned as "not yet implemented" in
// indi-saf-cpp/README.md). Mirrors link_api.hpp's ILinkApi pattern:
// 1b is written against this interface so it can be developed/tested
// now with a fake implementation, and rewired to a real socket/FIFO
// connection later without touching 1b's logic.
//
// This is deliberately a much simpler interface than ILinkApi -- the
// local indiserver connection isn't a scheduled-contact-window link,
// it's assumed to be either up or trivially reconnectable, so there's
// no isLinkUp()-style gate here. If that assumption turns out to be
// wrong in practice (e.g. indiserver itself restarts and the FIFO/
// socket needs re-opening), this interface will need a connection-
// state method added, same as ILinkApi has one.

#pragma once
#include <string>

namespace saf {

enum class IndiServerSendResult {
    Sent,       // write succeeded (does not imply indiserver "accepted"
                // the vector in any deeper sense -- just that the bytes
                // were written to the socket/FIFO)
    Failed,     // write failed (e.g. connection down, broken pipe)
};

// Abstract interface -- implement one concrete class once the real
// connection mechanism (Unix socket vs. named FIFO vs. TCP loopback)
// is decided.
class IIndiServerApi {
public:
    virtual ~IIndiServerApi() = default;

    // Sends one already-recomposed vector XML string (see
    // indi_xml_bridge.hpp's recomposeVectorXml()) to the local
    // indiserver. Blocking or non-blocking is an implementation
    // detail of the concrete class; 1b treats this call as
    // synchronous for now.
    virtual IndiServerSendResult send(const std::string& vectorXml) = 0;
};

// Trivial stand-in implementation for local development/testing
// before the real indiserver connection exists. Always accepts.
// Swap this out for the real implementation later; nothing in 1b's
// calling code needs to change beyond which class gets constructed.
class StubIndiServerApi : public IIndiServerApi {
public:
    IndiServerSendResult send(const std::string& /*vectorXml*/) override {
        return IndiServerSendResult::Sent;
    }
};

} // namespace saf
