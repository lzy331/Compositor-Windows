// Verifies ManifestCodec against the Swift-synthesized Codable shape of ProjectStore.swift:
// byte-exact pretty printing, required-vs-optional key semantics, UUID and unkeyed-array forms,
// Int unboxing rules, ColorRange dictionary encodings, and round-trip fidelity.

#include "document/Manifest.h"
#include "io/ManifestCodec.h"
#include "io/ProjectError.h"

#include <cstdio>
#include <nlohmann/json.hpp>
#include <string>

using compositor::AdjustmentColor;
using compositor::AdjustmentKind;
using compositor::BlackWhiteSettings;
using compositor::BlendMode;
using compositor::CanvasGuide;
using compositor::ColorBalanceSettings;
using compositor::ColorOverlayEffect;
using compositor::ColorRange;
using compositor::CurvePoint;
using compositor::ExposureSettings;
using compositor::GradientMapSettings;
using compositor::GrainSettings;
using compositor::HueBand;
using compositor::HueSaturationSettings;
using compositor::InnerGlowEffect;
using compositor::InnerShadowEffect;
using compositor::LayerAdjustment;
using compositor::LayerEffects;
using compositor::LayerSampling;
using compositor::LayerShapeStyle;
using compositor::LayerTextColorRun;
using compositor::LayerTextFontRun;
using compositor::LayerTextStyle;
using compositor::LayerTransform;
using compositor::LevelRange;
using compositor::LevelsChannel;
using compositor::ManifestCodec;
using compositor::OuterGlowEffect;
using compositor::Point;
using compositor::ProjectError;
using compositor::ProjectLayerRecord;
using compositor::ProjectManifest;
using compositor::RangeAdjustment;
using compositor::ShapeKind;
using compositor::ShadowEffect;
using compositor::Size;
using compositor::StrokeEffect;
using compositor::TextAlignment;
using compositor::Uuid;

using Json = nlohmann::json;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool decodeThrowsInvalid(const std::string& text) {
    try {
        (void)ManifestCodec::decode(text);
        return false;
    } catch (const ProjectError& error) {
        return error.kind() == ProjectError::Kind::Invalid;
    }
}

void checkInvalid(const Json& document, const std::string& what) {
    if (document.is_string()) {
        if (!decodeThrowsInvalid(document.get<std::string>())) {
            std::printf("FAIL expected invalid: %s\n", what.c_str());
            ++failures;
        }
        return;
    }
    if (!decodeThrowsInvalid(document.dump())) {
        std::printf("FAIL expected invalid: %s\n", what.c_str());
        ++failures;
    }
}

void checkDecodes(const Json& document, const std::string& what) {
    const std::string text = document.is_string() ? document.get<std::string>() : document.dump();
    try {
        (void)ManifestCodec::decode(text);
    } catch (const ProjectError&) {
        std::printf("FAIL expected valid: %s\n", what.c_str());
        ++failures;
    }
}

Uuid uuid(const char* text) {
    const std::optional<Uuid> parsed = Uuid::parse(text);
    check(parsed.has_value(), std::string("test uuid parses: ") + text);
    return parsed.value_or(Uuid());
}

/// The base valid manifest every strictness case mutates.
Json base() {
    return Json{
        {"format", "com.compositor.project"},
        {"version", 11},
        {"colorSpace", "sRGB"},
        {"documentID", "00000000-0000-4000-8000-000000000001"},
        {"width", 1920},
        {"height", 1080},
        {"layers", Json::array()},
    };
}

Json minimalTransform() {
    return Json{
        {"origin", {0.0, 0.0}},
        {"size", {100.0, 200.0}},
        {"rotation", 0.0},
        {"flipX", false},
        {"flipY", false},
        {"sampling", "High quality"},
    };
}

/// The default LayerTransform: zero origin and size, no rotation or flips, high-quality sampling.
Json defaultTransformJson() {
    return Json{
        {"origin", {0.0, 0.0}},
        {"size", {0.0, 0.0}},
        {"rotation", 0.0},
        {"flipX", false},
        {"flipY", false},
        {"sampling", "High quality"},
    };
}

Json minimalRecord() {
    return Json{
        {"id", "11111111-2222-4333-8444-555555555555"},
        {"name", "Layer"},
        {"isVisible", true},
        {"transform", minimalTransform()},
    };
}

Json levelRangeJson() {
    return Json{{"black", 0.0}, {"gamma", 1.0}, {"white", 255.0}, {"outputBlack", 0.0}, {"outputWhite", 255.0}};
}

Json curveChannelJson() {
    return Json::array({Json{{"x", 0.0}, {"y", 0.0}}, Json{{"x", 255.0}, {"y", 255.0}}});
}

Json minimalAdjustment() {
    return Json{
        {"kind", "Hue/Saturation"},
        {"hue", 0.0},
        {"saturation", 0.0},
        {"lightness", 0.0},
        {"colorize", false},
        {"levels", Json{{"channel", "RGB"}, {"ranges", Json::array({levelRangeJson(), levelRangeJson(), levelRangeJson(), levelRangeJson()})}}},
        {"curves", Json{{"channel", "RGB"}, {"channels", Json::array({curveChannelJson(), curveChannelJson(), curveChannelJson(), curveChannelJson()})}}},
    };
}

Json bandJson(double a, double b, double c, double d) {
    return Json{{"falloffStart", a}, {"rangeStart", b}, {"rangeEnd", c}, {"falloffEnd", d}};
}

/// minimalAdjustment plus the hsvSettings block of the fully-populated layer: two range
/// adjustments and all seven default bands.
Json fullAdjustmentJson() {
    Json adjustment = minimalAdjustment();
    adjustment["hsvSettings"] = Json{
        {"range", "Reds"},
        {"colorize", true},
        {"invertRange", true},
        {"adjustments", Json::array({"Master", Json{{"hue", 10.0}, {"saturation", -5.0}, {"lightness", 2.5}},
                                     "Reds", Json{{"hue", -20.0}, {"saturation", 15.0}, {"lightness", 0.0}}})},
        {"bands", Json::array({"Master", bandJson(0.0, 0.0, 360.0, 360.0),
                               "Reds", bandJson(315.0, 345.0, 15.0, 45.0),
                               "Yellows", bandJson(15.0, 45.0, 75.0, 105.0),
                               "Greens", bandJson(75.0, 105.0, 135.0, 165.0),
                               "Cyans", bandJson(135.0, 165.0, 195.0, 225.0),
                               "Blues", bandJson(195.0, 225.0, 255.0, 285.0),
                               "Magentas", bandJson(255.0, 285.0, 315.0, 345.0)})},
    };
    return adjustment;
}

/// A fully-populated record, every optional set, matching makeFullManifest's second layer.
Json fullRecordJson() {
    return Json{
        {"id", "AAAAAAAA-BBBB-4CCC-8DDD-EEEEEEEEEEEE"},
        {"name", "L\xC3\xA4yer/1\n\"x\""},
        {"isVisible", false},
        {"transform", Json{{"origin", {12.5, -33.25}}, {"size", {640.0, 480.0}}, {"rotation", 45.5},
                           {"flipX", true}, {"flipY", false}, {"sampling", "Smooth"}}},
        {"imageFile", "images/aaaa.png"},
        {"parentID", "11111111-2222-4333-8444-555555555555"},
        {"isGroup", false},
        {"opacity", 0.5},
        {"blendMode", "Hue"},
        {"maskFile", "images/aaaa.mask.png"},
        {"maskEnabled", true},
        {"maskSourceID", "22222222-3333-4444-8555-666666666666"},
        {"adjustment", fullAdjustmentJson()},
        {"maskPlacement", Json{{"origin", {-5.5, 10.0}}, {"size", {128.0, 96.0}}, {"rotation", 90.0},
                               {"flipX", false}, {"flipY", true}, {"sampling", "Nearest"}}},
        {"maskLinked", false},
        {"shape", Json{{"kind", "Line"}, {"red", 0.5}, {"green", 0.25}, {"blue", 1.0},
                       {"cornerRadius", 0.0}, {"lineWidth", 4.0}, {"start", {0.0, 0.0}}, {"end", {1.0, 1.0}}}},
        {"effects", Json{
            {"stroke", Json{{"enabled", true}, {"size", 3.0}, {"red", 1.0}, {"green", 0.0}, {"blue", 0.0},
                            {"opacity", 0.9}, {"inside", true}}},
            {"shadow", Json{{"enabled", false}, {"angle", 45.0}, {"distance", 10.0}, {"blur", 5.0},
                            {"red", 0.0}, {"green", 0.0}, {"blue", 0.5}, {"opacity", 0.4}}},
            {"colorOverlay", Json{{"red", 0.2}, {"green", 0.4}, {"blue", 0.6}, {"opacity", 0.8}}},
            {"innerShadow", Json{{"enabled", true}, {"angle", 120.0}, {"distance", 4.0}, {"blur", 3.0},
                                 {"red", 0.0}, {"green", 1.0}, {"blue", 0.0}, {"opacity", 0.5}}},
            {"outerGlow", Json{{"size", 8.0}, {"red", 1.0}, {"green", 1.0}, {"blue", 0.0}, {"opacity", 0.6}}},
            {"innerGlow", Json{{"enabled", true}, {"size", 6.0}, {"red", 0.5}, {"green", 0.5}, {"blue", 0.5},
                               {"opacity", 1.0}}},
        }},
        {"text", Json{
            {"content", "H\xC3\xA9llo\nWorld"},
            {"fontName", "Helvetica Neue"},
            {"fontSize", 48.5},
            {"red", 0.1}, {"green", 0.2}, {"blue", 0.3},
            {"alignment", "Center"},
            {"tracking", 12.5},
            {"leading", 60.0},
            {"boxSize", {512.0, 256.0}},
            {"colorRuns", Json::array({Json{{"location", 0}, {"length", 5}, {"red", 1.0}, {"green", 0.0}, {"blue", 0.0}},
                                       Json{{"location", 6}, {"length", 5}, {"red", 0.0}, {"green", 1.0}, {"blue", 0.0}}})},
            {"fontRuns", Json::array({Json{{"location", 0}, {"length", 5}, {"fontName", "Arial"}},
                                      Json{{"location", 6}, {"length", 5}, {"fontName", "Consolas"}}})},
        }},
    };
}

/// Field-by-field clone of the JSON above as a C++ model.
ProjectManifest makeFullManifest() {
    ProjectManifest manifest;
    manifest.resolution = 300;
    manifest.documentID = uuid("00000000-0000-4000-8000-0000000000AA");
    manifest.width = 1920;
    manifest.height = 1080;
    manifest.activeLayerID = uuid("11111111-2222-4333-8444-555555555555");

    ProjectLayerRecord group;
    group.id = *manifest.activeLayerID;
    group.name = "Group";
    group.isVisible = true;
    group.isGroup = true;
    group.opacity = 0.75;
    group.blendMode = BlendMode::Multiply;

    ProjectLayerRecord layer;
    layer.id = uuid("AAAAAAAA-BBBB-4CCC-8DDD-EEEEEEEEEEEE");
    layer.name = "L\xC3\xA4yer/1\n\"x\"";
    layer.isVisible = false;
    layer.transform.origin = {12.5, -33.25};
    layer.transform.size = {640, 480};
    layer.transform.rotation = 45.5;
    layer.transform.flipX = true;
    layer.transform.sampling = LayerSampling::Smooth;
    layer.imageFile = "images/aaaa.png";
    layer.parentID = group.id;
    layer.isGroup = false;
    layer.opacity = 0.5;
    layer.blendMode = BlendMode::Hue;
    layer.maskFile = "images/aaaa.mask.png";
    layer.maskEnabled = true;
    layer.maskSourceID = uuid("22222222-3333-4444-8555-666666666666");
    layer.maskPlacement = LayerTransform{{-5.5, 10}, {128, 96}, 90, false, true, LayerSampling::Nearest};
    layer.maskLinked = false;

    LayerAdjustment adjustment;
    adjustment.kind = AdjustmentKind::Hsv;
    HueSaturationSettings hsv;
    hsv.range = ColorRange::Reds;
    hsv.colorize = true;
    hsv.invertRange = true;
    hsv.adjustments[ColorRange::Master] = RangeAdjustment{10, -5, 2.5};
    hsv.adjustments[ColorRange::Reds] = RangeAdjustment{-20, 15, 0};
    adjustment.hsvSettings = hsv;
    layer.adjustment = adjustment;

    layer.shape = LayerShapeStyle{ShapeKind::Line, 0.5, 0.25, 1.0, 0.0, 4.0, Point{0, 0}, Point{1, 1}};

    LayerEffects effects;
    StrokeEffect stroke;
    stroke.enabled = true;
    stroke.size = 3;
    stroke.red = 1;
    stroke.opacity = 0.9;
    stroke.inside = true;
    effects.stroke = stroke;
    ShadowEffect shadow;
    shadow.enabled = false;
    shadow.angle = 45;
    shadow.distance = 10;
    shadow.blur = 5;
    shadow.blue = 0.5;
    shadow.opacity = 0.4;
    effects.shadow = shadow;
    ColorOverlayEffect overlay;
    overlay.red = 0.2;
    overlay.green = 0.4;
    overlay.blue = 0.6;
    overlay.opacity = 0.8;
    effects.colorOverlay = overlay;
    InnerShadowEffect innerShadow;
    innerShadow.enabled = true;
    innerShadow.angle = 120;
    innerShadow.distance = 4;
    innerShadow.blur = 3;
    innerShadow.green = 1;
    innerShadow.opacity = 0.5;
    effects.innerShadow = innerShadow;
    OuterGlowEffect outerGlow;
    outerGlow.size = 8;
    outerGlow.red = 1;
    outerGlow.green = 1;
    outerGlow.blue = 0;
    outerGlow.opacity = 0.6;
    effects.outerGlow = outerGlow;
    InnerGlowEffect innerGlow;
    innerGlow.enabled = true;
    innerGlow.size = 6;
    innerGlow.red = 0.5;
    innerGlow.green = 0.5;
    innerGlow.blue = 0.5;
    innerGlow.opacity = 1;
    effects.innerGlow = innerGlow;
    layer.effects = effects;

    LayerTextStyle text;
    text.content = "H\xC3\xA9llo\nWorld";
    text.fontName = "Helvetica Neue";
    text.fontSize = 48.5;
    text.red = 0.1;
    text.green = 0.2;
    text.blue = 0.3;
    text.alignment = TextAlignment::Center;
    text.tracking = 12.5;
    text.leading = 60;
    text.boxSize = Size{512, 256};
    text.colorRuns = std::vector<LayerTextColorRun>{{0, 5, 1, 0, 0}, {6, 5, 0, 1, 0}};
    text.fontRuns = std::vector<LayerTextFontRun>{{0, 5, "Arial"}, {6, 5, "Consolas"}};
    layer.text = text;

    manifest.layers = {group, layer};

    CanvasGuide horizontal;
    horizontal.id = uuid("33333333-4444-4555-8666-777777777777");
    horizontal.axis = CanvasGuide::Axis::Horizontal;
    horizontal.position = 540;
    CanvasGuide vertical;
    vertical.id = uuid("44444444-5555-4666-8777-888888888888");
    vertical.axis = CanvasGuide::Axis::Vertical;
    vertical.position = -123.75;
    manifest.guides = std::vector<CanvasGuide>{horizontal, vertical};
    return manifest;
}

/// An array wrapped in `depth` further arrays, so `depth + 1` array levels in all.
Json nest(int depth) {
    Json value = Json::array();
    for (int index = 0; index < depth; ++index) value = Json::array({value});
    return value;
}

}  // namespace

int main() {
    // Byte-exact pretty printing: a minimal manifest, empty layers on two blank lines.
    ProjectManifest minimal;
    minimal.documentID = uuid("00000000-0000-4000-8000-000000000001");
    minimal.width = 1920;
    minimal.height = 1080;
    const std::string minimalText = ManifestCodec::encode(minimal);
    check(minimalText ==
              "{\n"
              "  \"colorSpace\" : \"sRGB\",\n"
              "  \"documentID\" : \"00000000-0000-4000-8000-000000000001\",\n"
              "  \"format\" : \"com.compositor.project\",\n"
              "  \"height\" : 1080,\n"
              "  \"layers\" : [\n"
              "\n"
              "  ],\n"
              "  \"version\" : 11,\n"
              "  \"width\" : 1920\n"
              "}",
          "minimal manifest bytes");

    // Byte-exact pretty printing with one layer: three container levels of indent.
    ProjectManifest one;
    one.documentID = minimal.documentID;
    one.width = 1920;
    one.height = 1080;
    ProjectLayerRecord record;
    record.id = uuid("11111111-2222-4333-8444-555555555555");
    record.name = "Layer";
    record.transform.size = {100, 200};
    one.layers = {record};
    check(ManifestCodec::encode(one) ==
              "{\n"
              "  \"colorSpace\" : \"sRGB\",\n"
              "  \"documentID\" : \"00000000-0000-4000-8000-000000000001\",\n"
              "  \"format\" : \"com.compositor.project\",\n"
              "  \"height\" : 1080,\n"
              "  \"layers\" : [\n"
              "    {\n"
              "      \"id\" : \"11111111-2222-4333-8444-555555555555\",\n"
              "      \"isVisible\" : true,\n"
              "      \"name\" : \"Layer\",\n"
              "      \"transform\" : {\n"
              "        \"flipX\" : false,\n"
              "        \"flipY\" : false,\n"
              "        \"origin\" : [\n"
              "          0,\n"
              "          0\n"
              "        ],\n"
              "        \"rotation\" : 0,\n"
              "        \"sampling\" : \"High quality\",\n"
              "        \"size\" : [\n"
              "          100,\n"
              "          200\n"
              "        ]\n"
              "      }\n"
              "    }\n"
              "  ],\n"
              "  \"version\" : 11,\n"
              "  \"width\" : 1920\n"
              "}",
          "one-layer manifest bytes");

    // Escaping through the codec: slashes, quotes and newlines.
    ProjectManifest escaped;
    escaped.documentID = minimal.documentID;
    escaped.width = 1;
    escaped.height = 1;
    ProjectLayerRecord escapedLayer;
    escapedLayer.id = record.id;
    escapedLayer.name = "a/b\"c";
    escaped.layers = {escapedLayer};
    const std::string escapedText = ManifestCodec::encode(escaped);
    check(escapedText.find("\"name\" : \"a\\/b\\\"c\"") != std::string::npos, "escaped name bytes");

    // Round trip: every field of a fully-populated manifest survives encode then decode.
    const ProjectManifest full = makeFullManifest();
    const std::string fullText = ManifestCodec::encode(full);
    const ProjectManifest reparsed = ManifestCodec::decode(fullText);
    check(reparsed == full, "full manifest round trip");
    check(ManifestCodec::encode(reparsed) == fullText, "full manifest byte stability");

    // The same full content, assembled as JSON with different key order, decodes equal too.
    Json fullJson = base();
    fullJson["resolution"] = 300;
    fullJson["documentID"] = "00000000-0000-4000-8000-0000000000AA";
    fullJson["activeLayerID"] = "11111111-2222-4333-8444-555555555555";
    fullJson["layers"] = Json::array({Json{{"id", "11111111-2222-4333-8444-555555555555"}, {"name", "Group"},
                                           {"isVisible", true}, {"transform", defaultTransformJson()},
                                           {"isGroup", true}, {"opacity", 0.75}, {"blendMode", "Multiply"}},
                                      fullRecordJson()});
    fullJson["guides"] = Json::array({Json{{"id", "33333333-4444-4555-8666-777777777777"},
                                           {"axis", "horizontal"}, {"position", 540.0}},
                                      Json{{"id", "44444444-5555-4666-8777-888888888888"},
                                           {"axis", "vertical"}, {"position", -123.75}}});
    try {
        check(ManifestCodec::decode(fullJson.dump()) == full, "full manifest from independent JSON");
    } catch (const ProjectError&) {
        std::printf("FAIL full manifest from independent JSON threw\n");
        ++failures;
    }

    // Required keys: missing or null is invalid, exactly as Swift's synthesized decode.
    checkDecodes(base(), "base manifest");
    for (const char* key : {"format", "version", "colorSpace", "documentID", "width", "height", "layers"}) {
        Json document = base();
        document.erase(key);
        checkInvalid(document, std::string("missing ") + key);
        Json nulled = base();
        nulled[key] = nullptr;
        checkInvalid(nulled, std::string("null ") + key);
    }
    checkInvalid("[]", "top-level array");
    checkInvalid("{oops", "malformed json");
    checkInvalid("", "empty text");
    checkDecodes(Json{{"format", "com.compositor.project"}, {"version", 11}, {"colorSpace", "sRGB"},
                      {"documentID", "00000000-0000-4000-8000-000000000001"}, {"width", 1920},
                      {"height", 1080}, {"layers", Json::array()}, {"futureKey", 42}},
                 "unknown keys are ignored");

    // Duplicate keys: the last value wins, as Foundation's parser does.
    checkDecodes(std::string("{\"format\":\"nope\",\"format\":\"com.compositor.project\",\"version\":11,")
                     .append("\"colorSpace\":\"sRGB\",\"documentID\":\"00000000-0000-4000-8000-000000000001\",")
                     .append("\"width\":1920,\"height\":1080,\"layers\":[]}"),
                 "duplicate keys last-wins");

    // Numbers: Swift's Int unboxing over the JSON number space.
    checkDecodes([] { Json d = base(); d["width"] = 1920.0; return d.dump(); }(), "width integral float");
    checkInvalid([] { Json d = base(); d["width"] = 1920.5; return d.dump(); }(), "width fractional");
    checkInvalid([] { Json d = base(); d["width"] = true; return d.dump(); }(), "width bool");
    checkInvalid([] { Json d = base(); d["width"] = "1920"; return d.dump(); }(), "width string");
    checkInvalid([] { Json d = base(); d["width"] = 9223372036854775808ull; return d.dump(); }(), "width 2^63");
    checkInvalid([] { Json d = base(); d["width"] = 1e30; return d.dump(); }(), "width 1e30");
    checkDecodes([] { Json d = base(); d["width"] = -9223372036854775807ll - 1; return d.dump(); }(), "width -2^63");
    checkInvalid([] { Json d = base(); d["version"] = 11.5; return d.dump(); }(), "version fractional");
    checkDecodes([] { Json d = base(); d["version"] = 1; return d.dump(); }(), "version 1 decodes");
    checkDecodes([] { Json d = base(); d["version"] = 12; return d.dump(); }(), "version 12 decodes (gate is validate)");
    checkInvalid([]{ Json d = base(); d["colorSpace"] = 5; return d.dump(); }(), "colorSpace number");

    // UUIDs parse case-insensitively and print uppercase.
    try {
        const Json document = [] { Json d = base(); d["documentID"] = "abcdefab-cdef-4abc-8def-abcdefabcdef"; return d.dump(); }();
        const ProjectManifest decoded = ManifestCodec::decode(document);
        check(decoded.documentID.toString() == "ABCDEFAB-CDEF-4ABC-8DEF-ABCDEFABCDEF", "uuid uppercase output");
    } catch (const ProjectError&) {
        std::printf("FAIL lowercase uuid rejected\n");
        ++failures;
    }
    checkInvalid([]{ Json d = base(); d["documentID"] = "not-a-uuid"; return d.dump(); }(), "bad uuid");
    checkInvalid([]{ Json d = base(); d["documentID"] = "00000000000040008000000000000001"; return d.dump(); }(),
                 "unhyphenated uuid");

    // Optionals: absent and null are nil; values survive.
    try {
        const ProjectManifest plain = ManifestCodec::decode(base().dump());
        check(!plain.resolution.has_value() && !plain.activeLayerID.has_value() && !plain.guides.has_value(),
              "absent optionals are nil");
        Json nulled = base();
        nulled["resolution"] = nullptr;
        nulled["activeLayerID"] = nullptr;
        nulled["guides"] = nullptr;
        const ProjectManifest decoded = ManifestCodec::decode(nulled.dump());
        check(!decoded.resolution.has_value() && !decoded.activeLayerID.has_value() && !decoded.guides.has_value(),
              "null optionals are nil");
        Json guides = base();
        guides["guides"] = Json::array();
        check(ManifestCodec::decode(guides.dump()).guides.has_value(), "empty guides array is present");
    } catch (const ProjectError&) {
        std::printf("FAIL optional handling threw\n");
        ++failures;
    }

    // Layer records: required keys, then each optional form.
    checkDecodes([] { Json d = base(); d["layers"] = Json::array({minimalRecord()}); return d.dump(); }(),
                 "minimal record");
    for (const char* key : {"id", "name", "isVisible", "transform"}) {
        Json document = base();
        Json item = minimalRecord();
        item.erase(key);
        document["layers"] = Json::array({item});
        checkInvalid(document, std::string("record missing ") + key);
    }
    for (const char* key : {"origin", "size", "rotation", "flipX", "flipY", "sampling"}) {
        Json document = base();
        Json item = minimalRecord();
        item["transform"].erase(key);
        document["layers"] = Json::array({item});
        checkInvalid(document, std::string("transform missing ") + key);
    }
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["transform"]["origin"] = {10.0}; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "origin one element");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["transform"]["origin"] = 10.0; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "origin not an array");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["transform"]["origin"] = {"a", 20.0}; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "origin non-number");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["isVisible"] = 1; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "isVisible number");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["blendMode"] = "multiply"; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "blendMode wrong case");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["transform"]["sampling"] = "smooth"; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "sampling wrong case");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["opacity"] = "0.5"; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "opacity string");

    try {
        Json document = base();
        Json item = minimalRecord();
        item["transform"]["origin"] = {10.0, 20.0, 30.0};
        item["blendMode"] = "Multiply";
        item["opacity"] = 0.5;
        item["parentID"] = "11111111-2222-4333-8444-555555555555";
        item["maskEnabled"] = false;
        document["layers"] = Json::array({item});
        const ProjectManifest decoded = ManifestCodec::decode(document.dump());
        check(decoded.layers.front().transform.origin == Point{10, 20}, "origin reads first two");
        check(decoded.layers.front().blendMode.value_or(BlendMode::Normal) == BlendMode::Multiply, "blendMode value");
        check(decoded.layers.front().maskEnabled.has_value() && !*decoded.layers.front().maskEnabled,
              "maskEnabled false is present");
        check(decoded.layers.front().parentID.has_value(), "parentID present");
    } catch (const ProjectError&) {
        std::printf("FAIL record optional values threw\n");
        ++failures;
    }

    // Shapes: line-only fields, required color fields.
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["shape"] = Json{{"kind", "Line"}, {"red", 0.0}, {"green", 0.0}, {"blue", 0.0}}; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "shape missing cornerRadius");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["shape"] = Json{{"kind", "line"}, {"red", 0.0}, {"green", 0.0}, {"blue", 0.0}, {"cornerRadius", 0.0}}; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "shape kind wrong case");
    checkDecodes([]{ Json d = base(); Json item = minimalRecord(); item["shape"] = Json{{"kind", "Ellipse"}, {"red", 0.1}, {"green", 0.2}, {"blue", 0.3}, {"cornerRadius", 5.0}}; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "shape without line fields");

    // Effects: an empty object is all-nil; enabled false must stay present.
    checkDecodes([]{ Json d = base(); Json item = minimalRecord(); item["effects"] = Json::object(); d["layers"] = Json::array({item}); return d.dump(); }(),
                 "empty effects");
    try {
        Json document = base();
        Json item = minimalRecord();
        item["effects"] = Json{{"shadow", Json{{"enabled", false}, {"angle", 45.0}, {"distance", 10.0},
                                               {"blur", 5.0}, {"red", 0.0}, {"green", 0.0}, {"blue", 0.0},
                                               {"opacity", 0.5}}}};
        document["layers"] = Json::array({item});
        const ProjectLayerRecord decoded = ManifestCodec::decode(document.dump()).layers.front();
        check(decoded.effects.has_value() && decoded.effects->shadow.has_value(), "shadow present");
        check(*decoded.effects->shadow->enabled == false, "shadow enabled false survives");
        const ProjectManifest again = ManifestCodec::decode(ManifestCodec::encode(
            ProjectManifest{{}, 11, "sRGB", std::nullopt, uuid("00000000-0000-4000-8000-000000000001"), 1, 1,
                            std::nullopt, {decoded}, std::nullopt}));
        check(*again.layers.front().effects->shadow->enabled == false, "shadow enabled false re-encodes");
    } catch (const ProjectError&) {
        std::printf("FAIL effects decode threw\n");
        ++failures;
    }
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); item["effects"] = Json{{"stroke", Json{{"size", 4.0}, {"red", 0.0}, {"green", 0.0}, {"blue", 0.0}, {"opacity", 1.0}}}}; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "stroke missing inside");

    // Text: required layout fields, optional runs.
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json text = fullRecordJson()["text"]; text.erase("tracking"); item["text"] = text; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "text missing tracking");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json text = fullRecordJson()["text"]; text["alignment"] = "center"; item["text"] = text; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "alignment wrong case");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json text = fullRecordJson()["text"]; text["colorRuns"][0].erase("green"); item["text"] = text; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "colorRun missing green");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json text = fullRecordJson()["text"]; text["fontRuns"][0]["location"] = "0"; item["text"] = text; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "fontRun location string");
    checkDecodes([]{ Json d = base(); Json item = minimalRecord(); item["text"] = fullRecordJson()["text"]; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "full text");

    // Adjustments: required blocks, ColorRange unkeyed arrays, last-wins duplicates.
    checkDecodes([]{ Json d = base(); Json item = minimalRecord(); item["adjustment"] = minimalAdjustment(); d["layers"] = Json::array({item}); return d.dump(); }(),
                 "minimal adjustment");
    for (const char* key : {"kind", "hue", "saturation", "lightness", "colorize", "levels", "curves"}) {
        Json document = base();
        Json item = minimalRecord();
        Json adjustment = minimalAdjustment();
        adjustment.erase(key);
        item["adjustment"] = adjustment;
        document["layers"] = Json::array({item});
        checkInvalid(document, std::string("adjustment missing ") + key);
    }
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json adjustment = minimalAdjustment(); adjustment["kind"] = "Hue Saturation"; item["adjustment"] = adjustment; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "adjustment kind wrong spelling");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json adjustment = minimalAdjustment(); adjustment["levels"]["channel"] = "blue"; item["adjustment"] = adjustment; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "levels channel wrong case");
    checkDecodes([]{ Json d = base(); Json item = minimalRecord(); Json adjustment = minimalAdjustment(); adjustment["kind"] = "Gaussian Blur"; adjustment["blurRadius"] = 12.5; item["adjustment"] = adjustment; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "gaussian blur adjustment");

    try {
        Json document = base();
        Json item = minimalRecord();
        Json adjustment = minimalAdjustment();
        adjustment["hsvSettings"] = Json{
            {"range", "Reds"},
            {"colorize", true},
            {"invertRange", false},
            {"adjustments", Json::array({"Master", Json{{"hue", 10.0}, {"saturation", 0.0}, {"lightness", 0.0}},
                                         "Master", Json{{"hue", 99.0}, {"saturation", 0.0}, {"lightness", 0.0}}})},
            {"bands", Json::array({"Reds", bandJson(-30.0, -15.0, 15.0, 30.0)})},
        };
        item["adjustment"] = adjustment;
        document["layers"] = Json::array({item});
        const LayerAdjustment decoded = ManifestCodec::decode(document.dump()).layers.front().adjustment.value();
        check(decoded.hsvSettings.has_value(), "hsvSettings present");
        check(decoded.hsvSettings->adjustments.at(ColorRange::Master).hue == 99, "duplicate ColorRange last wins");
        check(decoded.hsvSettings->adjustments.size() == 1, "duplicate ColorRange one entry");
        check(decoded.hsvSettings->band().falloffEnd == 30, "band decodes");
    } catch (const ProjectError&) {
        std::printf("FAIL hsvSettings decode threw\n");
        ++failures;
    }
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json adjustment = minimalAdjustment(); adjustment["hsvSettings"] = Json{{"range", "Master"}, {"colorize", false}, {"invertRange", false}, {"adjustments", Json::array({"Master"})}, {"bands", Json::array()}}; item["adjustment"] = adjustment; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "odd ColorRange array");
    checkInvalid([]{ Json d = base(); Json item = minimalRecord(); Json adjustment = minimalAdjustment(); adjustment["hsvSettings"] = Json{{"range", "Master"}, {"colorize", false}, {"invertRange", false}, {"adjustments", Json::array({"Nope", Json{{"hue", 0.0}, {"saturation", 0.0}, {"lightness", 0.0}}})}, {"bands", Json::array()}}; item["adjustment"] = adjustment; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "bad ColorRange name");
    checkDecodes([]{ Json d = base(); Json item = minimalRecord(); Json adjustment = minimalAdjustment(); adjustment["hsvSettings"] = Json{{"range", "Master"}, {"colorize", false}, {"invertRange", false}, {"adjustments", Json{{"Master", Json{{"hue", 0.0}, {"saturation", 0.0}, {"lightness", 0.0}}}}}, {"bands", Json{{"Master", bandJson(-30.0, -15.0, 15.0, 30.0)}}}}; item["adjustment"] = adjustment; d["layers"] = Json::array({item}); return d.dump(); }(),
                 "keyed ColorRange object leniency");

    // Guides.
    checkInvalid([]{ Json d = base(); d["guides"] = Json::array({Json{{"id", "33333333-4444-4555-8666-777777777777"}, {"axis", "Vertical"}, {"position", 0.0}}}); return d.dump(); }(),
                 "guide axis wrong case");
    checkInvalid([]{ Json d = base(); d["guides"] = Json::array({Json{{"id", "33333333-4444-4555-8666-777777777777"}, {"axis", "vertical"}}}); return d.dump(); }(),
                 "guide missing position");
    checkDecodes([]{ Json d = base(); d["guides"] = Json::array({Json{{"id", "33333333-4444-4555-8666-777777777777"}, {"axis", "vertical"}, {"position", 12.5}}}); return d.dump(); }(),
                 "guide decodes");

    // Header decoding.
    try {
        const ManifestCodec::Header header = ManifestCodec::decodeHeader(base().dump());
        check(header.format == "com.compositor.project" && header.version == 11, "header values");
    } catch (const ProjectError&) {
        std::printf("FAIL header decode threw\n");
        ++failures;
    }
    for (const char* text : {"{}", "[]", "{oops", "", "{\"format\":\"com.compositor.project\"}",
                             "{\"version\":11}"}) {
        check(decodeThrowsInvalid(text), std::string("header invalid: ") + text);
    }

    // Nesting: 512 containers (the root object plus 511 arrays) parse; 513 are rejected
    // before the recursive-descent parser can touch them.
    checkDecodes([]{ Json d = base(); d["pad"] = nest(510); return d.dump(); }(), "512-container nesting");
    checkInvalid([]{ Json d = base(); d["pad"] = nest(511); return d.dump(); }(), "513-container nesting");
    {
        std::string deep(100000, '[');
        deep.append(100000, ']');
        check(decodeThrowsInvalid(deep), "100k nesting rejected before the parser");
    }

    if (failures == 0) std::printf("test_manifestcodec: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
