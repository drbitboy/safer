// link_api.hpp
//
// STUBBED interface for the ground<->space link, since the real
// send/receive API is TBD. 3a is written against this interface so it
// can be developed/tested now with a fake implementation, and rewired
// to the real link API later without touching 3a's logic.
//
// Open question carried over from the design doc (decision #4 /
// open questions): does a true "accepted" response from the real link
// guarantee the peer has durably received the message, or only that
// local hardware has taken custody of the bytes? Until that's known,
// treat SendResult::Accepted conservatively -- it's what currently
// triggers the pending file's deletion in 3a (outbound_store.hpp),
// but that policy is flagged for revisit.

#pragma once
#include <string>
#include <optional>

namespace saf {

enum class SendResult {
    Accepted,   // link took custody / confirmed -- meaning TBD, see above
    Rejected,   // link explicitly refused (e.g. malformed, too large)
    LinkDown,   // no contact window currently open
};

// Abstract interface -- implement one concrete class per real link
// technology once the API is known (e.g. LinkApiRadioImpl,
// LinkApiTestSocketImpl for local testing, etc.)
class ILinkApi {
public:
    virtual ~ILinkApi() = default;

    // Returns true if a contact window is currently open and the link
    // believes it can accept traffic right now. 3a should check this
    // before attempting to drain the outbound store.
    virtual bool isLinkUp() = 0;

    // Sends one already-encoded wire message (see wire_format.hpp).
    // Blocking or non-blocking is an implementation detail of the
    // concrete class; 3a treats this call as synchronous for now.
    virtual SendResult send(const std::string& wireMessage) = 0;
};

// Trivial stand-in implementation for local development/testing before
// the real link API exists. Always reports the link up and always
// accepts. Swap this out for the real implementation later; nothing
// in 3a's calling code needs to change beyond which class gets
// constructed.
class StubLinkApi : public ILinkApi {
public:
    bool isLinkUp() override { return true; }

    SendResult send(const std::string& /*wireMessage*/) override {
        return SendResult::Accepted;
    }
};

} // namespace saf
