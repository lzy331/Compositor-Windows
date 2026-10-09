#pragma once

// The image side of a .comp: PNG sniffing, decoding and encoding with the same checks the
// original applies through ImageIO, and the layer-mask validity rule.

#include <QByteArray>
#include <QImage>

#include <cstdint>
#include <optional>
#include <string>

namespace compositor {

/// A layer's pixels: the full image, the Layers-panel thumbnail set and the layer name.
struct ImportedImage {
    QImage image;
    QImage thumbnail;
    std::string name;
};

/// What the header of an asset PNG declares, mirroring the ImageIO properties the original reads
/// (pixel width/height and bit depth; color type is kept for exactness of sniffing).
struct PngProbe {
    int64_t width = 0;
    int64_t height = 0;
    int bitDepth = 0;
    int colorType = 0;
};

/// Sniffs PNG bytes the way the original's ImageIO guard does: real PNG type, exactly one frame,
/// width/height > 0, depth <= 8. std::nullopt stands in for the original throwing missingImage.
std::optional<PngProbe> probePNG(const QByteArray& bytes);

/// Decodes a complete PNG into an ImportedImage with its thumbnail (longest side pinned to 96,
/// never upscaled). Color images come back as RGBA8888; 8-bit grayscale stays Format_Grayscale8
/// so the mask rules can see it. std::nullopt stands in for missingImage.
std::optional<ImportedImage> decodePNGAsset(const QByteArray& bytes, const std::string& name);

/// Encodes an image as PNG. std::nullopt stands in for ProjectError.encode.
std::optional<QByteArray> encodePNG(const QImage& image);

/// LayerMask.isValid: single channel, 8 bits per component, no alpha.
bool isValidMaskImage(const QImage& image);

/// The Layers-panel thumbnail: smooth, scale factor min(1, 96 / longest side).
QImage thumbnailFor(const QImage& image);

}  // namespace compositor
