#pragma once

#include <QVector>

#include "sync/SyncTypes.h"

namespace termsync::transfer::sync {

// Pure, deterministic diff engine. Given a local and remote listing (and, for
// two-way sync, the last-sync state), it produces the list of actions needed
// to reconcile them. It performs no I/O — this is what makes it exhaustively
// unit-testable and what the dry-run preview surfaces before execution.
class DirectoryDiffer
{
public:
    DirectoryDiffer(Direction direction, CompareStrategy compare,
                    ConflictPolicy conflictPolicy, bool deleteOrphans,
                    int mtimeToleranceSecs = 2);

    QVector<SyncAction> diff(const Listing &local, const Listing &remote,
                             const SyncState &lastState = {}) const;

private:
    bool changed(const SyncEntry &a, const SyncEntry &b) const;
    QVector<SyncAction> diffOneWay(const Listing &from, const Listing &to,
                                   bool localIsSource) const;
    QVector<SyncAction> diffTwoWay(const Listing &local, const Listing &remote,
                                   const SyncState &lastState) const;

    Direction m_direction;
    CompareStrategy m_compare;
    ConflictPolicy m_conflictPolicy;
    bool m_deleteOrphans;
    int m_tolerance;
};

} // namespace termsync::transfer::sync
