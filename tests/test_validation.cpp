// Verifies validateManifest against the exact gates of ProjectStore.validate + validateGuides:
// version-gated features (masks, maskSourceID, adjustments, blur/noise kinds, folder opacity,
// guides, per-letter text colors and fonts), ownership rules, size caps, and guide limits.

#include "document/Manifest.h"
#include "document/ManifestValidation.h"
#include "io/ProjectError.h"

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using compositor::AdjustmentKind;
using compositor::BlendMode;
using compositor::CanvasGuide;
using compositor::DocumentLimits;
using compositor::LayerAdjustment;
using compositor::LayerTextColorRun;
using compositor::LayerTextFontRun;
using compositor::LayerTextStyle;
using compositor::ProjectError;
using compositor::ProjectLayerRecord;
using compositor::ProjectManifest;
using compositor::Size;
using compositor::Uuid;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::printf("FAIL %s\n", what.c_str());
        ++failures;
    }
}

bool throwsKind(const std::function<void()>& body, ProjectError::Kind kind) {
    try {
        body();
        return false;
    } catch (const ProjectError& error) {
        return error.kind() == kind;
    }
}

void checkInvalid(const std::function<void()>& body, const std::string& what) {
    if (!throwsKind(body, ProjectError::Kind::Invalid)) {
        std::printf("FAIL expected invalid: %s\n", what.c_str());
        ++failures;
    }
}

void checkOk(const ProjectManifest& manifest, const std::string& what) {
    try {
        compositor::validateManifest(manifest);
    } catch (const ProjectError&) {
        std::printf("FAIL expected valid: %s\n", what.c_str());
        ++failures;
    }
}

ProjectLayerRecord layer(const Uuid& id) {
    ProjectLayerRecord record;
    record.id = id;
    record.name = "Layer";
    record.transform.size = Size{8, 8};
    return record;
}

ProjectLayerRecord group(const Uuid& id) {
    ProjectLayerRecord record = layer(id);
    record.isGroup = true;
    return record;
}

ProjectManifest base() {
    ProjectManifest manifest;
    manifest.documentID = Uuid::generate();
    manifest.width = 1920;
    manifest.height = 1080;
    return manifest;
}

/// A raster layer with its own image file, ready to have a feature switched on.
ProjectLayerRecord rasterLayer() {
    ProjectLayerRecord record = layer(Uuid::generate());
    record.imageFile = record.id.toString() + ".png";
    return record;
}

void testDocumentGates() {
    checkOk(base(), "plain document passes");

    {
        ProjectManifest manifest = base();
        manifest.format = "com.example.other";
        checkInvalid([&] { compositor::validateManifest(manifest); }, "foreign format");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 0;
        check(throwsKind([&] { compositor::validateManifest(manifest); }, ProjectError::Kind::Version),
              "version 0 is a version error");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 12;
        check(throwsKind([&] { compositor::validateManifest(manifest); }, ProjectError::Kind::Version),
              "version 12 is a version error");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 1;
        checkOk(manifest, "version 1 passes");
    }
    {
        ProjectManifest manifest = base();
        manifest.colorSpace = "Display P3";
        checkInvalid([&] { compositor::validateManifest(manifest); }, "non-sRGB colorspace");
    }
    {
        ProjectManifest manifest = base();
        manifest.resolution = 0.5;
        checkInvalid([&] { compositor::validateManifest(manifest); }, "resolution below 1");
    }
    {
        ProjectManifest manifest = base();
        manifest.resolution = 9601;
        checkInvalid([&] { compositor::validateManifest(manifest); }, "resolution above 9600");
    }
    {
        ProjectManifest manifest = base();
        manifest.resolution = 300;
        checkOk(manifest, "resolution 300 passes");
    }
    {
        ProjectManifest manifest = base();
        manifest.width = 0;
        check(throwsKind([&] { compositor::validateManifest(manifest); }, ProjectError::Kind::TooLarge),
              "zero width is tooLarge");
    }
    {
        ProjectManifest manifest = base();
        manifest.height = DocumentLimits::maxSide + 1;
        check(throwsKind([&] { compositor::validateManifest(manifest); }, ProjectError::Kind::TooLarge),
              "height above maxSide is tooLarge");
    }
    {
        ProjectManifest manifest = base();
        manifest.layers.resize(10'001, layer(Uuid::generate()));
        check(throwsKind([&] { compositor::validateManifest(manifest); }, ProjectError::Kind::TooLarge),
              "10001 layers is tooLarge");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        manifest.layers.push_back(record);
        manifest.activeLayerID = record.id;
        checkOk(manifest, "active layer present passes");

        manifest.activeLayerID = Uuid::generate();
        checkInvalid([&] { compositor::validateManifest(manifest); }, "dangling active layer");
    }
}

void testLayerIdentityGates() {
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.name = "   \t\n ";
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "blank name");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.name = std::string(16'385, 'x');
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "name over 16384 bytes");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.imageFile = Uuid::generate().toString() + ".png";
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "imageFile not named after id");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.transform.size = {0, 100};
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "degenerate transform");
    }
    {
        ProjectManifest manifest = base();
        manifest.layers.push_back(layer(Uuid::generate()));
        const ProjectLayerRecord duplicate = manifest.layers.front();
        manifest.layers.push_back(duplicate);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "duplicate layer ids");
    }
}

void testMaskVersionGates() {
    {
        ProjectManifest manifest = base();
        manifest.version = 4;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.maskFile = record.id.toString() + ".mask.png";
        manifest.layers.push_back(record);
        checkOk(manifest, "layer mask at version 4 passes");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 3;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.maskFile = record.id.toString() + ".mask.png";
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "layer mask at version 3");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 5;
        ProjectLayerRecord record = group(Uuid::generate());
        record.maskFile = record.id.toString() + ".mask.png";
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "folder mask at version 5");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 6;
        ProjectLayerRecord record = group(Uuid::generate());
        record.maskFile = record.id.toString() + ".mask.png";
        manifest.layers.push_back(record);
        checkOk(manifest, "folder mask at version 6 passes");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.maskFile = Uuid::generate().toString() + ".mask.png";
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "maskFile not named after id");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.maskEnabled = true;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "maskEnabled without maskFile");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.maskPlacement = record.transform;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "maskPlacement without maskFile");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 4;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.maskSourceID = Uuid::generate();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "maskSourceID at version 4");
    }
}

void testAdjustmentGates() {
    {
        ProjectManifest manifest = base();
        manifest.version = 6;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.adjustment = LayerAdjustment();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "adjustment at version 6");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.adjustment = LayerAdjustment();
        manifest.layers.push_back(record);
        checkOk(manifest, "adjustment at version 11 passes");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = group(Uuid::generate());
        record.adjustment = LayerAdjustment();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "folder adjustment");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = rasterLayer();
        record.adjustment = LayerAdjustment();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "adjustment with pixels");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 8;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.adjustment = LayerAdjustment();
        record.adjustment->kind = AdjustmentKind::GaussianBlur;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "blur at version 8");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 9;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.adjustment = LayerAdjustment();
        record.adjustment->kind = AdjustmentKind::AddNoise;
        manifest.layers.push_back(record);
        checkOk(manifest, "noise at version 9 passes");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.adjustment = LayerAdjustment();
        record.adjustment->blurRadius = 0;
        record.adjustment->kind = AdjustmentKind::GaussianBlur;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "adjustment with invalid fields");
    }
}

void testTextGates() {
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = rasterLayer();
        record.text = LayerTextStyle();
        manifest.layers.push_back(record);
        checkOk(manifest, "text at version 11 passes");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.text = LayerTextStyle();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "text without imageFile");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = group(Uuid::generate());
        record.imageFile = std::nullopt;
        record.text = LayerTextStyle();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "folder text");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = rasterLayer();
        record.text = LayerTextStyle();
        record.adjustment = LayerAdjustment();
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "text with adjustment");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 9;
        ProjectLayerRecord record = rasterLayer();
        LayerTextStyle text;
        text.colorRuns = std::vector<LayerTextColorRun>{{0, 4, 1, 0, 0}};
        record.text = text;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "text color runs at version 9");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 10;
        ProjectLayerRecord record = rasterLayer();
        LayerTextStyle text;
        text.colorRuns = std::vector<LayerTextColorRun>{{0, 4, 1, 0, 0}};
        record.text = text;
        manifest.layers.push_back(record);
        checkOk(manifest, "text color runs at version 10 pass");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 10;
        ProjectLayerRecord record = rasterLayer();
        LayerTextStyle text;
        text.fontRuns = std::vector<LayerTextFontRun>{{0, 4, "Menlo"}};
        record.text = text;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "text font runs at version 10");
    }
    {
        ProjectManifest manifest = base();
        LayerTextStyle text;
        text.content = "Hi";
        text.colorRuns = std::vector<LayerTextColorRun>{{0, 3, 1, 0, 0}};
        check(!text.isValid(), "color run past content end is invalid");
    }
}

void testStyleVersionGates() {
    {
        ProjectManifest manifest = base();
        manifest.version = 2;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.opacity = 0.5;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "opacity at version 2");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 2;
        ProjectLayerRecord record = layer(Uuid::generate());
        record.blendMode = BlendMode::Multiply;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "blend mode at version 2");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 2;
        manifest.layers.push_back(layer(Uuid::generate()));
        checkOk(manifest, "version 2 without styles passes");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 7;
        ProjectLayerRecord record = group(Uuid::generate());
        record.opacity = 0.5;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "folder opacity at version 7");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 8;
        ProjectLayerRecord record = group(Uuid::generate());
        record.opacity = 0.5;
        manifest.layers.push_back(record);
        checkOk(manifest, "folder opacity at version 8 passes");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = group(Uuid::generate());
        record.blendMode = BlendMode::Screen;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "folder with non-normal blend");
    }
    {
        ProjectManifest manifest = base();
        ProjectLayerRecord record = layer(Uuid::generate());
        record.opacity = 1.5;
        manifest.layers.push_back(record);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "opacity above 1");
    }
}

void testHierarchyVersionGates() {
    {
        ProjectManifest manifest = base();
        manifest.version = 1;
        manifest.layers.push_back(group(Uuid::generate()));
        checkInvalid([&] { compositor::validateManifest(manifest); }, "folder at version 1");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 1;
        ProjectLayerRecord parent = layer(Uuid::generate());
        ProjectLayerRecord child = layer(Uuid::generate());
        child.parentID = parent.id;
        manifest.layers.push_back(parent);
        manifest.layers.push_back(child);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "parentID at version 1");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 4;
        ProjectLayerRecord target = layer(Uuid::generate());
        ProjectLayerRecord source = layer(Uuid::generate());
        target.maskSourceID = source.id;
        manifest.layers.push_back(target);
        manifest.layers.push_back(source);
        checkInvalid([&] { compositor::validateManifest(manifest); }, "maskSourceID chain at version 4");
    }
}

void testGuideGates() {
    {
        ProjectManifest manifest = base();
        manifest.version = 7;
        CanvasGuide guide;
        guide.id = Uuid::generate();
        manifest.guides = std::vector<CanvasGuide>{guide};
        checkInvalid([&] { compositor::validateManifest(manifest); }, "guides at version 7");
    }
    {
        ProjectManifest manifest = base();
        manifest.version = 8;
        CanvasGuide guide;
        guide.id = Uuid::generate();
        guide.position = 500;
        manifest.guides = std::vector<CanvasGuide>{guide};
        checkOk(manifest, "guide at version 8 passes");
    }
    {
        ProjectManifest manifest = base();
        std::vector<CanvasGuide> guides;
        for (int i = 0; i < 1001; ++i) {
            CanvasGuide guide;
            guide.id = Uuid::generate();
            guides.push_back(guide);
        }
        manifest.guides = guides;
        check(throwsKind([&] { compositor::validateManifest(manifest); }, ProjectError::Kind::TooLarge),
              "1001 guides is tooLarge");
    }
    {
        ProjectManifest manifest = base();
        std::vector<CanvasGuide> guides;
        for (int i = 0; i < 1000; ++i) {
            CanvasGuide guide;
            guide.id = Uuid::generate();
            guides.push_back(guide);
        }
        manifest.guides = guides;
        checkOk(manifest, "1000 guides pass");
    }
    {
        ProjectManifest manifest = base();
        CanvasGuide guide;
        guide.id = Uuid::generate();
        CanvasGuide duplicate = guide;
        duplicate.position = 10;
        manifest.guides = std::vector<CanvasGuide>{guide, duplicate};
        checkInvalid([&] { compositor::validateManifest(manifest); }, "duplicate guide ids");
    }
    {
        ProjectManifest manifest = base();
        CanvasGuide guide;
        guide.id = Uuid::generate();
        guide.position = 1'000'001;
        manifest.guides = std::vector<CanvasGuide>{guide};
        checkInvalid([&] { compositor::validateManifest(manifest); }, "guide position out of range");
    }
    {
        ProjectManifest manifest = base();
        manifest.guides = std::vector<CanvasGuide>{};
        checkOk(manifest, "empty guides pass");
    }
}

}  // namespace

int main() {
    testDocumentGates();
    testLayerIdentityGates();
    testMaskVersionGates();
    testAdjustmentGates();
    testTextGates();
    testStyleVersionGates();
    testHierarchyVersionGates();
    testGuideGates();
    if (failures == 0) std::printf("test_validation: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
