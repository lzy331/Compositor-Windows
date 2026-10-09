#include "ui/CanvasWidget.h"

#include "document/Hierarchy.h"
#include "render/DocumentRenderer.h"
#include "render/Geometry.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

#include <cmath>

namespace compositor {
namespace {

QPixmap checkerboard() {
    QPixmap pixmap(16, 16);
    QPainter painter(&pixmap);
    painter.fillRect(pixmap.rect(), QColor(188, 188, 188));
    painter.fillRect(0, 0, 8, 8, QColor(150, 150, 150));
    painter.fillRect(8, 8, 8, 8, QColor(150, 150, 150));
    return pixmap;
}

}  // namespace

CanvasWidget::CanvasWidget(EditorSession& session, QWidget* parent) : QWidget(parent), session_(session) {
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    setMouseTracking(true);
    setMinimumSize(240, 180);
    connect(&session_, &EditorSession::documentChanged, this, [this] {
        compositeDirty_ = true;
        update();
    });
    connect(&session_, &EditorSession::canvasInvalidated, this, [this] {
        compositeDirty_ = true;
        updateCursor();
        update();
    });
    connect(&session_, &EditorSession::viewportChanged, this, qOverload<>(&QWidget::update));
    connect(&session_, &EditorSession::selectionChanged, this, qOverload<>(&QWidget::update));
    updateCursor();
}

Size CanvasWidget::documentSize() const {
    const ProjectSnapshot& document = *session_.document();
    return Size(static_cast<double>(document.manifest.width), static_cast<double>(document.manifest.height));
}

void CanvasWidget::rebuildComposite() {
    composite_ = QImage();
    compositeRaster_ = render::Raster();
    compositeDirty_ = false;
    if (!session_.document()) return;
    try {
        const QImage* preview = session_.strokePreview();
        if (preview && session_.activeLayerID()) {
            // Substitute the in-progress stroke's pixels for the active layer, so the canvas
            // shows the stroke (and an erase's holes) exactly as the commit will leave them.
            ProjectSnapshot working = *session_.document();
            const std::optional<LayerTransform> transform = session_.strokePreviewTransform();
            for (ProjectLayerRecord& layer : working.manifest.layers) {
                if (layer.id != *session_.activeLayerID()) continue;
                working.images[layer.id] = ImportedImage{*preview, QImage(), layer.name};
                if (transform) layer.transform = *transform;
            }
            compositeRaster_ = render::DocumentRenderer::render(working);
        } else {
            compositeRaster_ = render::DocumentRenderer::render(*session_.document());
        }
        if (!compositeRaster_.pixels.empty() && compositeRaster_.width > 0 && compositeRaster_.height > 0) {
            composite_ = QImage(compositeRaster_.pixels.data(), compositeRaster_.width, compositeRaster_.height,
                                compositeRaster_.width * 4, QImage::Format_RGBA8888_Premultiplied);
        }
    } catch (const std::exception&) {
        composite_ = QImage();
    }
}

void CanvasWidget::updateCursor() {
    if (spaceDown_ || session_.tool() == EditorSession::Tool::Hand) setCursor(Qt::OpenHandCursor);
    else if (session_.tool() == EditorSession::Tool::Move) setCursor(Qt::ArrowCursor);
    else setCursor(Qt::CrossCursor);
}

const ProjectLayerRecord* CanvasWidget::hitLayer(const Point& documentPoint) const {
    if (!session_.document()) return nullptr;
    const auto visible = LayerHierarchy::visibleLayers(session_.document()->manifest.layers);
    for (auto it = visible.rbegin(); it != visible.rend(); ++it) {
        const ProjectLayerRecord* layer = *it;
        const Point local = render::layerDocumentToLocal(layer->transform).apply(documentPoint);
        const double halfWidth = layer->transform.size.width / 2;
        const double halfHeight = layer->transform.size.height / 2;
        if (local.x >= -halfWidth && local.x <= halfWidth && local.y >= -halfHeight && local.y <= halfHeight) return layer;
    }
    return nullptr;
}

void CanvasWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(43, 43, 43));
    if (!session_.document()) return;
    if (compositeDirty_) rebuildComposite();

    const Size size = documentSize();
    const Rect placed = session_.viewport().documentRect(size);
    const QRectF viewRect(placed.x, placed.y, placed.width, placed.height);
    painter.save();
    painter.setClipRect(viewRect);
    painter.fillRect(viewRect, QBrush(checkerboard()));
    if (!composite_.isNull()) {
        painter.setRenderHint(QPainter::SmoothPixmapTransform, session_.viewport().pointsPerPixel() < 1);
        painter.drawImage(viewRect, composite_);
    }
    painter.restore();
    painter.setPen(QColor(90, 90, 90));
    painter.drawRect(viewRect.adjusted(0, 0, -1, -1));

    if (session_.document()->manifest.guides) {
        painter.setPen(QColor(80, 190, 210));
        for (const CanvasGuide& guide : *session_.document()->manifest.guides) {
            if (guide.axis == CanvasGuide::Axis::Horizontal) {
                const double y = session_.viewport().viewPoint(Point(0, guide.position), size).y;
                painter.drawLine(QPointF(viewRect.left(), y), QPointF(viewRect.right(), y));
            } else {
                const double x = session_.viewport().viewPoint(Point(guide.position, 0), size).x;
                painter.drawLine(QPointF(x, viewRect.top()), QPointF(x, viewRect.bottom()));
            }
        }
    }

    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Uuid& id : session_.selectedLayerIDs()) {
        const ProjectLayerRecord* layer = session_.layer(id);
        if (!layer) continue;
        const render::Affine placement = render::layerLocalToDocument(layer->transform);
        const double halfWidth = layer->transform.size.width / 2;
        const double halfHeight = layer->transform.size.height / 2;
        const Point corners[4] = {placement.apply(-halfWidth, -halfHeight), placement.apply(halfWidth, -halfHeight),
                                  placement.apply(halfWidth, halfHeight), placement.apply(-halfWidth, halfHeight)};
        QPolygonF polygon;
        for (const Point& corner : corners) {
            const Point view = session_.viewport().viewPoint(corner, size);
            polygon << QPointF(view.x, view.y);
        }
        const bool active = session_.activeLayerID() && *session_.activeLayerID() == id;
        painter.setPen(QPen(active ? QColor(90, 170, 255) : QColor(90, 170, 255, 140), active ? 1.5 : 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(polygon);
    }
}

void CanvasWidget::resizeEvent(QResizeEvent*) {
    session_.resizeViewport(Size(width(), height()), devicePixelRatioF());
}

void CanvasWidget::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    if (!session_.document()) return;
    const QPoint pos = event->position().toPoint();
    const bool pan = event->button() == Qt::MiddleButton || spaceDown_
        || session_.tool() == EditorSession::Tool::Hand;
    if (pan && (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        panning_ = true;
        lastView_ = pos;
        setCursor(Qt::ClosedHandCursor);
        grabMouse();
        return;
    }
    if (event->button() != Qt::LeftButton) return;
    const Point anchor(pos.x(), pos.y());
    if (session_.tool() == EditorSession::Tool::Zoom) {
        const double factor = (event->modifiers() & Qt::AltModifier) ? 0.5 : 2;
        session_.zoomTo(session_.viewport().zoom() * factor, anchor);
        return;
    }
    if (session_.tool() == EditorSession::Tool::Brush) {
        painting_ = true;
        session_.beginStroke(session_.viewport().documentPoint(anchor, documentSize()));
        grabMouse();
        return;
    }
    if (session_.tool() != EditorSession::Tool::Move) return;
    const Point documentPoint = session_.viewport().documentPoint(anchor, documentSize());
    const ProjectLayerRecord* hit = hitLayer(documentPoint);
    if (!hit) return;
    if (!session_.activeLayerID() || *session_.activeLayerID() != hit->id) session_.selectLayer(hit->id);
    session_.beginEdit("Move Layer");
    moving_ = true;
    movingId_ = hit->id;
    origin0_ = hit->transform.origin;
    pressDocument_ = documentPoint;
    grabMouse();
}

void CanvasWidget::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (panning_) {
        session_.translateView(Size(pos.x() - lastView_.x(), pos.y() - lastView_.y()));
        lastView_ = pos;
        return;
    }
    if (painting_) {
        session_.continueStroke(session_.viewport().documentPoint(Point(pos.x(), pos.y()), documentSize()));
        return;
    }
    if (!moving_ || !session_.document()) return;
    const Point now = session_.viewport().documentPoint(Point(pos.x(), pos.y()), documentSize());
    session_.setLayerOrigin(movingId_, Point(origin0_.x + now.x - pressDocument_.x, origin0_.y + now.y - pressDocument_.y));
}

void CanvasWidget::mouseReleaseEvent(QMouseEvent*) {
    if (panning_ || moving_ || painting_) releaseMouse();
    if (panning_) {
        panning_ = false;
        updateCursor();
    }
    if (moving_) {
        moving_ = false;
        session_.endEdit();
    }
    if (painting_) {
        painting_ = false;
        session_.endStroke();
    }
}

void CanvasWidget::wheelEvent(QWheelEvent* event) {
    if (!session_.document()) return;
    const double notches = event->angleDelta().y() / 120.0;
    if (notches == 0) return;
    const QPointF pos = event->position();
    session_.zoomTo(session_.viewport().zoom() * std::pow(1.2, notches), Point(pos.x(), pos.y()));
    event->accept();
}

void CanvasWidget::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceDown_ = true;
        updateCursor();
        return;
    }
    QWidget::keyPressEvent(event);
}

void CanvasWidget::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        spaceDown_ = false;
        updateCursor();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

void CanvasWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void CanvasWidget::dropEvent(QDropEvent* event) {
    std::vector<std::string> paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) paths.push_back(url.toLocalFile().toStdString());
    }
    if (paths.empty()) return;
    const QPointF pos = event->position();
    const std::optional<Point> anchor = session_.document()
        ? std::optional<Point>(session_.viewport().documentPoint(Point(pos.x(), pos.y()), documentSize()))
        : std::nullopt;
    session_.importImageFiles(paths, anchor);
    event->acceptProposedAction();
}

}  // namespace compositor
