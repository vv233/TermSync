// Deterministic tests for the sync DirectoryDiffer and executor. No network:
// listings are built in-memory (and, for the end-to-end idempotency test, from
// two real temp directories via a local test double).

#include <gtest/gtest.h>

#include <QTemporaryDir>

#include "sync/DirectoryDiffer.h"
#include "sync/SyncEngine.h"

using namespace termsync::transfer::sync;

namespace {

SyncEntry file(quint64 size, qint64 mtime)
{
    return SyncEntry{size, mtime, false};
}

// Counts actions of a given type in the result.
int count(const QVector<SyncAction> &actions, ActionType type)
{
    int n = 0;
    for (const auto &a : actions)
        if (a.type == type)
            ++n;
    return n;
}

bool has(const QVector<SyncAction> &actions, ActionType type, const QString &path)
{
    for (const auto &a : actions)
        if (a.type == type && a.relativePath == path)
            return true;
    return false;
}

} // namespace

// ---- One-way ---------------------------------------------------------------

TEST(SyncDiff, OneWayUploadsMissingAndChanged)
{
    Listing local{{"a.txt", file(10, 100)}, {"b.txt", file(20, 200)}};
    Listing remote{{"a.txt", file(10, 100)}}; // b missing; a identical

    DirectoryDiffer d(Direction::LocalToRemote, CompareStrategy::MtimeSize,
                      ConflictPolicy::Skip, /*deleteOrphans=*/false);
    const auto actions = d.diff(local, remote);

    EXPECT_TRUE(has(actions, ActionType::Upload, "b.txt"));
    EXPECT_TRUE(has(actions, ActionType::Skip, "a.txt"));
    EXPECT_EQ(count(actions, ActionType::Upload), 1);
}

TEST(SyncDiff, OneWayChangedBySize)
{
    Listing local{{"a.txt", file(15, 100)}};
    Listing remote{{"a.txt", file(10, 100)}}; // same mtime, different size

    DirectoryDiffer d(Direction::LocalToRemote, CompareStrategy::MtimeSize,
                      ConflictPolicy::Skip, false);
    EXPECT_TRUE(has(d.diff(local, remote), ActionType::Upload, "a.txt"));
}

TEST(SyncDiff, OneWayMtimeToleranceSkips)
{
    Listing local{{"a.txt", file(10, 101)}};
    Listing remote{{"a.txt", file(10, 100)}}; // 1s diff, within 2s tolerance

    DirectoryDiffer d(Direction::LocalToRemote, CompareStrategy::MtimeSize,
                      ConflictPolicy::Skip, false, /*tolerance=*/2);
    EXPECT_TRUE(has(d.diff(local, remote), ActionType::Skip, "a.txt"));
}

TEST(SyncDiff, OneWayDeleteOrphans)
{
    Listing local{{"a.txt", file(10, 100)}};
    Listing remote{{"a.txt", file(10, 100)}, {"orphan.txt", file(5, 50)}};

    DirectoryDiffer noDelete(Direction::LocalToRemote, CompareStrategy::MtimeSize,
                             ConflictPolicy::Skip, /*deleteOrphans=*/false);
    EXPECT_EQ(count(noDelete.diff(local, remote), ActionType::DeleteRemote), 0);

    DirectoryDiffer withDelete(Direction::LocalToRemote, CompareStrategy::MtimeSize,
                               ConflictPolicy::Skip, /*deleteOrphans=*/true);
    EXPECT_TRUE(has(withDelete.diff(local, remote), ActionType::DeleteRemote,
                    "orphan.txt"));
}

TEST(SyncDiff, RemoteToLocalMirrors)
{
    Listing local{{"a.txt", file(10, 100)}};
    Listing remote{{"a.txt", file(10, 100)}, {"new.txt", file(7, 70)}};

    DirectoryDiffer d(Direction::RemoteToLocal, CompareStrategy::MtimeSize,
                      ConflictPolicy::Skip, false);
    const auto actions = d.diff(local, remote);
    EXPECT_TRUE(has(actions, ActionType::Download, "new.txt"));
}

// ---- Two-way ---------------------------------------------------------------

TEST(SyncDiff, TwoWayNewFilesEachSide)
{
    Listing local{{"onlyLocal.txt", file(10, 100)}};
    Listing remote{{"onlyRemote.txt", file(20, 200)}};
    SyncState empty;

    DirectoryDiffer d(Direction::TwoWay, CompareStrategy::MtimeSize,
                      ConflictPolicy::NewerWins, false);
    const auto actions = d.diff(local, remote, empty);
    EXPECT_TRUE(has(actions, ActionType::Upload, "onlyLocal.txt"));
    EXPECT_TRUE(has(actions, ActionType::Download, "onlyRemote.txt"));
}

TEST(SyncDiff, TwoWaySecondRunIsClean)
{
    Listing local{{"a.txt", file(10, 100)}, {"b.txt", file(20, 200)}};
    Listing remote{{"a.txt", file(10, 100)}, {"b.txt", file(20, 200)}};
    // State captured after a prior successful sync.
    const SyncState state = buildStateFromListings(local, remote);

    DirectoryDiffer d(Direction::TwoWay, CompareStrategy::MtimeSize,
                      ConflictPolicy::NewerWins, false);
    const auto actions = d.diff(local, remote, state);
    EXPECT_EQ(count(actions, ActionType::Upload), 0);
    EXPECT_EQ(count(actions, ActionType::Download), 0);
    EXPECT_EQ(count(actions, ActionType::DeleteLocal), 0);
    EXPECT_EQ(count(actions, ActionType::DeleteRemote), 0);
}

TEST(SyncDiff, TwoWayLocalModifiedUploads)
{
    Listing prevLocal{{"a.txt", file(10, 100)}};
    Listing prevRemote{{"a.txt", file(10, 100)}};
    const SyncState state = buildStateFromListings(prevLocal, prevRemote);

    Listing local{{"a.txt", file(12, 150)}};  // local edited
    Listing remote{{"a.txt", file(10, 100)}}; // remote unchanged

    DirectoryDiffer d(Direction::TwoWay, CompareStrategy::MtimeSize,
                      ConflictPolicy::NewerWins, false);
    EXPECT_TRUE(has(d.diff(local, remote, state), ActionType::Upload, "a.txt"));
}

TEST(SyncDiff, TwoWayRemoteDeletedPropagates)
{
    Listing prevLocal{{"a.txt", file(10, 100)}};
    Listing prevRemote{{"a.txt", file(10, 100)}};
    const SyncState state = buildStateFromListings(prevLocal, prevRemote);

    Listing local{{"a.txt", file(10, 100)}}; // still present, unchanged
    Listing remote{};                        // deleted on remote

    DirectoryDiffer d(Direction::TwoWay, CompareStrategy::MtimeSize,
                      ConflictPolicy::NewerWins, false);
    EXPECT_TRUE(has(d.diff(local, remote, state), ActionType::DeleteLocal, "a.txt"));
}

TEST(SyncDiff, TwoWayBothModifiedConflict_SkipPolicy)
{
    Listing prevLocal{{"a.txt", file(10, 100)}};
    Listing prevRemote{{"a.txt", file(10, 100)}};
    const SyncState state = buildStateFromListings(prevLocal, prevRemote);

    Listing local{{"a.txt", file(11, 150)}};  // both edited differently
    Listing remote{{"a.txt", file(12, 160)}};

    DirectoryDiffer skip(Direction::TwoWay, CompareStrategy::MtimeSize,
                         ConflictPolicy::Skip, false);
    EXPECT_TRUE(has(skip.diff(local, remote, state), ActionType::Skip, "a.txt"));

    DirectoryDiffer prompt(Direction::TwoWay, CompareStrategy::MtimeSize,
                           ConflictPolicy::Prompt, false);
    EXPECT_TRUE(has(prompt.diff(local, remote, state), ActionType::Conflict, "a.txt"));
}

TEST(SyncDiff, TwoWayBothModifiedConflict_NewerWins)
{
    Listing prevLocal{{"a.txt", file(10, 100)}};
    Listing prevRemote{{"a.txt", file(10, 100)}};
    const SyncState state = buildStateFromListings(prevLocal, prevRemote);

    Listing local{{"a.txt", file(11, 150)}};   // local older
    Listing remote{{"a.txt", file(12, 999)}};  // remote newer

    DirectoryDiffer d(Direction::TwoWay, CompareStrategy::MtimeSize,
                      ConflictPolicy::NewerWins, false);
    EXPECT_TRUE(has(d.diff(local, remote, state), ActionType::Download, "a.txt"));
}

// ---- State persistence -----------------------------------------------------

TEST(SyncEngine, StateRoundTripsThroughJson)
{
    QTemporaryDir dir;
    Listing local{{"a.txt", file(10, 100)}};
    Listing remote{{"a.txt", file(10, 100)}, {"b.txt", file(20, 200)}};
    const SyncState state = buildStateFromListings(local, remote);

    const QString path = dir.filePath("state.json");
    ASSERT_TRUE(saveSyncState(path, state));
    const SyncState loaded = loadSyncState(path);

    ASSERT_TRUE(loaded.contains("a.txt"));
    EXPECT_TRUE(loaded.value("a.txt").local.present);
    EXPECT_TRUE(loaded.value("a.txt").remote.present);
    EXPECT_FALSE(loaded.value("b.txt").local.present);
    EXPECT_TRUE(loaded.value("b.txt").remote.present);
    EXPECT_EQ(loaded.value("b.txt").remote.entry.size, 20u);
}

// ---- End-to-end idempotency with two real directories ----------------------

TEST(SyncEngine, OneWaySyncThenSecondRunIsClean)
{
    QTemporaryDir localDir, remoteDir;
    // Seed the local side with two files and a subdir.
    QDir(localDir.path()).mkpath("sub");
    auto writeFile = [](const QString &path, const QByteArray &data) {
        QFile f(path);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write(data);
    };
    writeFile(localDir.filePath("a.txt"), "aaaa");
    writeFile(localDir.filePath("sub/b.txt"), "bbbbbb");

    // A local-to-local executor standing in for local<->remote.
    SyncCallbacks cb;
    cb.upload = [&](const QString &rel) {
        const QString dst = QDir(remoteDir.path()).filePath(rel);
        QDir().mkpath(QFileInfo(dst).absolutePath());
        return QFile::copy(QDir(localDir.path()).filePath(rel), dst);
    };
    cb.mkdirRemote = [&](const QString &rel) {
        return QDir().mkpath(QDir(remoteDir.path()).filePath(rel));
    };

    DirectoryDiffer d(Direction::LocalToRemote, CompareStrategy::MtimeSize,
                      ConflictPolicy::Skip, /*deleteOrphans=*/true);

    // First run: should upload both files (+ create the subdir).
    {
        const Listing local = enumerateLocalTree(localDir.path());
        const Listing remote = enumerateLocalTree(remoteDir.path());
        const auto actions = d.diff(local, remote);
        const SyncReport report = executeSync(actions, cb);
        EXPECT_EQ(report.failed, 0);
        EXPECT_EQ(report.uploaded, 2);
    }

    // Second run: nothing should change.
    {
        const Listing local = enumerateLocalTree(localDir.path());
        const Listing remote = enumerateLocalTree(remoteDir.path());
        const auto actions = d.diff(local, remote);
        EXPECT_EQ(count(actions, ActionType::Upload), 0);
        EXPECT_EQ(count(actions, ActionType::DeleteRemote), 0);
    }
}
