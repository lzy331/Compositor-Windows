#include "document/Hierarchy.h"

#include "io/ProjectError.h"

#include <map>
#include <set>

namespace compositor {

void LayerHierarchy::validate(const std::vector<ProjectLayerRecord>& layers) {
    std::map<Uuid, const ProjectLayerRecord*> byID;
    for (const ProjectLayerRecord& layer : layers) {
        // Swift's `updateValue` stores either way and returns the previous value; the guard fails
        // on a duplicate. The isGroup check is the second condition of the same guard.
        const bool duplicate = byID.count(layer.id) != 0;
        byID[layer.id] = &layer;
        if (duplicate) throw ProjectError::invalid();
        if (layer.isGroupRecord() && layer.imageFile.has_value()) throw ProjectError::invalid();
    }
    for (const ProjectLayerRecord& layer : layers) {
        std::set<Uuid> seen{layer.id};
        std::optional<Uuid> parent = layer.parentID;
        while (parent.has_value()) {
            if (seen.size() > 64) throw ProjectError::invalid();
            if (!seen.insert(*parent).second) throw ProjectError::invalid();
            const auto node = byID.find(*parent);
            if (node == byID.end()) throw ProjectError::invalid();
            if (!node->second->isGroupRecord()) throw ProjectError::invalid();
            parent = node->second->parentID;
        }
        // A folder may keep only 63 ancestors; a plain layer's own chain may run the full 64.
        if (layer.isGroupRecord() && seen.size() > 64) throw ProjectError::invalid();
    }
}

void LiveMaskGraph::validate(const std::vector<ProjectLayerRecord>& layers) {
    std::map<Uuid, const ProjectLayerRecord*> records;
    for (const ProjectLayerRecord& layer : layers) {
        if (records.count(layer.id) != 0) throw ProjectError::invalid();
        records[layer.id] = &layer;
    }
    for (const ProjectLayerRecord& layer : layers) {
        std::set<Uuid> path;
        std::optional<Uuid> current = layer.id;
        while (current.has_value()) {
            if (path.size() >= 256) throw ProjectError::invalid();
            if (!path.insert(*current).second) throw ProjectError::invalid();
            const auto record = records.find(*current);
            if (record == records.end()) throw ProjectError::invalid();
            const ProjectLayerRecord& node = *record->second;
            if (node.maskSourceID.has_value()) {
                // A folder never carries a chain, and its source must be a plain, unadjusted layer.
                if (node.isGroupRecord()) throw ProjectError::invalid();
                const auto source = records.find(*node.maskSourceID);
                if (source == records.end()) throw ProjectError::invalid();
                if (source->second->isGroupRecord()) throw ProjectError::invalid();
                if (source->second->adjustment.has_value()) throw ProjectError::invalid();
            }
            current = node.maskSourceID;
        }
    }
}

namespace {

using Children = std::map<std::optional<Uuid>, std::vector<const ProjectLayerRecord*>>;

void appendEntries(const Children& children, std::optional<Uuid> parent, int depth, bool visible, bool topFirst,
                   const std::set<Uuid>& collapsed, std::vector<LayerEntry>& result) {
    if (depth > 64) return;
    const auto siblings = children.find(parent);
    if (siblings == children.end()) return;
    auto append = [&](const ProjectLayerRecord* layer) {
        const bool effective = visible && layer->isVisible;
        result.push_back({layer, depth, effective});
        if (layer->isGroupRecord() && collapsed.count(layer->id) == 0)
            appendEntries(children, layer->id, depth + 1, effective, topFirst, collapsed, result);
    };
    if (topFirst) {
        for (auto it = siblings->second.rbegin(); it != siblings->second.rend(); ++it) append(*it);
    } else {
        for (const ProjectLayerRecord* layer : siblings->second) append(layer);
    }
}

}  // namespace

std::vector<LayerEntry> LayerHierarchy::entries(const std::vector<ProjectLayerRecord>& layers, bool topFirst,
                                                const std::set<Uuid>& collapsed) {
    Children children;
    for (const ProjectLayerRecord& layer : layers) children[layer.parentID].push_back(&layer);
    std::vector<LayerEntry> result;
    appendEntries(children, std::nullopt, 0, true, topFirst, collapsed, result);
    return result;
}

std::vector<const ProjectLayerRecord*> LayerHierarchy::visibleLayers(const std::vector<ProjectLayerRecord>& layers) {
    std::vector<const ProjectLayerRecord*> result;
    for (const LayerEntry& entry : entries(layers)) {
        if (entry.visible && !entry.layer->isGroupRecord()) result.push_back(entry.layer);
    }
    return result;
}

double LayerOpacity::effective(double own, std::optional<Uuid> parent, const Folder& folder) {
    double opacity = own;
    std::optional<Uuid> id = parent;
    int depth = 0;
    while (id.has_value() && depth < 64) {
        const std::optional<std::pair<double, std::optional<Uuid>>> node = folder(*id);
        if (!node) break;
        opacity *= node->first;
        id = node->second;
        depth += 1;
    }
    return opacity;
}

double LayerOpacity::effectiveOpacity(const ProjectLayerRecord& layer,
                                      const std::map<Uuid, const ProjectLayerRecord*>& byID) {
    return effective(layer.effectiveOpacityValue(), layer.parentID, [&byID](const Uuid& id) {
        const auto it = byID.find(id);
        if (it == byID.end() || it->second == nullptr) return std::optional<std::pair<double, std::optional<Uuid>>>{};
        return std::optional<std::pair<double, std::optional<Uuid>>>{
            std::pair{it->second->effectiveOpacityValue(), it->second->parentID}};
    });
}

}  // namespace compositor
