// lilxml_integration_test.cpp
//
// Real end-to-end test against the actual vendored liblilxml source
// (../indi-saf-cpp/lilxml.c/.h, base64.c/.h) -- not just declarations.
// Requires linking lilxml.c and base64.c, so unlike
// saf_local_config_test.cpp this DOES need liblilxml, but since it's
// now vendored in-tree that's no longer an external dependency to
// separately provide. Build and run manually:
//
//   IDIR=../indi-saf-cpp
//   g++ -std=c++17 -c $IDIR/indi_xml_bridge.cpp -o /tmp/bridge.o
//   g++ -std=c++17 -c $IDIR/mailbox.cpp -o /tmp/mailbox.o
//   g++ -std=c++17 -c $IDIR/wire_format.cpp -o /tmp/wire_format.o
//   gcc -c $IDIR/lilxml.c -o /tmp/lilxml.o
//   gcc -c $IDIR/base64.c -o /tmp/base64.o
//   g++ -std=c++17 -I$IDIR lilxml_integration_test.cpp \
//       /tmp/bridge.o /tmp/mailbox.o /tmp/wire_format.o \
//       /tmp/lilxml.o /tmp/base64.o -o /tmp/lilxml_integration_test
//   /tmp/lilxml_integration_test


#include "mailbox.hpp"
#include "indi_xml_bridge.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

extern "C" {
#include "lilxml.h"
}

int main() {
    // --- Test 1: a single complete message in one chunk ---
    {
        LilXML* lp = newLilXML();
        assert(lp != nullptr);

        const char* xml =
            "<setNumberVector device=\"CCD\" name=\"EXPOSURE\">"
            "<oneNumber name=\"VALUE\">3.5</oneNumber>"
            "</setNumberVector>";

        char errmsg[256] = {0};
        XMLEle** roots = parseXMLChunk(lp, const_cast<char*>(xml),
                                        static_cast<int>(std::strlen(xml)), errmsg);
        assert(roots != nullptr);
        int count = 0;
        for (int i = 0; roots[i] != nullptr; ++i) ++count;
        assert(count == 1); // exactly one complete top-level element

        std::string tag = tagXMLEle(roots[0]);
        assert(tag == "setNumberVector");

        auto elements = saf::decomposeVector(roots[0], "Number", "set");
        assert(elements.size() == 1);
        assert(elements[0].device == "CCD");
        assert(elements[0].property == "EXPOSURE");
        assert(elements[0].element == "VALUE");
        assert(elements[0].value == "3.5");
        assert(elements[0].msg_type == "set");

        delXMLEle(roots[0]);
        delLilXML(lp);
        std::printf("Test 1 (single chunk, complete message): PASS\n");
    }

    // --- Test 2: the message split across TWO separate chunks ---
    // (This is the actual scenario runLoop()'s (B.2) needs to handle
    // correctly when recv() returns a partial message.)
    {
        LilXML* lp = newLilXML();
        assert(lp != nullptr);

        const char* full =
            "<setNumberVector device=\"CCD\" name=\"EXPOSURE\">"
            "<oneNumber name=\"VALUE\">7.25</oneNumber>"
            "</setNumberVector>";
        size_t splitPoint = std::strlen(full) / 2; // arbitrary mid-message split

        std::string part1(full, splitPoint);
        std::string part2(full + splitPoint);

        char errmsg[256] = {0};

        // First chunk: message is incomplete, expect zero complete
        // elements back (but a non-NULL, empty array per the earlier
        // VERIFY assumption).
        XMLEle** roots1 = parseXMLChunk(lp, const_cast<char*>(part1.data()),
                                         static_cast<int>(part1.size()), errmsg);
        assert(roots1 != nullptr);
        int count1 = 0;
        for (int i = 0; roots1[i] != nullptr; ++i) ++count1;
        assert(count1 == 0); // nothing complete yet

        // Second chunk: completes the message. The LilXML* context
        // (lp) carries the partial-parse state from the first call --
        // this is the actual mechanism under test.
        XMLEle** roots2 = parseXMLChunk(lp, const_cast<char*>(part2.data()),
                                         static_cast<int>(part2.size()), errmsg);
        assert(roots2 != nullptr);
        int count2 = 0;
        for (int i = 0; roots2[i] != nullptr; ++i) ++count2;
        assert(count2 == 1); // now complete

        auto elements = saf::decomposeVector(roots2[0], "Number", "set");
        assert(elements.size() == 1);
        assert(elements[0].value == "7.25"); // correctly reassembled
                                              // across the split

        delXMLEle(roots2[0]);
        delLilXML(lp);
        std::printf("Test 2 (message split across two chunks): PASS\n");
    }

    // --- Test 3: recomposeVectorXml() output round-trips back through
    //     the real parser (confirms the hand-written XML templating
    //     in indi_xml_bridge.cpp actually produces valid, parseable
    //     INDI XML, not just plausible-looking text). ---
    {
        saf::MailboxElement el;
        el.msg_type = "new";
        el.device = "Filter Wheel";
        el.property = "FILTER_SLOT";
        el.element = "SLOT_VALUE";
        el.vec_type = "Number";
        el.value = "4";

        std::string xml = saf::recomposeVectorXml(el);

        LilXML* lp = newLilXML();
        char errmsg[256] = {0};
        XMLEle** roots = parseXMLChunk(lp, const_cast<char*>(xml.data()),
                                        static_cast<int>(xml.size()), errmsg);
        assert(roots != nullptr);
        int count = 0;
        for (int i = 0; roots[i] != nullptr; ++i) ++count;
        assert(count == 1);

        std::string tag = tagXMLEle(roots[0]);
        assert(tag == "newNumberVector");

        auto elements = saf::decomposeVector(roots[0], "Number", "new");
        assert(elements.size() == 1);
        assert(elements[0].device == "Filter Wheel");
        assert(elements[0].value == "4");

        delXMLEle(roots[0]);
        delLilXML(lp);
        std::printf("Test 3 (recomposeVectorXml round-trips through real parser): PASS\n");
    }

    std::printf("All tests passed.\n");
    return 0;
}
