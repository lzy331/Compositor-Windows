#pragma once

// The port of LayerRenderer.draw: one layer's pixels resampled into a document-grid target,
// with its opacity, blend mode, own mask and any folder masks, plus the clipping chain's
// extracted coverage. Large reductions draw from sharp halvings (never for Nearest).

#include "core/BlendMode.h"
#include "document/Manifest.h"
#include "render/Raster.h"
#include "render/Resample.h"

#include <vector>

namespace compositor::render {

/// One folder mask on the way out from a layer: `mask` covers the folder's rect, placed by
/// `transform` (FolderMaskClip).
struct FolderClip {
    const GrayImage* mask = nullptr;
    LayerTransform transform;
};

/// What all `folders` masks multiply into the coverage of document point `p` (each mask's
/// edge votes whole, its pixels sampled at the folder's own quality).
double folderClipFactor(const Point& p, const std::vector<FolderClip>& folders);

/// Draws `image` as a layer: its transform's rect (with the reduction's overshoot) is mapped
/// into `target`, and every pixel it covers is composited with `opacity`, `mode`, mask and
/// folder clips up to `f`. `origin` maps target pixel (x, y) to document pixel
/// (origin.x + x, origin.y + y) — (0, 0) for an export, the bounds' corner for a surface.
void drawLayer(const Raster& image, const LayerTransform& transform, double opacity, BlendMode mode,
               const GrayImage* mask, const std::vector<FolderClip>* folders, const GrayImage* clip,
               DownsampleStore& downsamples, Raster& target, Point origin = Point());

}  // namespace compositor::render
