#include "store/BookmarkStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace termsync::core {

bool BookmarkStore::load(const QString &filePath)
{
    m_bookmarks.clear();
    QFile f(filePath);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Bookmark b;
        b.id = o["id"].toString();
        b.name = o["name"].toString();
        b.host = o["host"].toString();
        b.remotePath = o["remotePath"].toString();
        b.localPath = o["localPath"].toString();
        m_bookmarks.append(b);
    }
    return true;
}

bool BookmarkStore::save(const QString &filePath) const
{
    QJsonArray arr;
    for (const Bookmark &b : m_bookmarks) {
        QJsonObject o;
        o["id"] = b.id;
        o["name"] = b.name;
        o["host"] = b.host;
        o["remotePath"] = b.remotePath;
        o["localPath"] = b.localPath;
        arr.append(o);
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(QJsonDocument(arr).toJson(QJsonDocument::Indented)) >= 0;
}

QVector<Bookmark> BookmarkStore::forHost(const QString &host) const
{
    QVector<Bookmark> out;
    for (const Bookmark &b : m_bookmarks)
        if (b.host.isEmpty() || b.host == host)
            out.append(b);
    return out;
}

void BookmarkStore::add(const Bookmark &b)
{
    for (Bookmark &existing : m_bookmarks) {
        if (existing.id == b.id) {
            existing = b;
            return;
        }
    }
    m_bookmarks.append(b);
}

bool BookmarkStore::remove(const QString &id)
{
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        if (m_bookmarks[i].id == id) {
            m_bookmarks.removeAt(i);
            return true;
        }
    }
    return false;
}

Bookmark *BookmarkStore::find(const QString &id)
{
    for (Bookmark &b : m_bookmarks)
        if (b.id == id)
            return &b;
    return nullptr;
}

QString BookmarkStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace termsync::core
