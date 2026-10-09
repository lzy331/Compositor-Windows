#include "ui/LayersPanel.h"

#include "document/Hierarchy.h"
#include "io/ImageAssets.h"

#include <QComboBox>
#include <QFrame>
#include <QInputDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace compositor {
namespace {

class LayerRow : public QWidget {
public:
    LayerRow(EditorSession& session, LayersPanel& panel, const ProjectLayerRecord& record, int depth, bool collapsed,
             QWidget* parent)
        : QWidget(parent), panel_(&panel), id_(record.id) {
        auto* row = new QHBoxLayout(this);
        row->setContentsMargins(8 + depth * 16, 2, 6, 2);
        row->setSpacing(4);
        setFixedHeight(34);
        if (session.selectedLayerIDs().count(record.id)) setStyleSheet(QStringLiteral("background: #3d5c80;"));

        if (record.isGroupRecord()) {
            auto* disclosure = new QToolButton(this);
            disclosure->setText(collapsed ? QStringLiteral("\u25B8") : QStringLiteral("\u25BE"));
            disclosure->setAutoRaise(true);
            disclosure->setFixedWidth(16);
            connect(disclosure, &QToolButton::clicked, this, [this] { panel_->toggleCollapsed(id_); });
            row->addWidget(disclosure);
        }

        auto* eye = new QToolButton(this);
        eye->setText(record.isVisible ? QStringLiteral("\u25CF") : QStringLiteral("\u25CB"));
        eye->setAutoRaise(true);
        eye->setFixedWidth(18);
        eye->setToolTip(QStringLiteral("Show or hide"));
        connect(eye, &QToolButton::clicked, this, [this, &session] { session.toggleLayerVisibility(id_); });
        row->addWidget(eye);

        auto* thumb = new QLabel(this);
        thumb->setFixedSize(28, 28);
        thumb->setAttribute(Qt::WA_TransparentForMouseEvents);
        thumb->setPixmap(thumbnail(session, record));
        row->addWidget(thumb);

        auto* name = new QLabel(QString::fromStdString(record.name), this);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        row->addWidget(name);
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        panel_->activate(id_, event->modifiers());
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) panel_->renameRequested(id_);
    }

private:
    static QPixmap thumbnail(EditorSession& session, const ProjectLayerRecord& record) {
        QImage icon(28, 28, QImage::Format_ARGB32_Premultiplied);
        icon.fill(QColor(70, 70, 70));
        QPainter painter(&icon);
        painter.fillRect(0, 0, 14, 14, QColor(96, 96, 96));
        painter.fillRect(14, 14, 14, 14, QColor(96, 96, 96));
        if (record.isGroupRecord()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(201, 162, 39));
            painter.drawRoundedRect(QRectF(4, 8, 20, 14), 2, 2);
        } else if (const ImportedImage* asset = session.imageFor(record.id)) {
            const QImage source = asset->thumbnail.isNull() ? asset->image : asset->thumbnail;
            painter.drawImage(QRect(1, 1, 26, 26), source);
        }
        return QPixmap::fromImage(icon);
    }

    LayersPanel* panel_;
    Uuid id_;
};

}  // namespace

void LayersPanel::toggleCollapsed(const Uuid& id) { session_.toggleGroupExpansion(id); }

LayersPanel::LayersPanel(EditorSession& session, QWidget* parent) : QWidget(parent), session_(session) {
    setMinimumWidth(220);
    setAutoFillBackground(true);
    QPalette panelPalette = palette();
    panelPalette.setColor(QPalette::Window, QColor(38, 38, 38));
    setPalette(panelPalette);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    rows_ = new QVBoxLayout(host);
    rows_->setContentsMargins(0, 4, 0, 4);
    rows_->setSpacing(1);
    rows_->addStretch();
    scroll->setWidget(host);
    root->addWidget(scroll, 1);

    opacity_ = new QSlider(Qt::Horizontal, this);
    opacity_->setRange(0, 100);
    auto* opacityRow = new QHBoxLayout;
    opacityRow->setContentsMargins(8, 6, 8, 0);
    opacityRow->addWidget(new QLabel(QStringLiteral("Opacity"), this));
    opacityRow->addWidget(opacity_, 1);
    root->addLayout(opacityRow);

    blend_ = new QComboBox(this);
    int added = 0;
    for (const std::vector<BlendMode>& group : blendModeGroups()) {
        if (added++ > 0) blend_->insertSeparator(blend_->count());
        for (BlendMode mode : group) blend_->addItem(QString::fromStdString(blendModeName(mode)), static_cast<int>(mode));
    }
    blend_->setContentsMargins(8, 4, 8, 4);
    root->addWidget(blend_);

    auto* buttons = new QHBoxLayout;
    buttons->setContentsMargins(4, 4, 4, 4);
    auto addButton = [&](const QString& text, auto slot) {
        auto* button = new QPushButton(text, this);
        connect(button, &QPushButton::clicked, this, slot);
        buttons->addWidget(button);
        return button;
    };
    addButton(QStringLiteral("Layer"), [this] { session_.addBlankLayer(); });
    addButton(QStringLiteral("Folder"), [this] { session_.addGroup(); });
    addButton(QStringLiteral("Copy"), [this] { session_.duplicateActiveLayer(); });
    addButton(QStringLiteral("Delete"), [this] { session_.deleteSelectedLayers(); });
    up_ = new QToolButton(this);
    up_->setText(QStringLiteral("Up"));
    up_->setToolTip(QStringLiteral("Move up"));
    down_ = new QToolButton(this);
    down_->setText(QStringLiteral("Down"));
    down_->setToolTip(QStringLiteral("Move down"));
    connect(up_, &QToolButton::clicked, this, [this] { session_.moveActiveLayer(1); });
    connect(down_, &QToolButton::clicked, this, [this] { session_.moveActiveLayer(-1); });
    buttons->addWidget(up_);
    buttons->addWidget(down_);
    root->addLayout(buttons);

    connect(&session_, &EditorSession::documentChanged, this, &LayersPanel::rebuild, Qt::QueuedConnection);
    connect(&session_, &EditorSession::selectionChanged, this, &LayersPanel::rebuild, Qt::QueuedConnection);
    connect(opacity_, &QSlider::valueChanged, this, [this](int value) {
        if (updating_ || !session_.activeLayerID()) return;
        if (!opacityGesture_) {
            session_.beginEdit("Layer Opacity");
            opacityGesture_ = true;
        }
        session_.setLayerOpacity(*session_.activeLayerID(), value / 100.0);
    });
    connect(opacity_, &QSlider::sliderReleased, this, [this] {
        if (!opacityGesture_) return;
        opacityGesture_ = false;
        session_.endEdit();
    });
    connect(blend_, &QComboBox::activated, this, [this](int index) {
        if (updating_ || !session_.activeLayerID()) return;
        session_.beginEdit("Blend Mode");
        session_.setLayerBlendMode(*session_.activeLayerID(), static_cast<BlendMode>(blend_->itemData(index).toInt()));
        session_.endEdit();
    });

    rebuild();
}

void LayersPanel::rebuild() {
    while (QLayoutItem* child = rows_->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    rowOrder_.clear();
    if (session_.document()) {
        const std::vector<LayerEntry> entries = LayerHierarchy::entries(session_.document()->manifest.layers, true,
                                                                        session_.collapsedGroupIDs());
        for (const LayerEntry& entry : entries) {
            rowOrder_.push_back(entry.layer->id);
            const bool collapsed = session_.collapsedGroupIDs().count(entry.layer->id) != 0;
            rows_->addWidget(new LayerRow(session_, *this, *entry.layer, entry.depth, collapsed, this));
        }
    }
    rows_->addStretch();
    syncControls();
}

void LayersPanel::syncControls() {
    updating_ = true;
    const ProjectLayerRecord* active = session_.activeLayer();
    opacity_->setEnabled(active != nullptr);
    blend_->setEnabled(active != nullptr);
    up_->setEnabled(session_.canMoveActiveLayer(1));
    down_->setEnabled(session_.canMoveActiveLayer(-1));
    if (active) {
        opacity_->setValue(static_cast<int>(std::lround(active->effectiveOpacityValue() * 100)));
        const int index = blend_->findData(static_cast<int>(active->effectiveBlendMode()));
        if (index >= 0) blend_->setCurrentIndex(index);
    }
    updating_ = false;
}

void LayersPanel::activate(const Uuid& id, Qt::KeyboardModifiers modifiers) {
    if ((modifiers & Qt::ShiftModifier) && session_.activeLayerID()) {
        const auto from = std::find(rowOrder_.begin(), rowOrder_.end(), *session_.activeLayerID());
        const auto to = std::find(rowOrder_.begin(), rowOrder_.end(), id);
        if (from != rowOrder_.end() && to != rowOrder_.end()) {
            std::set<Uuid> ids;
            for (auto it = std::min(from, to); it <= std::max(from, to); ++it) ids.insert(*it);
            session_.setSelection(std::move(ids), id);
            return;
        }
    }
    if (modifiers & Qt::ControlModifier) {
        std::set<Uuid> ids = session_.selectedLayerIDs();
        if (ids.count(id)) ids.erase(id);
        else ids.insert(id);
        std::optional<Uuid> active;
        if (ids.count(id)) active = id;
        else if (!ids.empty()) active = *ids.begin();
        session_.setSelection(std::move(ids), active);
        return;
    }
    session_.selectLayer(id);
}

void LayersPanel::renameRequested(const Uuid& id) {
    const ProjectLayerRecord* record = session_.layer(id);
    if (!record) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename Layer"), QStringLiteral("Name"),
                                                QLineEdit::Normal, QString::fromStdString(record->name), &accepted);
    if (accepted) session_.renameLayer(id, name.toStdString());
}

}  // namespace compositor
