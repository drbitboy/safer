// indi_xml_bridge.cpp
//
// See the *** CAVEAT *** at the top of indi_xml_bridge.hpp before
// relying on decomposeVector(). recomposeVectorXml() has no such
// dependency.

#include "indi_xml_bridge.hpp"
#include <sstream>
#include <stdexcept>

namespace saf {

std::vector<OutboundElement> decomposeVector(XMLEle* vectorRoot,
                                              const std::string& vecTypeName) {
    std::vector<OutboundElement> out;

    // VERIFY: attribute accessor name/signature. Common libindi form:
    //   const char* findXMLAttValu(XMLEle *ele, const char *name);
    // which returns "" (not nullptr) if the attribute is absent.
    std::string device   = findXMLAttValu(vectorRoot, "device");
    std::string property = findXMLAttValu(vectorRoot, "name");

    auto attrOrNullopt = [&](const char* attrName) -> std::optional<std::string> {
        std::string v = findXMLAttValu(vectorRoot, attrName);
        return v.empty() ? std::nullopt : std::optional<std::string>(v);
    };

    auto vecState   = attrOrNullopt("state");
    auto vecPerm    = attrOrNullopt("perm");
    auto vecTimeout = attrOrNullopt("timeout");
    auto vecTs      = attrOrNullopt("timestamp");
    auto vecLabel   = attrOrNullopt("label");
    auto vecGroup   = attrOrNullopt("group");

    if (device.empty() || property.empty()) {
        throw std::runtime_error(
            "decomposeVector: vector element missing required device/name attribute");
    }

    // VERIFY: child-iteration API. Common libindi form:
    //   int nXMLEle(XMLEle *ele);
    //   XMLEle* nextXMLEle(XMLEle *ele, int init);   // or similar iterator
    // Sketch below uses a generic "for each child" idiom -- adjust to
    // whatever MagAO-X's fork actually exposes (could be
    // childrenXMLEle()/an index-based loop, etc.)
    int nChildren = nXMLEle(vectorRoot);              // VERIFY
    for (int i = 0; i < nChildren; ++i) {
        XMLEle* child = nthXMLEle(vectorRoot, i);       // VERIFY

        OutboundElement el;
        el.device   = device;
        el.property = property;
        el.vec_type = vecTypeName;
        el.vec_state   = vecState;
        el.vec_perm    = vecPerm;
        el.vec_timeout = vecTimeout;
        el.vec_ts      = vecTs;
        el.vec_label   = vecLabel;
        el.vec_group   = vecGroup;

        el.element = findXMLAttValu(child, "name");     // VERIFY
        if (el.element.empty()) {
            // Per spec, name is #REQUIRED on every element -- treat a
            // missing one as malformed input and skip rather than
            // insert a garbage key.
            continue;
        }

        std::string label = findXMLAttValu(child, "label"); // VERIFY
        el.elem_label = label.empty() ? std::nullopt
                                       : std::optional<std::string>(label);

        // VERIFY: text-content accessor. Common libindi form:
        //   char* pcdataXMLEle(XMLEle *ele);
        // (returns the raw PCDATA / text content between the tags)
        const char* value = pcdataXMLEle(child);         // VERIFY
        el.value = value ? value : "";

        out.push_back(std::move(el));
    }

    return out;
}

std::string recomposeVectorXml(const OutboundElement& el, bool isSetNotDef) {
    // Self-contained string templating -- deliberately does NOT use
    // liblilxml, so it has no dependency on the unverified API above.
    // device/property/element/value are attacker-free here (already
    // validated as delimiter-free by wire_format::checkSafe on encode),
    // but XML-escaping is still applied defensively since this string
    // is sent directly to indiserver.
    auto xmlEscape = [](const std::string& s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '&':  out += "&amp;";  break;
                case '<':  out += "&lt;";   break;
                case '>':  out += "&gt;";   break;
                case '"':  out += "&quot;"; break;
                default:   out += c;        break;
            }
        }
        return out;
    };

    const std::string prefix = isSetNotDef ? "set" : "def";
    const std::string vectorTag = prefix + el.vec_type + "Vector";
    const std::string oneTag    = isSetNotDef ? ("one" + el.vec_type)
                                               : ("def" + el.vec_type);

    std::ostringstream oss;
    oss << "<" << vectorTag
        << " device=\"" << xmlEscape(el.device) << "\""
        << " name=\""   << xmlEscape(el.property) << "\"";
    if (el.vec_state)   oss << " state=\""   << xmlEscape(*el.vec_state)   << "\"";
    if (el.vec_perm)    oss << " perm=\""    << xmlEscape(*el.vec_perm)    << "\"";
    if (el.vec_timeout) oss << " timeout=\"" << xmlEscape(*el.vec_timeout) << "\"";
    if (el.vec_ts)       oss << " timestamp=\"" << xmlEscape(*el.vec_ts)   << "\"";
    if (el.vec_label)    oss << " label=\""  << xmlEscape(*el.vec_label)   << "\"";
    if (el.vec_group)    oss << " group=\""  << xmlEscape(*el.vec_group)   << "\"";
    oss << ">\n";

    oss << "  <" << oneTag << " name=\"" << xmlEscape(el.element) << "\"";
    if (el.elem_label) oss << " label=\"" << xmlEscape(*el.elem_label) << "\"";
    oss << ">" << xmlEscape(el.value) << "</" << oneTag << ">\n";

    oss << "</" << vectorTag << ">\n";
    return oss.str();
}

} // namespace saf
