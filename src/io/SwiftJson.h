#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Byte-exact reimplementation of the Swift Foundation JSON writer as used by the
// macOS app (JSONEncoder with [.prettyPrinted, .sortedKeys]).
//
// Pretty-print semantics (from swift-corelibs-foundation JSONWriter.swift):
//   member indent starts at 0; containers recurse with depth+1 guarded by
//   depth < 512; an object at level L writes "{\n", indents, elements joined by
//   ",\n"+indent, key + " : " + value, then "\n" + indent(L) + "}"; arrays write
//   the indent before every element including the first; empty containers render
//   as "{\n\n<indent(L)>}". Keys are sorted bytewise (std::map<std::string>).
//
// Double formatting (FloatingPointToString.swift _Float64ToASCII):
//   shortest round-trip digits, exponential iff the decimal exponent p satisfies
//   p < -4, or the binary exponent is > 1 (or == 1 with a non-boundary
//   significand, i.e. exponent bits > 0x434); exponents are always signed and
//   padded to at least two digits; JSONEncoder strips a trailing ".0".
namespace compositor::swiftjson {

struct JsonValue {
    enum class Kind { Null, Bool, Int, Double, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolValue = false;
    int64_t intValue = 0;
    double doubleValue = 0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::map<std::string, JsonValue> objectValue;

    JsonValue() = default;
    static JsonValue makeNull() { return {}; }
    static JsonValue makeBool(bool value);
    static JsonValue makeInt(int64_t value);
    static JsonValue makeDouble(double value);
    static JsonValue makeString(std::string value);
    static JsonValue makeArray(std::vector<JsonValue> value);
    static JsonValue makeObject(std::map<std::string, JsonValue> value);
};

// Swift `Double.description` then the JSON layer's ".0" strip.
std::string swiftDoubleString(double value);

// Pretty-printed JSON, keys sorted bytewise. Throws ProjectError on nesting
// deeper than 512 (matching the Swift writer's guard).
std::string serialize(const JsonValue& value);

// Escapes a string the way Swift's JSON writer does: quote, backslash and slash
// escaped; \b \f \n \r \t for those controls; other C0 controls as \u00xx with
// lowercase hex; everything else passes through as UTF-8.
std::string escapedString(const std::string& utf8);

} // namespace compositor::swiftjson
