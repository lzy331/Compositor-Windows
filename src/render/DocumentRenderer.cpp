#include "render/DocumentRenderer.h"

#include <cmath>
#include <map>
#include <new>
#include <optional>
#include <vector>

#include "document/Hierarchy.h"
#include "io/ProjectError.h"
#include "render/LayerDraw.h"
#include "render/MaskClip.h"
#include "render/RenderError.h"
#include "render/StackRenderer.h"

namespace compositor::render {

Raster DocumentRenderer::render(const ProjectSnapshot& snapshot) {
    const ProjectManifest& manifest = snapshot.manifest;
    if (manifest.width < 1 || manifest.width > DocumentLimits::maxSide || manifest.height < 1
        || manifest.height > DocumentLimits::maxSide
        || manifest.width * manifest.height > DocumentLimits::maxSurfacePixels) {
        throw RenderError::tooLarge();
    }

    Raster target;
    try {
        target.resize(static_cast<int>(manifest.width), static_cast<int>(manifest.height));
    } catch (const std::bad_alloc&) {
        throw RenderError::render();
    }

    std::map<Uuid, const ProjectLayerRecord*> records;
    for (const ProjectLayerRecord& layer : manifest.layers) records[layer.id] = &layer;
    for (const ProjectLayerRecord& layer : manifest.layers) {
        if (layer.imageFile && snapshot.images.find(layer.id) == snapshot.images.end()) {
            throw ProjectError::missingImage();
        }
        if (layer.maskFile && snapshot.masks.find(layer.id) == snapshot.masks.end()) {
            throw ProjectError::missingImage();
        }
    }
    LiveMaskGraph::validate(manifest.layers);

    std::map<Uuid, Raster> rasters;
    std::map<Uuid, GrayImage> masksGray;
    std::map<Uuid, GrayImage> maskThumbs;
    for (const ProjectLayerRecord& layer : manifest.layers) {
        if (layer.imageFile) {
            const auto asset = snapshot.images.find(layer.id);
            if (asset != snapshot.images.end()) rasters.emplace(layer.id, rasterFromQImage(asset->second.image));
        }
        if (layer.maskFile) {
            const auto asset = snapshot.masks.find(layer.id);
            if (asset != snapshot.masks.end()) {
                masksGray.emplace(layer.id, grayFromQImage(asset->second.image));
                maskThumbs.emplace(layer.id, grayFromQImage(asset->second.thumbnail));
            }
        }
    }

    DownsampleStore downsamples;
    MaskPlacementStore maskStore(downsamples);
    const Rect bounds(0, 0, static_cast<double>(manifest.width), static_cast<double>(manifest.height));

    auto source = [&records](const Uuid& id) -> std::optional<Uuid> {
        const auto it = records.find(id);
        if (it == records.end()) return std::nullopt;
        return it->second->maskSourceID;
    };
    auto drawOwn = [&](const Uuid& id, Raster& layerTarget, Point origin, const std::vector<FolderClip>* folders,
                       const GrayImage* clip) {
        const auto record = records.find(id);
        if (record == records.end()) return;
        const auto image = rasters.find(id);
        if (image == rasters.end()) return;
        const ProjectLayerRecord& layer = *record->second;
        const double opacity = LayerOpacity::effectiveOpacity(layer, records);
        const GrayImage* mask = nullptr;
        if (layer.maskFile && layer.maskEnabled.value_or(true)) {
            const auto raw = masksGray.find(id);
            const auto thumbnail = maskThumbs.find(id);
            if (raw != masksGray.end() && thumbnail != maskThumbs.end()) {
                mask = maskStore.clipImage(raw->second, thumbnail->second, layer.maskPlacement, layer.transform,
                                           image->second.width, image->second.height);
            }
        }
        drawLayer(image->second, layer.transform, opacity, layer.effectiveBlendMode(), mask, folders, clip,
                  downsamples, layerTarget, origin);
    };
    StackRenderer live(bounds, source, drawOwn);
    live.setAdjustment([&records](const Uuid& id) {
        const auto it = records.find(id);
        return it != records.end() && it->second->adjustment.has_value();
    });

    auto parent = [&records](const Uuid& id) -> std::optional<Uuid> {
        const auto it = records.find(id);
        if (it == records.end()) return std::nullopt;
        return it->second->parentID;
    };
    auto blend = [&records](const Uuid& id) -> BlendMode {
        const auto it = records.find(id);
        if (it == records.end()) return BlendMode::Normal;
        return it->second->effectiveBlendMode();
    };

    const std::vector<const ProjectLayerRecord*> visible = LayerHierarchy::visibleLayers(manifest.layers);
    std::vector<Uuid> ids;
    ids.reserve(visible.size());
    for (const ProjectLayerRecord* layer : visible) ids.push_back(layer->id);
    live.prepareStacks(ids, parent, blend);

    std::map<Uuid, std::optional<FolderClip>> clips;
    for (const Uuid& id : ids) {
        std::vector<FolderClip> appliers;
        std::optional<Uuid> folder = parent(id);
        int depth = 0;
        while (folder && depth < 64) {
            if (clips.find(*folder) == clips.end()) {
                std::optional<FolderClip> entry;
                const auto record = records.find(*folder);
                if (record != records.end()) {
                    const ProjectLayerRecord& f = *record->second;
                    if (f.maskFile && f.maskEnabled.value_or(true)) {
                        const auto image = masksGray.find(f.id);
                        if (image != masksGray.end()) entry = FolderClip{&image->second, f.transform};
                    }
                }
                clips.emplace(*folder, entry);
            }
            const std::optional<FolderClip>& entry = clips.at(*folder);
            if (entry) appliers.push_back(*entry);
            folder = parent(*folder);
            ++depth;
        }
        if (appliers.empty()) live.drawComposite(id, target);
        else live.drawComposite(id, target, &appliers);
    }
    return target;
}

}  // namespace compositor::render
