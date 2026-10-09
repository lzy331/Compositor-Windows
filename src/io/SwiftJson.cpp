#include "io/SwiftJson.h"

#include "io/ProjectError.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace compositor::swiftjson {
namespace {

constexpr int maxNestingDepth = 512;

void appendIndent(std::string& out, int depth) {
    out.append(static_cast<size_t>(depth) * 2, ' ');
}

}  // namespace

JsonValue JsonValue::makeBool(bool value) {
    JsonValue json;
    json.kind = Kind::Bool;
    json.boolValue = value;
    return json;
}

JsonValue JsonValue::makeInt(int64_t value) {
    JsonValue json;
    json.kind = Kind::Int;
    json.intValue = value;
    return json;
}

JsonValue JsonValue::makeDouble(double value) {
    JsonValue json;
    json.kind = Kind::Double;
    json.doubleValue = value;
    return json;
}

JsonValue JsonValue::makeString(std::string value) {
    JsonValue json;
    json.kind = Kind::String;
    json.stringValue = std::move(value);
    return json;
}

JsonValue JsonValue::makeArray(std::vector<JsonValue> value) {
    JsonValue json;
    json.kind = Kind::Array;
    json.arrayValue = std::move(value);
    return json;
}

JsonValue JsonValue::makeObject(std::map<std::string, JsonValue> value) {
    JsonValue json;
    json.kind = Kind::Object;
    json.objectValue = std::move(value);
    return json;
}

std::string swiftDoubleString(double value) {
    if (value == 0) return std::signbit(value) ? "-0" : "0";
    if (!std::isfinite(value)) throw ProjectError::encode();

    const bool negative = std::signbit(value);
    const double magnitude = negative ? -value : value;

    // Shortest round-trip digits in scientific form, e.g. "4.2e+01".
    char buffer[32];
    const std::to_chars_result converted =
        std::to_chars(buffer, buffer + sizeof(buffer), magnitude, std::chars_format::scientific);
    if (converted.ec != std::errc()) throw ProjectError::encode();
    const char* const begin = buffer;
    const char* const end = converted.ptr;
    const char* const exponentMark = std::find(begin, end, 'e');

    std::string digits;
    digits.reserve(static_cast<size_t>(exponentMark - begin));
    for (const char* it = begin; it != exponentMark; ++it)
        if (*it != '.') digits.push_back(*it);

    int decimalExponent = 0;
    bool exponentNegative = false;
    for (const char* it = exponentMark + 1; it != end; ++it) {
        if (*it == '-') exponentNegative = true;
        else if (*it != '+') decimalExponent = decimalExponent * 10 + (*it - '0');
    }
    if (exponentNegative) decimalExponent = -decimalExponent;

    // Swift forces exponential notation above a binary-exponent threshold: values in [2^53, 2^54)
    // print decimally only when exactly a power of two, everything above is always exponential.
    uint64_t bits = 0;
    std::memcpy(&bits, &magnitude, sizeof(bits));
    const int exponentBits = static_cast<int>((bits >> 52) & 0x7FF);
    const bool forceExponential =
        exponentBits > 1076 || (exponentBits == 1076 && (bits & 0xFFFFFFFFFFFFFull) != 0);

    std::string out = negative ? "-" : "";
    const int digitCount = static_cast<int>(digits.size());
    if (decimalExponent < -4 || forceExponential) {
        out.push_back(digits[0]);
        if (digitCount > 1) {
            out.push_back('.');
            out.append(digits, 1, std::string::npos);
        }
        out.push_back('e');
        out.push_back(decimalExponent < 0 ? '-' : '+');
        const int printed = decimalExponent < 0 ? -decimalExponent : decimalExponent;
        if (printed < 10) {
            out.push_back('0');
            out.push_back(static_cast<char>('0' + printed));
        } else {
            out += std::to_string(printed);
        }
    } else if (decimalExponent < 0) {
        out += "0.";
        out.append(static_cast<size_t>(-decimalExponent - 1), '0');
        out += digits;
    } else if (decimalExponent + 1 < digitCount) {
        out.append(digits, 0, static_cast<size_t>(decimalExponent) + 1);
        out.push_back('.');
        out.append(digits, static_cast<size_t>(decimalExponent) + 1, std::string::npos);
    } else {
        // Whole number; the JSON layer drops description's trailing ".0", so none is written.
        out += digits;
        out.append(static_cast<size_t>(decimalExponent) + 1 - digits.size(), '0');
    }
    return out;
}

std::string escapedString(const std::string& utf8) {
    static constexpr char hexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(utf8.size());
    for (const char raw : utf8) {
        const unsigned char byte = static_cast<unsigned char>(raw);
        switch (byte) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '/': out += "\\/"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (byte < 0x20) {
                    out += "\\u00";
                    out.push_back(hexDigits[byte >> 4]);
                    out.push_back(hexDigits[byte & 0xF]);
                } else {
                    out.push_back(raw);
                }
        }
    }
    return out;
}

namespace {

void writeValue(const JsonValue& value, std::string& out, int depth);

// `level` is the container's own nesting level: the root container is 1, and every
// member of a level-L container is written at level L. The Swift writer guards the
// same way (serializeJSON passes depth+1 and containers require depth < 512).
void writeArray(const JsonValue& value, std::string& out, int level) {
    if (level >= maxNestingDepth) throw ProjectError::invalid();
    out.push_back('[');
    out.push_back('\n');
    bool first = true;
    for (const JsonValue& element : value.arrayValue) {
        if (!first) out += ",\n";
        first = false;
        appendIndent(out, level);
        writeValue(element, out, level);
    }
    out.push_back('\n');
    appendIndent(out, level - 1);
    out.push_back(']');
}

void writeObject(const JsonValue& value, std::string& out, int level) {
    if (level >= maxNestingDepth) throw ProjectError::invalid();
    out.push_back('{');
    out.push_back('\n');
    bool first = true;
    for (const auto& [key, member] : value.objectValue) {
        if (!first) out += ",\n";
        first = false;
        appendIndent(out, level);
        out.push_back('"');
        out += escapedString(key);
        out += "\" : ";
        writeValue(member, out, level);
    }
    out.push_back('\n');
    appendIndent(out, level - 1);
    out.push_back('}');
}

void writeValue(const JsonValue& value, std::string& out, int depth) {
    switch (value.kind) {
        case JsonValue::Kind::Null: out += "null"; break;
        case JsonValue::Kind::Bool: out += value.boolValue ? "true" : "false"; break;
        case JsonValue::Kind::Int: out += std::to_string(value.intValue); break;
        case JsonValue::Kind::Double: out += swiftDoubleString(value.doubleValue); break;
        case JsonValue::Kind::String:
            out.push_back('"');
            out += escapedString(value.stringValue);
            out.push_back('"');
            break;
        case JsonValue::Kind::Array: writeArray(value, out, depth + 1); break;
        case JsonValue::Kind::Object: writeObject(value, out, depth + 1); break;
    }
}

}  // namespace

std::string serialize(const JsonValue& value) {
    std::string out;
    writeValue(value, out, 0);
    return out;
}

}  // namespace compositor::swiftjson
