// indi_xml_bridge.cpp
//
// XMLEle API usage below has been checked against a real lilxml.h
// (Brian supplied it directly) -- see indi_xml_bridge.hpp for what
// changed vs. the original unverified draft.

#include "indi_xml_bridge.hpp"
#include <sstream>
#include <stdexcept>

namespace saf {

std::vector<OutboundElement> decomposeVector(XMLEle* vectorRoot,
                                              const std::string& vecTypeName) {
    std::vector<OutboundElement> out;

    // Confirmed against the real liblilxml.h (Brian supplied it directly):
    // findXMLAttValu(XMLEle*, const char*) returns a plain char* value
    // (assigned into std::string here); the header doesn't document
    // its behavior on a missing attribute, but the common liblilxml
    // implementation returns "" rather than NULL in that case.
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

    // Confirmed against the real liblilxml.h: there is no index-based
    // child accessor (no "nthXMLEle") -- child iteration is stateful.
    // nXMLEle(ep) returns the child count (used here only to reserve
    // vector capacity); the actual walk uses nextXMLEle(ep, first),
    // called with first=1 to get the first child and first=0 on each
    // subsequent call, until it returns NULL. This matches the usage
    // example in lilxml.h itself.
    out.reserve(static_cast<size_t>(nXMLEle(vectorRoot)));
    for (XMLEle* child = nextXMLEle(vectorRoot, 1); child != nullptr;
         child = nextXMLEle(vectorRoot, 0)) {

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

        el.element = findXMLAttValu(child, "name");
        if (el.element.empty()) {
            // Per spec, name is #REQUIRED on every element -- treat a
            // missing one as malformed input and skip rather than
            // insert a garbage key.
            continue;
        }

        std::string label = findXMLAttValu(child, "label");
        el.elem_label = label.empty() ? std::nullopt
                                       : std::optional<std::string>(label);

        // Confirmed: pcdataXMLEle(XMLEle*) returns char* (text content
        // between the tags).
        const char* value = pcdataXMLEle(child);
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
