#include "sftp/SftpFileEngine.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <atomic>
#include <cerrno>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
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
#  include <netinet/in.h>
#  include <netinet/tcp.h>
#  include <sys/socket.h>
#  include <sys/select.h>
#  include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

#include <libssh2.h>
#include <libssh2_sftp.h>

namespace termsync::transfer {

// Token-bucket throttle shared by all parallel workers of one transfer, so the
// *aggregate* rate is capped (not each lane independently). consume() blocks the
// calling thread until `bytes` of budget is available. Thread-safe.
class RateLimiter
{
public:
    explicit RateLimiter(quint64 bytesPerSec)
        : m_rate(bytesPerSec)
        , m_tokens(0.0) // start empty so the average converges to the cap (no free burst)
        , m_last(std::chrono::steady_clock::now())
    {
    }

    void consume(quint64 bytes)
    {
        if (m_rate == 0)
            return;
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - m_last).count();
            m_last = now;
            m_tokens += elapsed * static_cast<double>(m_rate);
            const double cap = static_cast<double>(m_rate); // cap burst to ~1s
            if (m_tokens > cap)
                m_tokens = cap;
            if (m_tokens >= static_cast<double>(bytes)) {
                m_tokens -= static_cast<double>(bytes);
                return;
            }
            const double deficitSec =
                (static_cast<double>(bytes) - m_tokens) / static_cast<double>(m_rate);
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::duration<double>(deficitSec));
            lock.lock();
        }
    }

private:
    const quint64 m_rate;
    double m_tokens;
    std::chrono::steady_clock::time_point m_last;
    std::mutex m_mutex;
};

namespace {

// Aggregate transfer cap in bytes/sec (0 = unlimited). Env is a default/override;
// the engine's setRateLimitBytesPerSec() takes precedence when non-zero.
quint64 rateLimitFromEnv()
{
    bool ok = false;
    const int kbps = qEnvironmentVariableIntValue("TERMSYNC_SFTP_MAX_KBPS", &ok);
    if (!ok || kbps <= 0)
        return 0;
    return static_cast<quint64>(kbps) * 1024ull;
}

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

QString libssh2Error(LIBSSH2_SESSION *session, const QString &fallback)
{
    char *message = nullptr;
    int length = 0;
    const int code = libssh2_session_last_error(session, &message, &length, 0);
    if (length > 0 && message)
        return QString::fromUtf8(message, length);
    if (code)
        return QStringLiteral("%1 (libssh2 error %2)").arg(fallback).arg(code);
    return fallback;
}

bool waitForSocket(LIBSSH2_SESSION *session, socket_t socket, QString *error,
                   const QString &operation)
{
    for (;;) {
        fd_set readSet;
        fd_set writeSet;
        fd_set errorSet;
        FD_ZERO(&readSet);
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(socket, &errorSet);

        const int directions = libssh2_session_block_directions(session);
        if (!directions || (directions & LIBSSH2_SESSION_BLOCK_INBOUND))
            FD_SET(socket, &readSet);
        if (!directions || (directions & LIBSSH2_SESSION_BLOCK_OUTBOUND))
            FD_SET(socket, &writeSet);

        timeval timeout{};
        timeout.tv_sec = 30;
        timeout.tv_usec = 0;
#ifdef _WIN32
        const int rc = ::select(0, &readSet, &writeSet, &errorSet, &timeout);
        const int socketError = WSAGetLastError();
#else
        const int rc = ::select(socket + 1, &readSet, &writeSet, &errorSet, &timeout);
        const int socketError = errno;
#endif
        if (rc > 0)
            return true;
        if (rc == 0) {
            if (error)
                *error = QStringLiteral("Timed out while waiting for SFTP %1").arg(operation);
            return false;
        }
#ifndef _WIN32
        if (socketError == EINTR)
            continue;
#endif
        if (error)
            *error = QStringLiteral("Socket wait failed during SFTP %1").arg(operation);
        return false;
    }
}

class NonBlockingGuard
{
public:
    explicit NonBlockingGuard(LIBSSH2_SESSION *session, bool enabled)
        : m_session(session)
        , m_wasBlocking(session ? libssh2_session_get_blocking(session) : 1)
        , m_enabled(enabled)
    {
        if (m_session && m_enabled)
            libssh2_session_set_blocking(m_session, 0);
    }

    ~NonBlockingGuard()
    {
        if (m_session && m_enabled)
            libssh2_session_set_blocking(m_session, m_wasBlocking);
    }

private:
    LIBSSH2_SESSION *m_session = nullptr;
    int m_wasBlocking = 1;
    bool m_enabled = false;
};

LIBSSH2_SFTP_HANDLE *openSftpFile(LIBSSH2_SESSION *session, socket_t socket,
                                  LIBSSH2_SFTP *sftp, const QByteArray &path,
                                  unsigned long flags, long mode, QString *error,
                                  const QString &operation)
{
    for (;;) {
        LIBSSH2_SFTP_HANDLE *handle =
            libssh2_sftp_open(sftp, path.constData(), flags, mode);
        if (handle)
            return handle;
        if (libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN)
            break;
        if (!waitForSocket(session, socket, error, operation))
            return nullptr;
    }
    if (error)
        *error = libssh2Error(session, QStringLiteral("Could not open SFTP file"));
    return nullptr;
}

ssize_t readSftpFile(LIBSSH2_SESSION *session, socket_t socket,
                     LIBSSH2_SFTP_HANDLE *handle, char *buffer, qsizetype length,
                     QString *error)
{
    for (;;) {
        const ssize_t rc = libssh2_sftp_read(handle, buffer, static_cast<size_t>(length));
        if (rc != LIBSSH2_ERROR_EAGAIN)
            return rc;
        if (!waitForSocket(session, socket, error, QStringLiteral("read")))
            return LIBSSH2_ERROR_SOCKET_RECV;
    }
}

ssize_t writeSftpFile(LIBSSH2_SESSION *session, socket_t socket,
                      LIBSSH2_SFTP_HANDLE *handle, const char *buffer,
                      qsizetype length, QString *error)
{
    for (;;) {
        const ssize_t rc = libssh2_sftp_write(handle, buffer, static_cast<size_t>(length));
        if (rc > 0)
            return rc;
        if (rc < 0 && rc != LIBSSH2_ERROR_EAGAIN)
            return rc;
        if (!waitForSocket(session, socket, error, QStringLiteral("write")))
            return LIBSSH2_ERROR_SOCKET_SEND;
    }
}

bool closeSftpFile(LIBSSH2_SESSION *session, socket_t socket,
                   LIBSSH2_SFTP_HANDLE *handle, QString *error)
{
    if (!handle)
        return true;
    for (;;) {
        const int rc = libssh2_sftp_close(handle);
        if (rc == 0)
            return true;
        if (rc != LIBSSH2_ERROR_EAGAIN) {
            if (error)
                *error = libssh2Error(session, QStringLiteral("Could not close SFTP file"));
            return false;
        }
        if (!waitForSocket(session, socket, error, QStringLiteral("close")))
            return false;
    }
}

// The transfer buffer size drives libssh2's optimistic SFTP pipeline. Reads
// keep roughly buffer_size * 4 outstanding FXP_READs, and writes split the
// supplied buffer into many ~30 KB FXP_WRITEs before waiting for ACKs. libssh2's
// channel packet default is already 32 KB, matching the fast-client target.
qsizetype transferBufferSize(const char *specificKey, int defaultKb)
{
    bool ok = false;
    int kb = specificKey ? qEnvironmentVariableIntValue(specificKey, &ok) : 0;
    if (!ok)
        kb = qEnvironmentVariableIntValue("TERMSYNC_SFTP_BUFFER_KB", &ok);
    if (!ok)
        kb = defaultKb;
    kb = std::clamp(kb, 64, 16384);
    return static_cast<qsizetype>(kb) * 1024;
}

qsizetype downloadBufferSize()
{
    return transferBufferSize("TERMSYNC_SFTP_DOWNLOAD_BUFFER_KB", 1024);
}

qsizetype uploadBufferSize()
{
    return transferBufferSize("TERMSYNC_SFTP_UPLOAD_BUFFER_KB", 1024);
}

bool useNonblockingPump()
{
    bool ok = false;
    const int enabled = qEnvironmentVariableIntValue("TERMSYNC_SFTP_NONBLOCK", &ok);
    return ok && enabled != 0;
}

// Files at least this large use multiple parallel connections. Lowered so
// medium files also benefit; tune with TERMSYNC_SFTP_PARALLEL / _THRESHOLD_MB.
quint64 parallelTransferThreshold()
{
    bool ok = false;
    int mb = qEnvironmentVariableIntValue("TERMSYNC_SFTP_THRESHOLD_MB", &ok);
    if (!ok)
        mb = 16;
    return static_cast<quint64>(std::clamp(mb, 1, 4096)) * 1024ull * 1024ull;
}

bool isCancelled(const std::atomic<bool> *cancel, const std::atomic<bool> *stop = nullptr)
{
    return (cancel && cancel->load()) || (stop && stop->load());
}

// Park the calling thread while the pause flag is set. Returns false if the
// transfer was cancelled/stopped while parked, true once it may proceed.
bool waitWhilePaused(const std::atomic<bool> *pause, const std::atomic<bool> *cancel,
                     const std::atomic<bool> *stop)
{
    if (!pause)
        return true;
    while (pause->load()) {
        if (isCancelled(cancel, stop))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
    }
    return true;
}

// Heuristic: does this error look like a transport drop (worth reconnecting) as
// opposed to a permanent, retry-won't-help error (no such file, permission)?
bool looksLikeConnectionDrop(const QString &error)
{
    const QString e = error.toLower();
    for (const char *needle : {"socket", "recv", "send", "timed out", "timeout",
                               "disconnect", "eof", "reset", "broken", "handshake",
                               "not connected", "closed"}) {
        if (e.contains(QLatin1String(needle)))
            return true;
    }
    return false;
}

// On a relentless *upload* retry, rewind the lane by this much before resuming.
// libssh2_sftp_write can report bytes as written before the server has committed
// them, so the tail up to ~the outstanding window may be missing after a drop.
// Re-sending that overlap at the same offset is idempotent, so a generous margin
// is safe and cheap relative to a multi-GB transfer. (Download needs no margin —
// its watermark counts only disk-committed bytes.)
quint64 uploadResumeMargin()
{
    return 8ull * 1024ull * 1024ull;
}

// Bounded number of reconnect attempts before giving up (env-tunable).
int relentlessMaxAttempts(bool relentless)
{
    if (!relentless)
        return 1;
    bool ok = false;
    const int n = qEnvironmentVariableIntValue("TERMSYNC_SFTP_MAX_RECONNECTS", &ok);
    return ok ? qMax(1, n) : 20;
}

// Cancellable linear backoff (capped) between reconnect attempts.
void reconnectBackoff(int attempt, const std::atomic<bool> *cancel,
                      const std::atomic<bool> *stop)
{
    bool ok = false;
    int base = qEnvironmentVariableIntValue("TERMSYNC_SFTP_RECONNECT_MS", &ok);
    if (!ok || base <= 0)
        base = 500;
    int remaining = std::min(base * qMax(1, attempt), 5000);
    while (remaining > 0 && !isCancelled(cancel, stop)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        remaining -= 50;
    }
}

// Number of parallel connections, scaled by file size (~one worker per 32 MB)
// and capped. More connections hide latency and work around per-connection
// server throughput caps — the main reason multi-connection clients feel fast.
int parallelTransferCount(quint64 bytes, bool upload)
{
    bool ok = false;
    int configured = qEnvironmentVariableIntValue("TERMSYNC_SFTP_PARALLEL", &ok);
    if (ok)
        return std::clamp(configured, 1, 16);

    const quint64 perWorker = 32ull * 1024ull * 1024ull;
    int bySize = static_cast<int>((bytes + perWorker - 1) / perWorker);
    // 5 GB tuning on OpenSSH/WSL: uploads need more lanes to clear 100 MiB/s,
    // while downloads become less stable past six lanes.
    return std::clamp(bySize, 1, upload ? 8 : 6);
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

// Depth of the disk<->network buffer hand-off. >=2 means the disk side and the
// network side each always have a buffer to work on, so local I/O overlaps the
// SSH pipe instead of stalling it. Env-tunable for benchmarking.
int transferPipeDepth()
{
    bool ok = false;
    const int d = qEnvironmentVariableIntValue("TERMSYNC_SFTP_PIPE_DEPTH", &ok);
    return ok ? std::clamp(d, 2, 64) : 4;
}

// A bounded FIFO hand-off of buffers between exactly one producer thread and one
// consumer thread. The point is to keep disk I/O and network I/O running at the
// same time: the network thread never blocks on the disk (and vice versa) as
// long as the pipe has a spare buffer. Either side can raise fail() to abort the
// other. Buffers move by value (QByteArray is cheap to move).
class BufferPipe
{
public:
    explicit BufferPipe(int depth) : m_depth(depth) {}

    // Producer: wait for room, then enqueue. Returns false if the consumer aborted.
    bool push(QByteArray buffer)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notFull.wait(lock, [this] {
            return m_failed || static_cast<int>(m_queue.size()) < m_depth;
        });
        if (m_failed)
            return false;
        m_queue.push_back(std::move(buffer));
        m_notEmpty.notify_one();
        return true;
    }

    // Producer: signal that no more buffers will arrive.
    void close()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_closed = true;
        m_notEmpty.notify_all();
    }

    // Consumer: wait for a buffer. Returns false when drained-and-closed or aborted.
    bool pop(QByteArray *out)
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait(lock, [this] {
            return m_failed || !m_queue.empty() || m_closed;
        });
        if (m_failed || m_queue.empty())
            return false;
        *out = std::move(m_queue.front());
        m_queue.pop_front();
        m_notFull.notify_one();
        return true;
    }

    // Either side aborts; wakes the other out of any wait.
    void fail()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_failed = true;
        m_notEmpty.notify_all();
        m_notFull.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_notFull;
    std::condition_variable m_notEmpty;
    std::deque<QByteArray> m_queue;
    const int m_depth;
    bool m_closed = false;
    bool m_failed = false;
};

// Double-buffered SFTP->local copy. This (network) thread issues the FXP_READs;
// a spawned disk thread drains filled buffers to `local`, so the file write of
// one chunk overlaps the network read of the next. length==0 reads to EOF (whole
// file); otherwise exactly `length` bytes. onBytes(delta) runs on this thread as
// data arrives, for progress. `context` is the remote path, used in messages.
bool downloadPump(LIBSSH2_SESSION *session, socket_t socket,
                  LIBSSH2_SFTP_HANDLE *handle, QFile *local, quint64 length,
                  qsizetype bufferSize, const std::function<void(quint64)> &onBytes,
                  std::atomic<quint64> *diskProgress, RateLimiter *limiter,
                  const std::atomic<bool> *cancel, const std::atomic<bool> *stop,
                  const std::atomic<bool> *pause, QString *error, const QString &context)
{
    BufferPipe pipe(transferPipeDepth());
    std::atomic<bool> diskOk{true};
    QString diskError;
    // diskProgress advances only on bytes actually committed to disk (in FIFO
    // order from the start offset), so it is the exact contiguous watermark a
    // relentless retry resumes this range from. Buffers dropped on an aborted
    // pipe are never counted.
    std::thread disk([&] {
        QByteArray buffer;
        while (pipe.pop(&buffer)) {
            if (local->write(buffer.constData(), buffer.size()) != buffer.size()) {
                diskError = local->errorString();
                diskOk.store(false);
                pipe.fail();
                return;
            }
            if (diskProgress)
                diskProgress->fetch_add(static_cast<quint64>(buffer.size()));
        }
    });

    bool ok = true;
    const bool bounded = length != 0;
    quint64 remaining = length;
    for (;;) {
        if (isCancelled(cancel, stop) || !waitWhilePaused(pause, cancel, stop)) {
            if (error)
                *error = QStringLiteral("Cancelled");
            ok = false;
            break;
        }
        if (bounded && remaining == 0)
            break;
        qsizetype want = bufferSize;
        if (bounded)
            want = static_cast<qsizetype>(qMin<quint64>(bufferSize, remaining));
        QByteArray buffer(want, Qt::Uninitialized);
        QString ioError;
        const ssize_t rc =
            readSftpFile(session, socket, handle, buffer.data(), want, &ioError);
        if (rc > 0) {
            buffer.truncate(rc);
            if (limiter)
                limiter->consume(static_cast<quint64>(rc));
            onBytes(static_cast<quint64>(rc));
            if (bounded)
                remaining -= static_cast<quint64>(rc);
            if (!pipe.push(std::move(buffer))) { // disk aborted
                ok = false;
                break;
            }
            continue;
        }
        if (rc == 0) {
            if (bounded) {
                if (error)
                    *error = QStringLiteral("Unexpected EOF while downloading: %1").arg(context);
                ok = false;
            }
            break; // EOF: expected for whole-file
        }
        if (error)
            *error = QStringLiteral("Failed while downloading: %1 (%2)")
                         .arg(context, ioError.isEmpty()
                                           ? libssh2Error(session, QStringLiteral("read failed"))
                                           : ioError);
        ok = false;
        break;
    }

    if (ok)
        pipe.close(); // let the disk thread drain the remaining queued buffers
    else
        pipe.fail(); // abort: drop the queue and unblock the disk thread
    disk.join();
    if (!diskOk.load()) {
        if (error)
            *error = diskError;
        return false;
    }
    return ok;
}

// Double-buffered local->SFTP copy. A spawned disk thread reads `local` into
// buffers; this (network) thread drains them with FXP_WRITE, so the disk read of
// one chunk overlaps the network send of the previous. Copies exactly `length`
// bytes from the file's current position. onBytes(delta) runs here for progress.
bool uploadPump(LIBSSH2_SESSION *session, socket_t socket,
                LIBSSH2_SFTP_HANDLE *handle, QFile *local, quint64 length,
                qsizetype bufferSize, const std::function<void(quint64)> &onBytes,
                RateLimiter *limiter, const std::atomic<bool> *cancel,
                const std::atomic<bool> *stop, const std::atomic<bool> *pause,
                QString *error, const QString &context)
{
    BufferPipe pipe(transferPipeDepth());
    std::atomic<bool> diskOk{true};
    QString diskError;
    std::thread disk([&] {
        quint64 remaining = length;
        while (remaining > 0) {
            const qint64 want = static_cast<qint64>(
                qMin<quint64>(static_cast<quint64>(bufferSize), remaining));
            QByteArray buffer = local->read(want);
            if (buffer.size() != want) {
                diskError = QStringLiteral("Unexpected EOF while uploading: %1").arg(context);
                diskOk.store(false);
                pipe.fail();
                return;
            }
            remaining -= static_cast<quint64>(buffer.size());
            if (!pipe.push(std::move(buffer))) // network aborted
                return;
        }
        pipe.close();
    });

    bool ok = true;
    for (;;) {
        QByteArray buffer;
        if (!pipe.pop(&buffer))
            break; // drained-and-closed, or disk aborted
        if (isCancelled(cancel, stop) || !waitWhilePaused(pause, cancel, stop)) {
            if (error)
                *error = QStringLiteral("Cancelled");
            ok = false;
            break;
        }
        const char *ptr = buffer.constData();
        qsizetype chunkRemaining = buffer.size();
        while (chunkRemaining > 0) {
            QString ioError;
            const ssize_t written =
                writeSftpFile(session, socket, handle, ptr, chunkRemaining, &ioError);
            if (written < 0) {
                if (error)
                    *error = QStringLiteral("Failed while uploading: %1 (%2)")
                                 .arg(context, ioError.isEmpty()
                                                   ? libssh2Error(session, QStringLiteral("write failed"))
                                                   : ioError);
                ok = false;
                break;
            }
            ptr += written;
            chunkRemaining -= written;
            if (limiter)
                limiter->consume(static_cast<quint64>(written));
            onBytes(static_cast<quint64>(written));
        }
        if (!ok)
            break;
    }

    pipe.fail(); // unblock the disk thread whether we finished or aborted
    disk.join();
    if (ok && !diskOk.load()) {
        if (error)
            *error = diskError;
        return false;
    }
    return ok;
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

    // Bias the KEX toward fast, AES-NI-friendly transport before the handshake.
    // SFTP throughput on a fast link is CPU-bound on the cipher/MAC, so we offer
    // AEAD (aes-gcm, no separate MAC pass) first, then aes-ctr, and keep cbc as a
    // last-resort fallback so we never fail to negotiate with an older server.
    // Order is client preference; libssh2 picks the first that the server also
    // supports. Compression is forced off (it only helps for text and otherwise
    // burns CPU that would cap the transfer). Overridable via env for debugging.
    if (!qEnvironmentVariableIsSet("TERMSYNC_SFTP_NO_CRYPTO_PREF")) {
        const char *ciphers =
            "aes128-gcm@openssh.com,aes256-gcm@openssh.com,"
            "aes128-ctr,aes192-ctr,aes256-ctr,"
            "aes128-cbc,aes256-cbc,3des-cbc";
        libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_CS, ciphers);
        libssh2_session_method_pref(session, LIBSSH2_METHOD_CRYPT_SC, ciphers);
        libssh2_session_method_pref(session, LIBSSH2_METHOD_COMP_CS, "none");
        libssh2_session_method_pref(session, LIBSSH2_METHOD_COMP_SC, "none");
    }

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
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    if (m_asciiMode) // text mode changes byte counts; use the dedicated path
        return asciiDownload(remotePath, localPath, progress, cancel);

    quint64 total = 0;
    statSize(remotePath, &total); // best-effort, for progress reporting

    // Resume: continue from what's already on disk. Requires a known total so we
    // know when we're done and can size the parallel split.
    quint64 startOffset = 0;
    if (m_resume && total > 0) {
        const qint64 existing = QFileInfo(localPath).size();
        if (existing > 0)
            startOffset = qMin(static_cast<quint64>(existing), total);
        if (startOffset == total) {
            if (progress)
                progress(total, total);
            return true; // already complete
        }
    }

    if (total >= parallelTransferThreshold() && !m_params.host.isEmpty())
        return downloadFileParallel(remotePath, localPath, total, startOffset, progress, cancel);

    // Sequential path: relentless retry with contiguous (disk-size) resume.
    const int maxAttempts = relentlessMaxAttempts(m_relentless);
    for (int attempt = 1;; ++attempt) {
        if (downloadFileSequential(remotePath, localPath, total, startOffset, progress, cancel))
            return true;
        if (isCancelled(cancel) || !m_relentless || attempt >= maxAttempts ||
            !looksLikeConnectionDrop(lastError()))
            return false;
        reconnectBackoff(attempt, cancel, nullptr);
        if (isCancelled(cancel))
            return false;
        reconnectForRetry(); // if it fails, next attempt retries/back-offs again
        if (total > 0) // resume from what's contiguously on disk
            startOffset = qMin(static_cast<quint64>(QFileInfo(localPath).size()), total);
    }
}

bool SftpFileEngine::downloadFileSequential(const QString &remotePath, const QString &localPath,
                                            quint64 total, quint64 startOffset,
                                            ProgressFn progress, const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }
    NonBlockingGuard nonblocking(session, useNonblockingPump());
    const QByteArray remote = remotePath.toUtf8();
    QString ioError;
    LIBSSH2_SFTP_HANDLE *remoteFile = openSftpFile(
        session, storedSocket(m_socket), sftp, remote, LIBSSH2_FXF_READ, 0, &ioError,
        QStringLiteral("open for read"));
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file: %1 (%2)")
                     .arg(remotePath, ioError));
        return false;
    }

    // Resuming keeps the existing prefix and appends; a fresh transfer truncates.
    QFile local(localPath);
    const QIODevice::OpenMode mode = startOffset > 0
        ? (QIODevice::ReadWrite)
        : (QIODevice::WriteOnly | QIODevice::Truncate);
    if (!local.open(mode) || (startOffset > 0 && !local.seek(static_cast<qint64>(startOffset)))) {
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(local.errorString());
        return false;
    }
    if (startOffset > 0)
        libssh2_sftp_seek64(remoteFile, static_cast<libssh2_uint64_t>(startOffset));

    RateLimiter limiter(effectiveRateBytesPerSec());
    quint64 done = startOffset;
    quint64 reported = startOffset;
    const bool ok = downloadPump(
        session, storedSocket(m_socket), remoteFile, &local, /*length=*/0,
        downloadBufferSize(),
        [&](quint64 delta) { done += delta; reportProgress(done, total, progress, &reported); },
        /*diskProgress=*/nullptr, &limiter, cancel, nullptr, m_pauseFlag, &ioError, remotePath);
    if (!ok) {
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(ioError);
        return false;
    }

    if (!closeSftpFile(session, storedSocket(m_socket), remoteFile, &ioError)) {
        setError(ioError);
        return false;
    }
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

    if (m_asciiMode) // text mode changes byte counts; use the dedicated path
        return asciiUpload(localPath, remotePath, progress, cancel);

    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        setError(local.errorString());
        return false;
    }
    const quint64 total = static_cast<quint64>(local.size());
    local.close();

    // Resume: pick up after whatever the remote already holds.
    quint64 startOffset = 0;
    if (m_resume) {
        quint64 remoteSize = 0;
        if (statSize(remotePath, &remoteSize) && remoteSize > 0)
            startOffset = qMin(remoteSize, total);
        if (startOffset == total && total > 0) {
            if (progress)
                progress(total, total);
            applyLocalPermissions(localPath, remotePath);
            return true; // already complete
        }
    }

    bool ok;
    if (total >= parallelTransferThreshold() && !m_params.host.isEmpty()) {
        ok = uploadFileParallel(localPath, remotePath, total, startOffset, progress, cancel);
    } else {
        // Sequential path: relentless retry, resuming from the authoritative remote
        // size (the server's committed count) so an un-acked tail is re-sent.
        const int maxAttempts = relentlessMaxAttempts(m_relentless);
        ok = false;
        for (int attempt = 1;; ++attempt) {
            if (uploadFileSequential(localPath, remotePath, total, startOffset, progress, cancel)) {
                ok = true;
                break;
            }
            if (isCancelled(cancel) || !m_relentless || attempt >= maxAttempts ||
                !looksLikeConnectionDrop(lastError()))
                break;
            reconnectBackoff(attempt, cancel, nullptr);
            if (isCancelled(cancel))
                break;
            reconnectForRetry();
            quint64 remoteSize = 0;
            startOffset = (statSize(remotePath, &remoteSize)) ? qMin(remoteSize, total) : 0;
        }
    }

    if (ok)
        applyLocalPermissions(localPath, remotePath); // no-op unless preserve is on
    return ok;
}

bool SftpFileEngine::uploadFileSequential(const QString &localPath, const QString &remotePath,
                                          quint64 total, quint64 startOffset,
                                          ProgressFn progress, const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly) ||
        (startOffset > 0 && !local.seek(static_cast<qint64>(startOffset)))) {
        setError(local.errorString());
        return false;
    }
    const QByteArray remote = remotePath.toUtf8();
    NonBlockingGuard nonblocking(session, useNonblockingPump());
    QString ioError;
    // Resuming keeps the remote prefix (no TRUNC) and seeks to the append point.
    unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT;
    if (startOffset == 0)
        flags |= LIBSSH2_FXF_TRUNC;
    LIBSSH2_SFTP_HANDLE *remoteFile = openSftpFile(
        session, storedSocket(m_socket), sftp, remote, flags,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
            LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH,
        &ioError, QStringLiteral("open for write"));
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file for writing: %1 (%2)")
                     .arg(remotePath, ioError));
        return false;
    }
    if (startOffset > 0)
        libssh2_sftp_seek64(remoteFile, static_cast<libssh2_uint64_t>(startOffset));

    RateLimiter limiter(effectiveRateBytesPerSec());
    quint64 done = startOffset;
    quint64 reported = startOffset;
    const bool ok = uploadPump(
        session, storedSocket(m_socket), remoteFile, &local, total - startOffset,
        uploadBufferSize(),
        [&](quint64 delta) { done += delta; reportProgress(done, total, progress, &reported); },
        &limiter, cancel, nullptr, m_pauseFlag, &ioError, remotePath);
    if (!ok) {
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(ioError);
        return false;
    }

    if (!closeSftpFile(session, storedSocket(m_socket), remoteFile, &ioError)) {
        setError(ioError);
        return false;
    }
    reportProgress(done, total, progress, &reported);
    return true;
}

bool SftpFileEngine::downloadFileParallel(const QString &remotePath, const QString &localPath,
                                          quint64 total, quint64 startOffset,
                                          ProgressFn progress, const std::atomic<bool> *cancel)
{
    const int workers = parallelTransferCount(total, false);
    if (workers <= 1)
        return downloadFileSequential(remotePath, localPath, total, startOffset, progress, cancel);

    // Resume keeps the existing prefix; a fresh transfer truncates. Either way the
    // file must be sized to `total` so workers can seek-and-write their ranges.
    QFile local(localPath);
    if (!local.open(startOffset > 0 ? QIODevice::ReadWrite
                                    : (QIODevice::WriteOnly | QIODevice::Truncate))) {
        setError(local.errorString());
        return false;
    }
    if (!local.resize(static_cast<qint64>(total))) {
        setError(local.errorString());
        return false;
    }
    local.close();

    std::atomic<quint64> done{startOffset};
    std::atomic<quint64> reported{startOffset};
    std::atomic<bool> stop{false};
    QMutex errorMutex;
    QString firstError;
    std::vector<std::thread> threads;
    threads.reserve(workers);

    RateLimiter limiter(effectiveRateBytesPerSec()); // shared across all lanes
    const int maxAttempts = relentlessMaxAttempts(m_relentless);
    std::vector<std::atomic<quint64>> laneDone(workers); // per-lane disk-committed bytes
    for (auto &d : laneDone)
        d.store(0);
    const quint64 region = total - startOffset;      // only fetch what's missing
    const quint64 span = (region + static_cast<quint64>(workers) - 1) /
                         static_cast<quint64>(workers);
    for (int i = 0; i < workers; ++i) {
        const quint64 offset = startOffset + span * static_cast<quint64>(i);
        if (offset >= total)
            break;
        const quint64 length = qMin(span, total - offset);
        threads.emplace_back([&, i, offset, length] {
            for (int attempt = 1; !isCancelled(cancel, &stop); ++attempt) {
                const quint64 did = laneDone[i].load();
                if (did >= length)
                    return; // lane already complete
                SftpFileEngine engine;
                QString err;
                if (connectSibling(&engine)) {
                    engine.m_limiter = &limiter;
                    engine.m_pauseFlag = m_pauseFlag;
                    // Download watermark is exact (disk-committed); resume right at it.
                    if (engine.downloadRange(remotePath, localPath, offset + did, length - did,
                                             &done, &reported, &laneDone[i], total,
                                             progress, cancel, &stop))
                        return; // lane done
                }
                err = engine.lastError();
                if (isCancelled(cancel, &stop))
                    return;
                if (!m_relentless || attempt >= maxAttempts || !looksLikeConnectionDrop(err)) {
                    QMutexLocker locker(&errorMutex);
                    if (firstError.isEmpty())
                        firstError = err;
                    stop.store(true);
                    return;
                }
                reconnectBackoff(attempt, cancel, &stop);
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
                                        quint64 total, quint64 startOffset,
                                        ProgressFn progress, const std::atomic<bool> *cancel)
{
    const int workers = parallelTransferCount(total, true);
    if (workers <= 1)
        return uploadFileSequential(localPath, remotePath, total, startOffset, progress, cancel);

    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    // Fresh transfer: create/truncate the remote up front so ranges write into a
    // clean file. Resume: leave the existing prefix in place.
    if (startOffset == 0) {
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
    }

    std::atomic<quint64> done{startOffset};
    std::atomic<quint64> reported{startOffset};
    std::atomic<bool> stop{false};
    QMutex errorMutex;
    QString firstError;
    std::vector<std::thread> threads;
    threads.reserve(workers);

    RateLimiter limiter(effectiveRateBytesPerSec()); // shared across all lanes
    const int maxAttempts = relentlessMaxAttempts(m_relentless);
    const quint64 region = total - startOffset;      // only send what's missing
    const quint64 span = (region + static_cast<quint64>(workers) - 1) /
                         static_cast<quint64>(workers);
    for (int i = 0; i < workers; ++i) {
        const quint64 offset = startOffset + span * static_cast<quint64>(i);
        if (offset >= total)
            break;
        const quint64 length = qMin(span, total - offset);
        threads.emplace_back([&, offset, length] {
            quint64 base = 0; // optimistic contiguous bytes confirmed for this lane
            for (int attempt = 1; !isCancelled(cancel, &stop); ++attempt) {
                if (base >= length)
                    return; // lane complete
                // Rewind by the margin so any un-committed tail is re-sent; the
                // overlap lands at the same offset and is idempotent.
                const quint64 margin = uploadResumeMargin();
                const quint64 resumeFrom = base > margin ? base - margin : 0;
                SftpFileEngine engine;
                std::atomic<quint64> sent{0};
                QString err;
                if (connectSibling(&engine)) {
                    engine.m_limiter = &limiter;
                    engine.m_pauseFlag = m_pauseFlag;
                    if (engine.uploadRange(localPath, remotePath, offset + resumeFrom,
                                           length - resumeFrom, &done, &reported, &sent,
                                           total, progress, cancel, &stop))
                        return; // lane done (clean close => bytes acked)
                }
                err = engine.lastError();
                if (isCancelled(cancel, &stop))
                    return;
                if (!m_relentless || attempt >= maxAttempts || !looksLikeConnectionDrop(err)) {
                    QMutexLocker locker(&errorMutex);
                    if (firstError.isEmpty())
                        firstError = err;
                    stop.store(true);
                    return;
                }
                base = resumeFrom + sent.load(); // advance past what we just sent
                reconnectBackoff(attempt, cancel, &stop);
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
                                   std::atomic<quint64> *reported,
                                   std::atomic<quint64> *rangeDone, quint64 total,
                                   ProgressFn progress, const std::atomic<bool> *cancel,
                                   const std::atomic<bool> *stop)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    if (!sftp) {
        setError(QStringLiteral("SFTP is not connected"));
        return false;
    }

    const QByteArray remote = remotePath.toUtf8();
    NonBlockingGuard nonblocking(session, useNonblockingPump());
    QString ioError;
    LIBSSH2_SFTP_HANDLE *remoteFile = openSftpFile(
        session, storedSocket(m_socket), sftp, remote, LIBSSH2_FXF_READ, 0, &ioError,
        QStringLiteral("open range for read"));
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file: %1 (%2)")
                     .arg(remotePath, ioError));
        return false;
    }
    libssh2_sftp_seek64(remoteFile, static_cast<libssh2_uint64_t>(offset));

    QFile local(localPath);
    if (!local.open(QIODevice::ReadWrite) || !local.seek(static_cast<qint64>(offset))) {
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(local.errorString());
        return false;
    }

    const bool ok = downloadPump(
        session, storedSocket(m_socket), remoteFile, &local, length, downloadBufferSize(),
        [&](quint64 delta) {
            const quint64 current = done->fetch_add(delta) + delta;
            reportProgress(qMin(current, total), total, progress, reported);
        },
        /*diskProgress=*/rangeDone, m_limiter, cancel, stop, m_pauseFlag, &ioError, remotePath);
    if (!ok) {
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(ioError);
        return false;
    }

    if (!closeSftpFile(session, storedSocket(m_socket), remoteFile, &ioError)) {
        setError(ioError);
        return false;
    }
    return true;
}

bool SftpFileEngine::uploadRange(const QString &localPath, const QString &remotePath,
                                 quint64 offset, quint64 length,
                                 std::atomic<quint64> *done,
                                 std::atomic<quint64> *reported,
                                 std::atomic<quint64> *rangeDone, quint64 total,
                                 ProgressFn progress, const std::atomic<bool> *cancel,
                                 const std::atomic<bool> *stop)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
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
    NonBlockingGuard nonblocking(session, useNonblockingPump());
    QString ioError;
    LIBSSH2_SFTP_HANDLE *remoteFile = openSftpFile(
        session, storedSocket(m_socket), sftp, remote, LIBSSH2_FXF_WRITE, 0, &ioError,
        QStringLiteral("open range for write"));
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file for writing: %1 (%2)")
                     .arg(remotePath, ioError));
        return false;
    }
    libssh2_sftp_seek64(remoteFile, static_cast<libssh2_uint64_t>(offset));

    const bool ok = uploadPump(
        session, storedSocket(m_socket), remoteFile, &local, length, uploadBufferSize(),
        [&](quint64 delta) {
            if (rangeDone)
                rangeDone->fetch_add(delta);
            const quint64 current = done->fetch_add(delta) + delta;
            reportProgress(qMin(current, total), total, progress, reported);
        },
        m_limiter, cancel, stop, m_pauseFlag, &ioError, remotePath);
    if (!ok) {
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(ioError);
        return false;
    }

    if (!closeSftpFile(session, storedSocket(m_socket), remoteFile, &ioError)) {
        setError(ioError);
        return false;
    }
    return true;
}

quint64 SftpFileEngine::effectiveRateBytesPerSec() const
{
    return m_rateBytesPerSec ? m_rateBytesPerSec : rateLimitFromEnv();
}

bool SftpFileEngine::reconnectForRetry()
{
    if (m_params.host.isEmpty())
        return false;
    const QString expected = m_hostKeyFingerprint;
    disconnectFromHost();
    return connectToHost(m_params, [expected](const QString &fp) {
        return expected.isEmpty() || fp == expected;
    });
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

bool SftpFileEngine::readlink(const QString &remotePath, QString *target)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp || !target) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray path = remotePath.toUtf8();
    char buffer[1024];
    const int rc = libssh2_sftp_readlink(sftp, path.constData(), buffer, sizeof(buffer));
    if (rc <= 0) {
        setError(QStringLiteral("Could not read symlink: %1").arg(remotePath));
        return false;
    }
    *target = QString::fromUtf8(buffer, rc);
    return true;
}

bool SftpFileEngine::realpath(const QString &remotePath, QString *resolved)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp || !resolved) { setError(QStringLiteral("SFTP is not connected")); return false; }
    const QByteArray path = remotePath.toUtf8();
    char buffer[1024];
    const int rc = libssh2_sftp_realpath(sftp, path.constData(), buffer, sizeof(buffer));
    if (rc <= 0) {
        setError(QStringLiteral("Could not resolve path: %1").arg(remotePath));
        return false;
    }
    *resolved = QString::fromUtf8(buffer, rc);
    return true;
}

bool SftpFileEngine::createSymlink(const QString &target, const QString &linkPath)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    if (!sftp) { setError(QStringLiteral("SFTP is not connected")); return false; }
    QByteArray orig = target.toUtf8();
    QByteArray link = linkPath.toUtf8();
    if (libssh2_sftp_symlink(sftp, orig.constData(), link.data()) != 0) {
        setError(QStringLiteral("Could not create symlink: %1 -> %2").arg(linkPath, target));
        return false;
    }
    return true;
}

void SftpFileEngine::applyLocalPermissions(const QString &localPath, const QString &remotePath)
{
    if (!m_preservePerms)
        return;
    const QFileDevice::Permissions p = QFile::permissions(localPath);
    quint32 mode = 0;
    if (p & QFileDevice::ReadOwner)  mode |= 0400;
    if (p & QFileDevice::WriteOwner) mode |= 0200;
    if (p & QFileDevice::ExeOwner)   mode |= 0100;
    if (p & QFileDevice::ReadGroup)  mode |= 0040;
    if (p & QFileDevice::WriteGroup) mode |= 0020;
    if (p & QFileDevice::ExeGroup)   mode |= 0010;
    if (p & QFileDevice::ReadOther)  mode |= 0004;
    if (p & QFileDevice::WriteOther) mode |= 0002;
    if (p & QFileDevice::ExeOther)   mode |= 0001;
    if (mode)
        setPermissions(remotePath, mode); // best-effort; failure is non-fatal
}

// Text-mode transfers: translate line endings. Small files (config, scripts),
// so a simple whole-buffer sequential path is fine — no parallel/resume.
bool SftpFileEngine::asciiUpload(const QString &localPath, const QString &remotePath,
                                 ProgressFn progress, const std::atomic<bool> *cancel)
{
    QFile local(localPath);
    if (!local.open(QIODevice::ReadOnly)) {
        setError(local.errorString());
        return false;
    }
    QByteArray data = local.readAll();
    local.close();
    // Normalise to LF, then emit CRLF (canonical network text form).
    data.replace("\r\n", "\n");
    data.replace('\n', "\r\n");

    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const QByteArray remote = remotePath.toUtf8();
    QString ioError;
    LIBSSH2_SFTP_HANDLE *remoteFile = openSftpFile(
        session, storedSocket(m_socket), sftp, remote,
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
            LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH,
        &ioError, QStringLiteral("open for write"));
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file for writing: %1 (%2)")
                     .arg(remotePath, ioError));
        return false;
    }
    const char *ptr = data.constData();
    qsizetype remaining = data.size();
    while (remaining > 0) {
        if (isCancelled(cancel)) {
            closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const ssize_t w = writeSftpFile(session, storedSocket(m_socket), remoteFile,
                                        ptr, remaining, &ioError);
        if (w < 0) {
            closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
            setError(QStringLiteral("Failed while uploading: %1 (%2)").arg(remotePath, ioError));
            return false;
        }
        ptr += w;
        remaining -= w;
        if (progress)
            progress(static_cast<quint64>(data.size() - remaining), static_cast<quint64>(data.size()));
    }
    if (!closeSftpFile(session, storedSocket(m_socket), remoteFile, &ioError)) {
        setError(ioError);
        return false;
    }
    applyLocalPermissions(localPath, remotePath);
    return true;
}

bool SftpFileEngine::asciiDownload(const QString &remotePath, const QString &localPath,
                                   ProgressFn progress, const std::atomic<bool> *cancel)
{
    auto *sftp = static_cast<LIBSSH2_SFTP *>(m_sftp);
    auto *session = static_cast<LIBSSH2_SESSION *>(m_session);
    const QByteArray remote = remotePath.toUtf8();
    QString ioError;
    LIBSSH2_SFTP_HANDLE *remoteFile = openSftpFile(
        session, storedSocket(m_socket), sftp, remote, LIBSSH2_FXF_READ, 0, &ioError,
        QStringLiteral("open for read"));
    if (!remoteFile) {
        setError(QStringLiteral("Could not open remote file: %1 (%2)").arg(remotePath, ioError));
        return false;
    }
    QByteArray data;
    QByteArray buffer(downloadBufferSize(), Qt::Uninitialized);
    for (;;) {
        if (isCancelled(cancel)) {
            closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const ssize_t rc = readSftpFile(session, storedSocket(m_socket), remoteFile,
                                        buffer.data(), buffer.size(), &ioError);
        if (rc > 0) {
            data.append(buffer.constData(), rc);
            if (progress)
                progress(static_cast<quint64>(data.size()), 0);
            continue;
        }
        if (rc == 0)
            break;
        closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
        setError(QStringLiteral("Failed while downloading: %1 (%2)").arg(remotePath, ioError));
        return false;
    }
    closeSftpFile(session, storedSocket(m_socket), remoteFile, nullptr);
    // Canonicalise to the local platform's line ending.
    data.replace("\r\n", "\n");
#ifdef _WIN32
    data.replace('\n', "\r\n");
#endif
    QFile local(localPath);
    if (!local.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        local.write(data) != data.size()) {
        setError(local.errorString());
        return false;
    }
    if (progress)
        progress(static_cast<quint64>(data.size()), static_cast<quint64>(data.size()));
    return true;
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
    // Disable Nagle so small SFTP request packets aren't delayed, and enlarge
    // the socket receive buffer so the kernel can keep the pipeline full.
    int one = 1;
    ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                 reinterpret_cast<const char *>(&one), sizeof(one));
    int rcvbuf = 4 * 1024 * 1024;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVBUF,
                 reinterpret_cast<const char *>(&rcvbuf), sizeof(rcvbuf));
    int sndbuf = 4 * 1024 * 1024;
    ::setsockopt(sock, SOL_SOCKET, SO_SNDBUF,
                 reinterpret_cast<const char *>(&sndbuf), sizeof(sndbuf));
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
