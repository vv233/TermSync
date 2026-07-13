#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <functional>

#include "FileEngine.h"
#include "model/ConnectionProfile.h"
#include "ssh/SshConnection.h"
#include "sync/SyncTypes.h"

class QThread;

namespace termsync::transfer {

// A single queued file transfer.
struct TransferItem
{
    enum Direction { Upload, Download };
    enum State { Queued, Active, Done, Failed, Cancelled };
    // A regular per-file transfer, or a whole-directory bundle streamed through
    // one `tar` channel (fast for many small files; see enqueueBulkDir*).
    enum Kind { File, BulkDir };

    int id = 0;
    Direction direction = Download;
    Kind kind = File;
    QString localPath;
    QString remotePath;
    QString displayName;
    quint64 size = 0;
    State state = Queued;
    // BulkDir only: gzip tri-state (-1 auto-detect, 0 off, 1 on). Auto compresses
    // many-small-files trees and streams large/incompressible ones raw for speed.
    // localPath/remotePath name the *parent* dir the folder is extracted into /
    // archived from.
    int gzip = -1;
};

class SftpWorker; // internal

// GUI-thread facade over an SftpFileEngine running on a dedicated worker
// thread. Serializes all remote operations (list / mkdir / remove / rename /
// chmod) and file transfers over one SFTP connection, reporting results and
// progress via queued signals so the UI never blocks on the network.
//
// Host-key policy is kept off the worker's GUI-unsafe thread: the caller passes
// the previously-trusted fingerprint (empty = first contact, auto-trust and
// report back so the caller can persist it).
class SftpSession : public QObject
{
    Q_OBJECT

public:
    SftpSession(const core::SshConnectionParams &params,
                const QString &expectedFingerprint,
                core::Protocol protocol = core::Protocol::SSH2,
                QObject *parent = nullptr);
    ~SftpSession() override;

    // Queues a transfer and returns its assigned id.
    int enqueue(TransferItem item);

    // Queues a whole-folder transfer that bundles the tree through a single
    // `tar` stream instead of one SFTP round-trip per file — the fast path for
    // directories with many small files. Returns the transfer id (progress and
    // completion arrive via the usual transferQueued/Progress/Finished signals).
    // `localParentDir` is the local folder the remote tree is extracted into;
    // `remoteParentDir` is the remote folder a local tree is unpacked into.
    // gzip < 0 = auto-detect (compress when the tree is many small files).
    int enqueueBulkDownload(const QString &remoteDir, const QString &localParentDir,
                            const QString &displayName, int gzip = -1);
    int enqueueBulkUpload(const QString &localDir, const QString &remoteParentDir,
                          const QString &displayName, int gzip = -1);

public slots:
    void connectToHost();
    void listDirectory(const QString &path);
    void makeDirectory(const QString &path);
    void removeEntry(const QString &path, bool isDir);
    void renameEntry(const QString &from, const QString &to);
    void changePermissions(const QString &path, quint32 mode);
    void cancel(int id);
    void cancelAll();
    // Park / unpark the active transfer (no effect on queued items until active).
    void pause(int id);
    void resume(int id);
    // Recursively enumerates a remote tree for the sync engine; the result
    // arrives via syncListingReady.
    void requestSyncListing(const QString &root);

    // Privilege escalation: when enabled, list / transfer / mkdir / delete /
    // rename run through `sudo` so root-owned files are accessible. The password
    // is validated against `sudo` and kept only in the worker (never persisted).
    // The result arrives via sudoModeChanged.
    void setSudo(bool enabled, const QString &password);

signals:
    void connected();
    // The detected remote OS id (e.g. "ubuntu", "debian", "windows"), probed
    // once after connect; empty if it couldn't be determined.
    void osDetected(const QString &osId);
    void hostKeyFingerprint(const QString &fingerprint);
    void connectionFailed(const QString &reason);
    void directoryListed(const QString &path, const QVector<SftpEntry> &entries);
    void operationFinished(const QString &op, bool ok, const QString &message);
    void transferQueued(const TransferItem &item);
    void transferProgress(int id, quint64 done, quint64 total);
    void transferFinished(int id, bool ok, const QString &message);
    void syncListingReady(const QString &root, const sync::Listing &listing, bool ok);
    // enabled = whether sudo mode is now active; ok/message report validation.
    void sudoModeChanged(bool enabled, bool ok, const QString &message);

private:
    QThread *m_thread = nullptr;
    SftpWorker *m_worker = nullptr;
    int m_nextId = 1;
};

} // namespace termsync::transfer

Q_DECLARE_METATYPE(termsync::transfer::TransferItem)
Q_DECLARE_METATYPE(termsync::transfer::SftpEntry)
Q_DECLARE_METATYPE(termsync::transfer::sync::Listing)
