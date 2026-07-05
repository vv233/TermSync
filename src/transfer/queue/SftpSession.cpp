#include "queue/SftpSession.h"

#include <QMetaObject>
#include <QMutex>
#include <QQueue>
#include <QSet>
#include <QThread>
#include <atomic>
#include <memory>

#include "ftp/FtpFileEngine.h"
#include "sftp/SftpFileEngine.h"

namespace termsync::transfer {

// ---------------------------------------------------------------------------
// SftpWorker — owns the blocking SftpFileEngine on the worker thread.
// ---------------------------------------------------------------------------
class SftpWorker : public QObject
{
    Q_OBJECT

public:
    SftpWorker(const core::SshConnectionParams &params,
               const QString &expectedFingerprint, core::Protocol protocol)
        : m_params(params)
        , m_expected(expectedFingerprint)
    {
        if (protocol == core::Protocol::FTP || protocol == core::Protocol::FTPS) {
            auto ftp = std::make_unique<FtpFileEngine>();
            ftp->setExplicitTls(protocol == core::Protocol::FTPS);
            m_engine = std::move(ftp);
        } else {
            m_engine = std::make_unique<SftpFileEngine>();
        }
        // The interactive queue is resilient by default: park via the pause flag,
        // and reconnect-and-resume through brief network drops.
        m_engine->setPauseFlag(&m_pauseFlag);
        m_engine->setRelentless(true);
    }

public slots:
    void doConnect()
    {
        auto verifier = [this](const QString &fp) -> bool {
            m_seenFingerprint = fp;
            // First contact (no stored key) trusts; otherwise require a match.
            return m_expected.isEmpty() || fp == m_expected;
        };
        if (!m_engine->connectToHost(m_params, verifier)) {
            emit connectionFailed(m_engine->lastError());
            return;
        }
        emit hostKeyFingerprint(m_seenFingerprint);
        emit connected();
    }

    void doList(const QString &path)
    {
        QVector<SftpEntry> entries;
        if (m_engine->listDirectory(path, &entries))
            emit directoryListed(path, entries);
        else
            emit operationFinished(QStringLiteral("list"), false, m_engine->lastError());
    }

    void doMkdir(const QString &path)
    {
        const bool ok = m_engine->makeDirectory(path);
        emit operationFinished(QStringLiteral("mkdir"), ok,
                               ok ? path : m_engine->lastError());
    }

    void doRemove(const QString &path, bool isDir)
    {
        const bool ok = isDir ? m_engine->removeDirectory(path)
                              : m_engine->removeFile(path);
        emit operationFinished(QStringLiteral("remove"), ok,
                               ok ? path : m_engine->lastError());
    }

    void doRename(const QString &from, const QString &to)
    {
        const bool ok = m_engine->rename(from, to);
        emit operationFinished(QStringLiteral("rename"), ok,
                               ok ? to : m_engine->lastError());
    }

    void doChmod(const QString &path, quint32 mode)
    {
        const bool ok = m_engine->setPermissions(path, mode);
        emit operationFinished(QStringLiteral("chmod"), ok,
                               ok ? path : m_engine->lastError());
    }

    void doSyncListing(const QString &root)
    {
        sync::Listing listing;
        const bool ok = m_engine->listRecursive(root, &listing);
        emit syncListingReady(root, listing, ok);
    }

    void enqueue(const TransferItem &item)
    {
        {
            QMutexLocker locker(&m_queueMutex);
            m_queue.enqueue(item);
        }
        // Kick the pump (queued, so it runs on this thread between events).
        QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
    }

    void cancel(int id)
    {
        QMutexLocker locker(&m_queueMutex);
        m_cancelled.insert(id);
        if (id == m_activeId)
            m_cancelFlag.store(true);
    }

    void cancelAll()
    {
        QMutexLocker locker(&m_queueMutex);
        for (const TransferItem &it : m_queue)
            m_cancelled.insert(it.id);
        m_cancelFlag.store(true);
    }

    void pause(int id)
    {
        QMutexLocker locker(&m_queueMutex);
        if (id == m_activeId)
            m_pauseFlag.store(true);
    }

    void resume(int id)
    {
        QMutexLocker locker(&m_queueMutex);
        if (id == m_activeId)
            m_pauseFlag.store(false);
    }

    void pump()
    {
        if (m_busy)
            return;
        TransferItem item;
        {
            QMutexLocker locker(&m_queueMutex);
            while (!m_queue.isEmpty() && m_cancelled.contains(m_queue.head().id)) {
                const TransferItem skipped = m_queue.dequeue();
                emit transferFinished(skipped.id, false, QStringLiteral("Cancelled"));
            }
            if (m_queue.isEmpty())
                return;
            item = m_queue.dequeue();
            m_activeId = item.id;
            m_cancelFlag.store(false);
            m_pauseFlag.store(false); // a stale pause must not carry to the next item
        }
        m_busy = true;

        auto progress = [this, id = item.id](quint64 done, quint64 total) {
            emit transferProgress(id, done, total);
        };

        bool ok = false;
        if (item.direction == TransferItem::Download)
            ok = m_engine->downloadFile(item.remotePath, item.localPath, progress,
                                        &m_cancelFlag);
        else
            ok = m_engine->uploadFile(item.localPath, item.remotePath, progress,
                                      &m_cancelFlag);

        emit transferFinished(item.id, ok, ok ? QString() : m_engine->lastError());

        m_busy = false;
        m_activeId = 0;
        // Continue with the next item, if any.
        QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
    }

signals:
    void connected();
    void hostKeyFingerprint(const QString &fingerprint);
    void connectionFailed(const QString &reason);
    void directoryListed(const QString &path, const QVector<SftpEntry> &entries);
    void operationFinished(const QString &op, bool ok, const QString &message);
    void transferProgress(int id, quint64 done, quint64 total);
    void transferFinished(int id, bool ok, const QString &message);
    void syncListingReady(const QString &root, const sync::Listing &listing, bool ok);

private:
    core::SshConnectionParams m_params;
    QString m_expected;
    QString m_seenFingerprint;
    std::unique_ptr<FileEngine> m_engine;

    QMutex m_queueMutex;
    QQueue<TransferItem> m_queue;
    QSet<int> m_cancelled;
    std::atomic<bool> m_cancelFlag{false};
    std::atomic<bool> m_pauseFlag{false};
    int m_activeId = 0;
    bool m_busy = false;
};

// ---------------------------------------------------------------------------
// SftpSession — GUI-thread facade.
// ---------------------------------------------------------------------------
SftpSession::SftpSession(const core::SshConnectionParams &params,
                         const QString &expectedFingerprint,
                         core::Protocol protocol, QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<termsync::transfer::TransferItem>();
    qRegisterMetaType<termsync::transfer::SftpEntry>();
    qRegisterMetaType<QVector<termsync::transfer::SftpEntry>>();
    qRegisterMetaType<termsync::transfer::sync::Listing>();

    m_thread = new QThread(this);
    m_worker = new SftpWorker(params, expectedFingerprint, protocol);
    m_worker->moveToThread(m_thread);

    connect(m_worker, &SftpWorker::connected, this, &SftpSession::connected);
    connect(m_worker, &SftpWorker::hostKeyFingerprint, this,
            &SftpSession::hostKeyFingerprint);
    connect(m_worker, &SftpWorker::connectionFailed, this,
            &SftpSession::connectionFailed);
    connect(m_worker, &SftpWorker::directoryListed, this,
            &SftpSession::directoryListed);
    connect(m_worker, &SftpWorker::operationFinished, this,
            &SftpSession::operationFinished);
    connect(m_worker, &SftpWorker::transferProgress, this,
            &SftpSession::transferProgress);
    connect(m_worker, &SftpWorker::transferFinished, this,
            &SftpSession::transferFinished);
    connect(m_worker, &SftpWorker::syncListingReady, this,
            &SftpSession::syncListingReady);

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

SftpSession::~SftpSession()
{
    m_thread->quit();
    m_thread->wait();
}

int SftpSession::enqueue(TransferItem item)
{
    item.id = m_nextId++;
    item.state = TransferItem::Queued;
    emit transferQueued(item);
    QMetaObject::invokeMethod(m_worker, "enqueue", Qt::QueuedConnection,
                              Q_ARG(termsync::transfer::TransferItem, item));
    return item.id;
}

void SftpSession::connectToHost()
{
    QMetaObject::invokeMethod(m_worker, "doConnect", Qt::QueuedConnection);
}

void SftpSession::listDirectory(const QString &path)
{
    QMetaObject::invokeMethod(m_worker, "doList", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void SftpSession::makeDirectory(const QString &path)
{
    QMetaObject::invokeMethod(m_worker, "doMkdir", Qt::QueuedConnection,
                              Q_ARG(QString, path));
}

void SftpSession::removeEntry(const QString &path, bool isDir)
{
    QMetaObject::invokeMethod(m_worker, "doRemove", Qt::QueuedConnection,
                              Q_ARG(QString, path), Q_ARG(bool, isDir));
}

void SftpSession::renameEntry(const QString &from, const QString &to)
{
    QMetaObject::invokeMethod(m_worker, "doRename", Qt::QueuedConnection,
                              Q_ARG(QString, from), Q_ARG(QString, to));
}

void SftpSession::changePermissions(const QString &path, quint32 mode)
{
    QMetaObject::invokeMethod(m_worker, "doChmod", Qt::QueuedConnection,
                              Q_ARG(QString, path), Q_ARG(quint32, mode));
}

void SftpSession::cancel(int id)
{
    QMetaObject::invokeMethod(m_worker, "cancel", Qt::QueuedConnection,
                              Q_ARG(int, id));
}

void SftpSession::cancelAll()
{
    QMetaObject::invokeMethod(m_worker, "cancelAll", Qt::QueuedConnection);
}

void SftpSession::pause(int id)
{
    QMetaObject::invokeMethod(m_worker, "pause", Qt::QueuedConnection, Q_ARG(int, id));
}

void SftpSession::resume(int id)
{
    QMetaObject::invokeMethod(m_worker, "resume", Qt::QueuedConnection, Q_ARG(int, id));
}

void SftpSession::requestSyncListing(const QString &root)
{
    QMetaObject::invokeMethod(m_worker, "doSyncListing", Qt::QueuedConnection,
                              Q_ARG(QString, root));
}

} // namespace termsync::transfer

#include "SftpSession.moc"
