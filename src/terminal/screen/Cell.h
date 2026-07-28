#pragma once

#include <cstdint>

#include "screen/Color.h"

namespace termsync::terminal {

// Character rendition flags (SGR attributes). Stored as a bitmask on each cell.
enum CellFlag : uint16_t {
    Bold          = 1 << 0,
    Faint         = 1 << 1,
    Italic        = 1 << 2,
    Underline     = 1 << 3,
    Blink         = 1 << 4,
    Reverse       = 1 << 5,
    Invisible     = 1 << 6,
    Strikethrough = 1 << 7,
    // Double-width (CJK) support: the lead cell of a wide glyph carries Wide;
    // the cell it spills into carries WideTrailer and holds no glyph.
    Wide          = 1 << 8,
    WideTrailer   = 1 << 9,
};

// One character cell of the terminal grid.
struct Cell
{
    char32_t ch = U' ';
    Color fg;
    Color bg;
    uint16_t flags = 0;

    bool hasFlag(CellFlag f) const { return (flags & f) != 0; }

    bool operator==(const Cell &o) const
    {
        return ch == o.ch && fg == o.fg && bg == o.bg && flags == o.flags;
    }
    bool operator!=(const Cell &o) const { return !(*this == o); }
};

// The "pen": the current rendition applied to newly written cells.
struct Pen
{
    Color fg;
    Color bg;
    uint16_t flags = 0;

    void reset() { *this = Pen{}; }
};

} // namespace termsync::terminal
