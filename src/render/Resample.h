#pragma once

#include <cstdint>
#include <map>

#include "core/Types.h"
#include "document/Manifest.h"
#include "render/Raster.h"

namespace compositor::render {

// How a draw resamples: the original's `interpolation(_:finalFactor:upright:)`.
enum class Interp { Nearest, Bilinear, Bicubic };

// Smooth -> bilinear, High -> bicubic (folder masks sample at the layer's raw setting).
Interp rawQuality(LayerSampling sampling);

// Halvings to draw from when an image lands `factor` output pixels per image pixel: the most that
// still leave the copy at least that large (0 from half size up, strict). Capped at 6.
int downsampleLevel(double factor);

// The resample for the last step of a draw; `upright` marks an unrotated layer drawn pixel for
// pixel, which is copied straight across. Shrinking uses bilinear: the sharp halvings have
// already done the heavy reduction, and a piece of an image must come out like the whole.
Interp interpolationFor(LayerSampling sampling, double finalFactor, bool upright);

// Exactly half the size, rounded up, with the 4-tap kernel (-1, 9, 9, -1)/16 per axis. Color
// copies are read as if padded with transparent pixels (out-of-range -> 0) and clamp channels
// past alpha, so edges fade without glowing; gray copies extend the edge instead.
Raster halveRaster(const Raster& image);
GrayImage halveGray(const GrayImage& image);

// One sample of `image` at the continuous texel coordinate (u, v), u in [0, width].
// Premultiplied throughout, clamped at the end.
struct Sampled {
    double r = 0, g = 0, b = 0, a = 0;
};
Sampled sampleRaster(const Raster& image, double u, double v, Interp interp);
double sampleGray(const GrayImage& image, double u, double v, Interp interp);

// A chain of halved copies per source image, keyed by the image's address. Copies are built on
// demand and the least recently used are dropped beyond the surface budget; the last few keys
// returned stay exempt so several copies can be used at once.
class DownsampleStore {
public:
    static constexpr int kMaxLevel = 6;
    static constexpr int64_t kPixelBudget = DocumentLimits::maxSurfacePixels;

    struct ReducedRaster {
        const Raster* image = nullptr;
        int level = 0;  // halvings applied (fewer than asked only if a copy failed)
    };
    struct ReducedGray {
        const GrayImage* image = nullptr;
        int level = 0;
    };

    ReducedRaster reduce(const Raster& image, int wanted);
    ReducedGray reduce(const GrayImage& image, int wanted);

private:
    template <typename Image>
    struct Entry {
        std::vector<Image> levels;
        uint64_t lastUse = 0;
        int64_t pixels() const {
            int64_t total = 0;
            for (const Image& level : levels) total += static_cast<int64_t>(level.width) * level.height;
            return total;
        }
    };

    void markRecent(const void* key);
    bool isRecent(const void* key) const;
    void evict();

    std::map<const void*, Entry<Raster>> rasters_;
    std::map<const void*, Entry<GrayImage>> grays_;
    const void* recent_[4] = {};
    int recentCount_ = 0;
    uint64_t clock_ = 0;
};

}  // namespace compositor::render
