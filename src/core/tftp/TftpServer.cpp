#include "tftp/TftpServer.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUdpSocket>

namespace termsync::core::tftp {

namespace {
constexpr int kTimeoutMs = 1000;
constexpr int kMaxRetries = 5;
} // namespace

// ===========================================================================
// TftpTransfer
// ===========================================================================
TftpTransfer::TftpTransfer(const QHostAddress &peer, quint16 peerPort,
                           QObject *parent)
    : QObject(parent), m_peer(peer), m_peerPort(peerPort)
{
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress(QHostAddress::AnyIPv4), 0); // fresh transfer ID
    connect(m_socket, &QUdpSocket::readyRead, this, &TftpTransfer::onReadyRead);

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(kTimeoutMs);
    connect(m_timer, &QTimer::timeout, this, &TftpTransfer::onTimeout);
}

bool TftpTransfer::startRead(const QString &filePath)
{
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        sendError(ErrorCode::FileNotFound, QStringLiteral("cannot open file"));
        fail(QStringLiteral("open failed"));
        return false;
    }
    m_writing = false;
    m_block = 1;
    m_lastData = m_file.read(kBlockSize);
    sendCurrentData();
    return true;
}

bool TftpTransfer::startWrite(const QString &filePath, bool allowOverwrite)
{
    if (QFileInfo::exists(filePath) && !allowOverwrite) {
        sendError(ErrorCode::FileAlreadyExists,
                  QStringLiteral("file already exists"));
        fail(QStringLiteral("exists"));
        return false;
    }
    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        sendError(ErrorCode::AccessViolation, QStringLiteral("cannot create file"));
        fail(QStringLiteral("create failed"));
        return false;
    }
    m_writing = true;
    m_block = 0;
    sendAck(0); // acknowledge the WRQ; client sends DATA block 1 next
    m_timer->start();
    return true;
}

void TftpTransfer::sendCurrentData()
{
    m_socket->writeDatagram(buildData(m_block, m_lastData), m_peer, m_peerPort);
    m_timer->start();
}

void TftpTransfer::sendAck(uint16_t block)
{
    m_socket->writeDatagram(buildAck(block), m_peer, m_peerPort);
}

void TftpTransfer::sendError(ErrorCode code, const QString &message)
{
    m_socket->writeDatagram(buildError(code, message), m_peer, m_peerPort);
}

void TftpTransfer::succeed(const QString &detail)
{
    if (m_done)
        return;
    m_done = true;
    m_timer->stop();
    m_file.close();
    emit finished(true, detail);
}

void TftpTransfer::fail(const QString &detail)
{
    if (m_done)
        return;
    m_done = true;
    m_timer->stop();
    m_file.close();
    emit finished(false, detail);
}

void TftpTransfer::onReadyRead()
{
    while (m_socket->hasPendingDatagrams()) {
        QByteArray buf(int(m_socket->pendingDatagramSize()), Qt::Uninitialized);
        QHostAddress from;
        quint16 fromPort = 0;
        m_socket->readDatagram(buf.data(), buf.size(), &from, &fromPort);

        // Enforce the transfer ID: only the original peer/port is our partner.
        if (fromPort != m_peerPort) {
            m_socket->writeDatagram(
                buildError(ErrorCode::UnknownTransferId,
                           QStringLiteral("unknown transfer id")),
                from, fromPort);
            continue;
        }

        const Packet p = parse(buf);
        if (!p.valid)
            continue;
        if (p.op == OpCode::Error) {
            fail(QStringLiteral("peer error: %1").arg(p.message));
            return;
        }

        if (!m_writing) {
            // Reading: expect ACK for the block we just sent.
            if (p.op == OpCode::Ack && p.block == m_block) {
                m_timer->stop();
                m_retries = 0;
                if (m_lastData.size() < kBlockSize) {
                    succeed(QStringLiteral("sent %1 block(s)").arg(m_block));
                    return;
                }
                ++m_block;
                m_lastData = m_file.read(kBlockSize);
                sendCurrentData();
            }
        } else {
            // Writing: expect the next DATA block.
            if (p.op != OpCode::Data)
                continue;
            const uint16_t expected = static_cast<uint16_t>(m_block + 1);
            if (p.block == expected) {
                m_file.write(p.payload);
                m_block = expected;
                m_retries = 0;
                sendAck(m_block);
                if (p.payload.size() < kBlockSize) {
                    succeed(QStringLiteral("received %1 block(s)").arg(m_block));
                    return;
                }
                m_timer->start();
            } else if (p.block == m_block) {
                sendAck(m_block); // duplicate — re-acknowledge
            }
        }
    }
}

void TftpTransfer::onTimeout()
{
    if (m_done)
        return;
    if (m_retries >= kMaxRetries) {
        fail(QStringLiteral("timed out"));
        return;
    }
    ++m_retries;
    emit log(QStringLiteral("retransmit (attempt %1)").arg(m_retries));
    if (!m_writing)
        sendCurrentData();
    else {
        sendAck(m_block);
        m_timer->start();
    }
}

// ===========================================================================
// TftpServer
// ===========================================================================
TftpServer::TftpServer(QObject *parent) : QObject(parent) {}

TftpServer::~TftpServer()
{
    stop();
}

bool TftpServer::start(quint16 port, const QHostAddress &address)
{
    stop();
    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(address, port)) {
        m_lastError = m_socket->errorString();
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    m_port = m_socket->localPort();
    connect(m_socket, &QUdpSocket::readyRead, this, &TftpServer::onReadyRead);
    emit logMessage(QStringLiteral("TFTP server listening on port %1, root %2")
                        .arg(m_port)
                        .arg(m_root));
    return true;
}

void TftpServer::stop()
{
    for (TftpTransfer *t : std::as_const(m_transfers))
        t->deleteLater();
    m_transfers.clear();
    if (m_socket) {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_port = 0;
}

bool TftpServer::isRunning() const
{
    return m_socket != nullptr;
}

QString TftpServer::resolvePath(const QString &name) const
{
    if (name.contains(QStringLiteral("..")))
        return {}; // reject traversal outright
    const QString clean = QDir::cleanPath(name);
    if (QDir::isAbsolutePath(clean))
        return {}; // requests are relative to the root
    const QDir root(m_root);
    const QString abs = QDir::cleanPath(root.absoluteFilePath(clean));
    const QString rootAbs = QDir::cleanPath(root.absolutePath());
    if (abs != rootAbs && !abs.startsWith(rootAbs + QLatin1Char('/')))
        return {};
    return abs;
}

void TftpServer::replyError(const QHostAddress &peer, quint16 peerPort,
                            ErrorCode code, const QString &message)
{
    if (m_socket)
        m_socket->writeDatagram(buildError(code, message), peer, peerPort);
}

void TftpServer::reap(TftpTransfer *t)
{
    m_transfers.removeAll(t);
    t->deleteLater();
}

void TftpServer::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray buf(int(m_socket->pendingDatagramSize()), Qt::Uninitialized);
        QHostAddress peer;
        quint16 peerPort = 0;
        m_socket->readDatagram(buf.data(), buf.size(), &peer, &peerPort);

        const Packet p = parse(buf);
        if (!p.valid || (p.op != OpCode::Rrq && p.op != OpCode::Wrq)) {
            // Stray DATA/ACK to the listening port, or garbage.
            continue;
        }

        const bool writing = (p.op == OpCode::Wrq);
        if (writing && m_readOnly) {
            replyError(peer, peerPort, ErrorCode::AccessViolation,
                       QStringLiteral("server is read-only"));
            emit logMessage(
                QStringLiteral("refused WRQ '%1' (read-only)").arg(p.filename));
            continue;
        }

        const QString path = resolvePath(p.filename);
        if (path.isEmpty()) {
            replyError(peer, peerPort, ErrorCode::AccessViolation,
                       QStringLiteral("illegal path"));
            continue;
        }
        if (!writing && !QFileInfo::exists(path)) {
            replyError(peer, peerPort, ErrorCode::FileNotFound,
                       QStringLiteral("file not found"));
            emit logMessage(
                QStringLiteral("RRQ '%1' — not found").arg(p.filename));
            continue;
        }

        auto *t = new TftpTransfer(peer, peerPort, this);
        const QString filename = p.filename;
        connect(t, &TftpTransfer::finished, this,
                [this, t, filename](bool ok, const QString &detail) {
                    emit transferFinished(filename, ok, detail);
                    reap(t);
                });
        connect(t, &TftpTransfer::log, this, [this, filename](const QString &m) {
            emit logMessage(QStringLiteral("[%1] %2").arg(filename, m));
        });

        const bool started =
            writing ? t->startWrite(path, m_allowOverwrite) : t->startRead(path);
        if (started) {
            m_transfers.append(t);
            emit transferStarted(filename, writing, peer);
            emit logMessage(QStringLiteral("%1 '%2' from %3")
                                .arg(writing ? QStringLiteral("WRQ")
                                             : QStringLiteral("RRQ"),
                                     filename, peer.toString()));
        } else {
            reap(t); // startXxx already sent an error to the client
        }
    }
}

} // namespace termsync::core::tftp
