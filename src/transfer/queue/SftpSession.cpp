#include "queue/SftpSession.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QRegularExpression>
#include <QMutex>
#include <QQueue>
#include <QRandomGenerator>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <memory>

#include "archive/TarArchive.h"
#include "ftp/FtpFileEngine.h"
#include "sftp/SftpFileEngine.h"

namespace termsync::transfer {

namespace {

// Single-quote a path for a POSIX shell command line.
QString shQuote(const QString &s)
{
    QString e = s;
    e.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + e + QLatin1Char('\'');
}

// Splits a remote path into (parent, base). Parent defaults to "." so a
// relative folder tars from the login directory.
void splitRemote(const QString &path, QString *parent, QString *base)
{
    QString p = path;
    while (p.size() > 1 && p.endsWith('/'))
        p.chop(1);
    const int slash = p.lastIndexOf('/');
    if (slash < 0) {
        *parent = QStringLiteral(".");
        *base = p;
    } else if (slash == 0) {
        *parent = QStringLiteral("/");
        *base = p.mid(1);
    } else {
        *parent = p.left(slash);
        *base = p.mid(slash + 1);
    }
}

QString randomTemp(const QString &suffix)
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("termsync-tar-%1%2")
                      .arg(QRandomGenerator::global()->generate(), 8, 16,
                           QLatin1Char('0'))
                      .arg(suffix));
}

// Parses one `ls -la --full-time` line into a FileEntry. Returns false for the
// "total N" header, blanks, or unparseable lines. Format:
//   drwxr-xr-x 2 root root 4096 2024-01-15 10:30:45.000000000 +0000 name
bool parseLsLine(const QString &line, SftpEntry *out)
{
    const QString l = line.trimmed();
    if (l.isEmpty() || l.startsWith(QStringLiteral("total ")))
        return false;
    // Split into at most 9 fields (perms..tz, then the name remainder).
    const QStringList f = l.split(QRegularExpression(QStringLiteral("\\s+")),
                                  Qt::SkipEmptyParts);
    if (f.size() < 9)
        return false;
    const QString perms = f[0];
    if (perms.size() < 10)
        return false;

    SftpEntry e;
    const QChar type = perms[0];
    e.isDirectory = type == QLatin1Char('d');
    e.isSymlink = type == QLatin1Char('l');
    e.size = f[4].toULongLong();
    e.modifiedAt = QDateTime::fromString(f[5] + ' ' + f[6].left(8),
                                         QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    // Name = fields from index 8 onward (perms,links,owner,group,size,date,
    // time,tz are 0..7). Rejoining tolerates spaces in names.
    QString name = f.mid(8).join(QLatin1Char(' '));
    if (e.isSymlink) {
        const int arrow = name.indexOf(QStringLiteral(" -> "));
        if (arrow >= 0)
            name = name.left(arrow);
        // Resolve directory-ness of the link target isn't known here; treat as
        // file unless the listing marks it (best-effort).
    }
    if (name.isEmpty())
        return false;
    e.name = name;
    e.permissions = 0; // not needed by the browser
    *out = e;
    return true;
}

// Maps the output of `cat /etc/os-release; uname -s` (and, as a last resort, the
// SSH banner) to a short OS id used for the host icon.
QString detectOsId(const QString &probe, const QString &banner)
{
    // Prefer the os-release ID= field (most specific).
    for (const QString &line : probe.split('\n')) {
        const QString l = line.trimmed();
        if (l.startsWith(QStringLiteral("ID="))) {
            QString id = l.mid(3).remove('"').remove('\'').toLower();
            static const QStringList known{
                "ubuntu", "debian",  "raspbian",  "fedora",    "rhel",
                "centos", "rocky",   "almalinux", "arch",      "alpine",
                "opensuse", "suse",  "linuxmint", "kali",      "freebsd"};
            for (const QString &k : known)
                if (id == k || id.startsWith(k))
                    return id.startsWith("opensuse") ? QStringLiteral("suse")
                           : id == "linuxmint"       ? QStringLiteral("mint")
                                                     : k;
            if (!id.isEmpty())
                return QStringLiteral("linux"); // some other distro
        }
    }
    const QString all = (probe + ' ' + banner);
    if (all.contains("Darwin", Qt::CaseInsensitive) ||
        all.contains("Mac", Qt::CaseInsensitive))
        return QStringLiteral("macos");
    if (all.contains("FreeBSD", Qt::CaseInsensitive))
        return QStringLiteral("freebsd");
    if (all.contains("Windows", Qt::CaseInsensitive))
        return QStringLiteral("windows");
    if (probe.contains("Linux", Qt::CaseInsensitive))
        return QStringLiteral("linux");
    return {};
}

} // namespace

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

        bool ok = false;
        m_keepaliveSecs = qEnvironmentVariableIntValue("TERMSYNC_SSH_KEEPALIVE_SECS", &ok);
        if (!ok)
            m_keepaliveSecs = 30;
        m_engine->setKeepaliveSeconds(m_keepaliveSecs);
    }

public slots:
    void doConnect()
    {
        auto verifier = [this](const QString &fp) -> bool {
            m_seenFingerprint = fp;
            // First contact (no stored key) trusts; otherwise require a match.
            return m_expected.isEmpty() || fp == m_expected;
        };
        // Busy-station retry: transient connect failures (refused / busy / reset)
        // retry with backoff, but auth / host-key rejections fail immediately.
        bool ok = false;
        int retries = qEnvironmentVariableIntValue("TERMSYNC_SSH_CONNECT_RETRIES", &ok);
        if (!ok)
            retries = 3;
        for (int attempt = 0;; ++attempt) {
            if (m_engine->connectToHost(m_params, verifier))
                break;
            const QString err = m_engine->lastError();
            const bool permanent = err.contains(QStringLiteral("authentication"), Qt::CaseInsensitive) ||
                                   err.contains(QStringLiteral("rejected"), Qt::CaseInsensitive);
            if (permanent || attempt >= retries) {
                emit connectionFailed(err);
                return;
            }
            QThread::msleep(static_cast<unsigned long>(qMin(1000 * (attempt + 1), 5000)));
        }
        emit hostKeyFingerprint(m_seenFingerprint);
        emit connected();

        // Probe the remote OS once (cheap, on the just-opened session) for the
        // host icon. Best-effort: ignore failures.
        {
            QString out;
            int ec = 0;
            if (m_engine->runCommand(
                    QStringLiteral(
                        "sh -c \"cat /etc/os-release 2>/dev/null; uname -s 2>/dev/null\""),
                    &out, &ec)) {
                const QString os = detectOsId(out, QString());
                if (!os.isEmpty())
                    emit osDetected(os);
            }
        }

        // Start the idle keepalive poll (runs on this worker thread).
        if (m_keepaliveSecs > 0 && !m_keepaliveTimer) {
            m_keepaliveTimer = new QTimer(this);
            m_keepaliveTimer->setInterval(m_keepaliveSecs * 1000);
            connect(m_keepaliveTimer, &QTimer::timeout, this, &SftpWorker::keepaliveTick);
            m_keepaliveTimer->start();
        }
    }

    void keepaliveTick()
    {
        // Only when idle; during a transfer the session is already active and the
        // event loop is blocked anyway.
        if (!m_busy)
            m_engine->keepalive();
    }

    void doSetSudo(bool enabled, const QString &password)
    {
        if (!enabled) {
            m_sudo = false;
            m_sudoPw.clear();
            emit sudoModeChanged(false, true, QString());
            return;
        }
        // Validate the password by caching sudo credentials (-v). -p '' silences
        // the prompt; -S reads the password from stdin (piped via echo).
        QString out;
        int ec = -1;
        m_sudoPw = password;
        const bool ran = m_engine->runCommand(
            QStringLiteral("echo %1 | sudo -S -p '' -v").arg(shQuote(password)),
            &out, &ec);
        if (ran && ec == 0) {
            m_sudo = true;
            emit sudoModeChanged(true, true, QString());
        } else {
            m_sudo = false;
            m_sudoPw.clear();
            emit sudoModeChanged(false, false,
                                 out.trimmed().isEmpty()
                                     ? QStringLiteral("sudo authentication failed")
                                     : out.trimmed());
        }
    }

    void doList(const QString &path)
    {
        if (m_sudo) {
            QString out;
            int ec = 0;
            if (runSudo(QStringLiteral("ls -la --full-time %1").arg(shQuote(path)),
                        &out, &ec) && ec == 0) {
                QVector<SftpEntry> entries;
                SftpEntry e;
                for (const QString &line : out.split('\n'))
                    if (parseLsLine(line, &e))
                        entries.append(e);
                emit directoryListed(path, entries);
            } else {
                emit operationFinished(QStringLiteral("list"), false,
                                       out.trimmed().isEmpty() ? m_engine->lastError()
                                                               : out.trimmed());
            }
            return;
        }
        QVector<SftpEntry> entries;
        if (m_engine->listDirectory(path, &entries))
            emit directoryListed(path, entries);
        else
            emit operationFinished(QStringLiteral("list"), false, m_engine->lastError());
    }

    void doMkdir(const QString &path)
    {
        bool ok;
        if (m_sudo) {
            int ec = 0;
            ok = runSudo(QStringLiteral("mkdir -p %1").arg(shQuote(path)), nullptr,
                         &ec) && ec == 0;
        } else {
            ok = m_engine->makeDirectory(path);
        }
        emit operationFinished(QStringLiteral("mkdir"), ok,
                               ok ? path : m_engine->lastError());
    }

    // Recursively delete a remote entry through the engine interface (works for
    // SFTP and FTP). SFTP rmdir only removes EMPTY directories, so a folder must
    // have its contents cleared first — otherwise deleting any non-empty folder
    // fails.
    bool removeRecursive(const QString &path, bool isDir)
    {
        if (!isDir)
            return m_engine->removeFile(path);
        QVector<SftpEntry> entries;
        if (!m_engine->listDirectory(path, &entries))
            return false;
        for (const SftpEntry &e : entries) {
            if (e.name == QLatin1String(".") || e.name == QLatin1String(".."))
                continue;
            const QString child =
                path.endsWith('/') ? path + e.name : path + '/' + e.name;
            // Don't recurse into a symlinked directory — just unlink the link.
            const bool childIsDir = e.isDirectory && !e.isSymlink;
            if (!removeRecursive(child, childIsDir))
                return false;
        }
        return m_engine->removeDirectory(path); // now empty
    }

    void doRemove(const QString &path, bool isDir)
    {
        bool ok;
        if (m_sudo) {
            int ec = 0;
            ok = runSudo(QStringLiteral("rm -rf %1").arg(shQuote(path)), nullptr,
                         &ec) && ec == 0;
        } else {
            ok = removeRecursive(path, isDir);
        }
        emit operationFinished(QStringLiteral("remove"), ok,
                               ok ? path : m_engine->lastError());
    }

    void doRename(const QString &from, const QString &to)
    {
        bool ok;
        if (m_sudo) {
            int ec = 0;
            ok = runSudo(QStringLiteral("mv %1 %2").arg(shQuote(from), shQuote(to)),
                         nullptr, &ec) && ec == 0;
        } else {
            ok = m_engine->rename(from, to);
        }
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
        if (item.kind == TransferItem::BulkDir) {
            ok = item.direction == TransferItem::Download ? doBulkDownload(item, progress)
                                                          : doBulkUpload(item, progress);
        } else if (item.direction == TransferItem::Download) {
            if (m_sudo)
                ok = m_engine->runCommandToFile(
                    sudoStreamCmd(QStringLiteral("cat %1").arg(shQuote(item.remotePath))),
                    item.localPath, progress, &m_cancelFlag);
            else
                ok = m_engine->downloadFile(item.remotePath, item.localPath, progress,
                                            &m_cancelFlag);
        } else {
            if (m_sudo) {
                // `sudo -n` relies on a credential cache that does NOT carry
                // across separate SSH exec channels, so a direct sudo upload
                // failed. Instead: upload to a user-writable temp via normal
                // SFTP, then self-authenticating `sudo cp` it into place (owned
                // by root), and remove the temp.
                const QString tmp =
                    QStringLiteral("/tmp/.termsync-sudo-%1")
                        .arg(QRandomGenerator::global()->generate(), 8, 16,
                             QLatin1Char('0'));
                ok = m_engine->uploadFile(item.localPath, tmp, progress,
                                          &m_cancelFlag);
                if (ok) {
                    int ec = 0;
                    ok = runSudo(QStringLiteral("cp %1 %2").arg(shQuote(tmp),
                                                               shQuote(item.remotePath)),
                                 nullptr, &ec) &&
                         ec == 0;
                    m_engine->removeFile(tmp); // best-effort cleanup (user owns it)
                }
            } else {
                ok = m_engine->uploadFile(item.localPath, item.remotePath, progress,
                                          &m_cancelFlag);
            }
        }

        const QString errMsg =
            ok ? QString()
               : (item.kind == TransferItem::BulkDir ? m_lastBulkError
                                                      : m_engine->lastError());
        emit transferFinished(item.id, ok, errMsg);

        m_busy = false;
        m_activeId = 0;
        // Continue with the next item, if any.
        QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
    }

private:
    // Runs `inner` under sudo, self-authenticating by piping the cached password
    // to `sudo -S`. For commands that do NOT read stdin (ls/mkdir/rm/mv/cat/tar-
    // download). Captures stdout+stderr into *out.
    bool runSudo(const QString &inner, QString *out, int *exitCode)
    {
        return m_engine->runCommand(
            QStringLiteral("echo %1 | sudo -S -p '' %2 2>&1")
                .arg(shQuote(m_sudoPw), inner),
            out, exitCode);
    }

    // Like runSudo but WITHOUT `2>&1` — for commands whose stdout is real data
    // streamed to a file (download / tar), so stderr never corrupts the stream.
    QString sudoStreamCmd(const QString &inner) const
    {
        return QStringLiteral("echo %1 | sudo -S -p '' %2").arg(shQuote(m_sudoPw), inner);
    }

    // Refreshes the sudo credential cache so a following `sudo -n` (used where the
    // command itself consumes stdin, e.g. uploads) succeeds without a prompt.
    bool sudoRefresh()
    {
        QString o;
        int ec = 0;
        return m_engine->runCommand(
                   QStringLiteral("echo %1 | sudo -S -p '' -v").arg(shQuote(m_sudoPw)),
                   &o, &ec) &&
               ec == 0;
    }

    // Download a whole remote folder as one `tar` stream, then extract locally.
    // Turns N per-file SFTP round-trips into a single streamed transfer.
    bool doBulkDownload(const TransferItem &item, const FileEngine::ProgressFn &progress)
    {
        QString parent, base;
        splitRemote(item.remotePath, &parent, &base);

        bool gzip = item.gzip == 1;
        if (item.gzip < 0) {
            // Detect "many small files": compress when the tree is dominated by
            // small files (compresses metadata + text well); stream large or
            // incompressible trees raw so we don't burn remote CPU for nothing.
            QString out;
            int ec = 0;
            const QString probe = QStringLiteral(
                "sh -c \"find %1 -type f 2>/dev/null | wc -l; "
                "find %1 -type f -size -64k 2>/dev/null | wc -l\"")
                .arg(shQuote(item.remotePath));
            if (m_engine->runCommand(probe, &out, &ec) && ec == 0) {
                const QStringList lines = out.split('\n', Qt::SkipEmptyParts);
                const int total = lines.value(0).trimmed().toInt();
                const int small = lines.value(1).trimmed().toInt();
                gzip = small >= 16 && small * 2 >= total;
            }
        }

        const QString archive = randomTemp(gzip ? QStringLiteral(".tgz")
                                                : QStringLiteral(".tar"));
        const QString tarInner =
            QStringLiteral("tar -C %1 -c%2f - %3")
                .arg(shQuote(parent), gzip ? QStringLiteral("z") : QString(),
                     shQuote(base));
        const QString cmd = m_sudo ? sudoStreamCmd(tarInner)
                                   : QStringLiteral("sh -c \"cd %1 && tar c%2f - %3\"")
                                         .arg(shQuote(parent),
                                              gzip ? QStringLiteral("z") : QString(),
                                              shQuote(base));

        int ec = 0;
        bool ok = m_engine->runCommandToFile(cmd, archive, progress, &m_cancelFlag, &ec);
        if (!ok) {
            m_lastBulkError = m_engine->lastError();
        } else {
            QString err;
            ok = archive::extractTarFile(archive, item.localPath, &err, progress,
                                         &m_cancelFlag);
            if (!ok)
                m_lastBulkError = err;
        }
        QFile::remove(archive);
        return ok;
    }

    // Bundle a local folder into one `tar` stream and unpack it on the server.
    bool doBulkUpload(const TransferItem &item, const FileEngine::ProgressFn &progress)
    {
        bool gzip = item.gzip == 1;
        if (item.gzip < 0) {
            int total = 0, small = 0;
            QDirIterator it(item.localPath,
                            QDir::Files | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                it.next();
                ++total;
                if (it.fileInfo().size() < 64 * 1024)
                    ++small;
                if (total > 4096) // enough of a sample to decide
                    break;
            }
            gzip = small >= 16 && small * 2 >= total;
        }

        const QString archive = randomTemp(gzip ? QStringLiteral(".tgz")
                                                : QStringLiteral(".tar"));
        QString err;
        if (!archive::createTarFile(item.localPath, item.displayName, archive, gzip,
                                    &err, progress, &m_cancelFlag)) {
            m_lastBulkError = err;
            QFile::remove(archive);
            return false;
        }

        QString cmd;
        if (m_sudo) {
            sudoRefresh(); // cache creds so `sudo -n` needs no prompt on the data stream
            cmd = QStringLiteral("sudo -n tar -C %1 -x%2f -")
                      .arg(shQuote(item.remotePath),
                           gzip ? QStringLiteral("z") : QString());
        } else {
            cmd = QStringLiteral("sh -c \"cd %1 && tar x%2f -\"")
                      .arg(shQuote(item.remotePath),
                           gzip ? QStringLiteral("z") : QString());
        }
        int ec = 0;
        const bool ok =
            m_engine->runCommandFromFile(cmd, archive, progress, &m_cancelFlag, &ec);
        if (!ok)
            m_lastBulkError = m_engine->lastError();
        QFile::remove(archive);
        return ok;
    }

signals:
    void connected();
    void osDetected(const QString &osId);
    void hostKeyFingerprint(const QString &fingerprint);
    void connectionFailed(const QString &reason);
    void directoryListed(const QString &path, const QVector<SftpEntry> &entries);
    void operationFinished(const QString &op, bool ok, const QString &message);
    void transferProgress(int id, quint64 done, quint64 total);
    void transferFinished(int id, bool ok, const QString &message);
    void syncListingReady(const QString &root, const sync::Listing &listing, bool ok);
    void sudoModeChanged(bool enabled, bool ok, const QString &message);

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
    int m_keepaliveSecs = 0;
    QTimer *m_keepaliveTimer = nullptr;
    QString m_lastBulkError; // message for a failed BulkDir transfer
    bool m_sudo = false;     // route operations through sudo
    QString m_sudoPw;        // sudo password (worker-thread only, not persisted)
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
    connect(m_worker, &SftpWorker::osDetected, this, &SftpSession::osDetected);
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
    connect(m_worker, &SftpWorker::sudoModeChanged, this,
            &SftpSession::sudoModeChanged);

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

int SftpSession::enqueueBulkDownload(const QString &remoteDir,
                                     const QString &localParentDir,
                                     const QString &displayName, int gzip)
{
    TransferItem item;
    item.kind = TransferItem::BulkDir;
    item.direction = TransferItem::Download;
    item.remotePath = remoteDir;       // remote folder to bundle
    item.localPath = localParentDir;   // local parent to extract into
    item.displayName = displayName;
    item.gzip = gzip;
    return enqueue(item);
}

int SftpSession::enqueueBulkUpload(const QString &localDir,
                                   const QString &remoteParentDir,
                                   const QString &displayName, int gzip)
{
    TransferItem item;
    item.kind = TransferItem::BulkDir;
    item.direction = TransferItem::Upload;
    item.localPath = localDir;         // local folder to bundle
    item.remotePath = remoteParentDir; // remote parent to unpack into
    item.displayName = displayName;
    item.gzip = gzip;
    return enqueue(item);
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

void SftpSession::setSudo(bool enabled, const QString &password)
{
    QMetaObject::invokeMethod(m_worker, "doSetSudo", Qt::QueuedConnection,
                              Q_ARG(bool, enabled), Q_ARG(QString, password));
}

} // namespace termsync::transfer

#include "SftpSession.moc"
