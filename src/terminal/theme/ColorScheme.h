#pragma once

#include <cstdint>

#include <QString>
#include <QVector>

namespace termsync::terminal {

// A terminal colour scheme: window background/foreground, cursor, and the 16
// ANSI colours (0-7 normal, 8-15 bright) that seed the low end of the 256-colour
// palette. Colours are 0xRRGGBB so this stays in the Qt-Core-only terminal lib
// (the QPainter renderer in src/ui converts to QColor). Pure data + a registry,
// so it is unit-testable.
struct ColorScheme
{
    QString name;
    bool dark = true;
    uint32_t background = 0x000000;
    uint32_t foreground = 0xffffff;
    uint32_t cursor = 0xffffff;
    uint32_t ansi[16] = {};
};

// Built-in schemes (stable order, matching the picker UI). Never empty.
const QVector<ColorScheme> &builtinSchemes();

// Looks up a scheme by name; returns nullptr if there is no such scheme.
const ColorScheme *findScheme(const QString &name);

// The default scheme's name (first built-in).
QString defaultSchemeName();

} // namespace termsync::terminal
