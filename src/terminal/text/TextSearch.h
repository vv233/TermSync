#pragma once

#include <QString>

#include <functional>

namespace termsync::terminal {

// Result of a document search. `found` is false when there is no match.
struct SearchMatch
{
    bool found = false;
    int row = 0;    // document row of the match
    int col = 0;    // start column of the match
    int length = 0; // match length (== needle length on success)
};

// Single-line, wrap-once search over a `rowCount`-row document whose text is
// supplied lazily by `lineAt(row)`. The scan starts at (startRow, startCol) and
// walks forward or backward, wrapping once through the whole document so a match
// anywhere is found. On the start row, `startCol` bounds which matches count:
// forward considers matches beginning at column >= startCol; backward considers
// matches beginning at column <= startCol (so callers pass the column just past
// / before the current match to advance). Matching does not span rows.
SearchMatch searchDocument(int rowCount,
                           const std::function<QString(int)> &lineAt,
                           const QString &needle, bool forward,
                           Qt::CaseSensitivity cs, int startRow, int startCol);

} // namespace termsync::terminal
