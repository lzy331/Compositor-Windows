#include "ui/DocumentHistory.h"
#include "ui/EditorSession.h"

#include "render/DocumentRenderer.h"

#include <QColor>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <string>

using namespace compositor;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    if (!condition) {
        std::cout << "FAIL " << what << "\n";
        ++failures;
    }
}

// The flattened document, the pixels the canvas and an export both show.
QColor renderedPixel(const EditorSession& session, int x, int y) {
    const render::Raster raster = render::DocumentRenderer::render(*session.document());
    if (x < 0 || y < 0 || x >= raster.width || y >= raster.height) return QColor();
    const uint8_t* p = raster.row(y) + x * 4;
    if (p[3] == 0) return QColor(0, 0, 0, 0);
    const int a = p[3];
    return QColor(std::min(255, p[0] * 255 / a), std::min(255, p[1] * 255 / a), std::min(255, p[2] * 255 / a), a);
}

ProjectSnapshot snapshotWithLayer(const std::string& name) {
    ProjectSnapshot snapshot;
    snapshot.manifest.documentID = Uuid::generate();
    snapshot.manifest.width = 4;
    snapshot.manifest.height = 4;
    ProjectLayerRecord layer;
    layer.id = Uuid::generate();
    layer.name = name;
    layer.transform.size = Size(4, 4);
    snapshot.manifest.layers.push_back(layer);
    return snapshot;
}

void testHistoryRecordsAndUndoes() {
    DocumentHistory history;
    ProjectSnapshot a = snapshotWithLayer("Base");
    history.begin("Paint", a, a.manifest.layers.front().id);
    ProjectSnapshot b = a;
    b.manifest.layers.front().name = "Painted";
    history.end(b, b.manifest.layers.front().id);
    check(history.canUndo(), "an edit becomes undoable");
    check(history.undoCount() == 1, "one entry after one edit");
    check(history.undoName() == "Paint", "the entry keeps the edit name");
    const auto undone = history.undo();
    check(undone.has_value() && undone->document.has_value(), "undo returns the earlier document");
    check(undone->document->manifest.layers.front().name == "Base", "undo restores the earlier name");
    check(history.canRedo(), "undo makes redo available");
    const auto redone = history.redo();
    check(redone.has_value() && redone->document->manifest.layers.front().name == "Painted",
          "redo restores the later state");
}

void testHistoryIgnoresNoOpEdits() {
    DocumentHistory history;
    ProjectSnapshot a = snapshotWithLayer("Base");
    history.begin("Select", a, a.manifest.layers.front().id);
    history.end(a, a.manifest.layers.front().id);
    check(!history.canUndo(), "a no-op edit records nothing and keeps redo valid");
}

void testHistoryEntryLimit() {
    DocumentHistory history(2, 1ll << 40);
    ProjectSnapshot document = snapshotWithLayer("Layer 1");
    for (int step = 0; step < 5; ++step) {
        history.begin("Rename " + std::to_string(step), document, document.manifest.layers.front().id);
        document.manifest.layers.front().name = "Layer " + std::to_string(step + 2);
        history.end(document, document.manifest.layers.front().id);
    }
    check(history.undoCount() == 2, "the entry limit trims the oldest entries");
}

void testHistorySavedMarker() {
    DocumentHistory history;
    check(!history.isModified(), "a fresh history is unmodified");
    ProjectSnapshot a = snapshotWithLayer("Base");
    history.begin("Paint", a, a.manifest.layers.front().id);
    ProjectSnapshot b = a;
    b.manifest.layers.front().name = "Painted";
    history.end(b, b.manifest.layers.front().id);
    check(history.isModified(), "an edit marks the document modified");
    history.markSaved();
    check(!history.isModified(), "saving clears the modified flag");
}

void testSessionLayerOps() {
    EditorSession session;
    check(!session.document().has_value(), "a new session has no document");
    session.createNewProject(64, 48);
    check(session.document().has_value(), "a new project has a document");
    check(session.document()->manifest.layers.size() == 1, "a new canvas starts with one layer");
    check(session.document()->manifest.layers.front().name == "Layer 1", "the first layer is named Layer 1");
    check(session.activeLayerID() && *session.activeLayerID() == session.document()->manifest.layers.front().id,
          "the first layer is selected");
    session.addBlankLayer();
    check(session.document()->manifest.layers.size() == 2, "adding a blank layer grows the list");
    session.renameLayer(*session.activeLayerID(), "Paint");
    check(session.activeLayer()->name == "Paint", "renaming updates the layer");
    check(session.canUndo(), "layer ops are undoable");
    session.undo();
    check(session.activeLayer()->name != "Paint", "undo reverses the rename");
    session.undo();
    check(session.document()->manifest.layers.size() == 1, "a second undo removes the added layer");
}

void testSessionDeleteFolderRemovesContents() {
    EditorSession session;
    session.createNewProject(32, 32);
    session.addGroup();
    const Uuid folder = *session.activeLayerID();
    session.addBlankLayer();
    check(session.activeLayer()->parentID && *session.activeLayer()->parentID == folder,
          "a layer added with a folder active becomes its child");
    check(session.document()->manifest.layers.size() == 3, "the folder holds the child");
    session.deleteActiveLayer();
    check(session.document()->manifest.layers.size() == 2, "deleting the child removes only the child");
    session.selectLayer(folder);
    session.deleteActiveLayer();
    check(session.document()->manifest.layers.size() == 1, "deleting the folder removes its contents too");
}

void testSessionMoveLayer() {
    EditorSession session;
    session.createNewProject(16, 16);
    session.addBlankLayer();
    session.addBlankLayer();
    const auto& layers = session.document()->manifest.layers;
    const Uuid middle = layers[1].id;
    session.selectLayer(middle);
    check(session.canMoveActiveLayer(1), "the middle layer can move up");
    check(!session.canMoveActiveLayer(2), "moving two past the top is refused");
    session.moveActiveLayer(1);
    check(session.document()->manifest.layers[2].id == middle, "up swaps with the sibling above");
}

void testSessionDuplicateSharesPixels() {
    EditorSession session;
    session.createNewProject(8, 8);
    QImage image(8, 8, QImage::Format_RGBA8888);
    image.fill(QColor(255, 0, 0));
    session.insertAsset(ImportedImage{image, thumbnailFor(image), "Red"});
    const Uuid original = *session.activeLayerID();
    const qint64 key = session.imageFor(original)->image.cacheKey();
    session.duplicateActiveLayer();
    const Uuid copy = *session.activeLayerID();
    check(copy != original, "the copy has a new id");
    check(session.activeLayer()->name == "Red copy", "the copy is named as the original plus copy");
    check(session.imageFor(copy)->image.cacheKey() == key, "the copy shares the original's pixels");
    check(session.document()->images.size() == 2, "each layer owns an image entry");
}

void testSessionRenameTrims() {
    EditorSession session;
    session.createNewProject(16, 16);
    const Uuid id = *session.activeLayerID();
    session.renameLayer(id, "  Spaced  ");
    check(session.layer(id)->name == "Spaced", "names are trimmed");
    session.renameLayer(id, "   ");
    check(session.layer(id)->name == "Spaced", "an empty name is rejected");
}

void testSessionBackgroundLayer() {
    EditorSession session;
    session.createNewProject(4, 3, 72, QColor(255, 255, 255));
    const ProjectLayerRecord& layer = session.document()->manifest.layers.front();
    check(layer.name == "Background", "a filled canvas names its layer Background");
    const ImportedImage* asset = session.imageFor(layer.id);
    check(asset != nullptr, "the background layer holds pixels");
    if (asset) {
        check(asset->image.pixelColor(0, 0) == QColor(255, 255, 255, 255), "the background is filled white");
        check(asset->thumbnail.width() == 4 && asset->thumbnail.height() == 3, "a small thumbnail stays its size");
    }
}

void testSessionInsertCentersAsset() {
    EditorSession session;
    session.createNewProject(64, 48);
    QImage image(4, 2, QImage::Format_RGBA8888);
    image.fill(QColor(0, 255, 0));
    const Point anchor(10, 10);
    session.insertAsset(ImportedImage{image, thumbnailFor(image), "Green"}, anchor);
    const ProjectLayerRecord* layer = session.activeLayer();
    check(layer != nullptr, "the inserted asset is selected");
    if (layer) {
        check(layer->transform.origin.x == 8 && layer->transform.origin.y == 9,
              "the asset centers on the point with the size floored");
        check(layer->transform.size.width == 4 && layer->transform.size.height == 2, "the layer takes the image size");
    }
}

void testSessionFitUsesInsets() {
    EditorSession session;
    session.resizeViewport(Size(800, 640), 1);
    session.createNewProject(400, 200);
    const double expected = std::min(704.0 / 400.0, 544.0 / 200.0);
    check(std::abs(session.viewport().zoom() - expected) < 1e-9, "fit leaves a 96-point inset");
    check(session.viewport().followsFit(), "fit follows the window");
}

void testSessionZoomAnchors() {
    EditorSession session;
    session.resizeViewport(Size(600, 400), 1);
    session.createNewProject(200, 200);
    const Point anchor(120, 90);
    const Point before = session.viewport().documentPoint(anchor, Size(200, 200));
    session.zoomTo(2, anchor);
    const Point after = session.viewport().documentPoint(anchor, Size(200, 200));
    check(std::abs(before.x - after.x) < 1e-9 && std::abs(before.y - after.y) < 1e-9,
          "zooming keeps the anchored document point still");
    check(!session.viewport().followsFit(), "zooming stops following the window");
}

void testSessionSaveLoadRoundTrip() {
    QTemporaryDir directory;
    check(directory.isValid(), "a temporary directory is available");
    if (!directory.isValid()) return;
    const std::string path = (QDir(directory.path()).filePath(QStringLiteral("Round.comp"))).toStdString();

    EditorSession session;
    session.createNewProject(12, 9);
    QImage image(6, 6, QImage::Format_RGBA8888);
    image.fill(QColor(12, 34, 56));
    session.insertAsset(ImportedImage{image, thumbnailFor(image), "Swatch"});
    session.renameLayer(*session.activeLayerID(), "Swatch Renamed");
    check(session.isModified(), "edits mark the session modified");
    check(session.saveProjectAs(path), "the project saves");
    check(!session.isModified(), "saving clears the modified flag");

    EditorSession reopened;
    check(reopened.loadProject(path), "the saved project loads");
    check(reopened.document().has_value() && reopened.document()->manifest.layers.size() == 2,
          "the reloaded document keeps both layers");
    check(reopened.activeLayer() != nullptr && reopened.activeLayer()->name == "Swatch Renamed",
          "the reloaded document keeps the selected layer and its name");
    const ImportedImage* asset = reopened.imageFor(*reopened.activeLayerID());
    check(asset != nullptr, "the reloaded layer keeps its pixels");
    if (asset) check(asset->image.pixelColor(0, 0) == QColor(12, 34, 56, 255), "the pixels survive the round trip");
    check(!reopened.isModified(), "a freshly opened project is unmodified");
}

void testBrushPaintsOnBlankLayer() {
    EditorSession session;
    session.createNewProject(64, 64);
    session.setTool(EditorSession::Tool::Brush);
    EditorSession::BrushSettings& brush = session.brush();
    brush.diameter = 16;
    brush.hardness = 1;
    brush.red = 1;
    brush.green = 0;
    brush.blue = 0;
    brush.opacity = 1;
    check(session.canPaint(), "a blank layer accepts paint");
    session.beginStroke(Point(16, 32));
    session.continueStroke(Point(48, 32));
    session.endStroke();
    const ProjectLayerRecord* layer = session.activeLayer();
    check(layer != nullptr && layer->imageFile.has_value(), "painting gives a blank layer pixels");
    check(renderedPixel(session, 32, 32) == QColor(255, 0, 0, 255), "the stroke center is the brush color");
    check(renderedPixel(session, 32, 2).alpha() == 0, "well clear of the stroke stays transparent");
    check(session.canUndo() && session.undoName() == "Brush Stroke", "a stroke is one undo step");
    session.undo();
    check(session.imageFor(*session.activeLayerID()) == nullptr, "undo removes the painted pixels");
}

void testBrushOpacityCapsStroke() {
    EditorSession session;
    session.createNewProject(64, 64);
    session.setTool(EditorSession::Tool::Brush);
    EditorSession::BrushSettings& brush = session.brush();
    brush.diameter = 20;
    brush.hardness = 1;
    brush.red = 0;
    brush.green = 0;
    brush.blue = 1;
    brush.opacity = 0.5;
    // Two passes over the same spot must not exceed the stroke-wide opacity cap.
    session.beginStroke(Point(32, 32));
    session.continueStroke(Point(32, 32));
    session.continueStroke(Point(32, 32.0));
    session.endStroke();
    const int alpha = renderedPixel(session, 32, 32).alpha();
    check(alpha > 100 && alpha < 150, "overlapping dabs stay within the 50% opacity cap");
}

void testEraserClearsPixels() {
    EditorSession session;
    session.createNewProject(32, 32, 72, QColor(255, 255, 255));
    const Uuid background = *session.activeLayerID();
    check(session.imageFor(background) != nullptr, "the background starts opaque");
    session.setTool(EditorSession::Tool::Brush);
    EditorSession::BrushSettings& brush = session.brush();
    brush.diameter = 12;
    brush.hardness = 1;
    brush.erasing = true;
    session.beginStroke(Point(16, 16));
    session.continueStroke(Point(16, 16));
    session.endStroke();
    check(renderedPixel(session, 16, 16).alpha() == 0, "erasing clears the covered pixels");
    check(renderedPixel(session, 0, 0).alpha() == 255, "untouched pixels stay opaque");
    check(session.undoName() == "Erase", "an erasing stroke is named Erase");
}

void testBrushRefusesFolder() {
    EditorSession session;
    session.createNewProject(32, 32);
    session.addGroup();
    session.setTool(EditorSession::Tool::Brush);
    check(!session.canPaint(), "a folder has no pixels to paint on");
    check(session.paintRefusal().has_value(), "painting a folder explains why it can't");
    session.beginStroke(Point(16, 16));
    check(!session.isStroking(), "a refused stroke never starts");
}

}  // namespace

int main() {
    testHistoryRecordsAndUndoes();
    testHistoryIgnoresNoOpEdits();
    testHistoryEntryLimit();
    testHistorySavedMarker();
    testSessionLayerOps();
    testSessionDeleteFolderRemovesContents();
    testSessionMoveLayer();
    testSessionDuplicateSharesPixels();
    testSessionRenameTrims();
    testSessionBackgroundLayer();
    testSessionInsertCentersAsset();
    testSessionFitUsesInsets();
    testSessionZoomAnchors();
    testSessionSaveLoadRoundTrip();
    testBrushPaintsOnBlankLayer();
    testBrushOpacityCapsStroke();
    testEraserClearsPixels();
    testBrushRefusesFolder();
    if (failures == 0) std::cout << "All ui tests passed\n";
    else std::cout << failures << " ui test(s) failed\n";
    return failures == 0 ? 0 : 1;
}
