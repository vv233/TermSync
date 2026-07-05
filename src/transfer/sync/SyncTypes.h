#pragma once

#include <QMap>
#include <QString>

namespace termsync::transfer::sync {

enum class Direction {
    LocalToRemote,  // one-way: make remote match local
    RemoteToLocal,  // one-way: make local match remote
    TwoWay,         // bidirectional (needs last-sync state)
};

enum class CompareStrategy {
    MtimeSize,   // fast: size + mtime (with skew tolerance)
    Checksum,    // exact: content hash (not yet wired; falls back to MtimeSize)
};

enum class ConflictPolicy {
    NewerWins,   // the side with the newer mtime wins
    Skip,        // leave both untouched, report conflict
    KeepBoth,    // (reported as conflict in M7; rename-on-collision is future)
    Prompt,      // reported as conflict for the UI to resolve
};

// Metadata for one path within a tree (relative to the sync root).
struct SyncEntry
{
    quint64 size = 0;
    qint64 mtime = 0;   // seconds since epoch
    bool isDir = false;
};

// A flat listing: relative path -> entry. Directories are included with a
// trailing-slash-free relative path and isDir = true.
using Listing = QMap<QString, SyncEntry>;

// One side's remembered state at the last successful sync.
struct SideState
{
    bool present = false;
    SyncEntry entry;
};

struct PairState
{
    SideState local;
    SideState remote;
};

// last_sync_state: relative path -> both sides' snapshot at last sync.
using SyncState = QMap<QString, PairState>;

enum class ActionType {
    Upload,        // copy local -> remote
    Download,      // copy remote -> local
    DeleteLocal,
    DeleteRemote,
    MakeRemoteDir,
    MakeLocalDir,
    Conflict,      // needs resolution (Prompt/KeepBoth, or unresolved)
    Skip,          // no change needed
};

struct SyncAction
{
    ActionType type = ActionType::Skip;
    QString relativePath;
    QString reason;
};

inline const char *actionName(ActionType t)
{
    switch (t) {
    case ActionType::Upload: return "upload";
    case ActionType::Download: return "download";
    case ActionType::DeleteLocal: return "delete-local";
    case ActionType::DeleteRemote: return "delete-remote";
    case ActionType::MakeRemoteDir: return "mkdir-remote";
    case ActionType::MakeLocalDir: return "mkdir-local";
    case ActionType::Conflict: return "conflict";
    case ActionType::Skip: return "skip";
    }
    return "?";
}

} // namespace termsync::transfer::sync
