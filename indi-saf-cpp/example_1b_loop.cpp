// example_1b_loop.cpp
//
// Illustrative only -- shows how FileMailbox + indi_xml_bridge +
// IIndiServerApi compose into 1b's drain loop, the inbound-side
// mirror of example_3a_loop.cpp. Not a complete program (no signal
// handling, no logging).
//
// Whatever receives off the link (not shown here -- that's the
// receiving counterpart to 3a's ILinkApi::send on the far side) is
// responsible for decodeWireMessage()-ing each incoming message and
// calling inboundStore.upsert() on it, using the SAME FileMailbox
// class as the outbound side (see mailbox.hpp). This loop is
// everything downstream of that: drain the inbound mailbox, recompose
// each pending record into vector XML, send it to the local
// indiserver, and erase the file once that send succeeds.
//
// Unlike 3a, there's no isLinkUp()-style gate here -- the local
// indiserver connection is assumed available (or trivially
// reconnectable); see indiserver_api.hpp for that assumption's
// caveat.

#include "mailbox.hpp"
#include "indi_xml_bridge.hpp"
#include "indiserver_api.hpp"
#include <thread>
#include <chrono>
#include <iostream>

void run1bLoop(saf::FileMailbox& inboundStore, saf::IIndiServerApi& indiServer) {
    using namespace std::chrono_literals;

    while (true) {
        auto pending = inboundStore.peekPending(100);
        if (pending.empty()) {
            std::this_thread::sleep_for(1s);
            continue;
        }

        for (const auto& rec : pending) {
            const saf::MailboxElement& el = rec.el;

            std::string vectorXml;
            try {
                vectorXml = saf::recomposeVectorXml(el);
            } catch (const std::exception& ex) {
                // Malformed pending record (e.g. bad msg_type) --
                // leave the file in place for inspection rather than
                // silently dropping it or crashing the loop.
                std::cerr << "1b: recompose failed for "
                          << el.msg_type << " " << el.device << "."
                          << el.property << "." << el.element
                          << ": " << ex.what() << "\n";
                continue;
            }

            saf::IndiServerSendResult result = indiServer.send(vectorXml);
            switch (result) {
                case saf::IndiServerSendResult::Sent:
                    // Mirrors 3a's SendResult::Accepted handling: erase
                    // the pending file now that indiserver has the
                    // bytes. Same caveat as 3a's design-doc decision
                    // #4 -- "sent" here doesn't necessarily mean
                    // indiserver's driver has durably applied it, just
                    // that the write succeeded.
                    inboundStore.erase(rec.filepath);
                    break;
                case saf::IndiServerSendResult::Failed:
                    // Leave the file in place; log and move on. A
                    // later upsert() for the same key will simply
                    // overwrite this pending file in place (see
                    // mailbox.hpp's latest-value-wins note) if a
                    // newer value for the same key arrives before
                    // this one is retried.
                    std::cerr << "1b: indiserver send failed for "
                              << el.msg_type << " " << el.device << "."
                              << el.property << "." << el.element << "\n";
                    break;
            }
        }
    }
}
