#include <QApplication>
#include <QFont>
#include <QPalette>
#include "MainWindow.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Clean light palette
    app.setStyle("Fusion");

    QPalette light;
    light.setColor(QPalette::Window,          QColor("#F8F9FE"));
    light.setColor(QPalette::WindowText,      QColor("#0F172A"));
    light.setColor(QPalette::Base,            QColor("#FFFFFF"));
    light.setColor(QPalette::AlternateBase,   QColor("#F8F9FE"));
    light.setColor(QPalette::Text,            QColor("#0F172A"));
    light.setColor(QPalette::Button,          QColor("#F1F5F9"));
    light.setColor(QPalette::ButtonText,      QColor("#0F172A"));
    light.setColor(QPalette::Highlight,       QColor("#4F6EF7"));
    light.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    light.setColor(QPalette::ToolTipBase,     QColor("#FFFFFF"));
    light.setColor(QPalette::ToolTipText,     QColor("#0F172A"));
    light.setColor(QPalette::PlaceholderText, QColor("#94A3B8"));
    app.setPalette(light);

    QFont font("Segoe UI", 10);
    font.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
