#pragma once

#include <QWidget>

#include <vector>

#include "ui/EditorSession.h"

class QComboBox;
class QSlider;
class QToolButton;
class QVBoxLayout;

namespace compositor {

class LayersPanel : public QWidget {
    Q_OBJECT
public:
    explicit LayersPanel(EditorSession& session, QWidget* parent = nullptr);
    void activate(const Uuid& id, Qt::KeyboardModifiers modifiers);
    void renameRequested(const Uuid& id);
    void toggleCollapsed(const Uuid& id);

private:
    void rebuild();
    void syncControls();

    EditorSession& session_;
    QVBoxLayout* rows_ = nullptr;
    QSlider* opacity_ = nullptr;
    QComboBox* blend_ = nullptr;
    QToolButton* up_ = nullptr;
    QToolButton* down_ = nullptr;
    std::vector<Uuid> rowOrder_;
    bool updating_ = false;
    bool opacityGesture_ = false;
};

}  // namespace compositor
