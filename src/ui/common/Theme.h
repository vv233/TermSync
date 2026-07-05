#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QStyleFactory>

namespace termsync::ui {

// A modern, Termius/Tabby-inspired dark theme: deep slate backgrounds, a teal
// accent, rounded corners and generous padding. Layout/interaction patterns are
// borrowed; all colors/assets are original. Shared by the app entry point and
// the render harnesses. A per-scheme theme picker arrives with the appearance
// settings milestone.
inline void applyDarkTheme(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // Palette — a calm slate/indigo dark scheme.
    const QColor bg(0x1a, 0x1b, 0x26);       // window/base
    const QColor surface(0x1f, 0x21, 0x30);  // panels
    const QColor text(0xc8, 0xd0, 0xe8);
    const QColor subtext(0x8a, 0x92, 0xb2);
    const QColor accent(0x2d, 0xd4, 0xbf);   // teal

    QPalette p;
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, surface);
    p.setColor(QPalette::ToolTipBase, surface);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Link, accent);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, QColor(0x10, 0x12, 0x18));
    p.setColor(QPalette::PlaceholderText, subtext);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x5a, 0x60, 0x78));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x5a, 0x60, 0x78));
    app.setPalette(p);

    // Rounded corners, hover states, thin scrollbars, accented tabs/selection.
    app.setStyleSheet(QStringLiteral(R"(
        QWidget { font-size: 10pt; }
        QMainWindow, QDialog { background: #1a1b26; }

        /* Menu bar / menus */
        QMenuBar { background: #16171f; border-bottom: 1px solid #2a2c3a; }
        QMenuBar::item { padding: 6px 12px; background: transparent; }
        QMenuBar::item:selected { background: #2a2c3a; border-radius: 4px; }
        QMenu { background: #1f2130; border: 1px solid #2a2c3a; padding: 4px; }
        QMenu::item { padding: 6px 24px; border-radius: 4px; }
        QMenu::item:selected { background: #2dd4bf; color: #101218; }
        QMenu::separator { height: 1px; background: #2a2c3a; margin: 4px 8px; }

        /* Toolbar */
        QToolBar { background: #16171f; border: 0; spacing: 6px; padding: 6px; }
        QToolButton { padding: 6px 10px; border-radius: 6px; color: #c8d0e8; }
        QToolButton:hover { background: #2a2c3a; }
        QToolButton:disabled { color: #5a6078; }

        /* Dock / panels */
        QDockWidget { titlebar-close-icon: none; color: #8a92b2; }
        QDockWidget::title { background: #16171f; padding: 8px 10px;
            border-bottom: 1px solid #2a2c3a; }

        /* Tabs */
        QTabWidget::pane { border: 0; background: #1a1b26; }
        QTabBar::tab { background: transparent; color: #8a92b2;
            padding: 8px 16px; margin-right: 2px;
            border-top-left-radius: 6px; border-top-right-radius: 6px; }
        QTabBar::tab:hover { background: #23252f; color: #c8d0e8; }
        QTabBar::tab:selected { background: #1f2130; color: #ffffff;
            border-bottom: 2px solid #2dd4bf; }

        /* Trees & tables (session list / file panes) */
        QTreeWidget, QTreeView, QTableWidget, QTableView {
            background: #1a1b26; border: 1px solid #2a2c3a; border-radius: 8px;
            outline: 0; }
        QTreeView::item, QTableView::item { padding: 5px; border-radius: 6px; }
        QTreeView::item:hover, QTableView::item:hover { background: #23252f; }
        QTreeView::item:selected, QTableView::item:selected {
            background: #2dd4bf; color: #101218; }
        QHeaderView::section { background: #16171f; color: #8a92b2;
            padding: 6px 8px; border: 0; border-right: 1px solid #2a2c3a;
            border-bottom: 1px solid #2a2c3a; }

        /* Inputs */
        QLineEdit, QSpinBox, QComboBox {
            background: #14151d; color: #c8d0e8; padding: 6px 8px;
            border: 1px solid #2a2c3a; border-radius: 6px;
            selection-background-color: #2dd4bf; selection-color: #101218; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #2dd4bf; }
        QComboBox::drop-down { border: 0; width: 20px; }
        QComboBox QAbstractItemView { background: #1f2130;
            border: 1px solid #2a2c3a; selection-background-color: #2dd4bf;
            selection-color: #101218; }

        /* Buttons */
        QPushButton { background: #262a3b; color: #c8d0e8; padding: 7px 16px;
            border: 1px solid #313547; border-radius: 6px; }
        QPushButton:hover { background: #313547; }
        QPushButton:pressed { background: #2dd4bf; color: #101218; }
        QPushButton:default { background: #2dd4bf; color: #101218; border: 0; }
        QPushButton:default:hover { background: #34e6cf; }

        /* Checkboxes */
        QCheckBox { spacing: 8px; }
        QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px;
            border: 1px solid #313547; background: #14151d; }
        QCheckBox::indicator:checked { background: #2dd4bf; border: 0; }

        /* Progress bars (transfer queue) */
        QProgressBar { background: #14151d; border: 1px solid #2a2c3a;
            border-radius: 6px; text-align: center; color: #c8d0e8; height: 16px; }
        QProgressBar::chunk { background: #2dd4bf; border-radius: 5px; }

        /* Status bar */
        QStatusBar { background: #16171f; color: #8a92b2;
            border-top: 1px solid #2a2c3a; }
        QStatusBar::item { border: 0; }

        /* Splitters */
        QSplitter::handle { background: #2a2c3a; }
        QSplitter::handle:horizontal { width: 2px; }
        QSplitter::handle:vertical { height: 2px; }

        /* Scrollbars — thin, rounded, subtle */
        QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }
        QScrollBar::handle:vertical { background: #3a3e52; min-height: 28px;
            border-radius: 5px; }
        QScrollBar::handle:vertical:hover { background: #4a4f68; }
        QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }
        QScrollBar::handle:horizontal { background: #3a3e52; min-width: 28px;
            border-radius: 5px; }
        QScrollBar::handle:horizontal:hover { background: #4a4f68; }
        QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
        QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    )"));
}

} // namespace termsync::ui
