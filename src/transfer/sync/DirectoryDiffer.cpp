#include "sync/DirectoryDiffer.h"

#include <QSet>
#include <cstdlib>

namespace termsync::transfer::sync {

DirectoryDiffer::DirectoryDiffer(Direction direction, CompareStrategy compare,
                                 ConflictPolicy conflictPolicy, bool deleteOrphans,
                                 int mtimeToleranceSecs)
    : m_direction(direction)
    , m_compare(compare)
    , m_conflictPolicy(conflictPolicy)
    , m_deleteOrphans(deleteOrphans)
    , m_tolerance(mtimeToleranceSecs)
{
}

bool DirectoryDiffer::changed(const SyncEntry &a, const SyncEntry &b) const
{
    // Checksum strategy is not yet wired (requires reading content on both
    // sides); it currently behaves like MtimeSize.
    if (a.size != b.size)
        return true;
    if (std::llabs(a.mtime - b.mtime) > m_tolerance)
        return true;
    return false;
}

QVector<SyncAction> DirectoryDiffer::diff(const Listing &local,
                                          const Listing &remote,
                                          const SyncState &lastState) const
{
    switch (m_direction) {
    case Direction::LocalToRemote:
        return diffOneWay(local, remote, /*localIsSource=*/true);
    case Direction::RemoteToLocal:
        return diffOneWay(remote, local, /*localIsSource=*/false);
    case Direction::TwoWay:
        return diffTwoWay(local, remote, lastState);
    }
    return {};
}

// One-way: make `to` match `from`. localIsSource tells us which physical side
// `from` is, so we emit Upload vs Download / DeleteRemote vs DeleteLocal.
QVector<SyncAction> DirectoryDiffer::diffOneWay(const Listing &from,
                                                const Listing &to,
                                                bool localIsSource) const
{
    const ActionType copyAction =
        localIsSource ? ActionType::Upload : ActionType::Download;
    const ActionType mkdirAction =
        localIsSource ? ActionType::MakeRemoteDir : ActionType::MakeLocalDir;
    const ActionType deleteAction =
        localIsSource ? ActionType::DeleteRemote : ActionType::DeleteLocal;

    QVector<SyncAction> actions;

    // Copies / directory creation for everything in the source.
    for (auto it = from.constBegin(); it != from.constEnd(); ++it) {
        const QString &path = it.key();
        const SyncEntry &src = it.value();
        const auto dstIt = to.constFind(path);

        if (src.isDir) {
            if (dstIt == to.constEnd())
                actions.push_back({mkdirAction, path, "new directory"});
            continue;
        }
        if (dstIt == to.constEnd())
            actions.push_back({copyAction, path, "missing on target"});
        else if (changed(src, dstIt.value()))
            actions.push_back({copyAction, path, "content differs"});
        else
            actions.push_back({ActionType::Skip, path, "up to date"});
    }

    // Orphans: present on target but not source.
    if (m_deleteOrphans) {
        for (auto it = to.constBegin(); it != to.constEnd(); ++it) {
            if (!from.contains(it.key()))
                actions.push_back({deleteAction, it.key(), "orphan on target"});
        }
    }

    return actions;
}

QVector<SyncAction> DirectoryDiffer::diffTwoWay(const Listing &local,
                                                const Listing &remote,
                                                const SyncState &lastState) const
{
    QVector<SyncAction> actions;

    // Union of all paths seen anywhere.
    QSet<QString> paths;
    for (auto it = local.constBegin(); it != local.constEnd(); ++it)
        paths.insert(it.key());
    for (auto it = remote.constBegin(); it != remote.constEnd(); ++it)
        paths.insert(it.key());
    for (auto it = lastState.constBegin(); it != lastState.constEnd(); ++it)
        paths.insert(it.key());

    // Deterministic order.
    QList<QString> sorted(paths.constBegin(), paths.constEnd());
    std::sort(sorted.begin(), sorted.end());

    for (const QString &path : sorted) {
        const bool L = local.contains(path);
        const bool R = remote.contains(path);
        const SyncEntry le = local.value(path);
        const SyncEntry re = remote.value(path);
        const PairState prior = lastState.value(path);

        // Directory handling: create on the missing side.
        if ((L && le.isDir) || (R && re.isDir)) {
            if (L && !R)
                actions.push_back({ActionType::MakeRemoteDir, path, "new directory"});
            else if (R && !L)
                actions.push_back({ActionType::MakeLocalDir, path, "new directory"});
            continue;
        }

        const bool localAdded = L && !prior.local.present;
        const bool localModified =
            L && prior.local.present && changed(le, prior.local.entry);
        const bool localRemoved = !L && prior.local.present;
        const bool remoteAdded = R && !prior.remote.present;
        const bool remoteModified =
            R && prior.remote.present && changed(re, prior.remote.entry);
        const bool remoteRemoved = !R && prior.remote.present;

        const bool lchg = localAdded || localModified;
        const bool rchg = remoteAdded || remoteModified;

        auto resolveConflict = [&](const QString &why) {
            switch (m_conflictPolicy) {
            case ConflictPolicy::NewerWins:
                if (le.mtime >= re.mtime)
                    actions.push_back({ActionType::Upload, path, "conflict: local newer"});
                else
                    actions.push_back({ActionType::Download, path, "conflict: remote newer"});
                break;
            case ConflictPolicy::Skip:
                actions.push_back({ActionType::Skip, path, "conflict: skipped"});
                break;
            case ConflictPolicy::KeepBoth:
            case ConflictPolicy::Prompt:
                actions.push_back({ActionType::Conflict, path, why});
                break;
            }
        };

        if (L && R) {
            if (lchg && rchg) {
                if (!changed(le, re))
                    actions.push_back({ActionType::Skip, path, "identical"});
                else
                    resolveConflict("both sides changed");
            } else if (lchg) {
                actions.push_back({ActionType::Upload, path, "local changed"});
            } else if (rchg) {
                actions.push_back({ActionType::Download, path, "remote changed"});
            } else {
                actions.push_back({ActionType::Skip, path, "up to date"});
            }
        } else if (L && !R) {
            if (remoteRemoved) {
                if (localModified)
                    resolveConflict("remote deleted, local modified");
                else
                    actions.push_back({ActionType::DeleteLocal, path, "deleted on remote"});
            } else {
                actions.push_back({ActionType::Upload, path, "new local file"});
            }
        } else if (!L && R) {
            if (localRemoved) {
                if (remoteModified)
                    resolveConflict("local deleted, remote modified");
                else
                    actions.push_back({ActionType::DeleteRemote, path, "deleted on local"});
            } else {
                actions.push_back({ActionType::Download, path, "new remote file"});
            }
        }
        // !L && !R: gone from both — nothing to do.
    }

    return actions;
}

} // namespace termsync::transfer::sync
