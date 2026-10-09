#include "render/StackRenderer.h"

#include <algorithm>
#include <new>

#include "render/Raster.h"

namespace compositor::render {

StackRenderer::StackRenderer(Rect bounds, SourceFn source, DrawOwnFn drawOwn)
    : source_(std::move(source)), drawOwn_(std::move(drawOwn)) {
    bounds_ = Rect(std::floor(bounds.x), std::floor(bounds.y),
                   std::ceil(bounds.maxX()) - std::floor(bounds.x),
                   std::ceil(bounds.maxY()) - std::floor(bounds.y));
    pixelW_ = static_cast<int>(std::lround(bounds_.width));
    pixelH_ = static_cast<int>(std::lround(bounds_.height));
    validPixelSize_ = pixelW_ > 0 && pixelH_ > 0
        && static_cast<int64_t>(pixelW_) * pixelH_ <= DocumentLimits::maxSurfacePixels;
}

void StackRenderer::prepareStacks(const std::vector<Uuid>& ids, const ParentFn& parent, const BlendFn& blend) {
    stacks_.clear();
    stacked_.clear();
    stackModes_.clear();
    for (size_t index = 0; index < ids.size(); ++index) {
        const Uuid& base = ids[index];
        if (source_(base).has_value()) continue;
        if (adjustment_ && adjustment_(base)) continue;
        std::vector<Uuid> children;
        for (size_t i = index + 1; i < ids.size(); ++i) {
            const Uuid& child = ids[i];
            const std::optional<Uuid> childSource = source_(child);
            if (!childSource || *childSource != base) break;
            if (parent(child) != parent(base)) break;
            children.push_back(child);
        }
        if (!children.empty()) {
            stacks_[base] = children;
            stackModes_[base] = blend(base);
            for (const Uuid& child : children) stacked_.insert(child);
        }
    }
}

void StackRenderer::drawComposite(const Uuid& id, Raster& target, const std::vector<FolderClip>* folders,
                                  const GrayImage* clip) {
    if (stacked_.count(id)) return;
    if (adjustment_ && adjustment_(id)) {
        // adjust(id, in:) paints the layer's own settings over what is below; M0 renders nothing.
        return;
    }
    const auto stack = stacks_.find(id);
    if (stack == stacks_.end() || !validPixelSize_ || target.isEmpty()) {
        if (stack != stacks_.end()) {
            for (const Uuid& child : stack->second) stacked_.erase(child);
        }
        draw(id, target, folders, clip);
        return;
    }
    Raster group;
    try {
        group.resize(pixelW_, pixelH_);
    } catch (const std::bad_alloc&) {
        for (const Uuid& child : stack->second) stacked_.erase(child);
        draw(id, target, folders, clip);
        return;
    }
    drawOwn_(id, group, Point{bounds_.x, bounds_.y}, nullptr, nullptr);
    GrayImage alpha = extractAlpha(group);
    unpremultiplyOpaque(group);
    for (const Uuid& child : stack->second) {
        if (adjustment_ && adjustment_(child)) continue;
        drawOwn_(child, group, Point{bounds_.x, bounds_.y}, nullptr, nullptr);
    }
    restoreAlpha(group, alpha);

    const int height = std::min(target.height, group.height);
    const int width = std::min(target.width, group.width);
    BlendMode mode = BlendMode::Normal;
    const auto modeIt = stackModes_.find(id);
    if (modeIt != stackModes_.end()) mode = modeIt->second;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t* p = group.row(y) + x * 4;
            if (p[3] == 0) continue;
            double f = 1;
            if (folders) {
                f *= folderClipFactor(Point{bounds_.x + x + 0.5, bounds_.y + y + 0.5}, *folders);
            }
            if (f <= 0) continue;
            if (clip && clip->width == pixelW_ && clip->height == pixelH_) {
                f *= clip->pixels[static_cast<size_t>(y) * clip->width + x] / 255.0;
            }
            if (f <= 0) continue;
            blendOver(target, x, y, mode, p[0], p[1], p[2], p[3], f);
        }
    }
}

void StackRenderer::draw(const Uuid& id, Raster& target, const std::vector<FolderClip>* folders,
                         const GrayImage* clip) {
    const GrayImage* effective = clip;
    GrayImage combined;
    const std::optional<Uuid> sourceID = source_(id);
    if (sourceID) {
        const GrayImage* cov = coverage(*sourceID);
        if (!cov) return;
        if (clip && clip->width == cov->width && clip->height == cov->height) {
            combined.width = cov->width;
            combined.height = cov->height;
            combined.pixels.resize(cov->pixels.size());
            for (size_t i = 0, n = combined.pixels.size(); i < n; ++i) {
                combined.pixels[i] = static_cast<uint8_t>(
                    (static_cast<unsigned>(clip->pixels[i]) * cov->pixels[i] + 127) / 255);
            }
            cov = &combined;
        }
        effective = cov;
    }
    drawOwn_(id, target, Point{bounds_.x, bounds_.y}, folders, effective);
}

const GrayImage* StackRenderer::coverage(const Uuid& id) {
    const auto hit = cache_.find(id);
    if (hit != cache_.end()) return &hit->second;
    if (visiting_.count(id) || visiting_.size() >= 256 || !validPixelSize_) return nullptr;
    visiting_.insert(id);
    Raster pixels;
    try {
        pixels.resize(pixelW_, pixelH_);
    } catch (const std::bad_alloc&) {
        visiting_.erase(id);
        return nullptr;
    }
    draw(id, pixels, nullptr, nullptr);
    visiting_.erase(id);
    const auto inserted = cache_.emplace(id, extractAlpha(pixels));
    return &inserted.first->second;
}

}  // namespace compositor::render
