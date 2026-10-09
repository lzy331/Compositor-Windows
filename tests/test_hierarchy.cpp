// Verifies LayerHierarchy.validate and LiveMaskGraph.validate against the exact guard order and
// boundaries of LayerGroups.swift and LiveLayerMask.swift: where invalid is thrown, how deep a
// chain may go (64 parents for a leaf, 63 for a folder, 256 mask links), and which cycles and
// dangling links are rejected.

#include "document/Hierarchy.h"
#include "document/Manifest.h"
#include "io/ProjectError.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using compositor::LayerHierarchy;
using compositor::LiveMaskGraph;
using compositor::ProjectError;
using compositor::ProjectLayerRecord;
using compositor::Uuid;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool throwsInvalid(const std::function<void()>& body) {
    try {
        body();
        return false;
    } catch (const ProjectError& error) {
        return error.kind() == ProjectError::Kind::Invalid;
    }
}

ProjectLayerRecord layer(const Uuid& id) {
    ProjectLayerRecord record;
    record.id = id;
    record.name = "Layer";
    return record;
}

ProjectLayerRecord group(const Uuid& id) {
    ProjectLayerRecord record = layer(id);
    record.isGroup = true;
    return record;
}

/// `count` folders chained by parent links; the first has no parent. Returns them in order.
std::vector<ProjectLayerRecord> groupChain(int count) {
    std::vector<ProjectLayerRecord> layers;
    for (int i = 0; i < count; ++i) {
        ProjectLayerRecord record = layer(Uuid::generate());
        record.isGroup = true;
        if (i > 0) record.parentID = layers.back().id;
        layers.push_back(record);
    }
    return layers;
}

void testRasterForests() {
    const Uuid a = Uuid::generate(), b = Uuid::generate();
    check(!throwsInvalid([&] { LayerHierarchy::validate({layer(a), layer(b)}); }), "flat layers pass");
    check(!throwsInvalid([&] { LayerHierarchy::validate({}); }), "empty list passes");

    const Uuid groupID = Uuid::generate();
    const auto groupWithChildren = std::vector<ProjectLayerRecord>{
        group(groupID),
        [&] {
            ProjectLayerRecord child = layer(Uuid::generate());
            child.parentID = groupID;
            return child;
        }(),
    };
    check(!throwsInvalid([&] { LayerHierarchy::validate(groupWithChildren); }), "child inside folder passes");

    check(throwsInvalid([&] { LayerHierarchy::validate({layer(a), layer(a)}); }), "duplicate ids reject");

    ProjectLayerRecord folderWithPixels = group(groupID);
    folderWithPixels.imageFile = groupID.toString() + ".png";
    check(throwsInvalid([&] { LayerHierarchy::validate({folderWithPixels}); }), "folder with pixels rejects");
}

void testParentLinks() {
    ProjectLayerRecord orphan = layer(Uuid::generate());
    orphan.parentID = Uuid::generate();
    check(throwsInvalid([&] { LayerHierarchy::validate({orphan}); }), "missing parent rejects");

    const Uuid rasterID = Uuid::generate(), groupID = Uuid::generate();
    ProjectLayerRecord rasterParent = layer(rasterID);
    ProjectLayerRecord child = layer(Uuid::generate());
    child.parentID = rasterID;
    check(throwsInvalid([&] { LayerHierarchy::validate({rasterParent, child}); }),
          "plain layer cannot parent");
    (void)groupID;

    ProjectLayerRecord selfParent = group(Uuid::generate());
    selfParent.parentID = selfParent.id;
    check(throwsInvalid([&] { LayerHierarchy::validate({selfParent}); }), "self-parent rejects");

    ProjectLayerRecord first = group(Uuid::generate());
    ProjectLayerRecord second = group(Uuid::generate());
    second.parentID = first.id;
    first.parentID = second.id;
    std::vector<ProjectLayerRecord> cycle{first, second};
    check(throwsInvalid([&] { LayerHierarchy::validate(cycle); }), "two-folder cycle rejects");

    // A folder nested in a folder nested in itself: the cycle sits two links up.
    ProjectLayerRecord top = group(Uuid::generate());
    ProjectLayerRecord middle = group(Uuid::generate());
    ProjectLayerRecord bottom = group(Uuid::generate());
    middle.parentID = top.id;
    bottom.parentID = middle.id;
    top.parentID = bottom.id;
    std::vector<ProjectLayerRecord> longCycle{top, middle, bottom};
    check(throwsInvalid([&] { LayerHierarchy::validate(longCycle); }), "three-folder cycle rejects");
}

void testDepthBoundaries() {
    // A plain layer may sit under 64 folders; a 65th link is rejected.
    {
        std::vector<ProjectLayerRecord> layers = groupChain(64);
        ProjectLayerRecord leaf = layer(Uuid::generate());
        leaf.parentID = layers.back().id;
        layers.push_back(leaf);
        check(!throwsInvalid([&] { LayerHierarchy::validate(layers); }), "leaf 64 deep passes");
    }
    {
        std::vector<ProjectLayerRecord> layers = groupChain(65);
        ProjectLayerRecord leaf = layer(Uuid::generate());
        leaf.parentID = layers.back().id;
        layers.push_back(leaf);
        check(throwsInvalid([&] { LayerHierarchy::validate(layers); }), "leaf 65 deep rejects");
    }
    // A folder is bounded one rung earlier: 63 ancestors pass, 64 are rejected.
    {
        std::vector<ProjectLayerRecord> layers = groupChain(64);
        check(!throwsInvalid([&] { LayerHierarchy::validate(layers); }), "folder 63 deep passes");
    }
    {
        std::vector<ProjectLayerRecord> layers = groupChain(65);
        check(throwsInvalid([&] { LayerHierarchy::validate(layers); }), "folder 64 deep rejects");
    }
}

ProjectLayerRecord maskTarget(const Uuid& id, const Uuid& source) {
    ProjectLayerRecord record = layer(id);
    record.maskSourceID = source;
    return record;
}

void testMaskChains() {
    const Uuid target = Uuid::generate(), source = Uuid::generate();
    check(!throwsInvalid([&] { LiveMaskGraph::validate({maskTarget(target, source), layer(source)}); }),
          "simple chain passes");

    check(throwsInvalid([&] { LiveMaskGraph::validate({maskTarget(target, Uuid::generate())}); }),
          "missing source rejects");

    ProjectLayerRecord sourceFolder = group(Uuid::generate());
    check(throwsInvalid([&] { LiveMaskGraph::validate({maskTarget(target, sourceFolder.id), sourceFolder}); }),
          "folder source rejects");

    ProjectLayerRecord adjusted = layer(source);
    adjusted.adjustment = compositor::LayerAdjustment();
    check(throwsInvalid([&] { LiveMaskGraph::validate({maskTarget(target, source), adjusted}); }),
          "adjusted source rejects");

    ProjectLayerRecord folderTarget = group(Uuid::generate());
    folderTarget.maskSourceID = Uuid::generate();
    check(throwsInvalid([&] { LiveMaskGraph::validate({folderTarget}); }), "folder cannot carry a chain");

    ProjectLayerRecord first = maskTarget(Uuid::generate(), Uuid::generate());
    ProjectLayerRecord second = layer(first.maskSourceID.value());
    second.maskSourceID = first.id;
    std::vector<ProjectLayerRecord> cycle{first, second};
    check(throwsInvalid([&] { LiveMaskGraph::validate(cycle); }), "mask cycle rejects");

    check(throwsInvalid([&] { LiveMaskGraph::validate({maskTarget(target, target)}); }), "self-source rejects");
}

void testMaskDepthBoundaries() {
    // A chain of 256 records resolves; the 257th link is rejected.
    {
        std::vector<ProjectLayerRecord> layers;
        for (int i = 0; i < 256; ++i) layers.push_back(layer(Uuid::generate()));
        for (int i = 0; i + 1 < 256; ++i) layers[i].maskSourceID = layers[i + 1].id;
        check(!throwsInvalid([&] { LiveMaskGraph::validate(layers); }), "256-long chain passes");
    }
    {
        std::vector<ProjectLayerRecord> layers;
        for (int i = 0; i < 257; ++i) layers.push_back(layer(Uuid::generate()));
        for (int i = 0; i + 1 < 257; ++i) layers[i].maskSourceID = layers[i + 1].id;
        check(throwsInvalid([&] { LiveMaskGraph::validate(layers); }), "257-long chain rejects");
    }
}

}  // namespace

int main() {
    testRasterForests();
    testParentLinks();
    testDepthBoundaries();
    testMaskChains();
    testMaskDepthBoundaries();
    if (failures == 0) std::printf("test_hierarchy: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
