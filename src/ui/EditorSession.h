#pragma once

#include <QColor>
#include <QObject>
#include <QString>

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "io/ProjectStore.h"
#include "ui/CanvasViewport.h"
#include "ui/DocumentHistory.h"

namespace compositor {

class BrushStroke;

/// The document session behind the window: the M0 slice of the original EditorSession.
/// Pixels stay in the snapshot maps, keyed by layer id, which is what ProjectStore saves.
class EditorSession : public QObject {
    Q_OBJECT
public:
    enum class Tool {
        Idle,
        Move,
        Marquee,
        Lasso,
        Wand,
        Crop,
        Brush,
        SpotHealing,
        CloneStamp,
        Blur,
        Gradient,
        Shape,
        Type,
        Eyedropper,
        Hand,
        Zoom
    };

    struct BrushSettings {
        double diameter = 40;
        double hardness = 1;
        double red = 0;
        double green = 0;
        double blue = 0;
        double opacity = 1;
        bool erasing = false;
    };

    explicit EditorSession(QObject* parent = nullptr);
    ~EditorSession() override;

    const std::optional<ProjectSnapshot>& document() const { return document_; }
    const std::optional<Uuid>& activeLayerID() const { return activeLayerID_; }
    const std::set<Uuid>& selectedLayerIDs() const { return selectedLayerIDs_; }
    const std::set<Uuid>& collapsedGroupIDs() const { return collapsedGroupIDs_; }
    const ProjectLayerRecord* layer(const Uuid& id) const;
    const ProjectLayerRecord* activeLayer() const;
    const ImportedImage* imageFor(const Uuid& id) const;
    const ImportedImage* maskFor(const Uuid& id) const;

    Tool tool() const { return tool_; }
    void setTool(Tool tool);
    BrushSettings& brush() { return brush_; }
    const BrushSettings& brush() const { return brush_; }

    CanvasViewport& viewport() { return viewport_; }
    const CanvasViewport& viewport() const { return viewport_; }
    void resizeViewport(const Size& size, double backingScale);

    bool canEditLayers() const;
    bool canUndo() const;
    bool canRedo() const;
    std::string undoName() const;
    std::string redoName() const;
    bool isModified() const;
    const std::string& projectPath() const { return projectPath_; }

    void beginEdit(const std::string& name);
    void endEdit();

    void selectLayer(const std::optional<Uuid>& id);
    void setSelection(std::set<Uuid> ids, const std::optional<Uuid>& active);
    void toggleGroupExpansion(const Uuid& id);

    void createNewProject(int width, int height, double resolution = 72,
                          const std::optional<QColor>& background = std::nullopt);
    bool loadProject(const std::string& path);
    bool saveProject();
    bool saveProjectAs(const std::string& path);

    void addBlankLayer();
    void addGroup();
    void deleteActiveLayer();
    void deleteSelectedLayers();
    void renameLayer(const Uuid& id, const std::string& name);
    void toggleLayerVisibility(const Uuid& id);
    bool canMoveActiveLayer(int offset) const;
    void moveActiveLayer(int offset);
    void duplicateActiveLayer();
    void setLayerOpacity(const Uuid& id, double opacity);
    void setLayerBlendMode(const Uuid& id, BlendMode mode);
    void setLayerOrigin(const Uuid& id, const Point& origin);

    void insertAsset(ImportedImage asset, const std::optional<Point>& centeredAt = std::nullopt);
    bool importImageFiles(const std::vector<std::string>& paths, const std::optional<Point>& centeredAt = std::nullopt);

    void fit();
    void zoomTo(double value, const std::optional<Point>& anchor = std::nullopt);
    void zoomKeyboard(int step);
    void translateView(const Size& delta);

    /// Whether a brush stroke can start on the active layer, and why not if it can't.
    bool canPaint() const;
    std::optional<QString> paintRefusal() const;

    void beginStroke(const Point& documentPoint);
    void continueStroke(const Point& documentPoint);
    void endStroke();
    bool isStroking() const { return stroke_ != nullptr; }
    /// The layer's pixels as the in-progress stroke has them, for the canvas's live preview.
    const QImage* strokePreview() const;
    std::optional<LayerTransform> strokePreviewTransform() const;

    void undo();
    void redo();

signals:
    void documentChanged();
    void canvasInvalidated();
    void selectionChanged();
    void viewportChanged();
    void errorOccurred(const QString& title, const QString& message);

private:
    void clearProject();
    void createDocument(int width, int height, bool emptyLayer, double resolution,
                        const std::optional<QColor>& background);
    void setActive(const std::optional<Uuid>& id);
    void restore(const DocumentHistory::Snapshot& snapshot);
    ProjectLayerRecord* mutableLayer(const Uuid& id);
    int indexOf(const Uuid& id) const;
    std::set<Uuid> descendantIDs(const Uuid& id) const;
    std::vector<const ProjectLayerRecord*> siblingsOf(const ProjectLayerRecord& layer) const;
    void finishDeletingLayer(const Uuid& id);
    bool placeAsset(ImportedImage asset, const std::optional<Point>& centeredAt);
    std::optional<Uuid> insertCopy(const Uuid& id);
    int64_t usedPixels() const;
    Size documentSize() const;
    void commitStroke();

    std::optional<ProjectSnapshot> document_;
    std::optional<Uuid> activeLayerID_;
    std::set<Uuid> selectedLayerIDs_;
    std::set<Uuid> collapsedGroupIDs_;
    bool isMaskSelected_ = false;
    bool isProjectBusy_ = false;
    std::string projectPath_;
    DocumentHistory history_;
    CanvasViewport viewport_;
    Tool tool_ = Tool::Move;
    BrushSettings brush_;
    bool createdCanvas_ = false;
    std::unique_ptr<BrushStroke> stroke_;
    Uuid strokeLayerID_;
    QImage strokePreview_;
    std::optional<LayerTransform> strokePreviewTransform_;
};

}  // namespace compositor
