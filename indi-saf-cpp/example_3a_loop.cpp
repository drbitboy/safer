// example_3a_loop.cpp
//
// Illustrative only -- shows how OutboundStore + wire_format + ILinkApi
// compose into 3a's drain loop. Not a complete program (no signal
// handling, no real scheduling of contact windows, no logging).

#include "outbound_store.hpp"
#include "wire_format.hpp"
#include "link_api.hpp"
#include <thread>
#include <chrono>
#include <iostream>

void run3aLoop(ssf::OutboundStore& store, ssf::ILinkApi& link) {
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

        for (const auto& el : pending) {
            std::string wireMsg = ssf::encodeWireMessage(el);
            ssf::SendResult result = link.send(wireMsg);

            switch (result) {
                case ssf::SendResult::Accepted:
                    // Design-doc decision #4 (flagged for revisit once
                    // the real link API semantics are known): delete
                    // on accepted send, trusting "accepted" to mean
                    // durably delivered.
                    store.erase(el.device, el.property, el.element);
                    break;
                case ssf::SendResult::Rejected:
                    // Leave the row in place; log and move on. A
                    // rejected message for a key that gets updated
                    // again later will simply be superseded by the
                    // next UPSERT from 1a.
                    std::cerr << "3a: link rejected message for "
                              << el.device << "." << el.property << "."
                              << el.element << "\n";
                    break;
                case ssf::SendResult::LinkDown:
                    // Window closed mid-drain -- stop this pass,
                    // remaining pending rows are retried next time
                    // isLinkUp() is true.
                    goto windowClosed;
            }
        }
        continue;

    windowClosed:
        std::this_thread::sleep_for(1s);
    }
}
