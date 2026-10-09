#include "render/Resample.h"

#include <algorithm>
#include <cmath>

namespace compositor::render {

Interp rawQuality(LayerSampling sampling) {
    switch (sampling) {
        case LayerSampling::Nearest: return Interp::Nearest;
        case LayerSampling::Smooth: return Interp::Bilinear;
        case LayerSampling::High: return Interp::Bicubic;
    }
    return Interp::Bicubic;
}

int downsampleLevel(double factor) {
    if (!std::isfinite(factor) || factor <= 0 || factor >= 0.5) return 0;
    return std::min(DownsampleStore::kMaxLevel, static_cast<int>(std::floor(std::log2(1.0 / factor))));
}

Interp interpolationFor(LayerSampling sampling, double finalFactor, bool upright) {
    if (sampling == LayerSampling::Nearest || (upright && std::abs(finalFactor - 1) < 0.001)) return Interp::Nearest;
    return finalFactor <= 1 ? Interp::Bilinear : rawQuality(sampling);
}

// The 4-tap reduction kernel's weights, at positions 2i-1 .. 2i+2.
static constexpr double kHalveWeights[4] = {-1.0 / 16, 9.0 / 16, 9.0 / 16, -1.0 / 16};

Raster halveRaster(const Raster& image) {
    const int width = (image.width + 1) / 2, height = (image.height + 1) / 2;
    Raster result(width, height);
    for (int y = 0; y < height; ++y) {
        uint8_t* out = result.row(y);
        for (int x = 0; x < width; ++x) {
            double sum[4] = {0, 0, 0, 0};
            for (int dy = 0; dy < 4; ++dy) {
                const int sy = 2 * y - 1 + dy;
                if (sy < 0 || sy >= image.height) continue;  // transparent padding
                const uint8_t* row = image.row(sy);
                for (int dx = 0; dx < 4; ++dx) {
                    const int sx = 2 * x - 1 + dx;
                    if (sx < 0 || sx >= image.width) continue;
                    const double weight = kHalveWeights[dx] * kHalveWeights[dy];
                    for (int c = 0; c < 4; ++c) sum[c] += weight * row[sx * 4 + c];
                }
            }
            const uint8_t a = static_cast<uint8_t>(std::lround(std::clamp(sum[3], 0.0, 255.0)));
            for (int c = 0; c < 3; ++c) {
                const uint8_t value = static_cast<uint8_t>(std::lround(std::clamp(sum[c], 0.0, 255.0)));
                out[x * 4 + c] = std::min(value, a);
            }
            out[x * 4 + 3] = a;
        }
    }
    return result;
}

GrayImage halveGray(const GrayImage& image) {
    const int width = (image.width + 1) / 2, height = (image.height + 1) / 2;
    GrayImage result;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0;
            for (int dy = 0; dy < 4; ++dy) {
                const int sy = std::clamp(2 * y - 1 + dy, 0, image.height - 1);
                const uint8_t* row = image.pixels.data() + static_cast<size_t>(sy) * image.width;
                for (int dx = 0; dx < 4; ++dx) {
                    const int sx = std::clamp(2 * x - 1 + dx, 0, image.width - 1);
                    sum += kHalveWeights[dx] * kHalveWeights[dy] * row[sx];
                }
            }
            result.pixels[static_cast<size_t>(y) * width + x] =
                static_cast<uint8_t>(std::lround(std::clamp(sum, 0.0, 255.0)));
        }
    }
    return result;
}

// Catmull-Rom weights at fraction t, for taps i0-1 .. i0+2.
static void bicubicWeights(double t, double weights[4]) {
    const double t2 = t * t, t3 = t2 * t;
    weights[0] = -0.5 * t3 + t2 - 0.5 * t;
    weights[1] = 1.5 * t3 - 2.5 * t2 + 1;
    weights[2] = -1.5 * t3 + 2 * t2 + 0.5 * t;
    weights[3] = 0.5 * t3 - 0.5 * t2;
}

Sampled sampleRaster(const Raster& image, double u, double v, Interp interp) {
    const int w = image.width, h = image.height;
    Sampled result;
    auto texel = [&](int x, int y, int c) -> double { return image.row(y)[x * 4 + c]; };
    if (interp == Interp::Nearest) {
        const int ix = std::clamp(static_cast<int>(std::floor(u)), 0, w - 1);
        const int iy = std::clamp(static_cast<int>(std::floor(v)), 0, h - 1);
        result.r = texel(ix, iy, 0);
        result.g = texel(ix, iy, 1);
        result.b = texel(ix, iy, 2);
        result.a = texel(ix, iy, 3);
    } else {
        const double cx = u - 0.5, cy = v - 0.5;
        const int i0 = static_cast<int>(std::floor(cx)), j0 = static_cast<int>(std::floor(cy));
        if (interp == Interp::Bilinear) {
            const double tx = cx - i0, ty = cy - j0;
            const int x0 = std::clamp(i0, 0, w - 1), x1 = std::clamp(i0 + 1, 0, w - 1);
            const int y0 = std::clamp(j0, 0, h - 1), y1 = std::clamp(j0 + 1, 0, h - 1);
            const double wx[2] = {1 - tx, tx}, wy[2] = {1 - ty, ty};
            double sum[4] = {0, 0, 0, 0};
            const int xs[2] = {x0, x1}, ys[2] = {y0, y1};
            for (int j = 0; j < 2; ++j) {
                for (int i = 0; i < 2; ++i) {
                    for (int c = 0; c < 4; ++c) sum[c] += wx[i] * wy[j] * texel(xs[i], ys[j], c);
                }
            }
            result.r = sum[0]; result.g = sum[1]; result.b = sum[2]; result.a = sum[3];
        } else {
            double wx[4], wy[4];
            bicubicWeights(cx - i0, wx);
            bicubicWeights(cy - j0, wy);
            double sum[4] = {0, 0, 0, 0};
            for (int j = 0; j < 4; ++j) {
                const int sy = std::clamp(j0 - 1 + j, 0, h - 1);
                for (int i = 0; i < 4; ++i) {
                    const int sx = std::clamp(i0 - 1 + i, 0, w - 1);
                    for (int c = 0; c < 4; ++c) sum[c] += wx[i] * wy[j] * texel(sx, sy, c);
                }
            }
            result.r = sum[0]; result.g = sum[1]; result.b = sum[2]; result.a = sum[3];
        }
    }
    result.r = std::min(std::clamp(result.r, 0.0, 255.0), result.a);
    result.g = std::min(std::clamp(result.g, 0.0, 255.0), result.a);
    result.b = std::min(std::clamp(result.b, 0.0, 255.0), result.a);
    result.a = std::clamp(result.a, 0.0, 255.0);
    return result;
}

double sampleGray(const GrayImage& image, double u, double v, Interp interp) {
    const int w = image.width, h = image.height;
    if (interp == Interp::Nearest) {
        const int ix = std::clamp(static_cast<int>(std::floor(u)), 0, w - 1);
        const int iy = std::clamp(static_cast<int>(std::floor(v)), 0, h - 1);
        return image.pixels[static_cast<size_t>(iy) * w + ix];
    }
    const double cx = u - 0.5, cy = v - 0.5;
    const int i0 = static_cast<int>(std::floor(cx)), j0 = static_cast<int>(std::floor(cy));
    double sum = 0;
    if (interp == Interp::Bilinear) {
        const double tx = cx - i0, ty = cy - j0;
        const int x0 = std::clamp(i0, 0, w - 1), x1 = std::clamp(i0 + 1, 0, w - 1);
        const int y0 = std::clamp(j0, 0, h - 1), y1 = std::clamp(j0 + 1, 0, h - 1);
        const double wx[2] = {1 - tx, tx}, wy[2] = {1 - ty, ty};
        const int xs[2] = {x0, x1}, ys[2] = {y0, y1};
        for (int j = 0; j < 2; ++j) {
            for (int i = 0; i < 2; ++i) {
                sum += wx[i] * wy[j] * image.pixels[static_cast<size_t>(ys[j]) * w + xs[i]];
            }
        }
    } else {
        double wx[4], wy[4];
        bicubicWeights(cx - i0, wx);
        bicubicWeights(cy - j0, wy);
        for (int j = 0; j < 4; ++j) {
            const int sy = std::clamp(j0 - 1 + j, 0, h - 1);
            for (int i = 0; i < 4; ++i) {
                const int sx = std::clamp(i0 - 1 + i, 0, w - 1);
                sum += wx[i] * wy[j] * image.pixels[static_cast<size_t>(sy) * w + sx];
            }
        }
    }
    return std::clamp(sum, 0.0, 255.0);
}

void DownsampleStore::markRecent(const void* key) {
    for (int i = 0; i < recentCount_; ++i) {
        if (recent_[i] == key) return;
    }
    if (recentCount_ < 4) {
        recent_[recentCount_++] = key;
    } else {
        for (int i = 0; i < 3; ++i) recent_[i] = recent_[i + 1];
        recent_[3] = key;
    }
}

bool DownsampleStore::isRecent(const void* key) const {
    for (int i = 0; i < recentCount_; ++i) {
        if (recent_[i] == key) return true;
    }
    return false;
}

void DownsampleStore::evict() {
    int64_t total = 0;
    for (const auto& item : rasters_) total += item.second.pixels();
    for (const auto& item : grays_) total += item.second.pixels();
    while (total > kPixelBudget) {
        auto oldestRaster = rasters_.end();
        for (auto it = rasters_.begin(); it != rasters_.end(); ++it) {
            if (isRecent(it->first)) continue;
            if (oldestRaster == rasters_.end() || it->second.lastUse < oldestRaster->second.lastUse) oldestRaster = it;
        }
        auto oldestGray = grays_.end();
        for (auto it = grays_.begin(); it != grays_.end(); ++it) {
            if (isRecent(it->first)) continue;
            if (oldestGray == grays_.end() || it->second.lastUse < oldestGray->second.lastUse) oldestGray = it;
        }
        if (oldestRaster == rasters_.end() && oldestGray == grays_.end()) break;
        if (oldestGray == grays_.end() ||
            (oldestRaster != rasters_.end() && oldestRaster->second.lastUse <= oldestGray->second.lastUse)) {
            total -= oldestRaster->second.pixels();
            rasters_.erase(oldestRaster);
        } else {
            total -= oldestGray->second.pixels();
            grays_.erase(oldestGray);
        }
    }
}

DownsampleStore::ReducedRaster DownsampleStore::reduce(const Raster& image, int wanted) {
    if (wanted < 1 || (image.width <= 1 && image.height <= 1)) return {&image, 0};
    const void* key = &image;
    clock_ += 1;
    auto [it, inserted] = rasters_.try_emplace(key);
    Entry<Raster>& entry = it->second;
    entry.lastUse = clock_;
    markRecent(key);
    while (static_cast<int>(entry.levels.size()) < wanted) {
        const Raster& previous = entry.levels.empty() ? image : entry.levels.back();
        if (previous.width <= 1 && previous.height <= 1) break;
        Raster next = halveRaster(previous);
        if (next.isEmpty()) break;
        entry.levels.push_back(std::move(next));
    }
    if (entry.levels.empty()) {
        if (inserted) rasters_.erase(it);
        return {&image, 0};
    }
    const int applied = std::min(wanted, static_cast<int>(entry.levels.size()));
    evict();
    return {&it->second.levels[applied - 1], applied};
}

DownsampleStore::ReducedGray DownsampleStore::reduce(const GrayImage& image, int wanted) {
    if (wanted < 1 || (image.width <= 1 && image.height <= 1)) return {&image, 0};
    const void* key = &image;
    clock_ += 1;
    auto [it, inserted] = grays_.try_emplace(key);
    Entry<GrayImage>& entry = it->second;
    entry.lastUse = clock_;
    markRecent(key);
    while (static_cast<int>(entry.levels.size()) < wanted) {
        const GrayImage& previous = entry.levels.empty() ? image : entry.levels.back();
        if (previous.width <= 1 && previous.height <= 1) break;
        GrayImage next = halveGray(previous);
        if (next.width <= 0 || next.height <= 0) break;
        entry.levels.push_back(std::move(next));
    }
    if (entry.levels.empty()) {
        if (inserted) grays_.erase(it);
        return {&image, 0};
    }
    const int applied = std::min(wanted, static_cast<int>(entry.levels.size()));
    evict();
    return {&it->second.levels[applied - 1], applied};
}

}  // namespace compositor::render
