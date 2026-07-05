#pragma once

#include <QString>
#include <QVector>
#include <limits>

namespace termsync::transfer::schedule {

// A persisted transfer/sync job that runs on a repeating interval (or once).
// Carries everything needed to run headlessly, so a scheduler process can
// execute it without any UI or extra state.
struct ScheduledJob
{
    QString id;                 // unique key
    QString name;

    // Connection.
    QString protocol = "sftp";  // sftp | ftp | ftps
    QString host;
    int port = 22;
    QString user;
    QString password;           // (plaintext here; real deployments use the vault)
    QString keyPath;
    QString passphrase;

    // Action.
    QString command;            // put | get | sync
    QString localPath;
    QString remotePath;
    bool down = false;          // sync: remote -> local
    bool recursive = false;
    bool deleteOrphans = false; // sync: delete orphaned files on the target

    // Transfer options.
    bool resume = false;
    bool relentless = false;
    bool ascii = false;
    bool preservePerms = false;
    qint64 throttleKbps = 0;

    // Schedule / bookkeeping.
    qint64 intervalSecs = 0;    // repeat every N seconds; 0 = one-shot
    qint64 lastRun = 0;         // epoch seconds, 0 = never run
    bool enabled = true;

    // Epoch second at which this job is next due. A never-run job is due now (0);
    // a disabled or already-run one-shot is never due.
    qint64 nextRun() const
    {
        if (!enabled)
            return std::numeric_limits<qint64>::max();
        if (lastRun == 0)
            return 0; // never ran -> due immediately
        if (intervalSecs <= 0)
            return std::numeric_limits<qint64>::max(); // one-shot, already ran
        return lastRun + intervalSecs;
    }

    bool isDue(qint64 nowSecs) const { return nextRun() <= nowSecs; }
};

// Owns the job list and its JSON persistence. Pure logic (no network / no clock
// of its own) so it is fully unit-testable — the caller passes `now`.
class JobScheduler
{
public:
    bool load(const QString &filePath); // missing file => empty, returns true
    bool save(const QString &filePath) const;

    void add(const ScheduledJob &job);          // replaces an existing id
    bool remove(const QString &id);
    ScheduledJob *find(const QString &id);

    const QVector<ScheduledJob> &jobs() const { return m_jobs; }
    QVector<ScheduledJob> dueJobs(qint64 nowSecs) const;

    // Record that a job ran at `nowSecs` (updates lastRun; disables a one-shot).
    void markRan(const QString &id, qint64 nowSecs);

private:
    QVector<ScheduledJob> m_jobs;
};

} // namespace termsync::transfer::schedule
