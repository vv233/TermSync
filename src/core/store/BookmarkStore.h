#pragma once

#include <QString>
#include <QVector>

#include "model/Bookmark.h"

namespace termsync::core {

// JSON-file-backed store of browser bookmarks. Pure model + persistence (no UI),
// so it is fully unit-testable. Bookmarks are small and few, so the whole list
// is kept in memory and rewritten on change.
class BookmarkStore
{
public:
    bool load(const QString &filePath); // missing file => empty, returns true
    bool save(const QString &filePath) const;

    const QVector<Bookmark> &bookmarks() const { return m_bookmarks; }
    // Bookmarks for a host (includes global, host-less ones).
    QVector<Bookmark> forHost(const QString &host) const;

    void add(const Bookmark &b);        // replaces one with the same id
    bool remove(const QString &id);
    Bookmark *find(const QString &id);

    static QString newId();

private:
    QVector<Bookmark> m_bookmarks;
};

} // namespace termsync::core
