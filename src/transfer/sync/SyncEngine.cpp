#include "sync/SyncEngine.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace termsync::transfer::sync {

Listing enumerateLocalTree(const QString &root)
{
    Listing listing;
    const QDir rootDir(root);
    if (!rootDir.exists())
        return listing;

    QDirIterator it(root, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        const QString rel = rootDir.relativeFilePath(info.absoluteFilePath());
        SyncEntry entry;
        entry.isDir = info.isDir();
        entry.size = info.isDir() ? 0 : static_cast<quint64>(info.size());
        entry.mtime = info.lastModified().toSecsSinceEpoch();
        listing.insert(rel, entry);
    }
    return listing;
}

SyncState buildStateFromListings(const Listing &local, const Listing &remote)
{
    SyncState state;
    auto merge = [&state](const Listing &listing, bool isLocal) {
        for (auto it = listing.constBegin(); it != listing.constEnd(); ++it) {
            PairState &ps = state[it.key()];
            SideState &side = isLocal ? ps.local : ps.remote;
            side.present = true;
            side.entry = it.value();
        }
    };
    merge(local, true);
    merge(remote, false);
    return state;
}

namespace {
QJsonObject entryToJson(const SyncEntry &e)
{
    QJsonObject o;
    o["size"] = static_cast<double>(e.size);
    o["mtime"] = static_cast<double>(e.mtime);
    o["isDir"] = e.isDir;
    return o;
}
SyncEntry entryFromJson(const QJsonObject &o)
{
    SyncEntry e;
    e.size = static_cast<quint64>(o["size"].toDouble());
    e.mtime = static_cast<qint64>(o["mtime"].toDouble());
    e.isDir = o["isDir"].toBool();
    return e;
}
} // namespace

bool saveSyncState(const QString &filePath, const SyncState &state)
{
    QJsonArray arr;
    for (auto it = state.constBegin(); it != state.constEnd(); ++it) {
        QJsonObject entry;
        entry["path"] = it.key();
        entry["localPresent"] = it.value().local.present;
        entry["local"] = entryToJson(it.value().local.entry);
        entry["remotePresent"] = it.value().remote.present;
        entry["remote"] = entryToJson(it.value().remote.entry);
        arr.append(entry);
    }
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return true;
}

SyncState loadSyncState(const QString &filePath)
{
    SyncState state;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return state;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        PairState ps;
        ps.local.present = o["localPresent"].toBool();
        ps.local.entry = entryFromJson(o["local"].toObject());
        ps.remote.present = o["remotePresent"].toBool();
        ps.remote.entry = entryFromJson(o["remote"].toObject());
        state.insert(o["path"].toString(), ps);
    }
    return state;
}

SyncReport executeSync(const QVector<SyncAction> &actions,
                       const SyncCallbacks &callbacks)
{
    SyncReport report;
    auto run = [&](const std::function<bool(const QString &)> &fn,
                   const SyncAction &a, int *counter) {
        if (!fn || !fn(a.relativePath)) {
            ++report.failed;
            report.errors << QStringLiteral("%1 %2")
                                 .arg(QString::fromLatin1(actionName(a.type)),
                                      a.relativePath);
            return;
        }
        ++(*counter);
    };

    for (const SyncAction &a : actions) {
        switch (a.type) {
        case ActionType::Upload:       run(callbacks.upload, a, &report.uploaded); break;
        case ActionType::Download:     run(callbacks.download, a, &report.downloaded); break;
        case ActionType::DeleteLocal:  run(callbacks.deleteLocal, a, &report.deleted); break;
        case ActionType::DeleteRemote: run(callbacks.deleteRemote, a, &report.deleted); break;
        case ActionType::MakeLocalDir: run(callbacks.mkdirLocal, a, &report.skipped); break;
        case ActionType::MakeRemoteDir:run(callbacks.mkdirRemote, a, &report.skipped); break;
        case ActionType::Conflict:     ++report.conflicts; break;
        case ActionType::Skip:         ++report.skipped; break;
        }
    }
    return report;
}

} // namespace termsync::transfer::sync
