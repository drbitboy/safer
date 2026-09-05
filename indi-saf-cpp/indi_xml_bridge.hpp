// indi_xml_bridge.hpp
//
// *** UPDATE ***
// The liblilxml API used below was originally written from general
// knowledge of libindi-family C APIs (unable to browse the actual
// MagAO-X headers in that session), with every call marked // VERIFY.
// Brian has since supplied MagAO-X's own fork of lilxml.h directly,
// and it has been checked against that:
//   - findXMLAttValu, pcdataXMLEle, nXMLEle: confirmed exact match.
//   - nthXMLEle: does NOT exist in liblilxml -- there is no index-based
//     child accessor. Child iteration is stateful via
//     nextXMLEle(XMLEle *ep, int first) (first=1 for the first child,
//     first=0 on each subsequent call, NULL when done). decomposeVector()
//     below has been rewritten to use this instead of the nonexistent
//     nthXMLEle.
//
// findXMLAttValu's behavior on a missing attribute isn't documented in
// the header itself (assumed to return "" based on common liblilxml
// behavior) -- otherwise this API surface is now fully confirmed
// against the actual fork this bridge targets.
//
// Everything else in this bridge (the decompose/recompose *logic*, the
// OutboundElement shape, the wire format) does not depend on this
// caveat and can be trusted as specified in the design doc.

#pragma once
#include <string>
#include <vector>
#include "outbound_store.hpp"

// VERIFY: header path/name -- Brian's copy is confirmed to be
// MagAO-X's own fork of liblilxml, so the API surface is right;
// just confirm the actual include path in the MagAO-X source tree
// (e.g. "INDI/liblilxml/lilxml.h") matches this bridge's build setup.
extern "C" {
#include "lilxml.h"   // VERIFY: exact path, e.g. "INDI/liblilxml/lilxml.h"
}

namespace saf {

// --- 1a: decompose -----------------------------------------------------
//
// Given one already-parsed INDI vector element (e.g. a setNumberVector
// or defTextVector received from the local indiserver), produces one
// OutboundElement per child element (oneNumber/defNumber/oneSwitch/...).
// Does NOT touch the database -- caller (1a's read loop) is responsible
// for calling OutboundStore::upsert() on each returned element.
//
// device/property/element/value/vec_type are always populated.
// vec_state/vec_perm/vec_timeout/vec_ts/vec_label/vec_group/elem_label
// are populated only if present as XML attributes on the vector or
// child element (per the spec, most are #IMPLIED / optional).
//
// XMLEle*, findXMLAttValu, pcdataXMLEle, nXMLEle, and nextXMLEle are
// all confirmed against MagAO-X's own fork of lilxml.h (see the
// UPDATE note above).
std::vector<OutboundElement> decomposeVector(XMLEle* vectorRoot,
                                              const std::string& vecTypeName,
                                              const std::string& msgType);
    // vecTypeName: "Text" | "Number" | "Switch" | "Light" -- caller
    // determines this from the outer tag name (e.g. "setNumberVector")
    // before calling in, since the child tag name alone
    // (oneNumber/defNumber) doesn't distinguish set* from def*.
    // msgType: "def" | "set" | "new" -- also derived by the caller
    // from the same outer tag name (e.g. "setNumberVector" -> "set").
    // Populates OutboundElement::msg_type on every returned element;
    // required by outbound_store.hpp's file-naming scheme.

// --- 1b: recompose ------------------------------------------------------
//
// Given ONE OutboundElement-shaped record decoded off the wire (see
// wire_format.hpp), builds a minimal-but-valid INDI vector XML string
// containing exactly that one child element, suitable for sending to
// the local indiserver. Per the protocol spec, partial vectors (only
// some members present) are explicitly permitted for setXXXVector, so
// a single-element vector is valid on the wire even though it's
// unusual.
//
// Returns plain text XML (not a parsed XMLEle*) since the destination
// is a socket/FIFO write to indiserver, not further in-process parsing.
//
// NOTE: this function does NOT use liblilxml at all -- it's simple
// string templating, since we're only ever emitting one element per
// vector and don't need a general XML tree builder for that. This
// keeps it independent of the VERIFY caveat above.
std::string recomposeVectorXml(const OutboundElement& el, bool isSetNotDef);
    // isSetNotDef: true  -> emit <setXXXVector>...</setXXXVector>
    //              false -> emit <defXXXVector>...</defXXXVector>
    // 1b should use setXXXVector for ordinary forwarded updates; def*
    // is only relevant if this bridge is ever responsible for the
    // very first definition of a property to a fresh indiserver
    // (not the common case for a running bridge).

} // namespace saf
