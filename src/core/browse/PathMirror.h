#pragma once

#include <QString>

namespace termsync::core {

// Synchronized browsing: when the active pane navigates from `sourceRoot` to
// `sourceNew`, this returns the path the other pane should mirror to under
// `otherRoot` (otherRoot + the same relative sub-path). Returns an empty string
// if `sourceNew` is not within `sourceRoot`, so the caller leaves the other pane
// alone. Paths are treated as '/'-separated (remote convention; local QDir also
// accepts '/').
inline QString mirrorPath(const QString &sourceRoot, const QString &sourceNew,
                          const QString &otherRoot)
{
    const auto trimTrailing = [](QString s) {
        while (s.size() > 1 && s.endsWith('/'))
            s.chop(1);
        return s;
    };
    const QString root = trimTrailing(sourceRoot);
    const QString cur = trimTrailing(sourceNew);
    const QString other = trimTrailing(otherRoot);

    if (cur == root)
        return other;
    const QString prefix = root + '/';
    if (!cur.startsWith(prefix))
        return {}; // navigated outside the mirrored subtree
    const QString rel = cur.mid(prefix.size());
    return other.isEmpty() ? rel : other + '/' + rel;
}

} // namespace termsync::core
