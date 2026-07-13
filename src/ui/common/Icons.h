#pragma once

#include <QColor>
#include <QIcon>

namespace termsync::ui {

// Crisp, stroke-based line icons drawn with QPainter (no image assets, no .qrc),
// so the app's toolbars stop using the dated native QStyle::standardIcon glyphs
// and read as one modern set. Tinted to a colour so they match the dark theme.
enum class Glyph {
    Back,
    Forward,
    Up,
    Refresh,
    NewFolder,
    Upload,
    Download,
    Rename,
    Trash,
    Sort,
    Grid,
    Home,
    Drive,
    Folder,
    File,
    Close,
    WinMinimize,
    WinMaximize,
    WinRestore,
};

// Returns a themed line icon for `glyph`. Rendered at high resolution and scaled
// by Qt, so it stays sharp at any toolbar size.
QIcon lineIcon(Glyph glyph, const QColor &color = QColor(0xc8, 0xd0, 0xe8));

// The application / window / taskbar icon: a rounded teal tile with a terminal
// prompt, generated at several sizes so it is crisp everywhere.
QIcon appIcon();

} // namespace termsync::ui
