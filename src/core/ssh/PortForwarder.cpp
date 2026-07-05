#include "ssh/PortForwarder.h"

#include <QHostAddress>
#include <QMetaObject>
#include <QMutex>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QVector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#  include <netdb.h>
#  include <sys/socket.h>
#  include <unistd.h>
using socket_t = int;
static constexpr socket_t kInvalidSocket = -1;
#endif

#include <libssh2.h>

#include "net/Socks5.h"

namespace termsync::core {

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
} // namespace

// One bidirectional pipe between a local TCP socket and a direct-tcpip channel.
struct Pipe
{
    QTcpSocket *socket = nullptr;
    LIBSSH2_CHANNEL *channel = nullptr;
    // For Dynamic (SOCKS) pipes still performing the handshake.
    bool socksDone = false;
    QByteArray socksBuf;
    bool greeted = false;
};

class ForwardWorker : public QObject
{
    Q_OBJECT

public:
    ForwardWorker(const SshConnectionParams &params, const QVector<ForwardRule> &rules)
        : m_params(params), m_rules(rules) {}
    ~ForwardWorker() override { cleanup(); }

public slots:
    void run()
    {
        ensureGlobalInit();
        if (!connectSession()) {
            emit errorOccurred(tr("Port forward: SSH connection failed"));
            return;
        }
        for (const ForwardRule &rule : m_rules)
            startListener(rule);

        m_pump = new QTimer(this);
        m_pump->setInterval(10);
        connect(m_pump, &QTimer::timeout, this, &ForwardWorker::pump);
        m_pump->start();
    }

    void shutdown() { cleanup(); emit stopped(); }

signals:
    void listening(quint16 bindPort);
    void connectionOpened(quint16 bindPort, const QString &target);
    void errorOccurred(const QString &message);
    void stopped();

private:
    bool connectSession()
    {
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        const QByteArray host = m_params.host.toUtf8();
        const QByteArray port = QByteArray::number(m_params.port);
        struct addrinfo *res = nullptr;
        if (getaddrinfo(host.constData(), port.constData(), &hints, &res) != 0 || !res)
            return false;
        for (auto *ai = res; ai; ai = ai->ai_next) {
            m_socket = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (m_socket == kInvalidSocket)
                continue;
            if (::connect(m_socket, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0)
                break;
#ifdef _WIN32
            ::closesocket(m_socket);
#else
            ::close(m_socket);
#endif
            m_socket = kInvalidSocket;
        }
        freeaddrinfo(res);
        if (m_socket == kInvalidSocket)
            return false;

        m_session = libssh2_session_init();
        if (!m_session)
            return false;
        libssh2_session_set_blocking(m_session, 1);
        if (libssh2_session_handshake(m_session, static_cast<libssh2_socket_t>(m_socket)))
            return false;
        const QByteArray user = m_params.username.toUtf8();
        const QByteArray pass = m_params.password.toUtf8();
        if (libssh2_userauth_password(m_session, user.constData(), pass.constData()))
            return false;
        libssh2_session_set_blocking(m_session, 0);
        return true;
    }

    void startListener(const ForwardRule &rule)
    {
        auto *server = new QTcpServer(this);
        if (!server->listen(QHostAddress::LocalHost, rule.bindPort)) {
            emit errorOccurred(tr("Could not bind local port %1").arg(rule.bindPort));
            server->deleteLater();
            return;
        }
        m_servers.append(server);
        emit listening(rule.bindPort);
        connect(server, &QTcpServer::newConnection, this, [this, server, rule] {
            while (QTcpSocket *client = server->nextPendingConnection())
                onNewConnection(client, rule);
        });
    }

    void onNewConnection(QTcpSocket *client, const ForwardRule &rule)
    {
        auto *pipe = new Pipe;
        pipe->socket = client;
        m_pipes.append(pipe);
        connect(client, &QTcpSocket::disconnected, this,
                [this, pipe] { closePipe(pipe); });

        if (rule.type == ForwardRule::Local) {
            openChannel(pipe, rule.targetHost, rule.targetPort);
            emit connectionOpened(rule.bindPort,
                                  QStringLiteral("%1:%2")
                                      .arg(rule.targetHost)
                                      .arg(rule.targetPort));
        }
        // Dynamic pipes open their channel after the SOCKS handshake (see pump).
    }

    void openChannel(Pipe *pipe, const QString &host, quint16 port)
    {
        const QByteArray h = host.toUtf8();
        LIBSSH2_CHANNEL *ch = nullptr;
        // May return EAGAIN in non-blocking mode; retry briefly.
        for (int attempt = 0; attempt < 100 && !ch; ++attempt) {
            ch = libssh2_channel_direct_tcpip_ex(m_session, h.constData(), port,
                                                 "127.0.0.1", 0);
            if (!ch && libssh2_session_last_errno(m_session) != LIBSSH2_ERROR_EAGAIN)
                break;
        }
        if (!ch) {
            closePipe(pipe);
            return;
        }
        pipe->channel = ch;
    }

    void handleSocks(Pipe *pipe)
    {
        pipe->socksBuf += pipe->socket->readAll();
        if (!pipe->greeted) {
            const auto g = socks5::parseGreeting(pipe->socksBuf);
            if (g.status == socks5::Status::NeedMore)
                return;
            if (g.status == socks5::Status::Error || !g.noAuthOffered) {
                pipe->socket->write(socks5::greetingReply(false));
                closePipe(pipe);
                return;
            }
            pipe->socket->write(socks5::greetingReply(true));
            pipe->socksBuf.remove(0, g.consumed);
            pipe->greeted = true;
        }
        const auto req = socks5::parseRequest(pipe->socksBuf);
        if (req.status == socks5::Status::NeedMore)
            return;
        if (req.status == socks5::Status::Error || req.command != 0x01) {
            pipe->socket->write(socks5::requestReply(socks5::kCommandNotSupported));
            closePipe(pipe);
            return;
        }
        pipe->socksBuf.remove(0, req.consumed);
        openChannel(pipe, req.host, req.port);
        pipe->socket->write(socks5::requestReply(
            pipe->channel ? socks5::kSucceeded : socks5::kGeneralFailure));
        pipe->socksDone = true;
        emit connectionOpened(pipe->socket->localPort(),
                              QStringLiteral("%1:%2").arg(req.host).arg(req.port));
    }

    void pump()
    {
        for (Pipe *pipe : QVector<Pipe *>(m_pipes)) {
            if (!pipe->socket)
                continue;
            // SOCKS handshake for dynamic pipes not yet connected.
            if (!pipe->channel && !pipe->socksDone && pipe->socket->bytesAvailable())
                handleSocks(pipe);
            if (!pipe->channel)
                continue;

            // local -> channel
            while (pipe->socket->bytesAvailable() > 0) {
                QByteArray data = pipe->socket->readAll();
                int off = 0;
                while (off < data.size()) {
                    const ssize_t n = libssh2_channel_write(
                        pipe->channel, data.constData() + off, data.size() - off);
                    if (n == LIBSSH2_ERROR_EAGAIN) { QThread::yieldCurrentThread(); continue; }
                    if (n < 0) { closePipe(pipe); break; }
                    off += static_cast<int>(n);
                }
                if (!pipe->channel)
                    break;
            }
            if (!pipe->channel)
                continue;

            // channel -> local
            char buf[16384];
            for (;;) {
                const ssize_t n = libssh2_channel_read(pipe->channel, buf, sizeof(buf));
                if (n > 0) {
                    pipe->socket->write(buf, n);
                    continue;
                }
                if (n == LIBSSH2_ERROR_EAGAIN)
                    break;
                // 0 or error: check EOF
                if (n == 0 && !libssh2_channel_eof(pipe->channel))
                    break;
                closePipe(pipe);
                break;
            }
        }
    }

    void closePipe(Pipe *pipe)
    {
        m_pipes.removeAll(pipe);
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

    void cleanup()
    {
        if (m_pump) { m_pump->stop(); m_pump->deleteLater(); m_pump = nullptr; }
        for (QTcpServer *s : m_servers) s->deleteLater();
        m_servers.clear();
        while (!m_pipes.isEmpty())
            closePipe(m_pipes.first());
        if (m_session) {
            libssh2_session_disconnect(m_session, "forward done");
            libssh2_session_free(m_session);
            m_session = nullptr;
        }
        if (m_socket != kInvalidSocket) {
#ifdef _WIN32
            ::closesocket(m_socket);
#else
            ::close(m_socket);
#endif
            m_socket = kInvalidSocket;
        }
    }

    SshConnectionParams m_params;
    QVector<ForwardRule> m_rules;
    socket_t m_socket = kInvalidSocket;
    LIBSSH2_SESSION *m_session = nullptr;
    QVector<QTcpServer *> m_servers;
    QVector<Pipe *> m_pipes;
    QTimer *m_pump = nullptr;
};

// ---------------------------------------------------------------------------
PortForwarder::PortForwarder(const SshConnectionParams &params,
                             const QVector<ForwardRule> &rules, QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<termsync::core::ForwardRule>>();
    m_thread = new QThread(this);
    m_worker = new ForwardWorker(params, rules);
    m_worker->moveToThread(m_thread);
    connect(m_worker, &ForwardWorker::listening, this, &PortForwarder::listening);
    connect(m_worker, &ForwardWorker::connectionOpened, this,
            &PortForwarder::connectionOpened);
    connect(m_worker, &ForwardWorker::errorOccurred, this,
            &PortForwarder::errorOccurred);
    connect(m_worker, &ForwardWorker::stopped, this, &PortForwarder::stopped);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
}

PortForwarder::~PortForwarder()
{
    stop();
    m_thread->quit();
    m_thread->wait();
}

void PortForwarder::start()
{
    QMetaObject::invokeMethod(m_worker, "run", Qt::QueuedConnection);
}

void PortForwarder::stop()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "shutdown", Qt::QueuedConnection);
}

} // namespace termsync::core

#include "PortForwarder.moc"
