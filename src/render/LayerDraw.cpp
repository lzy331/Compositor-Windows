#include "render/LayerDraw.h"

#include "render/Geometry.h"

#include <algorithm>
#include <cmath>

namespace compositor::render {

double folderClipFactor(const Point& p, const std::vector<FolderClip>& folders) {
    double factor = 1;
    for (const FolderClip& folder : folders) {
        if (!folder.mask || folder.mask->width <= 0 || folder.mask->height <= 0) continue;
        const double width = folder.transform.size.width, height = folder.transform.size.height;
        if (width <= 0 || height <= 0) continue;
        const Affine toLocal = layerDocumentToLocal(folder.transform);
        // The clip rect's own edge antialiases; the context's antialias is on by default.
        const double coverage = pixelCoverage(toLocal, p, Rect(-width / 2, -height / 2, width, height), 0.5, true);
        if (coverage <= 0) return 0;
        const Point local = toLocal.apply(p);
        const double u = (local.x + width / 2) / width * folder.mask->width;
        const double v = (height / 2 - local.y) / height * folder.mask->height;
        factor *= coverage * sampleGray(*folder.mask, u, v, rawQuality(folder.transform.sampling)) / 255.0;
        if (factor <= 0) return 0;
    }
    return factor;
}

void drawLayer(const Raster& image, const LayerTransform& transform, double opacity, BlendMode mode,
               const GrayImage* mask, const std::vector<FolderClip>* folders, const GrayImage* clip,
               DownsampleStore& downsamples, Raster& target, Point origin) {
    if (target.isEmpty() || image.isEmpty()) return;
    const double width = transform.size.width, height = transform.size.height;
    if (width <= 0 || height <= 0 || opacity <= 0) return;
    if (mask && (mask->width <= 0 || mask->height <= 0)) mask = nullptr;
    const bool applyClip = clip != nullptr && clip->width == target.width && clip->height == target.height;

    // Large reductions draw from sharp halvings; Nearest skips them entirely.
    DownsampleStore::ReducedRaster source{&image, 0};
    if (transform.sampling != LayerSampling::Nearest) {
        source = downsamples.reduce(image, downsampleLevel(width / std::max(1, image.width)));
    }
    const double widthScale = static_cast<double>(source.image->width << source.level) / std::max(1, image.width);
    const double heightScale = static_cast<double>(source.image->height << source.level) / std::max(1, image.height);
    // The reduced copy's reach in the drawing space (y up, centered): x from -w/2, y down from h/2.
    const Rect extent(-width / 2, height / 2 - height * heightScale, width * widthScale, height * heightScale);

    const GrayImage* maskSource = nullptr;
    Rect maskExtent;
    if (mask) {
        DownsampleStore::ReducedGray reduced{&*mask, 0};
        if (transform.sampling != LayerSampling::Nearest) {
            reduced = downsamples.reduce(*mask, downsampleLevel(width / std::max(1, mask->width)));
        }
        maskSource = reduced.image;
        const double maskWidthScale =
            static_cast<double>(maskSource->width << reduced.level) / std::max(1, mask->width);
        const double maskHeightScale =
            static_cast<double>(maskSource->height << reduced.level) / std::max(1, mask->height);
        maskExtent = Rect(-width / 2, height / 2 - height * maskHeightScale, width * maskWidthScale,
                          height * maskHeightScale);
    }

    const double finalFactor = width / std::max(1, image.width) * static_cast<double>(1 << source.level);
    const Interp interp = interpolationFor(transform.sampling, finalFactor, transform.radians() == 0);
    const bool antialias = transform.sampling != LayerSampling::Nearest;

    const Affine toDocument = layerLocalToDocument(transform);
    const Affine toLocal = layerDocumentToLocal(transform);

    // The extent's corners in document space, padded a pixel for antialiased edges.
    const Point corners[4] = {toDocument.apply(extent.minX(), extent.minY()),
                              toDocument.apply(extent.maxX(), extent.minY()),
                              toDocument.apply(extent.maxX(), extent.maxY()),
                              toDocument.apply(extent.minX(), extent.maxY())};
    double minX = corners[0].x, maxX = corners[0].x, minY = corners[0].y, maxY = corners[0].y;
    for (const Point& corner : corners) {
        minX = std::min(minX, corner.x);
        maxX = std::max(maxX, corner.x);
        minY = std::min(minY, corner.y);
        maxY = std::max(maxY, corner.y);
    }
    const int x0 = std::max(0, static_cast<int>(std::floor(minX - origin.x)) - 1);
    const int x1 = std::min(target.width - 1, static_cast<int>(std::ceil(maxX - origin.x)) + 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(minY - origin.y)) - 1);
    const int y1 = std::min(target.height - 1, static_cast<int>(std::ceil(maxY - origin.y)) + 1);

    const Rect layerRect(-width / 2, -height / 2, width, height);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Point pixelCenter{origin.x + x + 0.5, origin.y + y + 0.5};
            const double coverage = pixelCoverage(toLocal, pixelCenter, layerRect, 0.5, antialias);
            if (coverage <= 0) continue;
            double f = opacity * coverage;
            const Point local = toLocal.apply(pixelCenter);
            if (maskSource) {
                const double maskCoverage = pixelCoverage(toLocal, pixelCenter, maskExtent, 0.5, antialias);
                if (maskCoverage <= 0) continue;
                f *= maskCoverage
                     * sampleGray(*maskSource, (local.x - maskExtent.minX()) / maskExtent.width * maskSource->width,
                                  (maskExtent.maxY() - local.y) / maskExtent.height * maskSource->height, interp)
                     / 255.0;
            }
            if (folders && !folders->empty()) f *= folderClipFactor(pixelCenter, *folders);
            if (applyClip) f *= clip->pixels[static_cast<size_t>(y) * clip->width + x] / 255.0;
            if (f <= 0) continue;
            const Sampled sampled =
                sampleRaster(*source.image, (local.x - extent.minX()) / extent.width * source.image->width,
                             (extent.maxY() - local.y) / extent.height * source.image->height, interp);
            blendOver(target, x, y, mode, sampled.r, sampled.g, sampled.b, sampled.a, f);
        }
    }
}

}  // namespace compositor::render
