#pragma once

#include <QVector>

#include "screen/Cell.h"

namespace termsync::terminal {

using Line = QVector<Cell>;

// The terminal screen model: a grid of cells plus cursor, scroll region,
// scrollback, and an alternate screen. It knows nothing about rendering or
// networking — the VtParser drives it, and the widget (M3b) reads it.
//
// Coordinates are 0-based internally (row 0 = top visible line, col 0 = left).
class ScreenBuffer
{
public:
    ScreenBuffer(int cols = 80, int rows = 24, int scrollbackMax = 2000);

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    int cursorRow() const { return m_cursorRow; }
    int cursorCol() const { return m_cursorCol; }
    bool cursorVisible() const { return m_cursorVisible; }
    void setCursorVisible(bool v) { m_cursorVisible = v; }

    // Access to the visible grid and the scrollback (oldest first).
    const Line &line(int row) const { return m_lines[row]; }
    const QVector<Line> &scrollback() const { return m_scrollback; }
    int scrollbackSize() const { return m_scrollback.size(); }

    Pen &pen() { return m_pen; }
    const Pen &pen() const { return m_pen; }

    bool usingAltScreen() const { return m_usingAlt; }

    // --- Character output -------------------------------------------------
    void writeChar(char32_t ch);

    // --- Cursor movement --------------------------------------------------
    void carriageReturn();
    void lineFeed();
    void reverseIndex();      // RI: move up, scroll down at top margin
    void index();             // IND: move down, scroll up at bottom margin
    void backspace();
    void tab();
    void moveCursor(int row, int col);      // absolute, clamped
    void moveCursorUp(int n);
    void moveCursorDown(int n);
    void moveCursorForward(int n);
    void moveCursorBack(int n);
    void setCursorColumn(int col);          // CHA / HPA
    void setCursorRow(int row);             // VPA
    void saveCursor();
    void restoreCursor();

    // --- Erase / edit -----------------------------------------------------
    void eraseInDisplay(int mode);   // 0=below,1=above,2=all,3=all+scrollback
    void eraseInLine(int mode);      // 0=right,1=left,2=whole
    void eraseChars(int n);          // ECH
    void insertChars(int n);         // ICH
    void deleteChars(int n);         // DCH
    void insertLines(int n);         // IL
    void deleteLines(int n);         // DL

    // --- Scroll region ----------------------------------------------------
    void setScrollRegion(int top, int bottom);   // DECSTBM (1-based, clamped)
    void scrollUp(int n);
    void scrollDown(int n);

    // --- Modes ------------------------------------------------------------
    void setAutoWrap(bool on) { m_autoWrap = on; }
    bool autoWrap() const { return m_autoWrap; }
    void useAltScreen(bool on, bool clearOnEnter);

    // --- Sizing -----------------------------------------------------------
    void resize(int cols, int rows);

    // Full hard reset (RIS).
    void reset();

private:
    Line blankLine() const;
    Cell blankCell() const;
    void clampCursor();
    // Scrolls the region [top,bottom] up by n lines; when the region is the
    // full screen and not on the alt screen, evicted top lines go to scrollback.
    void scrollRegionUp(int top, int bottom, int n);
    void scrollRegionDown(int top, int bottom, int n);

    int m_cols;
    int m_rows;
    int m_scrollbackMax;

    QVector<Line> m_lines;         // visible grid, size m_rows
    QVector<Line> m_scrollback;    // oldest at front

    int m_cursorRow = 0;
    int m_cursorCol = 0;
    bool m_cursorVisible = true;
    bool m_pendingWrap = false;    // DEC pending-wrap (cursor past last col)

    int m_savedRow = 0;
    int m_savedCol = 0;
    Pen m_savedPen;

    Pen m_pen;

    int m_scrollTop = 0;           // 0-based inclusive
    int m_scrollBottom;            // 0-based inclusive
    bool m_autoWrap = true;

    // Alternate screen support.
    bool m_usingAlt = false;
    QVector<Line> m_savedPrimary;
    int m_savedPrimaryCursorRow = 0;
    int m_savedPrimaryCursorCol = 0;
};

} // namespace termsync::terminal
