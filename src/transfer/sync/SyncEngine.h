#pragma once

#include <QStringList>
#include <QVector>
#include <functional>

#include "sync/SyncTypes.h"

namespace termsync::transfer::sync {

// Recursively enumerates a local directory tree into a Listing (paths relative
// to `root`, forward-slash separated). Directories are included with isDir=true.
Listing enumerateLocalTree(const QString &root);

// Snapshots the two current listings into a last-sync state (for two-way sync).
SyncState buildStateFromListings(const Listing &local, const Listing &remote);

// Persists / loads the last-sync state as JSON.
bool saveSyncState(const QString &filePath, const SyncState &state);
SyncState loadSyncState(const QString &filePath);

// Callbacks the executor uses to perform each action. Each returns success.
// Any may be left unset (treated as failure if the action is emitted).
struct SyncCallbacks
{
    std::function<bool(const QString &rel)> upload;
    std::function<bool(const QString &rel)> download;
    std::function<bool(const QString &rel)> deleteLocal;
    std::function<bool(const QString &rel)> deleteRemote;
    std::function<bool(const QString &rel)> mkdirLocal;
    std::function<bool(const QString &rel)> mkdirRemote;
};

struct SyncReport
{
    int uploaded = 0;
    int downloaded = 0;
    int deleted = 0;
    int conflicts = 0;
    int skipped = 0;
    int failed = 0;
    QStringList errors;

    int changeCount() const { return uploaded + downloaded + deleted; }
};

// Runs the action list, collecting a report (does not abort on first failure).
SyncReport executeSync(const QVector<SyncAction> &actions,
                       const SyncCallbacks &callbacks);

} // namespace termsync::transfer::sync
