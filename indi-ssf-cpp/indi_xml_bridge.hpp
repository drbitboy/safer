// indi_xml_bridge.hpp
//
// *** CAVEAT ***
// This file assumes the standard liblilxml/libindi-family C API
// (LilXML*, XMLEle*, XMLAtt*, newLilXML, readXMLEle, findXMLEle,
// findXMLAtt, findAttValu, editXMLEle, etc.) that is consistent across
// INDI-derived codebases. I was NOT able to browse the actual headers
// in magao-x/MagAOX/INDI/liblilxml this session -- GitHub's file/raw
// content did not come back through search or fetch. Every call below
// is marked // VERIFY: -- check function names, parameter order/types,
// and header paths against the real MagAO-X source
// (INDI/liblilxml/lilxml.h, INDI/libcommon/) before compiling.
//
// Everything else in this bridge (the decompose/recompose *logic*, the
// OutboundElement shape, the wire format) does not depend on this
// caveat and can be trusted as specified in the design doc.

#pragma once
#include <string>
#include <vector>
#include "outbound_store.hpp"

// VERIFY: header path/name for MagAO-X's fork of liblilxml.
extern "C" {
#include "lilxml.h"   // VERIFY: exact path, e.g. "INDI/liblilxml/lilxml.h"
}

namespace ssf {

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
// VERIFY: XMLEle*, findXMLAtt, findXMLEle, valuXMLAtt / cdataXMLEle (or
// whatever MagAO-X's fork calls the "get text content of this element"
// accessor -- names vary slightly between libindi forks) all need
// confirming against the real header.
std::vector<OutboundElement> decomposeVector(XMLEle* vectorRoot, // VERIFY: type
                                              const std::string& vecTypeName);
    // vecTypeName: "Text" | "Number" | "Switch" | "Light" -- caller
    // determines this from the outer tag name (e.g. "setNumberVector")
    // before calling in, since the child tag name alone
    // (oneNumber/defNumber) doesn't distinguish set* from def*.

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

} // namespace ssf
