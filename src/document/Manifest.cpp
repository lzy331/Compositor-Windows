#include "document/Manifest.h"

namespace compositor {

std::string layerSamplingName(LayerSampling sampling) {
    switch (sampling) {
        case LayerSampling::Nearest: return "Nearest";
        case LayerSampling::Smooth: return "Smooth";
        case LayerSampling::High: return "High quality";
    }
    return "High quality";
}

std::optional<LayerSampling> layerSamplingFromName(const std::string& name) {
    if (name == "Nearest") return LayerSampling::Nearest;
    if (name == "Smooth") return LayerSampling::Smooth;
    if (name == "High quality") return LayerSampling::High;
    return std::nullopt;
}

std::string textAlignmentName(TextAlignment alignment) {
    switch (alignment) {
        case TextAlignment::Left: return "Left";
        case TextAlignment::Center: return "Center";
        case TextAlignment::Right: return "Right";
    }
    return "Left";
}

std::optional<TextAlignment> textAlignmentFromName(const std::string& name) {
    if (name == "Left") return TextAlignment::Left;
    if (name == "Center") return TextAlignment::Center;
    if (name == "Right") return TextAlignment::Right;
    return std::nullopt;
}

std::string shapeKindName(ShapeKind kind) {
    switch (kind) {
        case ShapeKind::Rectangle: return "Rectangle";
        case ShapeKind::Ellipse: return "Ellipse";
        case ShapeKind::Line: return "Line";
    }
    return "Rectangle";
}

std::optional<ShapeKind> shapeKindFromName(const std::string& name) {
    if (name == "Rectangle") return ShapeKind::Rectangle;
    if (name == "Ellipse") return ShapeKind::Ellipse;
    if (name == "Line") return ShapeKind::Line;
    return std::nullopt;
}

std::string guideAxisName(CanvasGuide::Axis axis) {
    return axis == CanvasGuide::Axis::Horizontal ? "horizontal" : "vertical";
}

std::optional<CanvasGuide::Axis> guideAxisFromName(const std::string& name) {
    if (name == "horizontal") return CanvasGuide::Axis::Horizontal;
    if (name == "vertical") return CanvasGuide::Axis::Vertical;
    return std::nullopt;
}

int64_t utf16Length(const std::string& utf8) {
    int64_t count = 0;
    for (size_t index = 0; index < utf8.size();) {
        const unsigned char byte = static_cast<unsigned char>(utf8[index]);
        size_t length = 1;
        uint32_t code = byte;
        if (byte >= 0xF0) { length = 4; code = byte & 0x07u; }
        else if (byte >= 0xE0) { length = 3; code = byte & 0x0Fu; }
        else if (byte >= 0xC0) { length = 2; code = byte & 0x1Fu; }
        for (size_t extra = 1; extra < length && index + extra < utf8.size(); ++extra)
            code = (code << 6) | (static_cast<unsigned char>(utf8[index + extra]) & 0x3Fu);
        count += code > 0xFFFF ? 2 : 1;
        index += length;
    }
    return count;
}

}  // namespace compositor
