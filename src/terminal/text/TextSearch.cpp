#include "text/TextSearch.h"

#include <algorithm>

namespace termsync::terminal {

SearchMatch searchDocument(int rowCount,
                           const std::function<QString(int)> &lineAt,
                           const QString &needle, bool forward,
                           Qt::CaseSensitivity cs, int startRow, int startCol)
{
    SearchMatch match;
    if (needle.isEmpty() || rowCount <= 0 || !lineAt)
        return match;

    startRow = ((startRow % rowCount) + rowCount) % rowCount;

    // Scan every row once (wrapping), so the start row is also re-checked at the
    // end for matches that sit before/after the start column.
    for (int step = 0; step <= rowCount; ++step) {
        int r = forward ? startRow + step : startRow - step;
        r = ((r % rowCount) + rowCount) % rowCount;
        const QString text = lineAt(r);

        int idx;
        if (forward) {
            const int from = (step == 0) ? std::max(0, startCol) : 0;
            idx = text.indexOf(needle, from, cs);
        } else {
            // Last occurrence starting at or before maxStart. <0 means nothing can
            // match earlier on this row; >=size means scan the whole row.
            const int maxStart = (step == 0) ? startCol : text.size() - 1;
            if (maxStart < 0)
                idx = -1;
            else if (maxStart >= text.size())
                idx = text.lastIndexOf(needle, -1, cs);
            else
                idx = text.lastIndexOf(needle, maxStart, cs);
        }

        if (idx >= 0) {
            match.found = true;
            match.row = r;
            match.col = idx;
            match.length = needle.size();
            return match;
        }
    }
    return match;
}

} // namespace termsync::terminal
