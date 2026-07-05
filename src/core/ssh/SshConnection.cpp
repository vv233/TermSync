#include "ssh/SshConnection.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <cstdlib>
#include <cstring>

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
#  include <sys/select.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

#include <libssh2.h>

namespace termsync::core {

namespace {

// One-time global initialisation of Winsock and libssh2.
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

void closeSocket(socket_t s)
{
    if (s == kInvalidSocket)
        return;
#ifdef _WIN32
    ::closesocket(s);
#else
    ::close(s);
#endif
}

} // namespace

// ---------------------------------------------------------------------------
// SshWorker — lives on the worker thread and owns all libssh2 state.
// ---------------------------------------------------------------------------
class SshWorker : public QObject
{
    Q_OBJECT

public:
    SshWorker() = default;
    ~SshWorker() override { teardown(); }

public slots:
    void start(const termsync::core::SshConnectionParams &params)
    {
        ensureGlobalInit();
        m_params = params;

        if (!openSocket()) {
            emit errorOccurred(tr("Could not connect to %1:%2")
                                   .arg(m_params.host)
                                   .arg(m_params.port));
            return;
        }

        m_session = libssh2_session_init();
        if (!m_session) {
            emit errorOccurred(tr("Failed to create SSH session"));
            teardown();
            return;
        }

        // Blocking mode for the handshake/auth phase keeps the setup simple;
        // we switch to non-blocking once the shell is running.
        libssh2_session_set_blocking(m_session, 1);

        if (libssh2_session_handshake(m_session, static_cast<libssh2_socket_t>(m_socket))) {
            emit errorOccurred(tr("SSH handshake failed"));
            teardown();
            return;
        }

        // Surface the host key and pause: authentication only continues once
        // the owner calls approveHostKey() -> proceed().
        emitFingerprint();
    }

    // Called after the host key has been approved (or rejected) by the owner.
    void proceed(bool accept)
    {
        if (!m_session)
            return; // already torn down
        if (!accept) {
            emit errorOccurred(tr("Host key rejected"));
            teardown();
            return;
        }

        if (!authenticate()) {
            emit authenticationFailed(tr("Password authentication failed"));
            teardown();
            return;
        }

        if (!openShell()) {
            emit errorOccurred(tr("Failed to open shell channel"));
            teardown();
            return;
        }

        libssh2_session_set_blocking(m_session, 0);
        emit connected();

        // React to incoming data the instant the socket becomes readable, so
        // output has no polling latency.
        m_readNotifier = new QSocketNotifier(static_cast<qintptr>(m_socket),
                                             QSocketNotifier::Read, this);
        connect(m_readNotifier, &QSocketNotifier::activated, this,
                &SshWorker::pumpIo);

        // A low-frequency safety net: libssh2 can buffer decrypted data that
        // arrived with an earlier packet, which won't re-trigger the notifier.
        m_pump = new QTimer(this);
        m_pump->setInterval(40);
        connect(m_pump, &QTimer::timeout, this, &SshWorker::pumpIo);
        m_pump->start();
    }

    void queueData(const QByteArray &data)
    {
        if (!m_channel)
            return;
        const char *ptr = data.constData();
        int remaining = data.size();
        while (remaining > 0) {
            ssize_t rc = libssh2_channel_write(m_channel, ptr, remaining);
            if (rc == LIBSSH2_ERROR_EAGAIN) {
                waitSocket();
                continue;
            }
            if (rc < 0) {
                emit errorOccurred(tr("Write error (%1)").arg(rc));
                return;
            }
            ptr += rc;
            remaining -= static_cast<int>(rc);
        }
    }

    void doResize(int cols, int rows)
    {
        if (m_channel)
            libssh2_channel_request_pty_size(m_channel, cols, rows);
    }

    void stop() { teardown(); }

signals:
    void connected();
    void hostKeyFingerprint(const QString &sha256Hex);
    void dataReceived(const QByteArray &data);
    void authenticationFailed(const QString &reason);
    void errorOccurred(const QString &message);
    void disconnected();

private slots:
    void pumpIo()
    {
        if (!m_channel)
            return;

        char buf[4096];
        for (;;) {
            ssize_t rc = libssh2_channel_read(m_channel, buf, sizeof(buf));
            if (rc > 0) {
                emit dataReceived(QByteArray(buf, static_cast<int>(rc)));
                continue;
            }
            if (rc == LIBSSH2_ERROR_EAGAIN)
                break; // nothing more for now
            if (rc == 0) {
                if (libssh2_channel_eof(m_channel))
                    { handleRemoteClose(); return; }
                break;
            }
            // rc < 0 and not EAGAIN: fatal.
            handleRemoteClose();
            return;
        }

        if (libssh2_channel_eof(m_channel))
            handleRemoteClose();
    }

private:
    // Blocking connect to host:port, returning a native socket (or invalid).
    static socket_t tcpConnect(const QByteArray &host, quint16 port)
    {
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        const QByteArray portStr = QByteArray::number(port);
        struct addrinfo *res = nullptr;
        if (getaddrinfo(host.constData(), portStr.constData(), &hints, &res) != 0 || !res)
            return kInvalidSocket;
        socket_t sock = kInvalidSocket;
        for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
            sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (sock == kInvalidSocket)
                continue;
            if (::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0)
                break;
            closeSocket(sock);
            sock = kInvalidSocket;
        }
        freeaddrinfo(res);
        return sock;
    }

    // Blocking send-all / recv-some on a native socket.
    static bool sendAll(socket_t s, const QByteArray &data)
    {
        int off = 0;
        while (off < data.size()) {
            const int n = ::send(s, data.constData() + off, data.size() - off, 0);
            if (n <= 0)
                return false;
            off += n;
        }
        return true;
    }
    static QByteArray recvSome(socket_t s)
    {
        char buf[512];
        const int n = ::recv(s, buf, sizeof(buf), 0);
        return n > 0 ? QByteArray(buf, n) : QByteArray();
    }

    // Performs the proxy handshake so the socket ends up connected to the target.
    bool proxyHandshake(socket_t s)
    {
        const ProxyConfig &p = m_params.proxy;
        if (p.type == ProxyConfig::Type::Socks5) {
            const bool userPass = !p.username.isEmpty();
            if (!sendAll(s, proxy::socks5Greeting(userPass)))
                return false;
            const int method = proxy::socks5ParseMethod(recvSome(s));
            if (method < 0)
                return false;
            if (method == 0x02) {
                if (!sendAll(s, proxy::socks5UserPass(p.username, p.password)))
                    return false;
                if (!proxy::socks5UserPassOk(recvSome(s)))
                    return false;
            }
            if (!sendAll(s, proxy::socks5ConnectRequest(m_params.host, m_params.port)))
                return false;
            return proxy::socks5ConnectOk(recvSome(s));
        }
        if (p.type == ProxyConfig::Type::Http) {
            if (!sendAll(s, proxy::httpConnectRequest(m_params.host, m_params.port,
                                                      p.username, p.password)))
                return false;
            return proxy::httpConnectOk(recvSome(s));
        }
        return true;
    }

    bool openSocket()
    {
        const ProxyConfig &proxy = m_params.proxy;
        socket_t sock;
        if (proxy.enabled()) {
            // Connect to the proxy, then tunnel to the real host.
            sock = tcpConnect(proxy.host.toUtf8(), proxy.port);
            if (sock == kInvalidSocket)
                return false;
            if (!proxyHandshake(sock)) {
                closeSocket(sock);
                return false;
            }
        } else {
            sock = tcpConnect(m_params.host.toUtf8(), m_params.port);
        }
        if (sock == kInvalidSocket)
            return false;
        m_socket = sock;
        return true;
    }

    void emitFingerprint()
    {
        const char *hash = libssh2_hostkey_hash(m_session, LIBSSH2_HOSTKEY_HASH_SHA256);
        if (!hash)
            return;
        QString hex;
        for (int i = 0; i < 32; ++i) {
            if (i)
                hex += ':';
            hex += QString("%1").arg(static_cast<unsigned char>(hash[i]), 2, 16,
                                     QChar('0'));
        }
        emit hostKeyFingerprint(hex);
    }

    bool authenticate()
    {
        switch (m_params.authMethod) {
        case termsync::core::SshAuthMethod::PublicKey:
            return authPublicKey();
        case termsync::core::SshAuthMethod::Agent:
            return authAgent();
        case termsync::core::SshAuthMethod::KeyboardInteractive:
            return authKeyboardInteractive();
        case termsync::core::SshAuthMethod::Password:
        default:
            return authPassword();
        }
    }

    bool authPassword()
    {
        const QByteArray user = m_params.username.toUtf8();
        const QByteArray pass = m_params.password.toUtf8();
        return libssh2_userauth_password(m_session, user.constData(),
                                         pass.constData()) == 0;
    }

    bool authPublicKey()
    {
        const QByteArray user = m_params.username.toUtf8();
        const QByteArray priv = m_params.privateKeyPath.toUtf8();
        const QByteArray pub = (m_params.privateKeyPath + ".pub").toUtf8();
        const QByteArray phrase = m_params.passphrase.toUtf8();
        const bool havePub = QFileInfo::exists(m_params.privateKeyPath + ".pub");
        return libssh2_userauth_publickey_fromfile(
                   m_session, user.constData(), havePub ? pub.constData() : nullptr,
                   priv.constData(),
                   phrase.isEmpty() ? nullptr : phrase.constData()) == 0;
    }

    bool authAgent()
    {
        const QByteArray user = m_params.username.toUtf8();
        LIBSSH2_AGENT *agent = libssh2_agent_init(m_session);
        if (!agent)
            return false;
        struct AgentGuard {
            LIBSSH2_AGENT *a;
            ~AgentGuard() { libssh2_agent_disconnect(a); libssh2_agent_free(a); }
        } guard{agent};

        if (libssh2_agent_connect(agent) != 0)
            return false;
        if (libssh2_agent_list_identities(agent) != 0)
            return false;

        struct libssh2_agent_publickey *identity = nullptr;
        for (;;) {
            const int rc = libssh2_agent_get_identity(agent, &identity,
                                                      identity /*prev*/);
            if (rc != 0) // 1 = end of list, <0 = error
                return false;
            if (libssh2_agent_userauth(agent, user.constData(), identity) == 0)
                return true; // authenticated with this identity
        }
    }

    // Answers keyboard-interactive prompts with the stored password. This
    // covers single-prompt "password" setups; true multi-prompt OTP with a
    // live dialog is a follow-up.
    static void kbdCallback(const char *, int, const char *, int,
                            int num_prompts,
                            const LIBSSH2_USERAUTH_KBDINT_PROMPT *,
                            LIBSSH2_USERAUTH_KBDINT_RESPONSE *responses,
                            void **abstract)
    {
        auto *self = static_cast<SshWorker *>(*abstract);
        const QByteArray pass = self->m_params.password.toUtf8();
        for (int i = 0; i < num_prompts; ++i) {
            responses[i].text = static_cast<char *>(malloc(pass.size()));
            memcpy(responses[i].text, pass.constData(), pass.size());
            responses[i].length = static_cast<unsigned int>(pass.size());
        }
    }

    bool authKeyboardInteractive()
    {
        const QByteArray user = m_params.username.toUtf8();
        *libssh2_session_abstract(m_session) = this;
        return libssh2_userauth_keyboard_interactive(
                   m_session, user.constData(), &SshWorker::kbdCallback) == 0;
    }

    bool openShell()
    {
        m_channel = libssh2_channel_open_session(m_session);
        if (!m_channel)
            return false;
        if (libssh2_channel_request_pty_ex(m_channel, "xterm-256color",
                                            sizeof("xterm-256color") - 1, nullptr, 0,
                                            m_params.cols, m_params.rows, 0, 0)) {
            return false;
        }
        if (libssh2_channel_shell(m_channel))
            return false;
        return true;
    }

    // Blocks until the socket is ready in the direction libssh2 wants.
    void waitSocket()
    {
        struct timeval tv{};
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        fd_set fd;
        FD_ZERO(&fd);
        FD_SET(m_socket, &fd);

        fd_set *readfd = nullptr;
        fd_set *writefd = nullptr;
        const int dir = libssh2_session_block_directions(m_session);
        if (dir & LIBSSH2_SESSION_BLOCK_INBOUND)
            readfd = &fd;
        if (dir & LIBSSH2_SESSION_BLOCK_OUTBOUND)
            writefd = &fd;

        ::select(static_cast<int>(m_socket + 1), readfd, writefd, nullptr, &tv);
    }

    void handleRemoteClose()
    {
        if (m_reportedClose)
            return;
        m_reportedClose = true;
        teardown();
    }

    void teardown()
    {
        if (m_readNotifier) {
            m_readNotifier->setEnabled(false);
            m_readNotifier->deleteLater();
            m_readNotifier = nullptr;
        }
        if (m_pump) {
            m_pump->stop();
            m_pump->deleteLater();
            m_pump = nullptr;
        }
        if (m_channel) {
            libssh2_channel_close(m_channel);
            libssh2_channel_free(m_channel);
            m_channel = nullptr;
        }
        if (m_session) {
            libssh2_session_disconnect(m_session, "Client disconnecting");
            libssh2_session_free(m_session);
            m_session = nullptr;
        }
        if (m_socket != kInvalidSocket) {
            closeSocket(m_socket);
            m_socket = kInvalidSocket;
        }
        emit disconnected();
    }

    SshConnectionParams m_params;
    socket_t m_socket = kInvalidSocket;
    LIBSSH2_SESSION *m_session = nullptr;
    LIBSSH2_CHANNEL *m_channel = nullptr;
    QTimer *m_pump = nullptr;
    QSocketNotifier *m_readNotifier = nullptr;
    bool m_reportedClose = false;
};

// ---------------------------------------------------------------------------
// SshConnection — public facade (UI thread).
// ---------------------------------------------------------------------------
SshConnection::SshConnection(QObject *parent)
    : AbstractTerminalConnection(parent)
{
    qRegisterMetaType<termsync::core::SshConnectionParams>();

    m_thread = new QThread(this);
    m_worker = new SshWorker;
    m_worker->moveToThread(m_thread);

    // Relay worker signals to the public interface (auto → queued across
    // threads). Track connection state on the way through.
    connect(m_worker, &SshWorker::connected, this, [this] {
        m_connected = true;
        emit connected();
    });
    connect(m_worker, &SshWorker::hostKeyFingerprint, this,
            &SshConnection::hostKeyFingerprint);
    connect(m_worker, &SshWorker::dataReceived, this, &SshConnection::dataReceived);
    connect(m_worker, &SshWorker::authenticationFailed, this,
            &SshConnection::authenticationFailed);
    connect(m_worker, &SshWorker::errorOccurred, this, &SshConnection::errorOccurred);
    connect(m_worker, &SshWorker::disconnected, this, [this] {
        m_connected = false;
        emit disconnected();
    });

    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

SshConnection::~SshConnection()
{
    disconnectFromHost();
    m_thread->quit();
    m_thread->wait();
}

void SshConnection::connectToHost(const SshConnectionParams &params)
{
    QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection,
                              Q_ARG(termsync::core::SshConnectionParams, params));
}

void SshConnection::approveHostKey(bool accept)
{
    QMetaObject::invokeMethod(m_worker, "proceed", Qt::QueuedConnection,
                              Q_ARG(bool, accept));
}

void SshConnection::sendData(const QByteArray &data)
{
    QMetaObject::invokeMethod(m_worker, "queueData", Qt::QueuedConnection,
                              Q_ARG(QByteArray, data));
}

void SshConnection::resize(int cols, int rows)
{
    QMetaObject::invokeMethod(m_worker, "doResize", Qt::QueuedConnection,
                              Q_ARG(int, cols), Q_ARG(int, rows));
}

void SshConnection::disconnectFromHost()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::QueuedConnection);
}

} // namespace termsync::core

#include "SshConnection.moc"
