// example_3a_loop.cpp
//
// Illustrative only -- shows how OutboundStore + wire_format + ILinkApi
// compose into 3a's drain loop. Not a complete program (no signal
// handling, no real scheduling of contact windows, no logging).
//
// noSQL branch: OutboundStore is now file-backed (see
// outbound_store.hpp). peekPending() globs `*.ready` files and
// returns each as a PendingRecord (parsed OutboundElement + the
// filepath it came from); this loop deletes that exact file via
// erase(filepath) once the link has accepted the send, same as the
// SQLite version deleted the row by key.

#include "outbound_store.hpp"
#include "wire_format.hpp"
#include "link_api.hpp"
#include <thread>
#include <chrono>
#include <iostream>

void run3aLoop(saf::OutboundStore& store, saf::ILinkApi& link) {
    using namespace std::chrono_literals;

    while (true) {
        if (!link.isLinkUp()) {
            std::this_thread::sleep_for(1s);
            continue;
        }

        auto pending = store.peekPending(100);
        if (pending.empty()) {
            std::this_thread::sleep_for(1s);
            continue;
        }

        for (const auto& rec : pending) {
            const saf::OutboundElement& el = rec.el;
            std::string wireMsg = saf::encodeWireMessage(el);
            saf::SendResult result = link.send(wireMsg);

            switch (result) {
                case saf::SendResult::Accepted:
                    // Design-doc decision #4 (flagged for revisit once
                    // the real link API semantics are known): delete
                    // on accepted send, trusting "accepted" to mean
                    // durably delivered.
                    store.erase(rec.filepath);
                    break;
                case saf::SendResult::Rejected:
                    // Leave the file in place; log and move on. A
                    // rejected message for a key that gets updated
                    // again later will simply be overwritten in place
                    // by the next upsert() from 1a (see
                    // outbound_store.hpp's latest-value-wins note).
                    std::cerr << "3a: link rejected message for "
                              << el.msg_type << " " << el.device << "."
                              << el.property << "." << el.element << "\n";
                    break;
                case saf::SendResult::LinkDown:
                    // Window closed mid-drain -- stop this pass,
                    // remaining pending files are retried next time
                    // isLinkUp() is true.
                    goto windowClosed;
            }
        }
        continue;

    windowClosed:
        std::this_thread::sleep_for(1s);
    }
}
