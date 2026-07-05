#include "sftp/SftpFileEngine.h"

#include <QFile>
#include <QMutex>

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
    char buffer[32768];
    for (;;) {
        if (cancel && cancel->load()) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const ssize_t rc = libssh2_sftp_read(remoteFile, buffer, sizeof(buffer));
        if (rc > 0) {
            if (local.write(buffer, rc) != rc) {
                libssh2_sftp_close(remoteFile);
                setError(local.errorString());
                return false;
            }
            done += static_cast<quint64>(rc);
            if (progress)
                progress(done, total ? total : done);
            continue;
        }
        if (rc == 0)
            break;
        libssh2_sftp_close(remoteFile);
        setError(QStringLiteral("Failed while downloading: %1").arg(remotePath));
        return false;
    }

    libssh2_sftp_close(remoteFile);
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
    while (!local.atEnd()) {
        if (cancel && cancel->load()) {
            libssh2_sftp_close(remoteFile);
            setError(QStringLiteral("Cancelled"));
            return false;
        }
        const QByteArray chunk = local.read(32768);
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
        if (progress)
            progress(done, total);
    }

    libssh2_sftp_close(remoteFile);
    return true;
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
    const QByteArray pass = params.password.toUtf8();
    if (libssh2_userauth_password(session, user.constData(), pass.constData()) != 0) {
        setError(QStringLiteral("Password authentication failed"));
        return false;
    }
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
