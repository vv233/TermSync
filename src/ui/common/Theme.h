#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace termsync::ui {

// Applies a modern dark theme via the cross-platform Fusion style, so the app
// doesn't look like a default Win32 dialog. Shared by the application entry
// point and the render harnesses. A per-scheme theme picker arrives with the
// appearance settings milestone.
inline void applyDarkTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette p;
    const QColor base(0x1e, 0x1e, 0x1e);
    const QColor panel(0x25, 0x25, 0x26);
    const QColor text(0xdc, 0xdc, 0xdc);
    const QColor accent(0x0a, 0x84, 0xff);

    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, panel);
    p.setColor(QPalette::ToolTipBase, panel);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, panel);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x7f, 0x7f, 0x7f));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x7f, 0x7f, 0x7f));
    app.setPalette(p);

    app.setStyleSheet(QStringLiteral(
        "QTabBar::tab { padding: 6px 12px; }"
        "QToolBar { border: 0; spacing: 4px; padding: 3px; }"
        "QStatusBar { border-top: 1px solid #333; }"
        "QTreeWidget, QTableWidget, QTableView, QTreeView { border: 1px solid #333; }"
        "QHeaderView::section { background: #2d2d30; padding: 4px; border: 0;"
        " border-right: 1px solid #333; }"
        "QPushButton { padding: 5px 12px; border: 1px solid #3c3c3c;"
        " border-radius: 3px; background: #2d2d30; }"
        "QPushButton:hover { background: #3a3a3d; }"
        "QLineEdit, QSpinBox, QComboBox { padding: 4px; border: 1px solid #3c3c3c;"
        " border-radius: 3px; background: #1e1e1e; }"));
}

} // namespace termsync::ui
