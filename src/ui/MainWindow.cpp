#include "ui/MainWindow.h"

#include "ui/CanvasWidget.h"
#include "ui/LayersPanel.h"

#include <QAction>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace compositor {
namespace {

QString zoomText(double zoom) {
    const double percent = zoom * 100;
    if (std::abs(percent - std::round(percent)) < 0.05) return QString::number(std::lround(percent)) + QLatin1Char('%');
    return QString::number(percent, 'f', 1) + QLatin1Char('%');
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Compositor"));
    auto* central = new QWidget(this);
    auto* row = new QHBoxLayout(central);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    auto* rail = new QWidget(central);
    rail->setFixedWidth(56);
    rail->setAutoFillBackground(true);
    QPalette railPalette = rail->palette();
    railPalette.setColor(QPalette::Window, QColor(38, 38, 38));
    rail->setPalette(railPalette);
    auto* railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(6, 8, 6, 8);
    railLayout->setSpacing(4);
    auto makeTool = [&](const QString& text, const QString& tip, EditorSession::Tool tool, bool erasing) {
        auto* button = new QToolButton(rail);
        button->setText(text);
        button->setToolTip(tip);
        button->setCheckable(true);
        button->setFixedSize(44, 32);
        connect(button, &QToolButton::clicked, this, [this, tool, erasing] { chooseTool(tool, erasing); });
        railLayout->addWidget(button);
        return button;
    };
    moveButton_ = makeTool(QStringLiteral("V"), QStringLiteral("Move (V)"), EditorSession::Tool::Move, false);
    handButton_ = makeTool(QStringLiteral("H"), QStringLiteral("Hand (H)"), EditorSession::Tool::Hand, false);
    zoomButton_ = makeTool(QStringLiteral("Z"), QStringLiteral("Zoom (Z)"), EditorSession::Tool::Zoom, false);
    brushButton_ = makeTool(QStringLiteral("B"), QStringLiteral("Brush (B)"), EditorSession::Tool::Brush, false);
    eraserButton_ = makeTool(QStringLiteral("E"), QStringLiteral("Eraser (E)"), EditorSession::Tool::Brush, true);
    railLayout->addStretch();
    moveButton_->setChecked(true);
    row->addWidget(rail);

    auto* splitter = new QSplitter(central);
    splitter->addWidget(new CanvasWidget(session_, splitter));
    splitter->addWidget(new LayersPanel(session_, splitter));
    splitter->setStretchFactor(0, 1);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    row->addWidget(splitter, 1);
    setCentralWidget(central);

    auto* toolbar = addToolBar(QStringLiteral("Main"));
    toolbar->setMovable(false);
    buildMenus();
    toolbar->addAction(undoAction_);
    toolbar->addAction(redoAction_);
    toolbar->addSeparator();
    auto* fit = toolbar->addAction(QStringLiteral("Fit"));
    auto* actual = toolbar->addAction(QStringLiteral("100%"));
    auto* zoomOut = toolbar->addAction(QStringLiteral("\u2212"));
    auto* zoomIn = toolbar->addAction(QStringLiteral("+"));
    connect(fit, &QAction::triggered, this, [this] { session_.fit(); });
    connect(actual, &QAction::triggered, this, [this] { session_.zoomTo(1); });
    connect(zoomOut, &QAction::triggered, this, [this] { session_.zoomKeyboard(-1); });
    connect(zoomIn, &QAction::triggered, this, [this] { session_.zoomKeyboard(1); });

    zoomLabel_ = new QLabel(this);
    sizeLabel_ = new QLabel(this);
    auto* colorLabel = new QLabel(QStringLiteral("sRGB \u00B7 Transparent"), this);
    statusBar()->addWidget(zoomLabel_);
    statusBar()->addWidget(sizeLabel_);
    statusBar()->addPermanentWidget(colorLabel);

    connect(&session_, &EditorSession::documentChanged, this, &MainWindow::refreshChrome);
    connect(&session_, &EditorSession::selectionChanged, this, &MainWindow::refreshChrome);
    connect(&session_, &EditorSession::viewportChanged, this, &MainWindow::refreshZoom);
    connect(&session_, &EditorSession::errorOccurred, this, [this](const QString& title, const QString& message) {
        QMessageBox::warning(this, title, message);
    });
    refreshChrome();
    refreshZoom();
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(QStringLiteral("&File"));
    file->addAction(QStringLiteral("&New Canvas..."), QKeySequence::New, this, &MainWindow::newProject);
    file->addAction(QStringLiteral("&Open..."), QKeySequence::Open, this, &MainWindow::openProject);
    file->addAction(QStringLiteral("&Save"), QKeySequence::Save, this, [this] { save(); });
    file->addAction(QStringLiteral("Save &As..."), QKeySequence::SaveAs, this, [this] { saveAs(); });
    file->addSeparator();
    file->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, this, &QWidget::close);

    auto* edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    undoAction_ = edit->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, this, [this] { session_.undo(); });
    redoAction_ = edit->addAction(QStringLiteral("&Redo"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), this,
                                  [this] { session_.redo(); });
    edit->addAction(QStringLiteral("Redo"), QKeySequence(Qt::CTRL | Qt::Key_Y), this, [this] { session_.redo(); });
    edit->addSeparator();
    edit->addAction(QStringLiteral("&Duplicate Layer"), QKeySequence(Qt::CTRL | Qt::Key_J), this,
                    [this] { session_.duplicateActiveLayer(); });
    edit->addAction(QStringLiteral("Delete Layer"), QKeySequence::Delete, this,
                    [this] { session_.deleteSelectedLayers(); });

    auto* layer = menuBar()->addMenu(QStringLiteral("&Layer"));
    layer->addAction(QStringLiteral("&New Layer"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), this,
                     [this] { session_.addBlankLayer(); });
    layer->addAction(QStringLiteral("New &Folder"), QKeySequence(Qt::CTRL | Qt::Key_G), this,
                     [this] { session_.addGroup(); });
    layer->addSeparator();
    layer->addAction(QStringLiteral("Move &Up"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up), this,
                     [this] { session_.moveActiveLayer(1); });
    layer->addAction(QStringLiteral("Move &Down"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down), this,
                     [this] { session_.moveActiveLayer(-1); });

    auto* view = menuBar()->addMenu(QStringLiteral("&View"));
    view->addAction(QStringLiteral("Zoom &In"), QKeySequence(Qt::CTRL | Qt::Key_Plus), this,
                    [this] { session_.zoomKeyboard(1); });
    view->addAction(QStringLiteral("Zoom In"), QKeySequence(Qt::CTRL | Qt::Key_Equal), this,
                    [this] { session_.zoomKeyboard(1); });
    view->addAction(QStringLiteral("Zoom &Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), this,
                    [this] { session_.zoomKeyboard(-1); });
    view->addAction(QStringLiteral("&Fit on Screen"), QKeySequence(Qt::CTRL | Qt::Key_0), this, [this] { session_.fit(); });
    view->addAction(QStringLiteral("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1), this,
                    [this] { session_.zoomTo(1); });

    auto* tools = menuBar()->addMenu(QStringLiteral("&Tools"));
    tools->addAction(QStringLiteral("&Move"), QKeySequence(Qt::Key_V), this,
                     [this] { chooseTool(EditorSession::Tool::Move, false); });
    tools->addAction(QStringLiteral("&Hand"), QKeySequence(Qt::Key_H), this,
                     [this] { chooseTool(EditorSession::Tool::Hand, false); });
    tools->addAction(QStringLiteral("&Zoom"), QKeySequence(Qt::Key_Z), this,
                     [this] { chooseTool(EditorSession::Tool::Zoom, false); });
    tools->addAction(QStringLiteral("&Brush"), QKeySequence(Qt::Key_B), this,
                     [this] { chooseTool(EditorSession::Tool::Brush, false); });
    tools->addAction(QStringLiteral("&Eraser"), QKeySequence(Qt::Key_E), this,
                     [this] { chooseTool(EditorSession::Tool::Brush, true); });
}

void MainWindow::chooseTool(EditorSession::Tool tool, bool erasing) {
    session_.brush().erasing = erasing;
    session_.setTool(tool);
    moveButton_->setChecked(tool == EditorSession::Tool::Move);
    handButton_->setChecked(tool == EditorSession::Tool::Hand);
    zoomButton_->setChecked(tool == EditorSession::Tool::Zoom);
    brushButton_->setChecked(tool == EditorSession::Tool::Brush && !erasing);
    eraserButton_->setChecked(tool == EditorSession::Tool::Brush && erasing);
}

void MainWindow::refreshChrome() {
    const QString name = session_.projectPath().empty()
        ? QStringLiteral("Untitled")
        : QFileInfo(QString::fromStdString(session_.projectPath())).completeBaseName();
    setWindowTitle((session_.isModified() ? QStringLiteral("* ") : QString()) + name + QStringLiteral(" \u2014 Compositor"));
    undoAction_->setEnabled(session_.canUndo());
    redoAction_->setEnabled(session_.canRedo());
    undoAction_->setText(session_.canUndo() ? QStringLiteral("Undo ") + QString::fromStdString(session_.undoName())
                                            : QStringLiteral("Undo"));
    redoAction_->setText(session_.canRedo() ? QStringLiteral("Redo ") + QString::fromStdString(session_.redoName())
                                            : QStringLiteral("Redo"));
    if (session_.document()) {
        sizeLabel_->setText(QString::number(session_.document()->manifest.width) + QStringLiteral(" \u00D7 ")
                            + QString::number(session_.document()->manifest.height) + QStringLiteral(" px"));
    } else {
        sizeLabel_->clear();
    }
}

void MainWindow::refreshZoom() {
    zoomLabel_->setText(zoomText(session_.viewport().zoom()));
}

bool MainWindow::confirmDiscard() {
    if (!session_.isModified()) return true;
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Save Changes"), QStringLiteral("Save changes to this project before closing it?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Discard) return true;
    return save();
}

bool MainWindow::save() {
    if (session_.projectPath().empty()) return saveAs();
    return session_.saveProject();
}

bool MainWindow::saveAs() {
    if (!session_.document()) return false;
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Project"), QString(),
                                                QStringLiteral("Compositor Project (*.comp)"));
    if (path.isEmpty()) return false;
    if (!path.endsWith(QStringLiteral(".comp"), Qt::CaseInsensitive)) path += QStringLiteral(".comp");
    return session_.saveProjectAs(path.toStdString());
}

void MainWindow::newProject() {
    if (!confirmDiscard()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("New Canvas"));
    auto* form = new QFormLayout(&dialog);
    auto* width = new QSpinBox(&dialog);
    auto* height = new QSpinBox(&dialog);
    auto* resolution = new QDoubleSpinBox(&dialog);
    auto* background = new QComboBox(&dialog);
    for (QSpinBox* box : {width, height}) {
        box->setRange(1, DocumentLimits::maxSide);
        box->setValue(box == width ? 1920 : 1080);
    }
    resolution->setRange(1, 9600);
    resolution->setDecimals(1);
    resolution->setValue(72);
    background->addItems({QStringLiteral("Transparent"), QStringLiteral("White"), QStringLiteral("Black")});
    form->addRow(QStringLiteral("Width"), width);
    form->addRow(QStringLiteral("Height"), height);
    form->addRow(QStringLiteral("Resolution"), resolution);
    form->addRow(QStringLiteral("Background"), background);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    std::optional<QColor> fill;
    if (background->currentIndex() == 1) fill = QColor(255, 255, 255);
    else if (background->currentIndex() == 2) fill = QColor(0, 0, 0);
    session_.createNewProject(width->value(), height->value(), resolution->value(), fill);
}

void MainWindow::openProject() {
    if (!confirmDiscard()) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Project"), QString(),
                                                      QStringLiteral("Compositor Project (*.comp)"));
    if (!path.isEmpty()) session_.loadProject(path.toStdString());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (confirmDiscard()) event->accept();
    else event->ignore();
}

}  // namespace compositor
