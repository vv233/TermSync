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

    int id = 0;
    Direction direction = Download;
    QString localPath;
    QString remotePath;
    QString displayName;
    quint64 size = 0;
    State state = Queued;
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

signals:
    void connected();
    void hostKeyFingerprint(const QString &fingerprint);
    void connectionFailed(const QString &reason);
    void directoryListed(const QString &path, const QVector<SftpEntry> &entries);
    void operationFinished(const QString &op, bool ok, const QString &message);
    void transferQueued(const TransferItem &item);
    void transferProgress(int id, quint64 done, quint64 total);
    void transferFinished(int id, bool ok, const QString &message);
    void syncListingReady(const QString &root, const sync::Listing &listing, bool ok);

private:
    QThread *m_thread = nullptr;
    SftpWorker *m_worker = nullptr;
    int m_nextId = 1;
};

} // namespace termsync::transfer

Q_DECLARE_METATYPE(termsync::transfer::TransferItem)
Q_DECLARE_METATYPE(termsync::transfer::SftpEntry)
Q_DECLARE_METATYPE(termsync::transfer::sync::Listing)
