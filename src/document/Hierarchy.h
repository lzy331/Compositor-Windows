#pragma once

// The structural rules a layer list must satisfy, guard for guard from the original's
// LayerGroups.swift (`LayerHierarchy.validate`) and LiveLayerMask.swift (`LiveMaskGraph.validate`),
// plus the hierarchy walks the compositor and the layer list both rely on.

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "document/Manifest.h"

namespace compositor {

/// One row of the hierarchy: a layer or folder, its nesting depth, and whether it shows
/// (its own visibility and every folder above it).
struct LayerEntry {
    const ProjectLayerRecord* layer = nullptr;
    int depth = 0;
    bool visible = true;
};

/// A flat layer list's parent links must describe a real forest: unique ids, folders without
/// pixels, every parent present, a folder, at most 64 links away, and never a loop.
class LayerHierarchy {
public:
    /// Throws ProjectError::invalid.
    static void validate(const std::vector<ProjectLayerRecord>& layers);

    /// Every layer and folder, depth-first. Bottom-to-top among siblings unless `topFirst`, and a
    /// folder's contents follow it unless the folder is collapsed. Depth stops past 64.
    static std::vector<LayerEntry> entries(const std::vector<ProjectLayerRecord>& layers, bool topFirst = false,
                                           const std::set<Uuid>& collapsed = {});

    /// The layers that show, folders left out, bottom to top.
    static std::vector<const ProjectLayerRecord*> visibleLayers(const std::vector<ProjectLayerRecord>& layers);
};

/// A folder's opacity multiplies into everything inside it: a layer at 50% in a folder at 50%
/// shows at 25%, while the layer itself still reads 50% in the panel. Folders are pass-through —
/// what's inside is drawn straight onto what is below, never composited as a unit — so the
/// folder's opacity is applied to each of those layers rather than to the folder as a whole.
class LayerOpacity {
public:
    /// What a folder lookup returns: its opacity and its own parent.
    using Folder = std::function<std::optional<std::pair<double, std::optional<Uuid>>>(const Uuid&)>;

    static double effective(double own, std::optional<Uuid> parent, const Folder& folder);

    /// The opacity this layer is drawn at, folders included.
    static double effectiveOpacity(const ProjectLayerRecord& layer,
                                   const std::map<Uuid, const ProjectLayerRecord*>& byID);
};

/// Clipping (live-mask) chains must resolve: each hop names a real record, folders never carry or
/// supply a chain, sources are unadjusted, and a chain is at most 256 links long, without loops.
class LiveMaskGraph {
public:
    /// Throws ProjectError::invalid.
    static void validate(const std::vector<ProjectLayerRecord>& layers);
};

}  // namespace compositor
