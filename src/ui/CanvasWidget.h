#pragma once

#include <QImage>
#include <QWidget>

#include "render/Raster.h"
#include "ui/EditorSession.h"

namespace compositor {

class CanvasWidget : public QWidget {
    Q_OBJECT
public:
    explicit CanvasWidget(EditorSession& session, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void rebuildComposite();
    void updateCursor();
    const ProjectLayerRecord* hitLayer(const Point& documentPoint) const;
    Size documentSize() const;

    EditorSession& session_;
    render::Raster compositeRaster_;
    QImage composite_;
    bool compositeDirty_ = true;
    bool moving_ = false;
    bool panning_ = false;
    bool painting_ = false;
    bool spaceDown_ = false;
    Uuid movingId_;
    Point origin0_;
    Point pressDocument_;
    QPoint lastView_;
};

}  // namespace compositor
