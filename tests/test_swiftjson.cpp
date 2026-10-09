// Verifies SwiftJson against the exact outputs of Swift's Float64 description with the
// JSON layer's ".0" strip, and against JSONWriter's pretty-print layout.

#include "io/ProjectError.h"
#include "io/SwiftJson.h"

#include <cstdio>
#include <string>

using compositor::ProjectError;
using compositor::swiftjson::JsonValue;
namespace swiftjson = compositor::swiftjson;

namespace {

int failures = 0;

std::string display(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

void check(const std::string& actual, const std::string& expected, const std::string& what) {
    if (actual != expected) {
        std::printf("FAIL %s\n  expected: %s\n  actual:   %s\n", what.c_str(),
                    display(expected).c_str(), display(actual).c_str());
        ++failures;
    }
}

void checkDouble(double value, const std::string& expected) {
    check(swiftjson::swiftDoubleString(value), expected, "double " + display(expected));
}

JsonValue object(std::map<std::string, JsonValue> members) { return JsonValue::makeObject(std::move(members)); }
JsonValue array(std::vector<JsonValue> elements) { return JsonValue::makeArray(std::move(elements)); }
JsonValue str(const char* text) { return JsonValue::makeString(text); }

}  // namespace

int main() {
    // Zero and small integers.
    checkDouble(0.0, "0");
    checkDouble(-0.0, "-0");
    checkDouble(1.0, "1");
    checkDouble(42.0, "42");
    checkDouble(72.0, "72");
    checkDouble(96.0, "96");
    checkDouble(300.0, "300");
    checkDouble(-90.0, "-90");
    checkDouble(-123.25, "-123.25");

    // Fractions.
    checkDouble(0.5, "0.5");
    checkDouble(0.75, "0.75");
    checkDouble(0.1, "0.1");
    checkDouble(12.5, "12.5");
    checkDouble(1.5, "1.5");
    checkDouble(0.0001, "0.0001");
    checkDouble(0.00012, "0.00012");
    checkDouble(123456789.5, "123456789.5");
    checkDouble(3.141592653589793, "3.141592653589793");

    // The decimal/exponential boundary: 2^53 stays decimal, 2^53+2 does not.
    checkDouble(1000000000000000.0, "1000000000000000");
    checkDouble(5000000000000000.0, "5000000000000000");
    checkDouble(9007199254740992.0, "9007199254740992");
    checkDouble(9007199254740994.0, "9.007199254740994e+15");
    checkDouble(9500000000000000.0, "9.5e+15");
    checkDouble(1e16, "1e+16");
    checkDouble(1.5e16, "1.5e+16");
    checkDouble(1e21, "1e+21");

    // Exponents.
    checkDouble(1e-5, "1e-05");
    checkDouble(-1e-5, "-1e-05");
    checkDouble(1e300, "1e+300");
    checkDouble(1e-300, "1e-300");
    checkDouble(5e-324, "5e-324");
    checkDouble(2.2250738585072014e-308, "2.2250738585072014e-308");
    checkDouble(1.7976931348623157e308, "1.7976931348623157e+308");

    // Non-finite doubles cannot be encoded.
    try {
        swiftjson::swiftDoubleString(std::nan(""));
        std::printf("FAIL nan did not throw\n");
        ++failures;
    } catch (const ProjectError& error) {
        if (error.kind() != ProjectError::Kind::Encode) {
            std::printf("FAIL nan threw the wrong error kind\n");
            ++failures;
        }
    }

    // Pretty printing: keys sorted bytewise, two-space indents, " : " separators,
    // empty containers on two blank lines, slashes escaped.
    check(swiftjson::serialize(object({})), "{\n\n}", "empty object");
    check(swiftjson::serialize(array({})), "[\n\n]", "empty array");
    check(swiftjson::serialize(object({{"b", JsonValue::makeInt(1)}, {"a", JsonValue::makeInt(2)}})),
          "{\n  \"a\" : 2,\n  \"b\" : 1\n}", "sorted keys");
    check(swiftjson::serialize(array({JsonValue::makeInt(1), JsonValue::makeBool(true), JsonValue::makeNull()})),
          "[\n  1,\n  true,\n  null\n]", "array elements");
    check(swiftjson::serialize(object({{"a", object({})}})), "{\n  \"a\" : {\n\n  }\n}", "nested empty object");
    check(swiftjson::serialize(object({{"origin", array({JsonValue::makeDouble(10), JsonValue::makeDouble(20)})}})),
          "{\n  \"origin\" : [\n    10,\n    20\n  ]\n}", "nested point array");
    check(swiftjson::serialize(object({{"format", str("com.compositor.project")}, {"version", JsonValue::makeInt(11)},
                                      {"activeLayerID", JsonValue::makeNull()}})),
          "{\n  \"activeLayerID\" : null,\n  \"format\" : \"com.compositor.project\",\n  \"version\" : 11\n}",
          "manifest fragment");

    // String escaping.
    check(swiftjson::escapedString("a/b\\c\"d"), "a\\/b\\\\c\\\"d", "basic escapes");
    check(swiftjson::escapedString(std::string("\x01\x1f")), "\\u0001\\u001f", "control escapes");
    check(swiftjson::escapedString("tab\there\n"), "tab\\there\\n", "whitespace escapes");

    // Deep nesting beyond the writer's guard is rejected, not crashed on.
    JsonValue deep = JsonValue::makeInt(1);
    for (int level = 0; level < 600; ++level) deep = array({deep});
    try {
        swiftjson::serialize(deep);
        std::printf("FAIL deep nesting did not throw\n");
        ++failures;
    } catch (const ProjectError&) {
    }

    if (failures == 0) std::printf("test_swiftjson: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
