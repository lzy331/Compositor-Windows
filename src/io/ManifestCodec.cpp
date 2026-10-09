#include "io/ManifestCodec.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <utility>

#include "io/ProjectError.h"
#include "io/SwiftJson.h"

namespace compositor {
namespace {

using Json = nlohmann::json;

[[noreturn]] void fail() { throw ProjectError::invalid(); }

// MARK: - The parsed document

/// Parses with the same acceptance the Swift side has: malformed JSON, numbers Foundation
/// cannot represent, and nesting deeper than Foundation's parser allows all mean an invalid
/// project. Duplicate keys keep the last value, as JSONSerialization does.
Json parse(const std::string& text) {
    // Foundation's JSON parser refuses documents nested deeper than 512 containers; the raw
    // scan finds that before nlohmann's recursion can grow the stack.
    {
        int depth = 0;
        bool inString = false, escaped = false;
        for (const char raw : text) {
            if (inString) {
                if (escaped) escaped = false;
                else if (raw == '\\') escaped = true;
                else if (raw == '"') inString = false;
            } else if (raw == '"') {
                inString = true;
            } else if (raw == '{' || raw == '[') {
                if (++depth > 512) fail();
            } else if (raw == '}' || raw == ']') {
                --depth;
            }
        }
    }
    try {
        return Json::parse(text);
    } catch (const nlohmann::json::exception&) {
        fail();
    }
}

const Json& asObject(const Json& value) {
    if (!value.is_object()) fail();
    return value;
}

const Json& asArray(const Json& value) {
    if (!value.is_array()) fail();
    return value;
}

// MARK: - Key access, with Swift's exact decode semantics
//
// A required key missing or null is a decode failure (defaults never stand in); an optional
// key missing or null is nil; any other type mismatch is a failure.

const Json* lookup(const Json& object, const char* key) {
    const auto found = object.find(key);
    if (found == object.end() || found->is_null()) return nullptr;
    return &*found;
}

const Json& require(const Json& object, const char* key) {
    const Json* value = lookup(object, key);
    if (!value) fail();
    return *value;
}

template <typename T>
std::optional<T> optional(const Json& object, const char* key, T (*decode)(const Json&)) {
    const Json* value = lookup(object, key);
    if (!value) return std::nullopt;
    return decode(*value);
}

bool boolValue(const Json& value) {
    if (!value.is_boolean()) fail();
    return value.get<bool>();
}

std::string stringValue(const Json& value) {
    if (!value.is_string()) fail();
    return value.get<std::string>();
}

/// Swift's Int unboxing: integers of either signedness that fit, and integral doubles in
/// range; booleans and fractional or out-of-range numbers are type mismatches.
int64_t intValue(const Json& value) {
    if (value.is_number_unsigned()) {
        const uint64_t raw = value.get<uint64_t>();
        if (raw > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) fail();
        return static_cast<int64_t>(raw);
    }
    if (value.is_number_integer()) return value.get<int64_t>();
    if (value.is_number_float()) {
        const double raw = value.get<double>();
        if (!std::isfinite(raw) || raw != std::trunc(raw)) fail();
        // 2^63 as a double is exact; anything at or beyond it cannot be an Int64.
        if (raw >= 9223372036854775808.0 || raw < -9223372036854775808.0) fail();
        return static_cast<int64_t>(raw);
    }
    fail();
}

double doubleValue(const Json& value) {
    if (!value.is_number()) fail();
    const double raw = value.get<double>();
    // Foundation cannot represent numbers that overflow Double, so they never decode.
    if (!std::isfinite(raw)) fail();
    return raw;
}

/// A JSON number as UInt32, over the same acceptance Int has.
uint32_t uint32Value(const Json& value) {
    const int64_t raw = intValue(value);
    if (raw < 0 || raw > 4294967295LL) fail();
    return static_cast<uint32_t>(raw);
}

Uuid uuidValue(const Json& value) {
    const std::string text = stringValue(value);
    const std::optional<Uuid> parsed = Uuid::parse(text);
    if (!parsed) fail();
    return *parsed;
}

template <typename Enum>
Enum enumValue(const Json& value, std::optional<Enum> (*fromName)(const std::string&)) {
    const std::string text = stringValue(value);
    const std::optional<Enum> parsed = fromName(text);
    if (!parsed) fail();
    return *parsed;
}

BlendMode blendModeValue(const Json& value) { return enumValue(value, blendModeFromName); }

/// CGPoint/CGSize decode as unkeyed pairs; extra elements are read past, missing ones fail.
Point pointValue(const Json& value) {
    const Json& array = asArray(value);
    if (array.size() < 2) fail();
    return {doubleValue(array[0]), doubleValue(array[1])};
}

Size sizeValue(const Json& value) {
    const Json& array = asArray(value);
    if (array.size() < 2) fail();
    return {doubleValue(array[0]), doubleValue(array[1])};
}

/// Dictionary<ColorRange, T>: an unkeyed alternating key/value array in Swift's encoding, or
/// a keyed object, which a hand-written file could use — both accepted, last duplicate wins.
template <typename T>
std::map<ColorRange, T> colorRangeMap(const Json& value, T (*decodeValue)(const Json&)) {
    std::map<ColorRange, T> result;
    if (value.is_array()) {
        if (value.size() % 2 != 0) fail();
        for (size_t index = 0; index + 1 < value.size(); index += 2)
            result[enumValue(value[index], colorRangeFromName)] = decodeValue(value[index + 1]);
    } else if (value.is_object()) {
        for (const auto& [name, element] : value.items()) {
            const std::optional<ColorRange> key = colorRangeFromName(name);
            if (!key) fail();
            result[*key] = decodeValue(element);
        }
    } else {
        fail();
    }
    return result;
}

// MARK: - Structure decoders

LayerTransform transformValue(const Json& value) {
    const Json& object = asObject(value);
    LayerTransform result;
    result.origin = pointValue(require(object, "origin"));
    result.size = sizeValue(require(object, "size"));
    result.rotation = doubleValue(require(object, "rotation"));
    result.flipX = boolValue(require(object, "flipX"));
    result.flipY = boolValue(require(object, "flipY"));
    result.sampling = enumValue(require(object, "sampling"), layerSamplingFromName);
    return result;
}

StrokeEffect strokeValue(const Json& value) {
    const Json& object = asObject(value);
    StrokeEffect result;
    result.enabled = optional(object, "enabled", boolValue);
    result.size = doubleValue(require(object, "size"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.opacity = doubleValue(require(object, "opacity"));
    result.inside = boolValue(require(object, "inside"));
    return result;
}

ShadowEffect shadowValue(const Json& value) {
    const Json& object = asObject(value);
    ShadowEffect result;
    result.enabled = optional(object, "enabled", boolValue);
    result.angle = doubleValue(require(object, "angle"));
    result.distance = doubleValue(require(object, "distance"));
    result.blur = doubleValue(require(object, "blur"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.opacity = doubleValue(require(object, "opacity"));
    return result;
}

ColorOverlayEffect colorOverlayValue(const Json& value) {
    const Json& object = asObject(value);
    ColorOverlayEffect result;
    result.enabled = optional(object, "enabled", boolValue);
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.opacity = doubleValue(require(object, "opacity"));
    return result;
}

InnerShadowEffect innerShadowValue(const Json& value) {
    const Json& object = asObject(value);
    InnerShadowEffect result;
    result.enabled = optional(object, "enabled", boolValue);
    result.angle = doubleValue(require(object, "angle"));
    result.distance = doubleValue(require(object, "distance"));
    result.blur = doubleValue(require(object, "blur"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.opacity = doubleValue(require(object, "opacity"));
    return result;
}

OuterGlowEffect outerGlowValue(const Json& value) {
    const Json& object = asObject(value);
    OuterGlowEffect result;
    result.enabled = optional(object, "enabled", boolValue);
    result.size = doubleValue(require(object, "size"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.opacity = doubleValue(require(object, "opacity"));
    return result;
}

InnerGlowEffect innerGlowValue(const Json& value) {
    const Json& object = asObject(value);
    InnerGlowEffect result;
    result.enabled = optional(object, "enabled", boolValue);
    result.size = doubleValue(require(object, "size"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.opacity = doubleValue(require(object, "opacity"));
    return result;
}

LayerEffects effectsValue(const Json& value) {
    const Json& object = asObject(value);
    LayerEffects result;
    result.stroke = optional(object, "stroke", strokeValue);
    result.shadow = optional(object, "shadow", shadowValue);
    result.colorOverlay = optional(object, "colorOverlay", colorOverlayValue);
    result.innerShadow = optional(object, "innerShadow", innerShadowValue);
    result.outerGlow = optional(object, "outerGlow", outerGlowValue);
    result.innerGlow = optional(object, "innerGlow", innerGlowValue);
    return result;
}

LayerTextColorRun colorRunValue(const Json& value) {
    const Json& object = asObject(value);
    LayerTextColorRun result;
    result.location = intValue(require(object, "location"));
    result.length = intValue(require(object, "length"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    return result;
}

LayerTextFontRun fontRunValue(const Json& value) {
    const Json& object = asObject(value);
    LayerTextFontRun result;
    result.location = intValue(require(object, "location"));
    result.length = intValue(require(object, "length"));
    result.fontName = stringValue(require(object, "fontName"));
    return result;
}

LayerTextStyle textValue(const Json& value) {
    const Json& object = asObject(value);
    LayerTextStyle result;
    result.content = stringValue(require(object, "content"));
    result.fontName = stringValue(require(object, "fontName"));
    result.fontSize = doubleValue(require(object, "fontSize"));
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.alignment = enumValue(require(object, "alignment"), textAlignmentFromName);
    result.tracking = doubleValue(require(object, "tracking"));
    result.leading = doubleValue(require(object, "leading"));
    if (const Json* box = lookup(object, "boxSize")) result.boxSize = sizeValue(*box);
    if (const Json* runs = lookup(object, "colorRuns")) {
        std::vector<LayerTextColorRun> list;
        for (const Json& element : asArray(*runs)) list.push_back(colorRunValue(element));
        result.colorRuns = std::move(list);
    }
    if (const Json* runs = lookup(object, "fontRuns")) {
        std::vector<LayerTextFontRun> list;
        for (const Json& element : asArray(*runs)) list.push_back(fontRunValue(element));
        result.fontRuns = std::move(list);
    }
    return result;
}

LayerShapeStyle shapeValue(const Json& value) {
    const Json& object = asObject(value);
    LayerShapeStyle result;
    result.kind = enumValue(require(object, "kind"), shapeKindFromName);
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    result.cornerRadius = doubleValue(require(object, "cornerRadius"));
    result.lineWidth = optional(object, "lineWidth", doubleValue);
    if (const Json* start = lookup(object, "start")) result.start = pointValue(*start);
    if (const Json* end = lookup(object, "end")) result.end = pointValue(*end);
    return result;
}

CanvasGuide guideValue(const Json& value) {
    const Json& object = asObject(value);
    CanvasGuide result;
    result.id = uuidValue(require(object, "id"));
    result.axis = enumValue(require(object, "axis"), guideAxisFromName);
    result.position = doubleValue(require(object, "position"));
    return result;
}

RangeAdjustment rangeAdjustmentValue(const Json& value) {
    const Json& object = asObject(value);
    RangeAdjustment result;
    result.hue = doubleValue(require(object, "hue"));
    result.saturation = doubleValue(require(object, "saturation"));
    result.lightness = doubleValue(require(object, "lightness"));
    return result;
}

HueBand bandValue(const Json& value) {
    const Json& object = asObject(value);
    HueBand result;
    result.falloffStart = doubleValue(require(object, "falloffStart"));
    result.rangeStart = doubleValue(require(object, "rangeStart"));
    result.rangeEnd = doubleValue(require(object, "rangeEnd"));
    result.falloffEnd = doubleValue(require(object, "falloffEnd"));
    return result;
}

LevelRange levelRangeValue(const Json& value) {
    const Json& object = asObject(value);
    LevelRange result;
    result.black = doubleValue(require(object, "black"));
    result.gamma = doubleValue(require(object, "gamma"));
    result.white = doubleValue(require(object, "white"));
    result.outputBlack = doubleValue(require(object, "outputBlack"));
    result.outputWhite = doubleValue(require(object, "outputWhite"));
    return result;
}

CurvePoint curvePointValue(const Json& value) {
    const Json& object = asObject(value);
    CurvePoint result;
    result.x = doubleValue(require(object, "x"));
    result.y = doubleValue(require(object, "y"));
    return result;
}

HueSaturationSettings hsvValue(const Json& value) {
    const Json& object = asObject(value);
    HueSaturationSettings result;
    result.range = enumValue(require(object, "range"), colorRangeFromName);
    result.colorize = boolValue(require(object, "colorize"));
    result.invertRange = boolValue(require(object, "invertRange"));
    result.adjustments = colorRangeMap(require(object, "adjustments"), rangeAdjustmentValue);
    result.bands = colorRangeMap(require(object, "bands"), bandValue);
    return result;
}

LevelsSettings levelsValue(const Json& value) {
    const Json& object = asObject(value);
    LevelsSettings result;
    result.channel = enumValue(require(object, "channel"), levelsChannelFromName);
    result.ranges.clear();
    for (const Json& element : asArray(require(object, "ranges")))
        result.ranges.push_back(levelRangeValue(element));
    return result;
}

CurvesSettings curvesValue(const Json& value) {
    const Json& object = asObject(value);
    CurvesSettings result;
    result.channel = enumValue(require(object, "channel"), levelsChannelFromName);
    result.channels.clear();
    for (const Json& points : asArray(require(object, "channels"))) {
        std::vector<CurvePoint> channel;
        for (const Json& element : asArray(points)) channel.push_back(curvePointValue(element));
        result.channels.push_back(std::move(channel));
    }
    return result;
}

ExposureSettings exposureValue(const Json& value) {
    const Json& object = asObject(value);
    ExposureSettings result;
    result.exposure = doubleValue(require(object, "exposure"));
    result.offset = doubleValue(require(object, "offset"));
    result.gamma = doubleValue(require(object, "gamma"));
    return result;
}

AdjustmentColor adjustmentColorValue(const Json& value) {
    const Json& object = asObject(value);
    AdjustmentColor result;
    result.red = doubleValue(require(object, "red"));
    result.green = doubleValue(require(object, "green"));
    result.blue = doubleValue(require(object, "blue"));
    return result;
}

GradientMapSettings gradientMapValue(const Json& value) {
    const Json& object = asObject(value);
    GradientMapSettings result;
    result.shadows = adjustmentColorValue(require(object, "shadows"));
    result.highlights = adjustmentColorValue(require(object, "highlights"));
    result.reversed = boolValue(require(object, "reversed"));
    return result;
}

BlackWhiteSettings blackWhiteValue(const Json& value) {
    const Json& object = asObject(value);
    BlackWhiteSettings result;
    result.reds = doubleValue(require(object, "reds"));
    result.yellows = doubleValue(require(object, "yellows"));
    result.greens = doubleValue(require(object, "greens"));
    result.cyans = doubleValue(require(object, "cyans"));
    result.blues = doubleValue(require(object, "blues"));
    result.magentas = doubleValue(require(object, "magentas"));
    result.tint = boolValue(require(object, "tint"));
    result.tintHue = doubleValue(require(object, "tintHue"));
    result.tintSaturation = doubleValue(require(object, "tintSaturation"));
    return result;
}

ColorBalanceSettings colorBalanceValue(const Json& value) {
    const Json& object = asObject(value);
    ColorBalanceSettings result;
    result.shadowCyanRed = doubleValue(require(object, "shadowCyanRed"));
    result.shadowMagentaGreen = doubleValue(require(object, "shadowMagentaGreen"));
    result.shadowYellowBlue = doubleValue(require(object, "shadowYellowBlue"));
    result.midCyanRed = doubleValue(require(object, "midCyanRed"));
    result.midMagentaGreen = doubleValue(require(object, "midMagentaGreen"));
    result.midYellowBlue = doubleValue(require(object, "midYellowBlue"));
    result.highlightCyanRed = doubleValue(require(object, "highlightCyanRed"));
    result.highlightMagentaGreen = doubleValue(require(object, "highlightMagentaGreen"));
    result.highlightYellowBlue = doubleValue(require(object, "highlightYellowBlue"));
    result.preserveLuminosity = boolValue(require(object, "preserveLuminosity"));
    return result;
}

GrainSettings grainValue(const Json& value) {
    const Json& object = asObject(value);
    GrainSettings result;
    result.amount = doubleValue(require(object, "amount"));
    result.size = doubleValue(require(object, "size"));
    result.roughness = doubleValue(require(object, "roughness"));
    result.seed = uint32Value(require(object, "seed"));
    return result;
}

LayerAdjustment adjustmentValue(const Json& value) {
    const Json& object = asObject(value);
    LayerAdjustment result;
    result.kind = enumValue(require(object, "kind"), adjustmentKindFromName);
    result.hue = doubleValue(require(object, "hue"));
    result.saturation = doubleValue(require(object, "saturation"));
    result.lightness = doubleValue(require(object, "lightness"));
    result.colorize = boolValue(require(object, "colorize"));
    result.hsvSettings = optional(object, "hsvSettings", hsvValue);
    result.levels = levelsValue(require(object, "levels"));
    result.curves = curvesValue(require(object, "curves"));
    result.exposureSettings = optional(object, "exposureSettings", exposureValue);
    result.gradientMapSettings = optional(object, "gradientMapSettings", gradientMapValue);
    result.grainSettings = optional(object, "grainSettings", grainValue);
    result.blackWhiteSettings = optional(object, "blackWhiteSettings", blackWhiteValue);
    result.colorBalanceSettings = optional(object, "colorBalanceSettings", colorBalanceValue);
    result.blurRadius = optional(object, "blurRadius", doubleValue);
    result.motionAngle = optional(object, "motionAngle", doubleValue);
    result.motionDistance = optional(object, "motionDistance", doubleValue);
    result.noiseAmount = optional(object, "noiseAmount", doubleValue);
    result.noiseGaussian = optional(object, "noiseGaussian", boolValue);
    result.noiseMonochromatic = optional(object, "noiseMonochromatic", boolValue);
    result.noiseSeed = optional(object, "noiseSeed", uint32Value);
    return result;
}

ProjectLayerRecord recordValue(const Json& value) {
    const Json& object = asObject(value);
    ProjectLayerRecord result;
    result.id = uuidValue(require(object, "id"));
    result.name = stringValue(require(object, "name"));
    result.isVisible = boolValue(require(object, "isVisible"));
    result.transform = transformValue(require(object, "transform"));
    result.imageFile = optional(object, "imageFile", stringValue);
    result.parentID = optional(object, "parentID", uuidValue);
    result.isGroup = optional(object, "isGroup", boolValue);
    result.opacity = optional(object, "opacity", doubleValue);
    result.blendMode = optional(object, "blendMode", blendModeValue);
    result.maskFile = optional(object, "maskFile", stringValue);
    result.maskEnabled = optional(object, "maskEnabled", boolValue);
    result.maskSourceID = optional(object, "maskSourceID", uuidValue);
    result.adjustment = optional(object, "adjustment", adjustmentValue);
    result.maskPlacement = optional(object, "maskPlacement", transformValue);
    result.maskLinked = optional(object, "maskLinked", boolValue);
    result.shape = optional(object, "shape", shapeValue);
    result.effects = optional(object, "effects", effectsValue);
    result.text = optional(object, "text", textValue);
    return result;
}

ProjectManifest manifestValue(const Json& value) {
    const Json& object = asObject(value);
    ProjectManifest result;
    result.format = stringValue(require(object, "format"));
    result.version = intValue(require(object, "version"));
    result.colorSpace = stringValue(require(object, "colorSpace"));
    result.resolution = optional(object, "resolution", doubleValue);
    result.documentID = uuidValue(require(object, "documentID"));
    result.width = intValue(require(object, "width"));
    result.height = intValue(require(object, "height"));
    result.activeLayerID = optional(object, "activeLayerID", uuidValue);
    result.layers.clear();
    for (const Json& element : asArray(require(object, "layers")))
        result.layers.push_back(recordValue(element));
    if (const Json* guides = lookup(object, "guides")) {
        std::vector<CanvasGuide> list;
        for (const Json& element : asArray(*guides)) list.push_back(guideValue(element));
        result.guides = std::move(list);
    }
    return result;
}

// MARK: - Encoders
//
// Every non-optional Swift property is written even when it holds its default, and every nil
// optional is skipped (encodeIfPresent). Objects are std::maps, so keys sort bytewise.

using swiftjson::JsonValue;

JsonValue encodeTransform(const LayerTransform& transform) {
    return JsonValue::makeObject({
        {"origin", JsonValue::makeArray({JsonValue::makeDouble(transform.origin.x),
                                         JsonValue::makeDouble(transform.origin.y)})},
        {"size", JsonValue::makeArray({JsonValue::makeDouble(transform.size.width),
                                       JsonValue::makeDouble(transform.size.height)})},
        {"rotation", JsonValue::makeDouble(transform.rotation)},
        {"flipX", JsonValue::makeBool(transform.flipX)},
        {"flipY", JsonValue::makeBool(transform.flipY)},
        {"sampling", JsonValue::makeString(layerSamplingName(transform.sampling))},
    });
}

JsonValue encodeStroke(const StrokeEffect& effect) {
    std::map<std::string, JsonValue> object;
    if (effect.enabled) object["enabled"] = JsonValue::makeBool(*effect.enabled);
    object["size"] = JsonValue::makeDouble(effect.size);
    object["red"] = JsonValue::makeDouble(effect.red);
    object["green"] = JsonValue::makeDouble(effect.green);
    object["blue"] = JsonValue::makeDouble(effect.blue);
    object["opacity"] = JsonValue::makeDouble(effect.opacity);
    object["inside"] = JsonValue::makeBool(effect.inside);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeShadow(const ShadowEffect& effect) {
    std::map<std::string, JsonValue> object;
    if (effect.enabled) object["enabled"] = JsonValue::makeBool(*effect.enabled);
    object["angle"] = JsonValue::makeDouble(effect.angle);
    object["distance"] = JsonValue::makeDouble(effect.distance);
    object["blur"] = JsonValue::makeDouble(effect.blur);
    object["red"] = JsonValue::makeDouble(effect.red);
    object["green"] = JsonValue::makeDouble(effect.green);
    object["blue"] = JsonValue::makeDouble(effect.blue);
    object["opacity"] = JsonValue::makeDouble(effect.opacity);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeColorOverlay(const ColorOverlayEffect& effect) {
    std::map<std::string, JsonValue> object;
    if (effect.enabled) object["enabled"] = JsonValue::makeBool(*effect.enabled);
    object["red"] = JsonValue::makeDouble(effect.red);
    object["green"] = JsonValue::makeDouble(effect.green);
    object["blue"] = JsonValue::makeDouble(effect.blue);
    object["opacity"] = JsonValue::makeDouble(effect.opacity);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeInnerShadow(const InnerShadowEffect& effect) {
    std::map<std::string, JsonValue> object;
    if (effect.enabled) object["enabled"] = JsonValue::makeBool(*effect.enabled);
    object["angle"] = JsonValue::makeDouble(effect.angle);
    object["distance"] = JsonValue::makeDouble(effect.distance);
    object["blur"] = JsonValue::makeDouble(effect.blur);
    object["red"] = JsonValue::makeDouble(effect.red);
    object["green"] = JsonValue::makeDouble(effect.green);
    object["blue"] = JsonValue::makeDouble(effect.blue);
    object["opacity"] = JsonValue::makeDouble(effect.opacity);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeOuterGlow(const OuterGlowEffect& effect) {
    std::map<std::string, JsonValue> object;
    if (effect.enabled) object["enabled"] = JsonValue::makeBool(*effect.enabled);
    object["size"] = JsonValue::makeDouble(effect.size);
    object["red"] = JsonValue::makeDouble(effect.red);
    object["green"] = JsonValue::makeDouble(effect.green);
    object["blue"] = JsonValue::makeDouble(effect.blue);
    object["opacity"] = JsonValue::makeDouble(effect.opacity);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeInnerGlow(const InnerGlowEffect& effect) {
    std::map<std::string, JsonValue> object;
    if (effect.enabled) object["enabled"] = JsonValue::makeBool(*effect.enabled);
    object["size"] = JsonValue::makeDouble(effect.size);
    object["red"] = JsonValue::makeDouble(effect.red);
    object["green"] = JsonValue::makeDouble(effect.green);
    object["blue"] = JsonValue::makeDouble(effect.blue);
    object["opacity"] = JsonValue::makeDouble(effect.opacity);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeEffects(const LayerEffects& effects) {
    std::map<std::string, JsonValue> object;
    if (effects.stroke) object["stroke"] = encodeStroke(*effects.stroke);
    if (effects.shadow) object["shadow"] = encodeShadow(*effects.shadow);
    if (effects.colorOverlay) object["colorOverlay"] = encodeColorOverlay(*effects.colorOverlay);
    if (effects.innerShadow) object["innerShadow"] = encodeInnerShadow(*effects.innerShadow);
    if (effects.outerGlow) object["outerGlow"] = encodeOuterGlow(*effects.outerGlow);
    if (effects.innerGlow) object["innerGlow"] = encodeInnerGlow(*effects.innerGlow);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeText(const LayerTextStyle& text) {
    std::map<std::string, JsonValue> object;
    object["content"] = JsonValue::makeString(text.content);
    object["fontName"] = JsonValue::makeString(text.fontName);
    object["fontSize"] = JsonValue::makeDouble(text.fontSize);
    object["red"] = JsonValue::makeDouble(text.red);
    object["green"] = JsonValue::makeDouble(text.green);
    object["blue"] = JsonValue::makeDouble(text.blue);
    object["alignment"] = JsonValue::makeString(textAlignmentName(text.alignment));
    object["tracking"] = JsonValue::makeDouble(text.tracking);
    object["leading"] = JsonValue::makeDouble(text.leading);
    if (text.boxSize)
        object["boxSize"] = JsonValue::makeArray({JsonValue::makeDouble(text.boxSize->width),
                                                  JsonValue::makeDouble(text.boxSize->height)});
    if (text.colorRuns) {
        std::vector<JsonValue> runs;
        runs.reserve(text.colorRuns->size());
        for (const LayerTextColorRun& run : *text.colorRuns)
            runs.push_back(JsonValue::makeObject({
                {"location", JsonValue::makeInt(run.location)},
                {"length", JsonValue::makeInt(run.length)},
                {"red", JsonValue::makeDouble(run.red)},
                {"green", JsonValue::makeDouble(run.green)},
                {"blue", JsonValue::makeDouble(run.blue)},
            }));
        object["colorRuns"] = JsonValue::makeArray(std::move(runs));
    }
    if (text.fontRuns) {
        std::vector<JsonValue> runs;
        runs.reserve(text.fontRuns->size());
        for (const LayerTextFontRun& run : *text.fontRuns)
            runs.push_back(JsonValue::makeObject({
                {"location", JsonValue::makeInt(run.location)},
                {"length", JsonValue::makeInt(run.length)},
                {"fontName", JsonValue::makeString(run.fontName)},
            }));
        object["fontRuns"] = JsonValue::makeArray(std::move(runs));
    }
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeShape(const LayerShapeStyle& shape) {
    std::map<std::string, JsonValue> object;
    object["kind"] = JsonValue::makeString(shapeKindName(shape.kind));
    object["red"] = JsonValue::makeDouble(shape.red);
    object["green"] = JsonValue::makeDouble(shape.green);
    object["blue"] = JsonValue::makeDouble(shape.blue);
    object["cornerRadius"] = JsonValue::makeDouble(shape.cornerRadius);
    if (shape.lineWidth) object["lineWidth"] = JsonValue::makeDouble(*shape.lineWidth);
    if (shape.start)
        object["start"] = JsonValue::makeArray({JsonValue::makeDouble(shape.start->x),
                                                JsonValue::makeDouble(shape.start->y)});
    if (shape.end)
        object["end"] = JsonValue::makeArray({JsonValue::makeDouble(shape.end->x),
                                              JsonValue::makeDouble(shape.end->y)});
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeGuide(const CanvasGuide& guide) {
    return JsonValue::makeObject({
        {"id", JsonValue::makeString(guide.id.toString())},
        {"axis", JsonValue::makeString(guideAxisName(guide.axis))},
        {"position", JsonValue::makeDouble(guide.position)},
    });
}

/// Swift encodes Dictionary<ColorRange, X> as a flat alternating array. Its iteration order is
/// hash-seeded, so no byte-exact order exists; allColorRanges() order keeps saves deterministic.
template <typename T, typename Encode>
JsonValue encodeColorRangeMap(const std::map<ColorRange, T>& map, Encode encodeValue) {
    std::vector<JsonValue> array;
    for (const ColorRange range : allColorRanges()) {
        const auto found = map.find(range);
        if (found == map.end()) continue;
        array.push_back(JsonValue::makeString(colorRangeName(range)));
        array.push_back(encodeValue(found->second));
    }
    return JsonValue::makeArray(std::move(array));
}

JsonValue encodeRangeAdjustment(const RangeAdjustment& adjustment) {
    return JsonValue::makeObject({
        {"hue", JsonValue::makeDouble(adjustment.hue)},
        {"saturation", JsonValue::makeDouble(adjustment.saturation)},
        {"lightness", JsonValue::makeDouble(adjustment.lightness)},
    });
}

JsonValue encodeBand(const HueBand& band) {
    return JsonValue::makeObject({
        {"falloffStart", JsonValue::makeDouble(band.falloffStart)},
        {"rangeStart", JsonValue::makeDouble(band.rangeStart)},
        {"rangeEnd", JsonValue::makeDouble(band.rangeEnd)},
        {"falloffEnd", JsonValue::makeDouble(band.falloffEnd)},
    });
}

JsonValue encodeHsvSettings(const HueSaturationSettings& settings) {
    std::map<std::string, JsonValue> object;
    object["range"] = JsonValue::makeString(colorRangeName(settings.range));
    object["colorize"] = JsonValue::makeBool(settings.colorize);
    object["invertRange"] = JsonValue::makeBool(settings.invertRange);
    object["adjustments"] = encodeColorRangeMap(settings.adjustments,
                                                [](const RangeAdjustment& value) { return encodeRangeAdjustment(value); });
    object["bands"] = encodeColorRangeMap(settings.bands, [](const HueBand& value) { return encodeBand(value); });
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeLevelRange(const LevelRange& range) {
    return JsonValue::makeObject({
        {"black", JsonValue::makeDouble(range.black)},
        {"gamma", JsonValue::makeDouble(range.gamma)},
        {"white", JsonValue::makeDouble(range.white)},
        {"outputBlack", JsonValue::makeDouble(range.outputBlack)},
        {"outputWhite", JsonValue::makeDouble(range.outputWhite)},
    });
}

JsonValue encodeLevels(const LevelsSettings& settings) {
    std::vector<JsonValue> ranges;
    ranges.reserve(settings.ranges.size());
    for (const LevelRange& range : settings.ranges) ranges.push_back(encodeLevelRange(range));
    return JsonValue::makeObject({
        {"channel", JsonValue::makeString(levelsChannelName(settings.channel))},
        {"ranges", JsonValue::makeArray(std::move(ranges))},
    });
}

JsonValue encodeCurves(const CurvesSettings& settings) {
    std::vector<JsonValue> channels;
    channels.reserve(settings.channels.size());
    for (const std::vector<CurvePoint>& points : settings.channels) {
        std::vector<JsonValue> encoded;
        encoded.reserve(points.size());
        for (const CurvePoint& point : points)
            encoded.push_back(JsonValue::makeObject({
                {"x", JsonValue::makeDouble(point.x)},
                {"y", JsonValue::makeDouble(point.y)},
            }));
        channels.push_back(JsonValue::makeArray(std::move(encoded)));
    }
    return JsonValue::makeObject({
        {"channel", JsonValue::makeString(levelsChannelName(settings.channel))},
        {"channels", JsonValue::makeArray(std::move(channels))},
    });
}

JsonValue encodeExposure(const ExposureSettings& settings) {
    return JsonValue::makeObject({
        {"exposure", JsonValue::makeDouble(settings.exposure)},
        {"offset", JsonValue::makeDouble(settings.offset)},
        {"gamma", JsonValue::makeDouble(settings.gamma)},
    });
}

JsonValue encodeAdjustmentColor(const AdjustmentColor& color) {
    return JsonValue::makeObject({
        {"red", JsonValue::makeDouble(color.red)},
        {"green", JsonValue::makeDouble(color.green)},
        {"blue", JsonValue::makeDouble(color.blue)},
    });
}

JsonValue encodeGradientMap(const GradientMapSettings& settings) {
    return JsonValue::makeObject({
        {"shadows", encodeAdjustmentColor(settings.shadows)},
        {"highlights", encodeAdjustmentColor(settings.highlights)},
        {"reversed", JsonValue::makeBool(settings.reversed)},
    });
}

JsonValue encodeBlackWhite(const BlackWhiteSettings& settings) {
    return JsonValue::makeObject({
        {"reds", JsonValue::makeDouble(settings.reds)},
        {"yellows", JsonValue::makeDouble(settings.yellows)},
        {"greens", JsonValue::makeDouble(settings.greens)},
        {"cyans", JsonValue::makeDouble(settings.cyans)},
        {"blues", JsonValue::makeDouble(settings.blues)},
        {"magentas", JsonValue::makeDouble(settings.magentas)},
        {"tint", JsonValue::makeBool(settings.tint)},
        {"tintHue", JsonValue::makeDouble(settings.tintHue)},
        {"tintSaturation", JsonValue::makeDouble(settings.tintSaturation)},
    });
}

JsonValue encodeColorBalance(const ColorBalanceSettings& settings) {
    return JsonValue::makeObject({
        {"shadowCyanRed", JsonValue::makeDouble(settings.shadowCyanRed)},
        {"shadowMagentaGreen", JsonValue::makeDouble(settings.shadowMagentaGreen)},
        {"shadowYellowBlue", JsonValue::makeDouble(settings.shadowYellowBlue)},
        {"midCyanRed", JsonValue::makeDouble(settings.midCyanRed)},
        {"midMagentaGreen", JsonValue::makeDouble(settings.midMagentaGreen)},
        {"midYellowBlue", JsonValue::makeDouble(settings.midYellowBlue)},
        {"highlightCyanRed", JsonValue::makeDouble(settings.highlightCyanRed)},
        {"highlightMagentaGreen", JsonValue::makeDouble(settings.highlightMagentaGreen)},
        {"highlightYellowBlue", JsonValue::makeDouble(settings.highlightYellowBlue)},
        {"preserveLuminosity", JsonValue::makeBool(settings.preserveLuminosity)},
    });
}

JsonValue encodeGrain(const GrainSettings& settings) {
    return JsonValue::makeObject({
        {"amount", JsonValue::makeDouble(settings.amount)},
        {"size", JsonValue::makeDouble(settings.size)},
        {"roughness", JsonValue::makeDouble(settings.roughness)},
        {"seed", JsonValue::makeInt(settings.seed)},
    });
}

JsonValue encodeAdjustment(const LayerAdjustment& adjustment) {
    std::map<std::string, JsonValue> object;
    object["kind"] = JsonValue::makeString(adjustmentKindName(adjustment.kind));
    object["hue"] = JsonValue::makeDouble(adjustment.hue);
    object["saturation"] = JsonValue::makeDouble(adjustment.saturation);
    object["lightness"] = JsonValue::makeDouble(adjustment.lightness);
    object["colorize"] = JsonValue::makeBool(adjustment.colorize);
    if (adjustment.hsvSettings) object["hsvSettings"] = encodeHsvSettings(*adjustment.hsvSettings);
    object["levels"] = encodeLevels(adjustment.levels);
    object["curves"] = encodeCurves(adjustment.curves);
    if (adjustment.exposureSettings) object["exposureSettings"] = encodeExposure(*adjustment.exposureSettings);
    if (adjustment.gradientMapSettings) object["gradientMapSettings"] = encodeGradientMap(*adjustment.gradientMapSettings);
    if (adjustment.grainSettings) object["grainSettings"] = encodeGrain(*adjustment.grainSettings);
    if (adjustment.blackWhiteSettings) object["blackWhiteSettings"] = encodeBlackWhite(*adjustment.blackWhiteSettings);
    if (adjustment.colorBalanceSettings) object["colorBalanceSettings"] = encodeColorBalance(*adjustment.colorBalanceSettings);
    if (adjustment.blurRadius) object["blurRadius"] = JsonValue::makeDouble(*adjustment.blurRadius);
    if (adjustment.motionAngle) object["motionAngle"] = JsonValue::makeDouble(*adjustment.motionAngle);
    if (adjustment.motionDistance) object["motionDistance"] = JsonValue::makeDouble(*adjustment.motionDistance);
    if (adjustment.noiseAmount) object["noiseAmount"] = JsonValue::makeDouble(*adjustment.noiseAmount);
    if (adjustment.noiseGaussian) object["noiseGaussian"] = JsonValue::makeBool(*adjustment.noiseGaussian);
    if (adjustment.noiseMonochromatic) object["noiseMonochromatic"] = JsonValue::makeBool(*adjustment.noiseMonochromatic);
    if (adjustment.noiseSeed) object["noiseSeed"] = JsonValue::makeInt(*adjustment.noiseSeed);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeRecord(const ProjectLayerRecord& record) {
    std::map<std::string, JsonValue> object;
    object["id"] = JsonValue::makeString(record.id.toString());
    object["name"] = JsonValue::makeString(record.name);
    object["isVisible"] = JsonValue::makeBool(record.isVisible);
    object["transform"] = encodeTransform(record.transform);
    if (record.imageFile) object["imageFile"] = JsonValue::makeString(*record.imageFile);
    if (record.parentID) object["parentID"] = JsonValue::makeString(record.parentID->toString());
    if (record.isGroup) object["isGroup"] = JsonValue::makeBool(*record.isGroup);
    if (record.opacity) object["opacity"] = JsonValue::makeDouble(*record.opacity);
    if (record.blendMode) object["blendMode"] = JsonValue::makeString(blendModeName(*record.blendMode));
    if (record.maskFile) object["maskFile"] = JsonValue::makeString(*record.maskFile);
    if (record.maskEnabled) object["maskEnabled"] = JsonValue::makeBool(*record.maskEnabled);
    if (record.maskSourceID) object["maskSourceID"] = JsonValue::makeString(record.maskSourceID->toString());
    if (record.adjustment) object["adjustment"] = encodeAdjustment(*record.adjustment);
    if (record.maskPlacement) object["maskPlacement"] = encodeTransform(*record.maskPlacement);
    if (record.maskLinked) object["maskLinked"] = JsonValue::makeBool(*record.maskLinked);
    if (record.shape) object["shape"] = encodeShape(*record.shape);
    if (record.effects) object["effects"] = encodeEffects(*record.effects);
    if (record.text) object["text"] = encodeText(*record.text);
    return JsonValue::makeObject(std::move(object));
}

JsonValue encodeManifest(const ProjectManifest& manifest) {
    std::map<std::string, JsonValue> object;
    object["format"] = JsonValue::makeString(manifest.format);
    object["version"] = JsonValue::makeInt(manifest.version);
    object["colorSpace"] = JsonValue::makeString(manifest.colorSpace);
    if (manifest.resolution) object["resolution"] = JsonValue::makeDouble(*manifest.resolution);
    object["documentID"] = JsonValue::makeString(manifest.documentID.toString());
    object["width"] = JsonValue::makeInt(manifest.width);
    object["height"] = JsonValue::makeInt(manifest.height);
    if (manifest.activeLayerID) object["activeLayerID"] = JsonValue::makeString(manifest.activeLayerID->toString());
    std::vector<JsonValue> layers;
    layers.reserve(manifest.layers.size());
    for (const ProjectLayerRecord& layer : manifest.layers) layers.push_back(encodeRecord(layer));
    object["layers"] = JsonValue::makeArray(std::move(layers));
    if (manifest.guides) {
        std::vector<JsonValue> guides;
        guides.reserve(manifest.guides->size());
        for (const CanvasGuide& guide : *manifest.guides) guides.push_back(encodeGuide(guide));
        object["guides"] = JsonValue::makeArray(std::move(guides));
    }
    return JsonValue::makeObject(std::move(object));
}

}  // namespace

ManifestCodec::Header ManifestCodec::decodeHeader(const std::string& text) {
    const Json document = parse(text);
    const Json& object = asObject(document);
    Header header;
    header.format = stringValue(require(object, "format"));
    header.version = intValue(require(object, "version"));
    return header;
}

ProjectManifest ManifestCodec::decode(const std::string& text) {
    return manifestValue(parse(text));
}

std::string ManifestCodec::encode(const ProjectManifest& manifest) {
    return swiftjson::serialize(encodeManifest(manifest));
}

}  // namespace compositor
