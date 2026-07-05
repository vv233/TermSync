#include "screen/ScreenBuffer.h"

#include <algorithm>

namespace termsync::terminal {

ScreenBuffer::ScreenBuffer(int cols, int rows, int scrollbackMax)
    : m_cols(std::max(1, cols))
    , m_rows(std::max(1, rows))
    , m_scrollbackMax(std::max(0, scrollbackMax))
    , m_scrollBottom(std::max(1, rows) - 1)
{
    m_lines.resize(m_rows);
    for (Line &l : m_lines)
        l = blankLine();
}

Cell ScreenBuffer::blankCell() const
{
    Cell c;
    // Blank cells carry the current background so that, e.g., erasing with a
    // colored background fills correctly.
    c.bg = m_pen.bg;
    return c;
}

Line ScreenBuffer::blankLine() const
{
    Line l;
    l.resize(m_cols);
    Cell blank = blankCell();
    for (Cell &c : l)
        c = blank;
    return l;
}

void ScreenBuffer::clampCursor()
{
    m_cursorRow = std::clamp(m_cursorRow, 0, m_rows - 1);
    m_cursorCol = std::clamp(m_cursorCol, 0, m_cols - 1);
}

// ---------------------------------------------------------------------------
// Character output
// ---------------------------------------------------------------------------
void ScreenBuffer::writeChar(char32_t ch)
{
    if (m_pendingWrap && m_autoWrap) {
        m_cursorCol = 0;
        lineFeed();
        m_pendingWrap = false;
    }

    Cell &cell = m_lines[m_cursorRow][m_cursorCol];
    cell.ch = ch;
    cell.fg = m_pen.fg;
    cell.bg = m_pen.bg;
    cell.flags = m_pen.flags;

    if (m_cursorCol == m_cols - 1) {
        // Stay on the last column; wrap happens on the next write.
        m_pendingWrap = true;
    } else {
        ++m_cursorCol;
    }
}

// ---------------------------------------------------------------------------
// Cursor movement
// ---------------------------------------------------------------------------
void ScreenBuffer::carriageReturn()
{
    m_cursorCol = 0;
    m_pendingWrap = false;
}

void ScreenBuffer::lineFeed()
{
    m_pendingWrap = false;
    if (m_cursorRow == m_scrollBottom) {
        scrollRegionUp(m_scrollTop, m_scrollBottom, 1);
    } else if (m_cursorRow < m_rows - 1) {
        ++m_cursorRow;
    }
}

void ScreenBuffer::index()
{
    lineFeed();
}

void ScreenBuffer::reverseIndex()
{
    m_pendingWrap = false;
    if (m_cursorRow == m_scrollTop)
        scrollRegionDown(m_scrollTop, m_scrollBottom, 1);
    else if (m_cursorRow > 0)
        --m_cursorRow;
}

void ScreenBuffer::backspace()
{
    m_pendingWrap = false;
    if (m_cursorCol > 0)
        --m_cursorCol;
}

void ScreenBuffer::tab()
{
    m_pendingWrap = false;
    // Advance to the next multiple-of-8 column (classic tab stops).
    int next = ((m_cursorCol / 8) + 1) * 8;
    m_cursorCol = std::min(next, m_cols - 1);
}

void ScreenBuffer::moveCursor(int row, int col)
{
    m_pendingWrap = false;
    m_cursorRow = row;
    m_cursorCol = col;
    clampCursor();
}

void ScreenBuffer::moveCursorUp(int n)
{
    m_pendingWrap = false;
    m_cursorRow = std::max(m_scrollTop, m_cursorRow - std::max(1, n));
}

void ScreenBuffer::moveCursorDown(int n)
{
    m_pendingWrap = false;
    m_cursorRow = std::min(m_scrollBottom, m_cursorRow + std::max(1, n));
}

void ScreenBuffer::moveCursorForward(int n)
{
    m_pendingWrap = false;
    m_cursorCol = std::min(m_cols - 1, m_cursorCol + std::max(1, n));
}

void ScreenBuffer::moveCursorBack(int n)
{
    m_pendingWrap = false;
    m_cursorCol = std::max(0, m_cursorCol - std::max(1, n));
}

void ScreenBuffer::setCursorColumn(int col)
{
    m_pendingWrap = false;
    m_cursorCol = std::clamp(col, 0, m_cols - 1);
}

void ScreenBuffer::setCursorRow(int row)
{
    m_pendingWrap = false;
    m_cursorRow = std::clamp(row, 0, m_rows - 1);
}

void ScreenBuffer::saveCursor()
{
    m_savedRow = m_cursorRow;
    m_savedCol = m_cursorCol;
    m_savedPen = m_pen;
}

void ScreenBuffer::restoreCursor()
{
    m_cursorRow = m_savedRow;
    m_cursorCol = m_savedCol;
    m_pen = m_savedPen;
    clampCursor();
    m_pendingWrap = false;
}

// ---------------------------------------------------------------------------
// Erase / edit
// ---------------------------------------------------------------------------
void ScreenBuffer::eraseInDisplay(int mode)
{
    const Cell blank = blankCell();
    auto clearLine = [&](int row, int from, int to) {
        for (int c = from; c <= to; ++c)
            m_lines[row][c] = blank;
    };

    if (mode == 0) { // cursor to end of screen
        clearLine(m_cursorRow, m_cursorCol, m_cols - 1);
        for (int r = m_cursorRow + 1; r < m_rows; ++r)
            clearLine(r, 0, m_cols - 1);
    } else if (mode == 1) { // start of screen to cursor
        for (int r = 0; r < m_cursorRow; ++r)
            clearLine(r, 0, m_cols - 1);
        clearLine(m_cursorRow, 0, m_cursorCol);
    } else if (mode == 2 || mode == 3) { // whole screen (3 also clears scrollback)
        for (int r = 0; r < m_rows; ++r)
            clearLine(r, 0, m_cols - 1);
        if (mode == 3)
            m_scrollback.clear();
    }
    m_pendingWrap = false;
}

void ScreenBuffer::eraseInLine(int mode)
{
    const Cell blank = blankCell();
    Line &line = m_lines[m_cursorRow];
    if (mode == 0) {
        for (int c = m_cursorCol; c < m_cols; ++c)
            line[c] = blank;
    } else if (mode == 1) {
        for (int c = 0; c <= m_cursorCol; ++c)
            line[c] = blank;
    } else if (mode == 2) {
        for (int c = 0; c < m_cols; ++c)
            line[c] = blank;
    }
    m_pendingWrap = false;
}

void ScreenBuffer::eraseChars(int n)
{
    const Cell blank = blankCell();
    n = std::max(1, n);
    for (int c = m_cursorCol; c < std::min(m_cols, m_cursorCol + n); ++c)
        m_lines[m_cursorRow][c] = blank;
    m_pendingWrap = false;
}

void ScreenBuffer::insertChars(int n)
{
    n = std::max(1, n);
    Line &line = m_lines[m_cursorRow];
    const Cell blank = blankCell();
    for (int i = 0; i < n; ++i) {
        line.insert(m_cursorCol, blank);
        line.removeLast();
    }
    m_pendingWrap = false;
}

void ScreenBuffer::deleteChars(int n)
{
    n = std::max(1, n);
    Line &line = m_lines[m_cursorRow];
    const Cell blank = blankCell();
    for (int i = 0; i < n && m_cursorCol < line.size(); ++i) {
        line.removeAt(m_cursorCol);
        line.append(blank);
    }
    m_pendingWrap = false;
}

void ScreenBuffer::insertLines(int n)
{
    if (m_cursorRow < m_scrollTop || m_cursorRow > m_scrollBottom)
        return;
    scrollRegionDown(m_cursorRow, m_scrollBottom, std::max(1, n));
    m_pendingWrap = false;
}

void ScreenBuffer::deleteLines(int n)
{
    if (m_cursorRow < m_scrollTop || m_cursorRow > m_scrollBottom)
        return;
    scrollRegionUp(m_cursorRow, m_scrollBottom, std::max(1, n));
    m_pendingWrap = false;
}

// ---------------------------------------------------------------------------
// Scroll region
// ---------------------------------------------------------------------------
void ScreenBuffer::setScrollRegion(int top, int bottom)
{
    // Inputs are 1-based; 0 means "default" (full screen).
    if (top <= 0)
        top = 1;
    if (bottom <= 0 || bottom > m_rows)
        bottom = m_rows;
    if (top >= bottom)
        { m_scrollTop = 0; m_scrollBottom = m_rows - 1; }
    else
        { m_scrollTop = top - 1; m_scrollBottom = bottom - 1; }
    // DECSTBM homes the cursor.
    m_cursorRow = m_scrollTop;
    m_cursorCol = 0;
    m_pendingWrap = false;
}

void ScreenBuffer::scrollUp(int n)
{
    scrollRegionUp(m_scrollTop, m_scrollBottom, std::max(1, n));
}

void ScreenBuffer::scrollDown(int n)
{
    scrollRegionDown(m_scrollTop, m_scrollBottom, std::max(1, n));
}

void ScreenBuffer::scrollRegionUp(int top, int bottom, int n)
{
    n = std::min(n, bottom - top + 1);
    const bool toScrollback =
        !m_usingAlt && top == 0 && bottom == m_rows - 1 && m_scrollbackMax > 0;

    for (int i = 0; i < n; ++i) {
        if (toScrollback) {
            m_scrollback.append(m_lines[top]);
            while (m_scrollback.size() > m_scrollbackMax)
                m_scrollback.removeFirst();
        }
        m_lines.remove(top);
        m_lines.insert(bottom, blankLine());
    }
}

void ScreenBuffer::scrollRegionDown(int top, int bottom, int n)
{
    n = std::min(n, bottom - top + 1);
    for (int i = 0; i < n; ++i) {
        m_lines.remove(bottom);
        m_lines.insert(top, blankLine());
    }
}

// ---------------------------------------------------------------------------
// Alt screen
// ---------------------------------------------------------------------------
void ScreenBuffer::useAltScreen(bool on, bool clearOnEnter)
{
    if (on == m_usingAlt)
        return;

    if (on) {
        m_savedPrimary = m_lines;
        m_savedPrimaryCursorRow = m_cursorRow;
        m_savedPrimaryCursorCol = m_cursorCol;
        m_usingAlt = true;
        if (clearOnEnter) {
            for (Line &l : m_lines)
                l = blankLine();
            m_cursorRow = 0;
            m_cursorCol = 0;
        }
    } else {
        m_lines = m_savedPrimary;
        m_cursorRow = m_savedPrimaryCursorRow;
        m_cursorCol = m_savedPrimaryCursorCol;
        m_savedPrimary.clear();
        m_usingAlt = false;
    }
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_pendingWrap = false;
    clampCursor();
}

// ---------------------------------------------------------------------------
// Sizing / reset
// ---------------------------------------------------------------------------
void ScreenBuffer::resize(int cols, int rows)
{
    cols = std::max(1, cols);
    rows = std::max(1, rows);
    if (cols == m_cols && rows == m_rows)
        return;

    auto resizeGrid = [&](QVector<Line> &grid) {
        // Adjust each line's width.
        for (Line &l : grid) {
            if (l.size() < cols) {
                Cell blank;
                while (l.size() < cols)
                    l.append(blank);
            } else if (l.size() > cols) {
                l.resize(cols);
            }
        }
        // Adjust number of rows.
        while (grid.size() < rows) {
            Line l;
            l.resize(cols);
            grid.append(l);
        }
        while (grid.size() > rows)
            grid.removeLast();
    };

    resizeGrid(m_lines);
    if (!m_savedPrimary.isEmpty())
        resizeGrid(m_savedPrimary);

    m_cols = cols;
    m_rows = rows;
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    clampCursor();
    m_pendingWrap = false;
}

void ScreenBuffer::reset()
{
    m_pen.reset();
    m_cursorRow = m_cursorCol = 0;
    m_savedRow = m_savedCol = 0;
    m_savedPen.reset();
    m_cursorVisible = true;
    m_autoWrap = true;
    m_pendingWrap = false;
    m_usingAlt = false;
    m_savedPrimary.clear();
    m_scrollTop = 0;
    m_scrollBottom = m_rows - 1;
    m_scrollback.clear();
    for (Line &l : m_lines)
        l = blankLine();
}

} // namespace termsync::terminal
