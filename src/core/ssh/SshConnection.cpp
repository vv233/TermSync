#include "ssh/SshConnection.h"

#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QSocketNotifier>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "ssh/X11Auth.h"

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
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <netinet/tcp.h>
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

// libssh2 invokes this when the remote opens a forwarded X11 connection; it
// dispatches to the SshWorker stored in the session abstract (defined below).
LIBSSH2_X11_OPEN_FUNC(x11OpenTrampoline);

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

        // Service any forwarded X11 connections on the same session.
        if (!m_x11Pipes.isEmpty())
            pumpX11All();

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
    // Connect to one address with a bounded timeout (non-blocking connect +
    // select) so an unreachable address fails fast instead of stalling on the
    // ~20s OS TCP timeout.
    static socket_t connectAddr(const struct addrinfo *ai, int timeoutMs)
    {
        socket_t sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (sock == kInvalidSocket)
            return kInvalidSocket;
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(sock, FIONBIO, &nb);
#else
        const int flags = ::fcntl(sock, F_GETFL, 0);
        ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
        bool connected =
            ::connect(sock, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0;
        if (!connected) {
#ifdef _WIN32
            const bool inProgress = WSAGetLastError() == WSAEWOULDBLOCK;
#else
            const bool inProgress = errno == EINPROGRESS;
#endif
            if (inProgress) {
                fd_set wset;
                FD_ZERO(&wset);
                FD_SET(sock, &wset);
                timeval tv{};
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
                const int n = ::select(0, nullptr, &wset, nullptr, &tv);
#else
                const int n = ::select(sock + 1, nullptr, &wset, nullptr, &tv);
#endif
                if (n > 0 && FD_ISSET(sock, &wset)) {
                    int soerr = 0;
                    socklen_t len = sizeof(soerr);
                    ::getsockopt(sock, SOL_SOCKET, SO_ERROR,
                                 reinterpret_cast<char *>(&soerr), &len);
                    connected = soerr == 0;
                }
            }
        }
#ifdef _WIN32
        nb = 0;
        ioctlsocket(sock, FIONBIO, &nb);
#else
        ::fcntl(sock, F_SETFL, flags);
#endif
        if (!connected) {
            closeSocket(sock);
            return kInvalidSocket;
        }
        return sock;
    }

    // Connect to host:port (IPv4 first, bounded per-address), returning a native
    // socket (or invalid).
    static socket_t tcpConnect(const QByteArray &host, quint16 port)
    {
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        const QByteArray portStr = QByteArray::number(port);
        struct addrinfo *res = nullptr;
        if (getaddrinfo(host.constData(), portStr.constData(), &hints, &res) != 0 || !res)
            return kInvalidSocket;
        std::vector<const struct addrinfo *> ordered;
        for (const struct addrinfo *ai = res; ai; ai = ai->ai_next)
            if (ai->ai_family == AF_INET)
                ordered.push_back(ai);
        for (const struct addrinfo *ai = res; ai; ai = ai->ai_next)
            if (ai->ai_family != AF_INET)
                ordered.push_back(ai);
        socket_t sock = kInvalidSocket;
        for (const struct addrinfo *ai : ordered) {
            sock = connectAddr(ai, 10000);
            if (sock != kInvalidSocket)
                break;
        }
        freeaddrinfo(res);
        if (sock != kInvalidSocket) {
            // Disable Nagle for low-latency keystrokes and SCP throughput.
            int one = 1;
            ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                         reinterpret_cast<const char *>(&one), sizeof(one));
        }
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
        // X11 forwarding must be requested BEFORE the shell starts so the server
        // sets DISPLAY in the shell's environment.
        if (m_params.x11Forwarding)
            requestX11();

        if (libssh2_channel_shell(m_channel))
            return false;
        return true;
    }

    // Requests X11 forwarding on the shell channel with a freshly-minted proxy
    // cookie, and arms the callback that accepts forwarded X11 channels.
    void requestX11()
    {
        m_proxyCookie = x11::generateCookie();
        m_localCookie = x11::readLocalCookie(m_params.x11Display);
        *libssh2_session_abstract(m_session) = this;
        libssh2_session_callback_set(m_session, LIBSSH2_CALLBACK_X11,
                                     reinterpret_cast<void *>(&x11OpenTrampoline));
        const QByteArray hex = x11::cookieToHex(m_proxyCookie).toLatin1();
        // single_connection = 0: allow multiple X11 clients; screen 0. The local
        // display only selects the X-server socket port (6000 + x11Display).
        libssh2_channel_x11_req_ex(m_channel, 0, x11::kAuthProtocol(),
                                   hex.constData(), 0);
    }

public:
    // Called (on this worker thread) from the libssh2 X11 open callback when the
    // remote opens a forwarded X11 connection. Connects to the local X server
    // and registers a proxy pipe; the cookie swap happens on the first packet.
    void onX11Open(LIBSSH2_CHANNEL *channel)
    {
        auto *pipe = new X11Pipe;
        pipe->channel = channel;
        pipe->socket = new QTcpSocket(this);
        connect(pipe->socket, &QTcpSocket::readyRead, this,
                &SshWorker::pumpX11All);
        connect(pipe->socket, &QTcpSocket::disconnected, this,
                [this, pipe] { closeX11Pipe(pipe); });
        m_x11Pipes.append(pipe);
        pipe->socket->connectToHost(m_params.x11Host,
                                    quint16(6000 + m_params.x11Display));
    }

private:

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

    // A forwarded X11 connection: an SSH x11 channel bridged to a local X socket.
    struct X11Pipe
    {
        LIBSSH2_CHANNEL *channel = nullptr;
        QTcpSocket *socket = nullptr;
        QByteArray setupBuf;    // buffers the initial X11 setup packet
        bool setupDone = false; // true once the cookie swap has been applied
    };

    void closeX11Pipe(X11Pipe *pipe)
    {
        if (!m_x11Pipes.removeOne(pipe))
            return;
        if (pipe->channel) {
            libssh2_channel_close(pipe->channel);
            libssh2_channel_free(pipe->channel);
        }
        if (pipe->socket) {
            pipe->socket->disconnect(this);
            pipe->socket->deleteLater();
        }
        delete pipe;
    }

    // Bridges each X11 pipe both ways. Called from the main I/O pump and from the
    // local X socket's readyRead, so both readable ends are drained promptly.
    void pumpX11All()
    {
        for (X11Pipe *pipe : QVector<X11Pipe *>(m_x11Pipes)) {
            if (!pipe->channel)
                continue;

            // channel (remote X client) -> local X server, swapping the cookie
            // in the first setup packet.
            char buf[16384];
            for (;;) {
                const ssize_t n =
                    libssh2_channel_read(pipe->channel, buf, sizeof(buf));
                if (n > 0) {
                    if (pipe->setupDone) {
                        pipe->socket->write(buf, n);
                    } else {
                        pipe->setupBuf.append(buf, int(n));
                        if (!applyX11Setup(pipe))
                            break; // pipe closed (mismatch) or needs more bytes
                    }
                    continue;
                }
                if (n == LIBSSH2_ERROR_EAGAIN)
                    break;
                if (n == 0 && !libssh2_channel_eof(pipe->channel))
                    break;
                closeX11Pipe(pipe);
                break;
            }
            if (!m_x11Pipes.contains(pipe))
                continue;

            // local X server -> channel (remote X client)
            while (pipe->socket && pipe->socket->bytesAvailable() > 0) {
                const QByteArray data = pipe->socket->readAll();
                int off = 0;
                while (off < data.size()) {
                    const ssize_t n = libssh2_channel_write(
                        pipe->channel, data.constData() + off, data.size() - off);
                    if (n == LIBSSH2_ERROR_EAGAIN) {
                        waitSocket();
                        continue;
                    }
                    if (n < 0) {
                        closeX11Pipe(pipe);
                        break;
                    }
                    off += int(n);
                }
                if (!m_x11Pipes.contains(pipe))
                    break;
            }
        }
    }

    // Tries to complete the X11 setup packet in pipe->setupBuf: rewrites the
    // cookie and flushes to the local socket. Returns false if the pipe was
    // closed or the packet is still incomplete.
    bool applyX11Setup(X11Pipe *pipe)
    {
        const x11::RewriteResult r =
            x11::rewriteSetup(pipe->setupBuf, m_proxyCookie, m_localCookie);
        switch (r.status) {
        case x11::RewriteStatus::NeedMore:
            return false; // buffer more from the channel
        case x11::RewriteStatus::Mismatch:
        case x11::RewriteStatus::Malformed:
            closeX11Pipe(pipe);
            return false;
        case x11::RewriteStatus::Ok:
        case x11::RewriteStatus::Passthrough:
            pipe->socket->write(r.rewritten);
            // Forward any bytes that followed the setup packet verbatim.
            if (r.consumed < pipe->setupBuf.size())
                pipe->socket->write(pipe->setupBuf.mid(r.consumed));
            pipe->setupBuf.clear();
            pipe->setupDone = true;
            return true;
        }
        return true;
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
        while (!m_x11Pipes.isEmpty())
            closeX11Pipe(m_x11Pipes.first());
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

    // X11 forwarding state.
    QByteArray m_proxyCookie; // cookie we handed the remote
    QByteArray m_localCookie; // real local X-server cookie (may be empty)
    QVector<X11Pipe *> m_x11Pipes;
};

namespace {
// Defined here (after SshWorker) so it can dispatch to the worker.
LIBSSH2_X11_OPEN_FUNC(x11OpenTrampoline)
{
    (void)session;
    (void)shost;
    (void)sport;
    if (auto *worker = static_cast<SshWorker *>(*abstract))
        worker->onX11Open(channel);
}
} // namespace

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
