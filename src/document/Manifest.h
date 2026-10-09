#pragma once

// The .comp manifest model: everything `manifest.json` can contain, ported from the original's
// ProjectStore.swift plus LayerTransform/LayerEffects/TypeTool/ShapeTool/Guides. Optional fields
// mirror the Swift optionals exactly, because "nil" decides which JSON keys a save writes.

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/BlendMode.h"
#include "core/Types.h"
#include "document/Adjustments.h"

namespace compositor {

// MARK: - Transform (LayerTransform.swift)

enum class LayerSampling { Nearest, Smooth, High };

std::string layerSamplingName(LayerSampling sampling);
std::optional<LayerSampling> layerSamplingFromName(const std::string& name);

/// Unrotated bounds in document pixels; rotation is clockwise around their center.
struct LayerTransform {
    Point origin;
    Size size;
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    LayerSampling sampling = LayerSampling::High;

    Point center() const { return {origin.x + size.width / 2, origin.y + size.height / 2}; }

    /// Rotation in radians, clockwise, truncated into (−360, 360) degrees first as Swift does.
    double radians() const { return std::fmod(rotation, 360.0) * 3.14159265358979323846 / 180.0; }

    /// True when this transform frames exactly the same picture at exactly the same place as `other`;
    /// only the sampling may differ. (LayerTransform.swift `samePlacement(as:)`.)
    bool samePlacement(const LayerTransform& other) const {
        LayerTransform mine = *this;
        mine.sampling = other.sampling;
        return mine == other;
    }

    bool isValid() const {
        return finite(origin.x) && finite(origin.y) && finite(size.width) && finite(size.height) && finite(rotation)
            && inRange(size.width, 1, 300'000) && inRange(size.height, 1, 300'000)
            && std::abs(origin.x) <= 1'000'000 && std::abs(origin.y) <= 1'000'000;
    }

    bool operator==(const LayerTransform& other) const {
        return origin == other.origin && size == other.size && rotation == other.rotation
            && flipX == other.flipX && flipY == other.flipY && sampling == other.sampling;
    }
};

// MARK: - Layer effects (LayerEffects.swift)

struct StrokeEffect {
    std::optional<bool> enabled;
    double size = 4;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 1;
    bool inside = false;
    bool isEnabled() const { return enabled.value_or(true); }
    bool isValid() const {
        return finite(size) && inRange(size, 0, 500) && finite(opacity) && inRange(opacity, 0, 1)
            && colorValid();
    }
    bool colorValid() const {
        return finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1)
            && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const StrokeEffect& other) const {
        return enabled == other.enabled && size == other.size && red == other.red && green == other.green
            && blue == other.blue && opacity == other.opacity && inside == other.inside;
    }
};

/// The layer's shape repeated behind it, offset and softened.
struct ShadowEffect {
    std::optional<bool> enabled;
    double angle = 90;
    double distance = 20;
    double blur = 20;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 0.5;
    bool isEnabled() const { return enabled.value_or(true); }
    /// Where the shadow sits, in layer pixels (y grows downward, as the layer's own pixels do).
    Size offset() const {
        const double radians = angle * 3.14159265358979323846 / 180;
        return {-std::cos(radians) * distance, std::sin(radians) * distance};
    }
    bool isValid() const {
        return finite(angle) && inRange(angle, -360, 360) && finite(distance) && inRange(distance, 0, 5000)
            && finite(blur) && inRange(blur, 0, 500) && finite(opacity) && inRange(opacity, 0, 1)
            && finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1)
            && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const ShadowEffect& other) const {
        return enabled == other.enabled && angle == other.angle && distance == other.distance
            && blur == other.blur && red == other.red && green == other.green && blue == other.blue
            && opacity == other.opacity;
    }
};

struct ColorOverlayEffect {
    std::optional<bool> enabled;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 1;
    bool isEnabled() const { return enabled.value_or(true); }
    bool isValid() const {
        return finite(opacity) && inRange(opacity, 0, 1) && finite(red) && inRange(red, 0, 1)
            && finite(green) && inRange(green, 0, 1) && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const ColorOverlayEffect& other) const {
        return enabled == other.enabled && red == other.red && green == other.green && blue == other.blue
            && opacity == other.opacity;
    }
};

struct InnerShadowEffect {
    std::optional<bool> enabled;
    double angle = 90;
    double distance = 10;
    double blur = 10;
    double red = 0;
    double green = 0;
    double blue = 0;
    double opacity = 0.5;
    bool isEnabled() const { return enabled.value_or(true); }
    Size offset() const {
        const double radians = angle * 3.14159265358979323846 / 180;
        return {-std::cos(radians) * distance, std::sin(radians) * distance};
    }
    bool isValid() const {
        return finite(angle) && inRange(angle, -360, 360) && finite(distance) && inRange(distance, 0, 5000)
            && finite(blur) && inRange(blur, 0, 500) && finite(opacity) && inRange(opacity, 0, 1)
            && finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1)
            && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const InnerShadowEffect& other) const {
        return enabled == other.enabled && angle == other.angle && distance == other.distance
            && blur == other.blur && red == other.red && green == other.green && blue == other.blue
            && opacity == other.opacity;
    }
};

struct OuterGlowEffect {
    std::optional<bool> enabled;
    double size = 20;
    double red = 1;
    double green = 1;
    double blue = 1;
    double opacity = 0.75;
    bool isEnabled() const { return enabled.value_or(true); }
    bool isValid() const {
        return finite(size) && inRange(size, 0, 500) && finite(opacity) && inRange(opacity, 0, 1)
            && finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1)
            && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const OuterGlowEffect& other) const {
        return enabled == other.enabled && size == other.size && red == other.red && green == other.green
            && blue == other.blue && opacity == other.opacity;
    }
};

struct InnerGlowEffect {
    std::optional<bool> enabled;
    double size = 10;
    double red = 1;
    double green = 1;
    double blue = 1;
    double opacity = 0.75;
    bool isEnabled() const { return enabled.value_or(true); }
    bool isValid() const {
        return finite(size) && inRange(size, 0, 500) && finite(opacity) && inRange(opacity, 0, 1)
            && finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1)
            && finite(blue) && inRange(blue, 0, 1);
    }
    bool operator==(const InnerGlowEffect& other) const {
        return enabled == other.enabled && size == other.size && red == other.red && green == other.green
            && blue == other.blue && opacity == other.opacity;
    }
};

/// What a layer draws around itself. Kept with the layer, so it follows every edit.
struct LayerEffects {
    std::optional<StrokeEffect> stroke;
    std::optional<ShadowEffect> shadow;
    std::optional<ColorOverlayEffect> colorOverlay;
    std::optional<InnerShadowEffect> innerShadow;
    std::optional<OuterGlowEffect> outerGlow;
    std::optional<InnerGlowEffect> innerGlow;

    bool isEmpty() const {
        return !stroke && !shadow && !colorOverlay && !innerShadow && !outerGlow && !innerGlow;
    }
    bool isValid() const {
        return (!stroke || stroke->isValid()) && (!shadow || shadow->isValid())
            && (!colorOverlay || colorOverlay->isValid()) && (!innerShadow || innerShadow->isValid())
            && (!outerGlow || outerGlow->isValid()) && (!innerGlow || innerGlow->isValid());
    }
    bool operator==(const LayerEffects& other) const {
        return stroke == other.stroke && shadow == other.shadow && colorOverlay == other.colorOverlay
            && innerShadow == other.innerShadow && outerGlow == other.outerGlow && innerGlow == other.innerGlow;
    }
};

// MARK: - Text (TypeTool.swift)

enum class TextAlignment { Left, Center, Right };

std::string textAlignmentName(TextAlignment alignment);
std::optional<TextAlignment> textAlignmentFromName(const std::string& name);

/// Letters painted in a color other than the layer's, in UTF-16 offsets into `content`.
struct LayerTextColorRun {
    int64_t location = 0;
    int64_t length = 0;
    double red = 0;
    double green = 0;
    double blue = 0;
    bool operator==(const LayerTextColorRun& other) const {
        return location == other.location && length == other.length && red == other.red
            && green == other.green && blue == other.blue;
    }
};

/// Letters set in a face other than the layer's, in the same offsets.
struct LayerTextFontRun {
    int64_t location = 0;
    int64_t length = 0;
    std::string fontName;
    bool operator==(const LayerTextFontRun& other) const {
        return location == other.location && length == other.length && fontName == other.fontName;
    }
};

/// UTF-16 code-unit count, the unit text offsets are kept in (Swift `content.utf16.count`).
int64_t utf16Length(const std::string& utf8);

struct LayerTextStyle {
    std::string content = "Text";
    std::string fontName = "Helvetica";
    double fontSize = 72;
    double red = 0;
    double green = 0;
    double blue = 0;
    TextAlignment alignment = TextAlignment::Left;
    double tracking = 0;
    /// Baseline to baseline, in layer pixels. 0 is Auto: 120% of the font size.
    double leading = 0;
    /// Fixed paragraph bounds in layer pixels. Nil supports older point-text layers.
    std::optional<Size> boxSize;
    std::optional<std::vector<LayerTextColorRun>> colorRuns;
    std::optional<std::vector<LayerTextFontRun>> fontRuns;

    double autoLeading() const { return fontSize * 1.2; }
    double lineHeight() const { return leading > 0 ? leading : autoLeading(); }
    static constexpr double padding = 12;

    bool boxIsValid() const {
        if (!boxSize) return true;
        return finite(boxSize->width) && finite(boxSize->height)
            && inRange(boxSize->width, 16, DocumentLimits::maxSide)
            && inRange(boxSize->height, 16, DocumentLimits::maxSide)
            && boxSize->width * boxSize->height <= DocumentLimits::maxSurfacePixels;
    }
    bool colorRunsAreValid() const {
        if (!colorRuns) return true;
        int64_t end = 0;
        for (const auto& run : *colorRuns) {
            if (run.location < end || run.length <= 0 || run.location > INT64_MAX - run.length) return false;
            if (!finite(run.red) || !inRange(run.red, 0, 1) || !finite(run.green) || !inRange(run.green, 0, 1)
                || !finite(run.blue) || !inRange(run.blue, 0, 1))
                return false;
            end = run.location + run.length;
        }
        return !colorRuns->empty() && end <= utf16Length(content);
    }
    bool fontRunsAreValid() const {
        if (!fontRuns) return true;
        int64_t end = 0;
        for (const auto& run : *fontRuns) {
            if (run.location < end || run.length <= 0 || run.location > INT64_MAX - run.length) return false;
            if (run.fontName.empty() || utf16Length(run.fontName) > 200) return false;
            if (run.fontName.find('\n') != std::string::npos || run.fontName.find('\r') != std::string::npos) return false;
            end = run.location + run.length;
        }
        return !fontRuns->empty() && end <= utf16Length(content);
    }
    bool isValid() const {
        return utf16Length(content) <= 100'000 && boxIsValid()
            && finite(fontSize) && inRange(fontSize, 1, 2000)
            && finite(red) && inRange(red, 0, 1) && finite(green) && inRange(green, 0, 1) && finite(blue) && inRange(blue, 0, 1)
            && finite(tracking) && inRange(tracking, -100, 1000)
            && finite(leading) && inRange(leading, 0, 5000)
            && colorRunsAreValid() && fontRunsAreValid();
    }
    bool operator==(const LayerTextStyle& other) const {
        return content == other.content && fontName == other.fontName && fontSize == other.fontSize
            && red == other.red && green == other.green && blue == other.blue && alignment == other.alignment
            && tracking == other.tracking && leading == other.leading && boxSize == other.boxSize
            && colorRuns == other.colorRuns && fontRuns == other.fontRuns;
    }
};

// MARK: - Shape (ShapeTool.swift)

enum class ShapeKind { Rectangle, Ellipse, Line };

std::string shapeKindName(ShapeKind kind);
std::optional<ShapeKind> shapeKindFromName(const std::string& name);

/// What a shape layer draws, kept so the shape can be drawn again at a new size.
struct LayerShapeStyle {
    ShapeKind kind = ShapeKind::Rectangle;
    double red = 0;
    double green = 0;
    double blue = 0;
    /// Document pixels, whatever size the shape is scaled to.
    double cornerRadius = 0;
    /// A line's thickness, and its two ends as fractions of the layer's box (0–1). Nil on other shapes.
    std::optional<double> lineWidth;
    std::optional<Point> start;
    std::optional<Point> end;
    bool operator==(const LayerShapeStyle& other) const {
        return kind == other.kind && red == other.red && green == other.green && blue == other.blue
            && cornerRadius == other.cornerRadius && lineWidth == other.lineWidth
            && start == other.start && end == other.end;
    }
};

// MARK: - Guides (Guides.swift)

/// A user-placed alignment line. Horizontal guides sit at a document Y; vertical at a document X.
struct CanvasGuide {
    enum class Axis { Horizontal, Vertical };
    Uuid id;
    Axis axis = Axis::Horizontal;
    /// Document pixels: Y for a horizontal guide, X for a vertical one.
    double position = 0;
    bool isValid() const { return finite(position) && std::abs(position) <= 1'000'000; }
    bool operator==(const CanvasGuide& other) const {
        return id == other.id && axis == other.axis && position == other.position;
    }
};

std::string guideAxisName(CanvasGuide::Axis axis);
std::optional<CanvasGuide::Axis> guideAxisFromName(const std::string& name);

// MARK: - The manifest (ProjectStore.swift)

struct ProjectLayerRecord {
    Uuid id;
    std::string name;
    bool isVisible = true;
    LayerTransform transform;
    std::optional<std::string> imageFile;
    std::optional<Uuid> parentID;
    std::optional<bool> isGroup;
    std::optional<double> opacity;
    std::optional<BlendMode> blendMode;
    std::optional<std::string> maskFile;
    std::optional<bool> maskEnabled;
    std::optional<Uuid> maskSourceID;
    std::optional<LayerAdjustment> adjustment;
    /// A mask moved apart from its layer: where it sits on the document.
    std::optional<LayerTransform> maskPlacement;
    /// Nil (older projects) is linked.
    std::optional<bool> maskLinked;
    /// A shape layer's shape, drawn again when the layer is scaled.
    std::optional<LayerShapeStyle> shape;
    /// The stroke and drop shadow drawn around the layer.
    std::optional<LayerEffects> effects;
    std::optional<LayerTextStyle> text;

    bool isGroupRecord() const { return isGroup.value_or(false); }
    double effectiveOpacityValue() const { return opacity.value_or(1); }
    BlendMode effectiveBlendMode() const { return blendMode.value_or(BlendMode::Normal); }

    bool operator==(const ProjectLayerRecord& other) const {
        return id == other.id && name == other.name && isVisible == other.isVisible
            && transform == other.transform && imageFile == other.imageFile && parentID == other.parentID
            && isGroup == other.isGroup && opacity == other.opacity && blendMode == other.blendMode
            && maskFile == other.maskFile && maskEnabled == other.maskEnabled
            && maskSourceID == other.maskSourceID && adjustment == other.adjustment
            && maskPlacement == other.maskPlacement && maskLinked == other.maskLinked && shape == other.shape
            && effects == other.effects && text == other.text;
    }
};

struct ProjectManifest {
    /// The format version new saves write.
    static constexpr int currentVersion = 11;
    static constexpr int supportedLow = 1;
    static constexpr int supportedHigh = currentVersion;

    std::string format = "com.compositor.project";
    /// The decoded version is kept at full Int width so an out-of-range value reaches the
    /// version gate (and its error message) exactly as the Swift Int would.
    int64_t version = currentVersion;
    std::string colorSpace = "sRGB";
    std::optional<double> resolution;
    Uuid documentID;
    /// Full Int width too; validate() narrows to the 1…maxSide range with Swift's exact
    /// guard order, so absurd values end in the same error they would there.
    int64_t width = 0;
    int64_t height = 0;
    std::optional<Uuid> activeLayerID;
    std::vector<ProjectLayerRecord> layers;
    /// Alignment guides. Missing on versions 1–7.
    std::optional<std::vector<CanvasGuide>> guides;

    bool operator==(const ProjectManifest& other) const {
        return format == other.format && version == other.version && colorSpace == other.colorSpace
            && resolution == other.resolution && documentID == other.documentID && width == other.width
            && height == other.height && activeLayerID == other.activeLayerID && layers == other.layers
            && guides == other.guides;
    }
};

}  // namespace compositor
