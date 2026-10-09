#pragma once

#include <QMainWindow>

#include "ui/EditorSession.h"

class QAction;
class QLabel;
class QToolButton;

namespace compositor {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildMenus();
    void refreshChrome();
    void refreshZoom();
    void chooseTool(EditorSession::Tool tool, bool erasing);
    bool confirmDiscard();
    bool save();
    bool saveAs();
    void newProject();
    void openProject();

    EditorSession session_;
    QLabel* zoomLabel_ = nullptr;
    QLabel* sizeLabel_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QToolButton* moveButton_ = nullptr;
    QToolButton* handButton_ = nullptr;
    QToolButton* zoomButton_ = nullptr;
    QToolButton* brushButton_ = nullptr;
    QToolButton* eraserButton_ = nullptr;
};

}  // namespace compositor
