#include "sftp/SftpFileEngine.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <algorithm>
#include <thread>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace termsync::transfer {

namespace {

void ensureGlobalInit()
{
    static QMutex mutex;
    static bool done = false;
    QMutexLocker locker(&mutex);
    if (done)
        return;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    libssh2_init(0);
    done = true;
}

socket_t storedSocket(quintptr value)
{
    return static_cast<socket_t>(value);
}

void closeNativeSocket(socket_t socket)
{
    if (socket == kInvalidSocket)
        return;
#ifdef _WIN32
    ::closesocket(socket);
#else
    ::close(socket);
#endif
}

bool isDir(unsigned long permissions)
{
    return (permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR;
}

bool isSymlink(unsigned long permissions)
{
    return (permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFLNK;
}

// libssh2_sftp_read uses buffer_size * 4 for optimistic read-ahead.
qsizetype transferBufferSize()
{
    bool ok = false;
    int kb = qEnvironmentVariableIntValue("TERMSYNC_SFTP_BUFFER_KB", &ok);
    if (!ok)
        kb = 256;
    kb = std::clamp(kb, 32, 4096);
    return static_cast<qsizetype>(kb) * 1024;
}

constexpr quint64 kParallelTransferThreshold = 64ull * 1024ull * 1024ull;

bool isCancelled(const std::atomic<bool> *cancel, const std::atomic<bool> *stop = nullptr)
{
    return (cancel && cancel->load()) || (stop && stop->load());
}

int parallelTransferCount(quint64 bytes)
{
    bool ok = false;
    int configured = qEnvironmentVariableIntValue("TERMSYNC_SFTP_PARALLEL", &ok);
    if (ok)
        return std::clamp(configured, 1, 16);

    Q_UNUSED(bytes);
    const int hardware = static_cast<int>((std::max)(1u, std::thread::hardware_concurrency()));
    return (std::min)(4, hardware);
}

quint64 progressStep(quint64 total)
{
    if (!total)
        return 1024ull * 1024ull;
    return qMax(1024ull * 1024ull, total / 200ull);
}

void reportProgress(quint64 current, quint64 total, SftpFileEngine::ProgressFn progress,
                    quint64 *lastReported)
{
    if (!progress || !lastReported)
        return;
    const quint64 step = progressStep(total);
    if (current != total && current < *lastReported + step)
        return;
    *lastReported = current;
    progress(current, total ? total : current);
}

void reportProgress(quint64 current, quint64 total, SftpFileEngine::ProgressFn progress,
                    std::atomic<quint64> *reported)
{
    if (!progress || !reported)
        return;
    const quint64 step = progressStep(total);
    quint64 previous = reported->load();
    for (;;) {
        if (current != total && current < previous + step)
            return;
        if (reported->compare_exchange_weak(previous, current))
            break;
    }
    progress(current, total ? total : current);
}

} // namespace

SftpFileEngine::SftpFileEngine()
{
    ensureGlobalInit();
}

SftpFileEngine::~SftpFileEngine()
{
    disconnectFromHost();
}

bool SftpFileEngine::connectToHost(const core::SshConnectionParams &params,
                                   HostKeyVerifier verifier)
{
    disconnectFromHost();
    m_lastError.clear();

    if (!openSocket(params.host, params.port))
        return false;

    auto *session = libssh2_session_init();
    if (!session) {
        setError(QStringLiteral("Failed to create SSH session"));
        disconnectFromHost();
        return false;
    }
    m_session = session;
    libssh2_session_set_blocking(session, 1);

    if (libssh2_session_handshake(session, static_cast<libssh2_socket_t>(storedSocket(m_socket)))) {
        setError(QStringLiteral("SSH handshake failed"));
        disconnectFromHost();
        return false;
    }

    emitFingerprint();
    if (verifier && !verifier(m_hostKeyFingerprint)) {
        setError(QStringLiteral("Host key rejected"));
        disconnectFromHost();
        return false;
    }

    if (!authenticate(params)) {
        disconnectFromHost();
        return false;
    }

    auto *sftp = libssh2_sftp_init(session);
    if (!sftp) {
        setError(QStringLiteral("Failed to initialise SFTP subsystem"));
        disconnectFromHost();
        return false;
    }
    m_sftp = sftp;
    m_params = params;
    return true;
}

void SftpFileEngine::disconnectFromHost()
{
    if (auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp)) {
        libssh2_sftp_shutdown(sftp);
        m_sftp = nullptr;
    }
    if (auto *session = static_cast<LIBSSH2_SESSION *>(m_session)) {
        libssh2_session_disconnect(session, "Client disconnecting");
        libssh2_session_free(session);
        m_session = nullptr;
    }
    closeSocket();
}

bool SftpFileEngine::isConnected() const
{
    return m_session && m_sftp;
}

bool SftpFileEngine::listDirectory(const QString &remotePath, QVector<SftpEntry> *entries)
{
    if (!entries) {
        setError(QStringLiteral("entries output pointer is null"));
        return false;
    }
    entries->clear();
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    const QByteArray path = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *dir =
        libssh2_sftp_opendir(sftp, path.isEmpty() ? "." : path.constData());
    if (!dir) {
        setError(QStringLiteral("Could not open remote directory: %1").arg(remotePath));
        return false;
    }

    for (;;) {
        char name[512];
        char longName[1024];
        LIBSSH2_SFTP_ATTRIBUTES attrs{};
        const int rc = libssh2_sftp_readdir_ex(dir, name, sizeof(name),
                                               longName, sizeof(longName), &attrs);
        if (rc > 0) {
            SftpEntry e;
            e.name = QString::fromUtf8(name, rc);
            e.longName = QString::fromUtf8(longName);
            if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)
                e.size = attrs.filesize;
            if (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
                e.permissions = attrs.permissions;
                e.isDirectory = isDir(attrs.permissions);
                e.isSymlink = isSymlink(attrs.permissions);
            }
            if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME)
                e.modifiedAt = QDateTime::fromSecsSinceEpoch(attrs.mtime);
            entries->append(e);
            continue;
        }
        if (rc == 0)
            break;
        libssh2_sftp_closedir(dir);
        setError(QStringLiteral("Failed while reading remote directory: %1").arg(remotePath));
        return false;
    }

    libssh2_sftp_closedir(dir);
    return true;
}

bool SftpFileEngine::downloadFile(const QString &remotePath, const QString &localPath,
                                  ProgressFn progress, const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    quint64 total = 0;
    statSize(remotePath, &total); // best-effort, for progress reporting
    if (total >= kParallelTransferThreshold && !m_params.host.isEmpty()) {
        if (downloadFileParallel(remotePath, localPath, total, progress, cancel))
            return true;
        return false;
    }

    return downloadFileSequential(remotePath, localPath, total, progress, cancel);
}

bool SftpFileEngine::downloadFileSequential(const QString &remotePath, const QString &localPath,
                                            quint64 total, ProgressFn progress,
                                            const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }
    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile =
        libssh2_sftp_open(sftp, remote.constData(), LIBSSH2_FXF_READ, 0);
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file: %1").arg(remotePath));
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        libssh2_sftp_close(remoteFile);
        setError(local.errorString());
        return false;
    }

    quint64 done = 0;
    quint64 reported = 0;
    QByteArray buffer(transferBufferSize(), Qt::Uninitialized);
    for (;;) {
        if (isCancelled(cancel)) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const ssize_t rc = libssh2_sftp_read(remoteFile, buffer.data(), buffer.size());
        if (rc > 0) {
            if (local.write(buffer.constData(), rc) != rc) {
                libssh2_sftp_close(remoteFile);
                setError(local.errorString());
                return false;
            }
            done += static_cast<quint64>(rc);
            reportProgress(done, total, progress, &reported);
            continue;
        }
        if (rc == 0)
            break;
        libssh2_sftp_close(remoteFile);
        setError(QStringLiteral("Failed while downloading: %1").arg(remotePath));
        return false;
    }

    libssh2_sftp_close(remoteFile);
    reportProgress(done, total ? total : done, progress, &reported);
    return true;
}

bool SftpFileEngine::uploadFile(const QString &localPath, const QString &remotePath,
                                ProgressFn progress, const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        setError(local.errorString());
        return false;
    }
    const quint64 total = static_cast<quint64>(local.size());
    local.close();

    if (total >= kParallelTransferThreshold && !m_params.host.isEmpty()) {
        if (uploadFileParallel(localPath, remotePath, total, progress, cancel))
            return true;
        return false;
    }

    return uploadFileSequential(localPath, remotePath, total, progress, cancel);
}

bool SftpFileEngine::uploadFileSequential(const QString &localPath, const QString &remotePath,
                                          quint64 total, ProgressFn progress,
                                          const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        setError(local.errorString());
        return false;
    }
    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile = libssh2_sftp_open(
        sftp, remote.constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
            LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file for writing: %1").arg(remotePath));
        return false;
    }

    quint64 done = 0;
    quint64 reported = 0;
    while (!local.atEnd()) {
        if (isCancelled(cancel)) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const QByteArray chunk = local.read(transferBufferSize());
        const char *ptr = chunk.constData();
        qsizetype remaining = chunk.size();
        while (remaining > 0) {
            const ssize_t written = libssh2_sftp_write(remoteFile, ptr, remaining);
            if (written < 0) {
                libssh2_sftp_close(remoteFile);
                setError(QStringLiteral("Failed while uploading: %1").arg(remotePath));
                return false;
            }
            ptr += written;
            remaining -= written;
            done += static_cast<quint64>(written);
        }
        reportProgress(done, total, progress, &reported);
    }

    libssh2_sftp_close(remoteFile);
    reportProgress(done, total, progress, &reported);
    return true;
}

bool SftpFileEngine::downloadFileParallel(const QString &remotePath, const QString &localPath,
                                          quint64 total, ProgressFn progress,
                                          const std::atomic<bool> *cancel)
{
    const int workers = parallelTransferCount(total);
    if (workers <= 1)
        return downloadFileSequential(remotePath, localPath, total, progress, cancel);

    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(local.errorString());
        return false;
    }
    if (!local.resize(static_cast<qint64>(total))) {
        setError(local.errorString());
        return false;
    }
    local.close();

    std::atomic<quint64> done{0};
    std::atomic<quint64> reported{0};
    std::atomic<bool> stop{false};
    QMutex errorMutex;
    QString firstError;
    std::vector<std::thread> threads;
    threads.reserve(workers);

    const quint64 span = (total + static_cast<quint64>(workers) - 1) /
                         static_cast<quint64>(workers);
    for (int i = 0; i < workers; ++i) {
        const quint64 offset = span * static_cast<quint64>(i);
        if (offset >= total)
            break;
        const quint64 length = qMin(span, total - offset);
        threads.emplace_back([&, offset, length] {
            SftpFileEngine engine;
            if (!connectSibling(&engine)) {
                QMutexLocker locker(&errorMutex);
                if (firstError.isEmpty())
                    firstError = engine.lastError();
                stop.store(true);
                return;
            }
            if (!engine.downloadRange(remotePath, localPath, offset, length, &done,
                                      &reported, total, progress, cancel, &stop)) {
                QMutexLocker locker(&errorMutex);
                if (firstError.isEmpty())
                    firstError = engine.lastError();
                stop.store(true);
            }
        });
    }

    for (auto &thread : threads)
        thread.join();

    if (isCancelled(cancel)) {
        setError(QStringLiteral("Cancelled"));
        return false;
    }
    if (!firstError.isEmpty()) {
        setError(firstError);
        return false;
    }
    if (progress)
        reportProgress(total, total, progress, &reported);
    return true;
}

bool SftpFileEngine::uploadFileParallel(const QString &localPath, const QString &remotePath,
                                        quint64 total, ProgressFn progress,
                                        const std::atomic<bool> *cancel)
{
    const int workers = parallelTransferCount(total);
    if (workers <= 1)
        return uploadFileSequential(localPath, remotePath, total, progress, cancel);

    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile = libssh2_sftp_open(
        sftp, remote.constData(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
            LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file for writing: %1").arg(remotePath));
        return false;
    }
    libssh2_sftp_close(remoteFile);

    std::atomic<quint64> done{0};
    std::atomic<quint64> reported{0};
    std::atomic<bool> stop{false};
    QMutex errorMutex;
    QString firstError;
    std::vector<std::thread> threads;
    threads.reserve(workers);

    const quint64 span = (total + static_cast<quint64>(workers) - 1) /
                         static_cast<quint64>(workers);
    for (int i = 0; i < workers; ++i) {
        const quint64 offset = span * static_cast<quint64>(i);
        if (offset >= total)
            break;
        const quint64 length = qMin(span, total - offset);
        threads.emplace_back([&, offset, length] {
            SftpFileEngine engine;
            if (!connectSibling(&engine)) {
                QMutexLocker locker(&errorMutex);
                if (firstError.isEmpty())
                    firstError = engine.lastError();
                stop.store(true);
                return;
            }
            if (!engine.uploadRange(localPath, remotePath, offset, length, &done,
                                    &reported, total, progress, cancel, &stop)) {
                QMutexLocker locker(&errorMutex);
                if (firstError.isEmpty())
                    firstError = engine.lastError();
                stop.store(true);
            }
        });
    }

    for (auto &thread : threads)
        thread.join();

    if (isCancelled(cancel)) {
        setError(QStringLiteral("Cancelled"));
        return false;
    }
    if (!firstError.isEmpty()) {
        setError(firstError);
        return false;
    }

    quint64 remoteSize = 0;
    if (!statSize(remotePath, &remoteSize) || remoteSize != total) {
        setError(QStringLiteral("Remote size mismatch after parallel upload: %1").arg(remotePath));
        return false;
    }
    if (progress)
        reportProgress(total, total, progress, &reported);
    return true;
}

bool SftpFileEngine::downloadRange(const QString &remotePath, const QString &localPath,
                                   quint64 offset, quint64 length,
                                   std::atomic<quint64> *done,
                                   std::atomic<quint64> *reported, quint64 total,
                                   ProgressFn progress, const std::atomic<bool> *cancel,
                                   const std::atomic<bool> *stop)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile =
        libssh2_sftp_open(sftp, remote.constData(), LIBSSH2_FXF_READ, 0);
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file: %1").arg(remotePath));
        return false;
    }
    libssh2_sftp_seek64(remoteFile, static_cast<libssh2_uint64_t>(offset));

    QFile local(localPath);
    if (!local.open(QIODevice::ReadWrite) || !local.seek(static_cast<qint64>(offset))) {
        libssh2_sftp_close(remoteFile);
        setError(local.errorString());
        return false;
    }

    QByteArray buffer(transferBufferSize(), Qt::Uninitialized);
    quint64 remaining = length;
    while (remaining > 0) {
        if (isCancelled(cancel, stop)) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const qsizetype want = static_cast<qsizetype>(
            qMin<quint64>(static_cast<quint64>(buffer.size()), remaining));
        const ssize_t rc = libssh2_sftp_read(remoteFile, buffer.data(), want);
        if (rc > 0) {
            if (local.write(buffer.constData(), rc) != rc) {
                libssh2_sftp_close(remoteFile);
                setError(local.errorString());
                return false;
            }
            remaining -= static_cast<quint64>(rc);
            const quint64 current = done->fetch_add(static_cast<quint64>(rc)) +
                                    static_cast<quint64>(rc);
            reportProgress(qMin(current, total), total, progress, reported);
            continue;
        }
        libssh2_sftp_close(remoteFile);
        setError(rc == 0 ? QStringLiteral("Unexpected EOF while downloading: %1").arg(remotePath)
                         : QStringLiteral("Failed while downloading: %1").arg(remotePath));
        return false;
    }

    libssh2_sftp_close(remoteFile);
    return true;
}

bool SftpFileEngine::uploadRange(const QString &localPath, const QString &remotePath,
                                 quint64 offset, quint64 length,
                                 std::atomic<quint64> *done,
                                 std::atomic<quint64> *reported, quint64 total,
                                 ProgressFn progress, const std::atomic<bool> *cancel,
                                 const std::atomic<bool> *stop)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly) || !local.seek(static_cast<qint64>(offset))) {
        setError(local.errorString());
        return false;
    }

    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE *remoteFile =
        libssh2_sftp_open(sftp, remote.constData(), LIBSSH2_FXF_WRITE, 0);
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file for writing: %1").arg(remotePath));
        return false;
    }
    libssh2_sftp_seek64(remoteFile, static_cast<libssh2_uint64_t>(offset));

    quint64 remaining = length;
    while (remaining > 0) {
        if (isCancelled(cancel, stop)) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const qint64 want = static_cast<qint64>(
            qMin<quint64>(static_cast<quint64>(transferBufferSize()), remaining));
        const QByteArray chunk = local.read(want);
        if (chunk.isEmpty()) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Unexpected EOF while uploading: %1").arg(localPath));
            return false;
        }
        const char *ptr = chunk.constData();
        qsizetype chunkRemaining = chunk.size();
        while (chunkRemaining > 0) {
            const ssize_t written = libssh2_sftp_write(remoteFile, ptr, chunkRemaining);
            if (written < 0) {
                libssh2_sftp_close(remoteFile);
                setError(QStringLiteral("Failed while uploading: %1").arg(remotePath));
                return false;
            }
            ptr += written;
            chunkRemaining -= written;
            remaining -= static_cast<quint64>(written);
            const quint64 current = done->fetch_add(static_cast<quint64>(written)) +
                                    static_cast<quint64>(written);
            reportProgress(qMin(current, total), total, progress, reported);
        }
    }

    libssh2_sftp_close(remoteFile);
    return true;
}

bool SftpFileEngine::connectSibling(SftpFileEngine *engine) const
{
    if (!engine)
        return false;
    const QString expected = m_hostKeyFingerprint;
    return engine->connectToHost(m_params, [expected](const QString &fp) {
        return expected.isEmpty() || fp == expected;
    });
}

bool SftpFileEngine::makeDirectory(const QString &remotePath)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray path = remotePath.toUtf8();
    if (libssh2_sftp_mkdir(sftp, path.constData(),
                           LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP |
                               LIBSSH2_SFTP_S_IXGRP | LIBSSH2_SFTP_S_IROTH |
                               LIBSSH2_SFTP_S_IXOTH) != 0) {
        setError(QStringLiteral("Could not create directory: %1").arg(remotePath));
        return false;
    }
    return true;
}

bool SftpFileEngine::removeFile(const QString &remotePath)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray path = remotePath.toUtf8();
    if (libssh2_sftp_unlink(sftp, path.constData()) != 0) {
        setError(QStringLiteral("Could not delete file: %1").arg(remotePath));
        return false;
    }
    return true;
}

bool SftpFileEngine::removeDirectory(const QString &remotePath)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray path = remotePath.toUtf8();
    if (libssh2_sftp_rmdir(sftp, path.constData()) != 0) {
        setError(QStringLiteral("Could not remove directory: %1").arg(remotePath));
        return false;
    }
    return true;
}

bool SftpFileEngine::rename(const QString &fromPath, const QString &toPath)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray from = fromPath.toUtf8();
    const QByteArray to = toPath.toUtf8();
    if (libssh2_sftp_rename(sftp, from.constData(), to.constData()) != 0) {
        setError(QStringLiteral("Could not rename %1 to %2").arg(fromPath, toPath));
        return false;
    }
    return true;
}

bool SftpFileEngine::setPermissions(const QString &remotePath, quint32 mode)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray path = remotePath.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    attrs.permissions = mode;
    if (libssh2_sftp_setstat(sftp, path.constData(), &attrs) != 0) {
        setError(QStringLiteral("Could not change permissions: %1").arg(remotePath));
        return false;
    }
    return true;
}

bool SftpFileEngine::statSize(const QString &remotePath, quint64 *size)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp || !size)
        return false;
    const QByteArray path = remotePath.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attrs{};
    if (libssh2_sftp_stat(sftp, path.constData(), &attrs) != 0)
        return false;
    if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) {
        *size = attrs.filesize;
        return true;
    }
    return false;
}

bool SftpFileEngine::openSocket(const QString &hostName, quint16 portNumber)
{
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    const QByteArray host = hostName.toUtf8();
    const QByteArray port = QByteArray::number(portNumber);
    struct addrinfo *res = nullptr;
    if (getaddrinfo(host.constData(), port.constData(), &hints, &res) != 0 || !res) {
        setError(QStringLiteral("Could not resolve %1").arg(hostName));
        return false;
    }

    socket_t sock = kInvalidSocket;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == kInvalidSocket)
            continue;
        if (::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0)
            break;
        closeNativeSocket(sock);
        sock = kInvalidSocket;
    }
    freeaddrinfo(res);

    if (sock == kInvalidSocket) {
        setError(QStringLiteral("Could not connect to %1:%2").arg(hostName).arg(portNumber));
        return false;
    }
    m_socket = static_cast<quintptr>(sock);
    return true;
}

bool SftpFileEngine::authenticate(const core::SshConnectionParams &params)
{
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const QByteArray user = params.username.toUtf8();

    bool ok = false;
    switch (params.authMethod) {
    case core::SshAuthMethod::PublicKey: {
        const QByteArray priv = params.privateKeyPath.toUtf8();
        const QByteArray pub = (params.privateKeyPath + ".pub").toUtf8();
        const QByteArray phrase = params.passphrase.toUtf8();
        const bool havePub = QFileInfo::exists(params.privateKeyPath + ".pub");
        ok = libssh2_userauth_publickey_fromfile(
                 session, user.constData(), havePub ? pub.constData() : nullptr,
                 priv.constData(),
                 phrase.isEmpty() ? nullptr : phrase.constData()) == 0;
        break;
    }
    case core::SshAuthMethod::Agent: {
        LIBSSH2_AGENT *agent = libssh2_agent_init(session);
        if (agent && libssh2_agent_connect(agent) == 0 &&
            libssh2_agent_list_identities(agent) == 0) {
            struct libssh2_agent_publickey *identity = nullptr;
            while (libssh2_agent_get_identity(agent, &identity, identity) == 0) {
                if (libssh2_agent_userauth(agent, user.constData(), identity) == 0) {
                    ok = true;
                    break;
                }
            }
        }
        if (agent) {
            libssh2_agent_disconnect(agent);
            libssh2_agent_free(agent);
        }
        break;
    }
    case core::SshAuthMethod::Password:
    case core::SshAuthMethod::KeyboardInteractive:
    default: {
        const QByteArray pass = params.password.toUtf8();
        ok = libssh2_userauth_password(session, user.constData(),
                                       pass.constData()) == 0;
        break;
    }
    }

    if (!ok)
        setError(QStringLiteral("Authentication failed"));
    return ok;
}

bool SftpFileEngine::scpDownload(const QString &remotePath, const QString &localPath,
                                 ProgressFn progress, const std::atomic<bool> *cancel)
{
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (!session) { setError(QStringLiteral("Not connected")); return false; }

    libssh2_struct_stat st{};
    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_CHANNEL *channel = libssh2_scp_recv2(session, remote.constData(), &st);
    if (!channel) {
        setError(QStringLiteral("SCP: could not open remote file: %1").arg(remotePath));
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        libssh2_channel_free(channel);
        setError(local.errorString());
        return false;
    }

    const quint64 total = static_cast<quint64>(st.st_size);
    quint64 got = 0;
    char buf[16384];
    while (got < total) {
        if (cancel && cancel->load()) {
            libssh2_channel_free(channel);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        qint64 want = static_cast<qint64>(sizeof(buf));
        if (total - got < static_cast<quint64>(want))
            want = static_cast<qint64>(total - got);
        const ssize_t n = libssh2_channel_read(channel, buf, want);
        if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
            libssh2_channel_free(channel);
            setError(QStringLiteral("SCP read error"));
            return false;
        }
        if (n > 0) {
            local.write(buf, n);
            got += static_cast<quint64>(n);
            if (progress)
                progress(got, total);
        }
    }
    libssh2_channel_send_eof(channel);
    libssh2_channel_free(channel);
    return true;
}

bool SftpFileEngine::scpUpload(const QString &localPath, const QString &remotePath,
                               quint32 mode, ProgressFn progress,
                               const std::atomic<bool> *cancel)
{
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (!session) { setError(QStringLiteral("Not connected")); return false; }

    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        setError(local.errorString());
        return false;
    }
    const quint64 total = static_cast<quint64>(local.size());

    const QByteArray remote = remotePath.toUtf8();
    LIBSSH2_CHANNEL *channel = libssh2_scp_send64(
        session, remote.constData(), mode & 0777,
        static_cast<libssh2_int64_t>(total), 0, 0);
    if (!channel) {
        setError(QStringLiteral("SCP: could not create remote file: %1").arg(remotePath));
        return false;
    }

    quint64 sent = 0;
    while (!local.atEnd()) {
        if (cancel && cancel->load()) {
            libssh2_channel_free(channel);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const QByteArray chunk = local.read(16384);
        const char *ptr = chunk.constData();
        qsizetype remaining = chunk.size();
        while (remaining > 0) {
            const ssize_t n = libssh2_channel_write(channel, ptr, remaining);
            if (n == LIBSSH2_ERROR_EAGAIN)
                continue;
            if (n < 0) {
                libssh2_channel_free(channel);
                setError(QStringLiteral("SCP write error"));
                return false;
            }
            ptr += n;
            remaining -= n;
            sent += static_cast<quint64>(n);
        }
        if (progress)
            progress(sent, total);
    }
    libssh2_channel_send_eof(channel);
    libssh2_channel_wait_eof(channel);
    libssh2_channel_wait_closed(channel);
    libssh2_channel_free(channel);
    return true;
}

void SftpFileEngine::setError(const QString &message)
{
    m_lastError = message;
}

void SftpFileEngine::emitFingerprint()
{
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const char *hash = libssh2_hostkey_hash(session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash)
        return;
    QString hex;
    for (int i = 0; i < 32; ++i) {
        if (i)
            hex += ':';
        hex += QString("%1").arg(static_cast<unsigned char>(hash[i]), 2, 16,
                                 QChar('0'));
    }
    m_hostKeyFingerprint = hex;
}

void SftpFileEngine::closeSocket()
{
    const socket_t sock = storedSocket(m_socket);
    if (sock != kInvalidSocket) {
        closeNativeSocket(sock);
        m_socket = static_cast<quintptr>(kInvalidSocket);
    }
}

} // namespace termsync::transfer
