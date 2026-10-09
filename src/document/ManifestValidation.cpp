#include "document/ManifestValidation.h"

#include "document/Hierarchy.h"
#include "io/ProjectError.h"

#include <cstdint>
#include <set>

namespace compositor {

namespace {

/// Swift's CharacterSet.whitespacesAndNewlines, used by `trimmingCharacters` on layer names:
/// Unicode whitespace (Zs) plus the newline controls, NEL and line/paragraph separators.
bool isWhitespaceAndNewlines(uint32_t codePoint) {
    switch (codePoint) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
        case 0x85: case 0xA0: case 0x1680:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return codePoint >= 0x2000 && codePoint <= 0x200A;
    }
}

/// True when the (UTF-8) name trims to nothing. A decoding error counts as non-blank, so
/// damaged bytes are never silently treated as whitespace.
bool isBlank(const std::string& text) {
    size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        uint32_t codePoint = lead;
        size_t length = 1;
        if (lead >= 0xF0) {
            if (index + 3 >= text.size()) return false;
            codePoint = (lead & 0x07u) << 18 | (static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 12
                | (static_cast<unsigned char>(text[index + 2]) & 0x3Fu) << 6
                | (static_cast<unsigned char>(text[index + 3]) & 0x3Fu);
            length = 4;
        } else if (lead >= 0xE0) {
            if (index + 2 >= text.size()) return false;
            codePoint = (lead & 0x0Fu) << 12 | (static_cast<unsigned char>(text[index + 1]) & 0x3Fu) << 6
                | (static_cast<unsigned char>(text[index + 2]) & 0x3Fu);
            length = 3;
        } else if (lead >= 0xC0) {
            if (index + 1 >= text.size()) return false;
            codePoint = (lead & 0x1Fu) << 6 | (static_cast<unsigned char>(text[index + 1]) & 0x3Fu);
            length = 2;
        } else if (lead >= 0x80) {
            return false;  // A stray continuation byte: not whitespace.
        }
        if (!isWhitespaceAndNewlines(codePoint)) return false;
        index += length;
    }
    return true;
}

void validateGuides(const ProjectManifest& manifest) {
    const std::vector<CanvasGuide> none;
    const std::vector<CanvasGuide>& guides = manifest.guides.has_value() ? *manifest.guides : none;
    if (manifest.version < 8) {
        if (!guides.empty()) throw ProjectError::invalid();
        return;
    }
    if (guides.size() > 1000) throw ProjectError::tooLarge();
    std::set<Uuid> ids;
    for (const CanvasGuide& guide : guides) {
        if (!ids.insert(guide.id).second || !guide.isValid()) throw ProjectError::invalid();
    }
}

}  // namespace

void validateManifest(const ProjectManifest& manifest) {
    if (manifest.format != "com.compositor.project") throw ProjectError::invalid();
    if (manifest.version < ProjectManifest::supportedLow || manifest.version > ProjectManifest::supportedHigh) {
        throw ProjectError::version(manifest.version);
    }
    if (manifest.colorSpace != "sRGB") throw ProjectError::invalid();
    if (manifest.resolution.has_value()) {
        if (!finite(*manifest.resolution) || !inRange(*manifest.resolution, 1, 9600)) throw ProjectError::invalid();
    }
    if (!(manifest.width >= 1 && manifest.width <= DocumentLimits::maxSide)
        || !(manifest.height >= 1 && manifest.height <= DocumentLimits::maxSide)
        || manifest.layers.size() > 10'000) {
        throw ProjectError::tooLarge();
    }
    for (const ProjectLayerRecord& layer : manifest.layers) {
        if (layer.text.has_value()) {
            // Per-letter colors arrived in version 10, per-letter faces in version 11.
            const LayerTextStyle& text = *layer.text;
            const bool ok = text.isValid()
                && (!text.colorRuns.has_value() || manifest.version >= 10)
                && (!text.fontRuns.has_value() || manifest.version >= 11)
                && layer.imageFile.has_value() && !layer.isGroupRecord() && !layer.adjustment.has_value();
            if (!ok) throw ProjectError::invalid();
        }
        if (layer.adjustment.has_value()) {
            const LayerAdjustment& adjustment = *layer.adjustment;
            const bool ok = manifest.version >= 7 && !layer.isGroupRecord()
                && !layer.imageFile.has_value() && adjustment.isValid();
            if (!ok) throw ProjectError::invalid();
            if (adjustment.kind == AdjustmentKind::GaussianBlur || adjustment.kind == AdjustmentKind::MotionBlur
                || adjustment.kind == AdjustmentKind::AddNoise) {
                if (!(manifest.version >= 9)) throw ProjectError::invalid();
            }
        }
        // Layer masks arrived in version 4, folder masks in version 6.
        const bool maskOk = (!layer.maskFile.has_value()
                || (manifest.version >= (layer.isGroupRecord() ? 6 : 4)
                    && *layer.maskFile == layer.id.toString() + ".mask.png"))
            && (!layer.maskEnabled.has_value() || layer.maskFile.has_value())
            && (!layer.maskPlacement.has_value()
                || (layer.maskPlacement->isValid() && layer.maskFile.has_value()));
        if (!maskOk) throw ProjectError::invalid();
        const double opacity = layer.effectiveOpacityValue();
        const BlendMode blend = layer.effectiveBlendMode();
        // Folders took an opacity of their own in version 8, which multiplies into what is inside
        // them; their blend mode is still pass-through, so it stays Normal.
        const bool styleOk = finite(opacity) && inRange(opacity, 0, 1)
            && (manifest.version >= 3 || (opacity == 1 && blend == BlendMode::Normal))
            && (!layer.isGroupRecord()
                || (blend == BlendMode::Normal && (manifest.version >= 8 || opacity == 1)));
        if (!styleOk) throw ProjectError::invalid();
    }
    LayerHierarchy::validate(manifest.layers);
    LiveMaskGraph::validate(manifest.layers);
    if (manifest.version < 5) {
        for (const ProjectLayerRecord& layer : manifest.layers) {
            if (layer.maskSourceID.has_value()) throw ProjectError::invalid();
        }
    }
    if (manifest.version == 1) {
        for (const ProjectLayerRecord& layer : manifest.layers) {
            if (layer.parentID.has_value() || layer.isGroupRecord()) throw ProjectError::invalid();
        }
    }
    std::set<Uuid> ids;
    for (const ProjectLayerRecord& layer : manifest.layers) {
        const bool ok = ids.insert(layer.id).second && layer.transform.isValid() && !isBlank(layer.name)
            && layer.name.size() <= 16'384
            && (!layer.imageFile.has_value() || *layer.imageFile == layer.id.toString() + ".png");
        if (!ok) throw ProjectError::invalid();
    }
    if (manifest.activeLayerID.has_value() && ids.count(*manifest.activeLayerID) == 0) {
        throw ProjectError::invalid();
    }
    validateGuides(manifest);
}

}  // namespace compositor
