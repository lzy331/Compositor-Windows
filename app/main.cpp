#include <QApplication>
#include <QPalette>

#include "ui/MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(43, 43, 43));
    palette.setColor(QPalette::WindowText, QColor(230, 230, 230));
    palette.setColor(QPalette::Base, QColor(32, 32, 32));
    palette.setColor(QPalette::AlternateBase, QColor(48, 48, 48));
    palette.setColor(QPalette::Text, QColor(230, 230, 230));
    palette.setColor(QPalette::Button, QColor(58, 58, 58));
    palette.setColor(QPalette::ButtonText, QColor(230, 230, 230));
    palette.setColor(QPalette::Highlight, QColor(61, 92, 128));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipBase, QColor(32, 32, 32));
    palette.setColor(QPalette::ToolTipText, QColor(230, 230, 230));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(120, 120, 120));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(120, 120, 120));
    app.setPalette(palette);
    app.setApplicationName(QStringLiteral("Compositor"));

    compositor::MainWindow window;
    window.resize(1280, 800);
    window.show();
    return app.exec();
}
