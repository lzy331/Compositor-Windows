#pragma once

// The port of LiveMaskRenderer: draws the layers that show, resolves clipping (live-mask)
// chains through per-render caches (coverage = a layer's own pixels' alpha, including its
// masks, independent of visibility and color), and draws a clipping stack's base and children
// as one unit that shares the base's alpha.

#include "core/BlendMode.h"
#include "core/Types.h"
#include "render/LayerDraw.h"
#include "render/Raster.h"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace compositor::render {

class StackRenderer {
public:
    using SourceFn = std::function<std::optional<Uuid>(const Uuid&)>;
    using ParentFn = std::function<std::optional<Uuid>(const Uuid&)>;
    using BlendFn = std::function<BlendMode(const Uuid&)>;
    using DrawOwnFn = std::function<void(const Uuid& id, Raster& target, Point origin,
                                         const std::vector<FolderClip>* folders, const GrayImage* clip)>;
    using AdjustmentFn = std::function<bool(const Uuid&)>;

    StackRenderer(Rect bounds, SourceFn source, DrawOwnFn drawOwn);

    /// How a layer's adjustment is found. M0 renders adjustments as nothing.
    void setAdjustment(AdjustmentFn adjustment) { adjustment_ = std::move(adjustment); }

    /// Finds the clipping stacks among `ids` (bottom to top): a base followed directly by
    /// layers whose source is it, in the same folder. Clears what a previous call found.
    void prepareStacks(const std::vector<Uuid>& ids, const ParentFn& parent, const BlendFn& blend);

    /// Draws layer `id` and its stack, if any, into `target`, clipped by `folders` and `clip`.
    void drawComposite(const Uuid& id, Raster& target, const std::vector<FolderClip>* folders = nullptr,
                       const GrayImage* clip = nullptr);

private:
    void draw(const Uuid& id, Raster& target, const std::vector<FolderClip>* folders, const GrayImage* clip);
    const GrayImage* coverage(const Uuid& id);

    Rect bounds_;
    SourceFn source_;
    DrawOwnFn drawOwn_;
    AdjustmentFn adjustment_;
    std::map<Uuid, std::vector<Uuid>> stacks_;
    std::set<Uuid> stacked_;
    std::map<Uuid, BlendMode> stackModes_;
    std::map<Uuid, GrayImage> cache_;
    std::set<Uuid> visiting_;
    int pixelW_ = 0;
    int pixelH_ = 0;
    bool validPixelSize_ = false;
};

}  // namespace compositor::render
