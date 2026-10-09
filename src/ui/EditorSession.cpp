#include "ui/EditorSession.h"

#include "document/Hierarchy.h"
#include "io/ImageAssets.h"
#include "io/ProjectError.h"
#include "ui/BrushStroke.h"

#include <QFileInfo>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace compositor {
namespace {

constexpr int kLayerLimit = 10'000;

std::string numberedName(const std::vector<ProjectLayerRecord>& layers, const std::string& prefix) {
    std::set<std::string> names;
    for (const ProjectLayerRecord& layer : layers) names.insert(layer.name);
    int number = 1;
    while (names.count(prefix + std::to_string(number))) ++number;
    return prefix + std::to_string(number);
}

std::string trimmed(const std::string& text) {
    return QString::fromStdString(text).trimmed().toStdString();
}

}  // namespace

EditorSession::EditorSession(QObject* parent) : QObject(parent) {}

EditorSession::~EditorSession() = default;

const ProjectLayerRecord* EditorSession::layer(const Uuid& id) const {
    if (!document_) return nullptr;
    for (const ProjectLayerRecord& record : document_->manifest.layers) {
        if (record.id == id) return &record;
    }
    return nullptr;
}

const ProjectLayerRecord* EditorSession::activeLayer() const {
    return activeLayerID_ ? layer(*activeLayerID_) : nullptr;
}

const ImportedImage* EditorSession::imageFor(const Uuid& id) const {
    if (!document_) return nullptr;
    const auto found = document_->images.find(id);
    return found == document_->images.end() ? nullptr : &found->second;
}

const ImportedImage* EditorSession::maskFor(const Uuid& id) const {
    if (!document_) return nullptr;
    const auto found = document_->masks.find(id);
    return found == document_->masks.end() ? nullptr : &found->second;
}

void EditorSession::setTool(Tool tool) {
    if (tool_ == tool) return;
    tool_ = tool;
    emit canvasInvalidated();
}

void EditorSession::resizeViewport(const Size& size, double backingScale) {
    std::optional<Size> documentSize;
    if (document_) documentSize = this->documentSize();
    viewport_.resize(size, backingScale, documentSize);
    emit viewportChanged();
}

bool EditorSession::canEditLayers() const { return document_.has_value() && !isProjectBusy_; }
bool EditorSession::canUndo() const { return !isProjectBusy_ && history_.canUndo(); }
bool EditorSession::canRedo() const { return !isProjectBusy_ && history_.canRedo(); }
std::string EditorSession::undoName() const { return history_.undoName(); }
std::string EditorSession::redoName() const { return history_.redoName(); }
bool EditorSession::isModified() const { return history_.isModified(); }

void EditorSession::beginEdit(const std::string& name) { history_.begin(name, document_, activeLayerID_); }

void EditorSession::endEdit() {
    history_.end(document_, activeLayerID_);
    if (!history_.isEditing()) emit documentChanged();
}

void EditorSession::setActive(const std::optional<Uuid>& id) {
    if (activeLayerID_ != id) isMaskSelected_ = false;
    activeLayerID_ = id;
    selectedLayerIDs_.clear();
    if (id) selectedLayerIDs_.insert(*id);
    if (document_) document_->manifest.activeLayerID = activeLayerID_;
}

void EditorSession::selectLayer(const std::optional<Uuid>& id) {
    setActive(id);
    emit selectionChanged();
    emit canvasInvalidated();
}

void EditorSession::setSelection(std::set<Uuid> ids, const std::optional<Uuid>& active) {
    if (active) ids.insert(*active);
    if (activeLayerID_ != active) isMaskSelected_ = false;
    selectedLayerIDs_ = std::move(ids);
    activeLayerID_ = active;
    if (document_) document_->manifest.activeLayerID = activeLayerID_;
    emit selectionChanged();
    emit canvasInvalidated();
}

void EditorSession::toggleGroupExpansion(const Uuid& id) {
    if (isProjectBusy_ || !document_) return;
    const ProjectLayerRecord* record = layer(id);
    if (!record || !record->isGroupRecord()) return;
    if (collapsedGroupIDs_.count(id)) {
        collapsedGroupIDs_.erase(id);
    } else {
        if (activeLayerID_ && descendantIDs(id).count(*activeLayerID_)) setActive(id);
        collapsedGroupIDs_.insert(id);
    }
    emit documentChanged();
    emit selectionChanged();
}

void EditorSession::clearProject() {
    document_.reset();
    activeLayerID_.reset();
    selectedLayerIDs_.clear();
    collapsedGroupIDs_.clear();
    isMaskSelected_ = false;
    projectPath_.clear();
    history_.reset();
}

void EditorSession::createNewProject(int width, int height, double resolution,
                                     const std::optional<QColor>& background) {
    if (isProjectBusy_ || width < 1 || width > DocumentLimits::maxSide || height < 1
        || height > DocumentLimits::maxSide) {
        return;
    }
    clearProject();
    createDocument(width, height, true, resolution, background);
}

void EditorSession::createDocument(int width, int height, bool emptyLayer, double resolution,
                                   const std::optional<QColor>& background) {
    if (isProjectBusy_ || width < 1 || width > DocumentLimits::maxSide || height < 1
        || height > DocumentLimits::maxSide) {
        return;
    }
    beginEdit("New Canvas");
    ProjectSnapshot snapshot;
    snapshot.manifest.documentID = Uuid::generate();
    snapshot.manifest.width = width;
    snapshot.manifest.height = height;
    snapshot.manifest.resolution = (resolution >= 1 && resolution <= 9600) ? resolution : 72;
    if (emptyLayer) {
        ProjectLayerRecord layer;
        layer.id = Uuid::generate();
        layer.transform.size = Size(width, height);
        if (background) {
            QImage image(width, height, QImage::Format_RGBA8888);
            image.fill(*background);
            layer.name = "Background";
            layer.imageFile = layer.id.toString() + ".png";
            snapshot.images.emplace(layer.id, ImportedImage{image, thumbnailFor(image), layer.name});
        } else {
            layer.name = "Layer 1";
        }
        snapshot.manifest.layers.push_back(layer);
        snapshot.manifest.activeLayerID = layer.id;
    }
    document_ = std::move(snapshot);
    activeLayerID_ = document_->manifest.activeLayerID;
    selectedLayerIDs_.clear();
    if (activeLayerID_) selectedLayerIDs_.insert(*activeLayerID_);
    collapsedGroupIDs_.clear();
    viewport_.fit(documentSize());
    endEdit();
    emit selectionChanged();
    emit viewportChanged();
}

bool EditorSession::loadProject(const std::string& path) {
    try {
        ProjectSnapshot snapshot = ProjectStore::load(path);
        document_ = std::move(snapshot);
        activeLayerID_ = document_->manifest.activeLayerID;
        if (activeLayerID_ && !layer(*activeLayerID_)) activeLayerID_.reset();
        selectedLayerIDs_.clear();
        if (activeLayerID_) selectedLayerIDs_.insert(*activeLayerID_);
        collapsedGroupIDs_.clear();
        isMaskSelected_ = false;
        projectPath_ = path;
        history_.reset();
        viewport_.fit(documentSize());
        emit documentChanged();
        emit selectionChanged();
        emit viewportChanged();
        return true;
    } catch (const std::exception& error) {
        emit errorOccurred(QStringLiteral("Open Project"), QString::fromStdString(error.what()));
        return false;
    }
}

bool EditorSession::saveProject() {
    if (projectPath_.empty()) return false;
    return saveProjectAs(projectPath_);
}

bool EditorSession::saveProjectAs(const std::string& path) {
    if (!document_ || path.empty()) return false;
    document_->manifest.activeLayerID = activeLayerID_;
    try {
        ProjectStore::save(*document_, path);
    } catch (const std::exception& error) {
        emit errorOccurred(QStringLiteral("Save Project"), QString::fromStdString(error.what()));
        return false;
    }
    projectPath_ = path;
    history_.markSaved();
    emit documentChanged();
    return true;
}

ProjectLayerRecord* EditorSession::mutableLayer(const Uuid& id) {
    if (!document_) return nullptr;
    for (ProjectLayerRecord& record : document_->manifest.layers) {
        if (record.id == id) return &record;
    }
    return nullptr;
}

int EditorSession::indexOf(const Uuid& id) const {
    if (!document_) return -1;
    const auto& layers = document_->manifest.layers;
    for (int index = 0; index < static_cast<int>(layers.size()); ++index) {
        if (layers[index].id == id) return index;
    }
    return -1;
}

std::set<Uuid> EditorSession::descendantIDs(const Uuid& id) const {
    std::map<Uuid, std::vector<Uuid>> children;
    if (document_) {
        for (const ProjectLayerRecord& record : document_->manifest.layers) {
            if (record.parentID) children[*record.parentID].push_back(record.id);
        }
    }
    std::set<Uuid> result;
    std::vector<Uuid> pending = children[id];
    while (!pending.empty()) {
        const Uuid current = pending.back();
        pending.pop_back();
        if (!result.insert(current).second) continue;
        const auto found = children.find(current);
        if (found != children.end()) pending.insert(pending.end(), found->second.begin(), found->second.end());
    }
    return result;
}

std::vector<const ProjectLayerRecord*> EditorSession::siblingsOf(const ProjectLayerRecord& layer) const {
    std::vector<const ProjectLayerRecord*> siblings;
    if (!document_) return siblings;
    for (const ProjectLayerRecord& record : document_->manifest.layers) {
        if (record.parentID == layer.parentID) siblings.push_back(&record);
    }
    return siblings;
}

Size EditorSession::documentSize() const {
    if (!document_) return {};
    return Size(static_cast<double>(document_->manifest.width), static_cast<double>(document_->manifest.height));
}

int64_t EditorSession::usedPixels() const {
    int64_t used = 0;
    if (!document_) return used;
    for (const auto& [id, asset] : document_->images) {
        (void)id;
        used += static_cast<int64_t>(asset.image.width()) * asset.image.height();
    }
    return used;
}

void EditorSession::addBlankLayer() {
    if (!canEditLayers() || !document_ || static_cast<int>(document_->manifest.layers.size()) >= kLayerLimit) return;
    auto& layers = document_->manifest.layers;
    ProjectLayerRecord created;
    created.id = Uuid::generate();
    created.name = numberedName(layers, "Layer ");
    created.transform.size = documentSize();
    const ProjectLayerRecord* active = activeLayer();
    if (active && active->isGroupRecord()) created.parentID = active->id;
    else if (active) created.parentID = active->parentID;
    if (created.parentID) collapsedGroupIDs_.erase(*created.parentID);

    int insertion = static_cast<int>(layers.size());
    if (activeLayerID_) {
        const int index = indexOf(*activeLayerID_);
        if (index >= 0) insertion = index + 1;
    }
    if (active && active->isGroupRecord()) {
        const std::set<Uuid> inside = descendantIDs(active->id);
        int topmost = -1;
        for (int index = 0; index < static_cast<int>(layers.size()); ++index) {
            if (inside.count(layers[index].id)) topmost = index;
        }
        if (topmost >= 0) insertion = std::max(insertion, topmost + 1);
    }

    beginEdit("New Blank Layer");
    layers.insert(layers.begin() + insertion, created);
    setActive(created.id);
    endEdit();
    emit selectionChanged();
}

void EditorSession::addGroup() {
    if (!canEditLayers() || !document_ || static_cast<int>(document_->manifest.layers.size()) >= kLayerLimit) return;
    auto& layers = document_->manifest.layers;
    ProjectLayerRecord group;
    group.id = Uuid::generate();
    group.name = numberedName(layers, "Folder ");
    group.isGroup = true;
    group.transform.size = documentSize();
    const ProjectLayerRecord* active = activeLayer();
    if (active && active->isGroupRecord()) group.parentID = active->id;
    else if (active) group.parentID = active->parentID;
    int insertion = static_cast<int>(layers.size());
    if (activeLayerID_) {
        const int index = indexOf(*activeLayerID_);
        if (index >= 0) insertion = index + 1;
    }
    std::vector<ProjectLayerRecord> trial = layers;
    trial.insert(trial.begin() + insertion, group);
    try {
        LayerHierarchy::validate(trial);
    } catch (const ProjectError&) {
        return;
    }
    beginEdit("New Folder");
    layers = std::move(trial);
    setActive(group.id);
    if (group.parentID) collapsedGroupIDs_.erase(*group.parentID);
    endEdit();
    emit selectionChanged();
}

void EditorSession::finishDeletingLayer(const Uuid& id) {
    const int index = indexOf(id);
    if (index < 0 || !document_) return;
    const std::set<Uuid> removed = [&] {
        std::set<Uuid> ids = descendantIDs(id);
        ids.insert(id);
        return ids;
    }();
    beginEdit("Delete Layer");
    auto& layers = document_->manifest.layers;
    layers.erase(std::remove_if(layers.begin(), layers.end(),
                                [&](const ProjectLayerRecord& record) { return removed.count(record.id) != 0; }),
                 layers.end());
    for (ProjectLayerRecord& record : layers) {
        if (record.maskSourceID && removed.count(*record.maskSourceID)) record.maskSourceID.reset();
    }
    for (const Uuid& gone : removed) {
        document_->images.erase(gone);
        document_->masks.erase(gone);
        collapsedGroupIDs_.erase(gone);
        selectedLayerIDs_.erase(gone);
    }
    if (activeLayerID_ && removed.count(*activeLayerID_)) {
        if (layers.empty()) setActive(std::nullopt);
        else setActive(layers[std::min(index, static_cast<int>(layers.size()) - 1)].id);
    }
    endEdit();
}

void EditorSession::deleteActiveLayer() {
    if (!canEditLayers() || !activeLayerID_) return;
    finishDeletingLayer(*activeLayerID_);
    emit selectionChanged();
}

void EditorSession::deleteSelectedLayers() {
    if (!canEditLayers() || !document_) return;
    std::vector<Uuid> ids;
    for (const ProjectLayerRecord& record : document_->manifest.layers) {
        if (selectedLayerIDs_.count(record.id)) ids.push_back(record.id);
    }
    if (ids.size() <= 1) {
        deleteActiveLayer();
        return;
    }
    beginEdit("Delete Layers");
    for (const Uuid& id : ids) finishDeletingLayer(id);
    endEdit();
    emit selectionChanged();
}

void EditorSession::renameLayer(const Uuid& id, const std::string& name) {
    const std::string next = trimmed(name);
    if (isProjectBusy_ || next.empty()) return;
    ProjectLayerRecord* record = mutableLayer(id);
    if (!record) return;
    beginEdit("Rename Layer");
    record->name = next;
    endEdit();
}

void EditorSession::toggleLayerVisibility(const Uuid& id) {
    ProjectLayerRecord* record = mutableLayer(id);
    if (!canEditLayers() || !record) return;
    beginEdit(record->isVisible ? "Hide Layer" : "Show Layer");
    record->isVisible = !record->isVisible;
    endEdit();
}

bool EditorSession::canMoveActiveLayer(int offset) const {
    const ProjectLayerRecord* active = activeLayer();
    if (!canEditLayers() || !active) return false;
    const std::vector<const ProjectLayerRecord*> siblings = siblingsOf(*active);
    int index = -1;
    for (int i = 0; i < static_cast<int>(siblings.size()); ++i) {
        if (siblings[i]->id == active->id) index = i;
    }
    if (index < 0) return false;
    const int target = index + offset;
    return target >= 0 && target < static_cast<int>(siblings.size());
}

void EditorSession::moveActiveLayer(int offset) {
    if (!canMoveActiveLayer(offset) || !document_ || !activeLayerID_) return;
    const ProjectLayerRecord* active = activeLayer();
    const std::vector<const ProjectLayerRecord*> siblings = siblingsOf(*active);
    int index = -1;
    for (int i = 0; i < static_cast<int>(siblings.size()); ++i) {
        if (siblings[i]->id == active->id) index = i;
    }
    const int from = indexOf(*activeLayerID_);
    const int to = indexOf(siblings[index + offset]->id);
    if (from < 0 || to < 0) return;
    beginEdit("Reorder Layers");
    std::swap(document_->manifest.layers[from], document_->manifest.layers[to]);
    endEdit();
}

std::optional<Uuid> EditorSession::insertCopy(const Uuid& id) {
    const int index = indexOf(id);
    const ProjectLayerRecord* source = layer(id);
    if (!document_ || !source || index < 0) return std::nullopt;
    std::set<Uuid> included = descendantIDs(id);
    included.insert(id);
    std::vector<const ProjectLayerRecord*> originals;
    for (const ProjectLayerRecord& record : document_->manifest.layers) {
        if (included.count(record.id)) originals.push_back(&record);
    }
    if (static_cast<int>(document_->manifest.layers.size() + originals.size()) > kLayerLimit) return std::nullopt;

    std::map<Uuid, Uuid> mapping;
    for (const ProjectLayerRecord* original : originals) mapping[original->id] = Uuid::generate();
    std::vector<ProjectLayerRecord> copies;
    copies.reserve(originals.size());
    for (const ProjectLayerRecord* original : originals) {
        ProjectLayerRecord copy = *original;
        copy.id = mapping[original->id];
        if (original->id == id) copy.name += " copy";
        if (copy.parentID) {
            const auto mapped = mapping.find(*copy.parentID);
            if (mapped != mapping.end()) copy.parentID = mapped->second;
        }
        if (copy.maskSourceID) {
            const auto mapped = mapping.find(*copy.maskSourceID);
            if (mapped != mapping.end()) copy.maskSourceID = mapped->second;
        }
        if (copy.imageFile) {
            const auto asset = document_->images.find(original->id);
            if (asset == document_->images.end()) copy.imageFile.reset();
            else {
                copy.imageFile = copy.id.toString() + ".png";
                document_->images.emplace(copy.id, asset->second);
            }
        }
        if (copy.maskFile) {
            const auto asset = document_->masks.find(original->id);
            if (asset == document_->masks.end()) copy.maskFile.reset();
            else {
                copy.maskFile = copy.id.toString() + ".mask.png";
                document_->masks.emplace(copy.id, asset->second);
            }
        }
        if (collapsedGroupIDs_.count(original->id)) collapsedGroupIDs_.insert(copy.id);
        copies.push_back(std::move(copy));
    }
    auto& layers = document_->manifest.layers;
    layers.insert(layers.begin() + index + 1, copies.begin(), copies.end());
    return mapping[id];
}

void EditorSession::duplicateActiveLayer() {
    if (!canEditLayers() || !activeLayerID_) return;
    beginEdit("Duplicate Layer");
    const std::optional<Uuid> copy = insertCopy(*activeLayerID_);
    if (copy) setActive(*copy);
    endEdit();
    emit selectionChanged();
}

void EditorSession::setLayerOpacity(const Uuid& id, double opacity) {
    ProjectLayerRecord* record = mutableLayer(id);
    if (!record || !std::isfinite(opacity)) return;
    record->opacity = std::clamp(opacity, 0.0, 1.0);
    emit canvasInvalidated();
}

void EditorSession::setLayerBlendMode(const Uuid& id, BlendMode mode) {
    ProjectLayerRecord* record = mutableLayer(id);
    if (!record) return;
    record->blendMode = mode;
    emit canvasInvalidated();
}

void EditorSession::setLayerOrigin(const Uuid& id, const Point& origin) {
    ProjectLayerRecord* record = mutableLayer(id);
    if (!record) return;
    record->transform.origin = origin;
    emit canvasInvalidated();
}

bool EditorSession::placeAsset(ImportedImage asset, const std::optional<Point>& centeredAt) {
    createdCanvas_ = false;
    const int width = asset.image.width();
    const int height = asset.image.height();
    if (width < 1 || height < 1) return false;
    if (!document_) {
        ProjectSnapshot snapshot;
        snapshot.manifest.documentID = Uuid::generate();
        snapshot.manifest.width = width;
        snapshot.manifest.height = height;
        snapshot.manifest.resolution = 72;
        document_ = std::move(snapshot);
        activeLayerID_.reset();
        selectedLayerIDs_.clear();
        viewport_.fit(documentSize());
        createdCanvas_ = true;
    }
    if (static_cast<int>(document_->manifest.layers.size()) >= kLayerLimit) return false;
    const Point center = centeredAt.value_or(
        Point(document_->manifest.width / 2.0, document_->manifest.height / 2.0));
    const ProjectLayerRecord* active = activeLayer();
    ProjectLayerRecord layer;
    layer.id = Uuid::generate();
    layer.name = asset.name.empty() ? "Image" : asset.name;
    if (active && active->isGroupRecord()) layer.parentID = active->id;
    else if (active) layer.parentID = active->parentID;
    if (layer.parentID) collapsedGroupIDs_.erase(*layer.parentID);
    layer.transform.origin = Point(std::floor(center.x - width / 2.0), std::floor(center.y - height / 2.0));
    layer.transform.size = Size(width, height);
    layer.imageFile = layer.id.toString() + ".png";
    document_->images.emplace(layer.id, std::move(asset));
    document_->manifest.layers.push_back(std::move(layer));
    setActive(document_->manifest.layers.back().id);
    return true;
}

void EditorSession::insertAsset(ImportedImage asset, const std::optional<Point>& centeredAt) {
    if (isProjectBusy_) return;
    beginEdit("Import Image");
    const bool placed = placeAsset(std::move(asset), document_ ? centeredAt : std::nullopt);
    const bool fitted = createdCanvas_;
    endEdit();
    if (placed) emit selectionChanged();
    if (fitted) emit viewportChanged();
}

bool EditorSession::importImageFiles(const std::vector<std::string>& paths, const std::optional<Point>& centeredAt) {
    if (paths.empty() || isProjectBusy_) return false;
    const std::optional<Point> point = document_ ? centeredAt : std::nullopt;
    beginEdit(paths.size() == 1 ? "Import Image" : "Import Images");
    std::string failures;
    bool placed = false;
    bool fitted = false;
    for (const std::string& path : paths) {
        QImageReader reader(QString::fromStdString(path));
        reader.setAutoTransform(true);
        if (!reader.canRead()) {
            failures += QFileInfo(reader.fileName()).fileName().toStdString() + ": This file is not a supported image.\n";
            continue;
        }
        const QSize declared = reader.size();
        auto tooLarge = [&](int width, int height) {
            return width < 1 || height < 1 || width > DocumentLimits::maxSide || height > DocumentLimits::maxSide
                || static_cast<int64_t>(width) * height > DocumentLimits::documentPixelBudget() - usedPixels();
        };
        if (declared.isValid() && tooLarge(declared.width(), declared.height())) {
            failures += QFileInfo(reader.fileName()).fileName().toStdString()
                + ": This image exceeds the supported size or the remaining document budget.\n";
            continue;
        }
        QImage image = reader.read();
        if (image.isNull() || (!declared.isValid() && tooLarge(image.width(), image.height()))) {
            failures += QFileInfo(reader.fileName()).fileName().toStdString() + ": The image could not be read.\n";
            continue;
        }
        if (image.format() != QImage::Format_RGBA8888) image = image.convertToFormat(QImage::Format_RGBA8888);
        if (image.isNull()) {
            failures += QFileInfo(reader.fileName()).fileName().toStdString() + ": The image could not be read.\n";
            continue;
        }
        const std::string name = QFileInfo(reader.fileName()).completeBaseName().toStdString();
        placed = placeAsset(ImportedImage{image, thumbnailFor(image), name}, point) || placed;
        fitted = createdCanvas_ || fitted;
    }
    endEdit();
    if (placed) emit selectionChanged();
    if (fitted) emit viewportChanged();
    if (!failures.empty()) {
        emit errorOccurred(QStringLiteral("Import Image"), QString::fromStdString(failures).trimmed());
        return false;
    }
    return placed;
}

void EditorSession::fit() {
    if (!document_) return;
    viewport_.fit(documentSize());
    emit viewportChanged();
}

void EditorSession::zoomTo(double value, const std::optional<Point>& anchor) {
    if (!document_) return;
    viewport_.setZoom(value, anchor.value_or(viewport_.center()), documentSize());
    emit viewportChanged();
}

void EditorSession::zoomKeyboard(int step) {
    if (!document_ || step == 0) return;
    const double target = viewport_.keyboardZoomTarget(step);
    if (target == viewport_.zoom()) return;
    viewport_.setZoom(target, viewport_.center(), documentSize());
    emit viewportChanged();
}

void EditorSession::translateView(const Size& delta) {
    viewport_.translate(delta);
    emit viewportChanged();
}

void EditorSession::restore(const DocumentHistory::Snapshot& snapshot) {
    const bool had = document_.has_value();
    const bool has = snapshot.document.has_value();
    const bool changedCanvas = had != has
        || (had && has
            && (document_->manifest.documentID != snapshot.document->manifest.documentID
                || document_->manifest.width != snapshot.document->manifest.width
                || document_->manifest.height != snapshot.document->manifest.height));
    document_ = snapshot.document;
    setActive(snapshot.activeLayerID);
    if (changedCanvas && document_) viewport_.fit(documentSize());
    emit documentChanged();
    emit selectionChanged();
    if (changedCanvas) emit viewportChanged();
}

void EditorSession::undo() {
    if (!canUndo()) return;
    const std::optional<DocumentHistory::Snapshot> snapshot = history_.undo();
    if (snapshot) restore(*snapshot);
}

void EditorSession::redo() {
    if (!canRedo()) return;
    const std::optional<DocumentHistory::Snapshot> snapshot = history_.redo();
    if (snapshot) restore(*snapshot);
}

bool EditorSession::canPaint() const {
    if (!canEditLayers() || !document_ || !activeLayerID_) return false;
    if (selectedLayerIDs_.size() != 1) return false;
    const ProjectLayerRecord* record = activeLayer();
    if (!record || record->isGroupRecord()) return false;
    return true;
}

std::optional<QString> EditorSession::paintRefusal() const {
    if (!canEditLayers() || !document_) return std::nullopt;
    const ProjectLayerRecord* record = activeLayer();
    if (!record || canPaint()) return std::nullopt;
    if (selectedLayerIDs_.size() > 1) {
        return QStringLiteral("Several layers are selected. Select just one to paint on it.");
    }
    if (record->isGroupRecord()) {
        return QStringLiteral("\u201C%1\u201D is a folder, which has no pixels of its own. Paint on a layer inside it.")
            .arg(QString::fromStdString(record->name));
    }
    return QStringLiteral("There\u2019s nothing to paint on.");
}

void EditorSession::beginStroke(const Point& documentPoint) {
    if (stroke_ || isProjectBusy_) return;
    if (const std::optional<QString> refusal = paintRefusal()) {
        emit errorOccurred(QStringLiteral("Paint"), *refusal);
        return;
    }
    if (!canPaint()) return;
    ProjectLayerRecord* record = mutableLayer(*activeLayerID_);
    if (!record) return;
    const auto found = document_->images.find(record->id);
    const QImage pixels = found == document_->images.end() ? QImage() : found->second.image;
    try {
        stroke_ = std::make_unique<BrushStroke>(pixels, record->transform, documentSize(), brush_);
    } catch (const std::exception& error) {
        emit errorOccurred(QStringLiteral("Paint"), QString::fromStdString(error.what()));
        return;
    }
    strokeLayerID_ = record->id;
    stroke_->append(documentPoint);
    strokePreview_ = stroke_->image();
    strokePreviewTransform_ = stroke_->gridTransform();
    emit canvasInvalidated();
}

void EditorSession::continueStroke(const Point& documentPoint) {
    if (!stroke_) return;
    stroke_->append(documentPoint);
    strokePreview_ = stroke_->image();
    strokePreviewTransform_ = stroke_->gridTransform();
    emit canvasInvalidated();
}

void EditorSession::endStroke() {
    if (!stroke_) return;
    try {
        stroke_->flush();
    } catch (const std::exception&) {
        stroke_.reset();
        strokePreview_ = QImage();
        strokePreviewTransform_.reset();
        emit canvasInvalidated();
        return;
    }
    if (!stroke_->painted()) {
        stroke_.reset();
        strokePreview_ = QImage();
        strokePreviewTransform_.reset();
        emit canvasInvalidated();
        return;
    }
    commitStroke();
}

const QImage* EditorSession::strokePreview() const { return stroke_ ? &strokePreview_ : nullptr; }

std::optional<LayerTransform> EditorSession::strokePreviewTransform() const {
    return stroke_ ? strokePreviewTransform_ : std::nullopt;
}

void EditorSession::commitStroke() {
    if (!stroke_ || !document_) return;
    ProjectLayerRecord* record = mutableLayer(strokeLayerID_);
    if (!record) {
        stroke_.reset();
        emit canvasInvalidated();
        return;
    }
    const BrushStroke::Result result = stroke_->commit();
    stroke_.reset();
    strokePreview_ = QImage();
    strokePreviewTransform_.reset();
    if (result.image.isNull()) {
        emit canvasInvalidated();
        return;
    }
    const std::string name = brush_.erasing ? "Erase" : "Brush Stroke";
    beginEdit(name);
    ImportedImage asset{result.image, thumbnailFor(result.image), record->name};
    document_->images[strokeLayerID_] = std::move(asset);
    record->imageFile = strokeLayerID_.toString() + ".png";
    record->transform = result.transform;
    endEdit();
    emit selectionChanged();
}

}  // namespace compositor
