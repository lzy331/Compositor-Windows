#include "render/MaskClip.h"

#include "render/Geometry.h"

#include <algorithm>
#include <cmath>

namespace compositor::render {

namespace {

constexpr int64_t kCachePixelBudget = 64'000'000;

}  // namespace

uint8_t maskBackground(const GrayImage& thumbnail) {
    const int width = thumbnail.width, height = thumbnail.height;
    if (width <= 0 || height <= 0) return 255;
    int64_t total = 0, count = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (y != 0 && y != height - 1 && x != 0 && x != width - 1) continue;
            total += thumbnail.pixels[static_cast<size_t>(y) * width + x];
            count += 1;
        }
    }
    return total * 2 >= count * 255 ? 255 : 0;
}

const GrayImage* MaskPlacementStore::clipImage(const GrayImage& raw, const GrayImage& thumbnail,
                                               const std::optional<LayerTransform>& placement,
                                               const LayerTransform& layer, int width, int height) {
    if (!placement || placement->samePlacement(layer) || width <= 0 || height <= 0) return &raw;

    clock_ += 1;
    for (Entry& entry : entries_) {
        if (entry.mask == &raw && entry.placement == *placement && entry.layer == layer
            && entry.width == width && entry.height == height) {
            entry.lastUse = clock_;
            return &entry.image;
        }
    }

    // Near the size the mask covers in the grid, the resample draws from a sharp halving.
    const double covered = placement->size.width / std::max(1.0, layer.size.width) * width;
    const DownsampleStore::ReducedGray source =
        downsamples_.reduce(raw, downsampleLevel(covered / std::max(1, raw.width)));
    const uint8_t background = maskBackground(thumbnail);

    GrayImage image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<size_t>(width) * height);
    const double layerWidth = layer.size.width, layerHeight = layer.size.height;
    const double maskWidth = placement->size.width, maskHeight = placement->size.height;
    const double rawWidth = raw.width, rawHeight = raw.height;
    const Affine toDocument = layerLocalToDocument(layer);
    const Affine toMask = layerDocumentToLocal(*placement);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // Grid pixel center -> layer local -> document -> placement local -> mask pixels.
            const Point local{-layerWidth / 2 + (x + 0.5) * layerWidth / width,
                              layerHeight / 2 - (y + 0.5) * layerHeight / height};
            const Point inMask = toMask.apply(toDocument.apply(local));
            const double u = (inMask.x + maskWidth / 2) / maskWidth * rawWidth;
            const double v = (maskHeight / 2 - inMask.y) / maskHeight * rawHeight;
            uint8_t value = background;
            if (u >= 0 && u <= rawWidth && v >= 0 && v <= rawHeight) {
                const double gray = sampleGray(*source.image, u * (source.image->width / rawWidth),
                                               v * (source.image->height / rawHeight), Interp::Bicubic);
                value = static_cast<uint8_t>(std::lround(std::clamp(gray, 0.0, 255.0)));
            }
            image.pixels[static_cast<size_t>(y) * width + x] = value;
        }
    }

    if (static_cast<int64_t>(width) * height <= kCachePixelBudget) {
        entries_.push_back(Entry{&raw, *placement, layer, width, height, std::move(image), clock_});
        int64_t total = 0;
        for (const Entry& entry : entries_) total += static_cast<int64_t>(entry.width) * entry.height;
        while (entries_.size() > 8 || total > kCachePixelBudget) {
            auto oldest = entries_.begin();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                if (it->lastUse < oldest->lastUse) oldest = it;
            }
            total -= static_cast<int64_t>(oldest->width) * oldest->height;
            entries_.erase(oldest);
        }
        return &entries_.back().image;
    }
    oversized_.push_back(std::make_unique<GrayImage>(std::move(image)));
    if (oversized_.size() > 8) oversized_.erase(oversized_.begin());
    return oversized_.back().get();
}

}  // namespace compositor::render
