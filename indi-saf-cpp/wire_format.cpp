// wire_format.cpp
#include "wire_format.hpp"
#include <stdexcept>
#include <sstream>
#include <vector>

namespace ssf {

namespace {

void checkSafe(const std::string& field, const char* fieldName) {
    if (field.find('|') != std::string::npos || field.find('\n') != std::string::npos) {
        throw std::runtime_error(
            std::string("wire_format: field '") + fieldName +
            "' contains an unescaped delimiter or newline: " + field);
    }
}

std::string emitOpt(const std::optional<std::string>& v) {
    return v.has_value() ? *v : std::string();
}

} // namespace

std::string encodeWireMessage(const OutboundElement& el) {
    checkSafe(el.device, "device");
    checkSafe(el.property, "property");
    checkSafe(el.element, "element");
    checkSafe(el.vec_type, "vec_type");
    checkSafe(el.value, "value");

    std::ostringstream oss;
    oss << el.device << '|'
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

    if (fields.size() != 12) {
        throw std::runtime_error(
            "wire_format: malformed line, expected 12 fields, got " +
            std::to_string(fields.size()) + ": " + line);
    }

    OutboundElement el;
    el.device   = fields[0];
    el.property = fields[1];
    el.element  = fields[2];
    el.vec_type = fields[3];
    el.value    = fields[4];
    auto optOrNone = [](const std::string& s) -> std::optional<std::string> {
        return s.empty() ? std::nullopt : std::optional<std::string>(s);
    };
    el.vec_state   = optOrNone(fields[5]);
    el.vec_perm    = optOrNone(fields[6]);
    el.vec_timeout = optOrNone(fields[7]);
    el.vec_ts      = optOrNone(fields[8]);
    el.vec_label   = optOrNone(fields[9]);
    el.vec_group   = optOrNone(fields[10]);
    el.elem_label  = optOrNone(fields[11]);
    return el;
}

} // namespace ssf
