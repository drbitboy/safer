// wire_format.cpp
#include "wire_format.hpp"
#include <stdexcept>
#include <sstream>
#include <vector>

namespace saf {

namespace {

// Delimiter-only check for fields that never touch a filename (they
// can freely contain '/', '.', etc. -- e.g. a floating-point value
// like "3.5", or a device path).
void checkSafe(const std::string& field, const char* fieldName) {
    if (field.find('|') != std::string::npos || field.find('\n') != std::string::npos) {
        throw std::runtime_error(
            std::string("wire_format: field '") + fieldName +
            "' contains an unescaped delimiter or newline: " + field);
    }
}

// Stricter check for the four fields that ALSO become part of an
// OutboundStore filename (see outbound_store.cpp's keyBasename):
// msg_type, device, property, element. '/' would corrupt the path;
// checked again here (in addition to OutboundStore's own check) so
// encodeWireMessage() fails fast regardless of caller.
void checkSafeAndFilename(const std::string& field, const char* fieldName) {
    checkSafe(field, fieldName);
    if (field.find('/') != std::string::npos) {
        throw std::runtime_error(
            std::string("wire_format: field '") + fieldName +
            "' contains '/', unsafe for use in an OutboundStore filename: " + field);
    }
}

std::string emitOpt(const std::optional<std::string>& v) {
    return v.has_value() ? *v : std::string();
}

} // namespace

std::string encodeWireMessage(const OutboundElement& el) {
    checkSafeAndFilename(el.msg_type, "msg_type");
    checkSafeAndFilename(el.device, "device");
    checkSafeAndFilename(el.property, "property");
    checkSafeAndFilename(el.element, "element");
    checkSafe(el.vec_type, "vec_type");
    checkSafe(el.value, "value");

    std::ostringstream oss;
    oss << el.msg_type << '|'
        << el.device << '|'
        << el.property << '|'
        << el.element << '|'
        << el.vec_type << '|'
        << el.value << '|'
        << emitOpt(el.vec_state) << '|'
        << emitOpt(el.vec_perm) << '|'
        << emitOpt(el.vec_timeout) << '|'
        << emitOpt(el.vec_ts) << '|'
        << emitOpt(el.vec_label) << '|'
        << emitOpt(el.vec_group) << '|'
        << emitOpt(el.elem_label);
    return oss.str();
}

OutboundElement decodeWireMessage(const std::string& line) {
    std::vector<std::string> fields;
    std::string cur;
    for (char c : line) {
        if (c == '|') {
            fields.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    fields.push_back(cur);

    if (fields.size() != 13) {
        throw std::runtime_error(
            "wire_format: malformed line, expected 13 fields, got " +
            std::to_string(fields.size()) + ": " + line);
    }

    OutboundElement el;
    el.msg_type = fields[0];
    el.device   = fields[1];
    el.property = fields[2];
    el.element  = fields[3];
    el.vec_type = fields[4];
    el.value    = fields[5];
    auto optOrNone = [](const std::string& s) -> std::optional<std::string> {
        return s.empty() ? std::nullopt : std::optional<std::string>(s);
    };
    el.vec_state   = optOrNone(fields[6]);
    el.vec_perm    = optOrNone(fields[7]);
    el.vec_timeout = optOrNone(fields[8]);
    el.vec_ts      = optOrNone(fields[9]);
    el.vec_label   = optOrNone(fields[10]);
    el.vec_group   = optOrNone(fields[11]);
    el.elem_label  = optOrNone(fields[12]);
    return el;
}

} // namespace saf
