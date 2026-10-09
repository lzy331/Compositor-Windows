#pragma once

// A mask moved apart from its layer: the raw gray mask resampled into the layer's pixel grid
// where it sits (LayerMask.clipImage / placed / drawSmooth), with what it shows beyond its
// pixels voted from its thumbnail (LayerMask.background), and the port of MaskPlacementCache.

#include "document/Manifest.h"
#include "render/Raster.h"
#include "render/Resample.h"

#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <vector>

namespace compositor::render {

/// What a placed mask shows beyond its pixels: 255 or 0, whichever most of its edge is.
uint8_t maskBackground(const GrayImage& thumbnail);

/// Masks resampled into their layers' grids, so redraws reuse them; the least recently used go
/// beyond a few entries or a pixel budget.
class MaskPlacementStore {
public:
    explicit MaskPlacementStore(DownsampleStore& downsamples) : downsamples_(downsamples) {}

    /// The mask over a layer's `width` × `height` pixel grid, or `raw` itself while `placement`
    /// is empty or covers the layer's own grid.
    const GrayImage* clipImage(const GrayImage& raw, const GrayImage& thumbnail,
                               const std::optional<LayerTransform>& placement, const LayerTransform& layer,
                               int width, int height);

private:
    struct Entry {
        const GrayImage* mask = nullptr;
        LayerTransform placement;
        LayerTransform layer;
        int width = 0;
        int height = 0;
        GrayImage image;
        uint64_t lastUse = 0;
    };

    DownsampleStore& downsamples_;
    std::list<Entry> entries_;
    std::vector<std::unique_ptr<GrayImage>> oversized_;
    uint64_t clock_ = 0;
};

}  // namespace compositor::render
