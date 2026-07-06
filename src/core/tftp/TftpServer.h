#pragma once

#include <QFile>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>

#include "tftp/TftpProtocol.h"

class QUdpSocket;
class QTimer;

namespace termsync::core::tftp {

// One in-flight TFTP transfer. Per RFC 1350 it runs on its own socket (a fresh
// transfer ID / ephemeral port); DATA/ACK exchange with timeout + retransmit.
class TftpTransfer : public QObject
{
    Q_OBJECT

public:
    TftpTransfer(const QHostAddress &peer, quint16 peerPort, QObject *parent = nullptr);

    // Server -> client (client issued RRQ): stream `filePath` out as DATA.
    bool startRead(const QString &filePath);
    // Client -> server (client issued WRQ): receive DATA into `filePath`.
    bool startWrite(const QString &filePath, bool allowOverwrite);

signals:
    void finished(bool ok, const QString &detail);
    void log(const QString &message);

private slots:
    void onReadyRead();
    void onTimeout();

private:
    void sendCurrentData();
    void sendAck(uint16_t block);
    void sendError(ErrorCode code, const QString &message);
    void succeed(const QString &detail);
    void fail(const QString &detail);

    QUdpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    QHostAddress m_peer;
    quint16 m_peerPort = 0;

    QFile m_file;
    bool m_writing = false;
    uint16_t m_block = 0;
    QByteArray m_lastData; // last DATA payload sent (read) — for retransmit
    int m_retries = 0;
    bool m_done = false;
};

// A minimal built-in TFTP server (SecureCRT feature). Serves files from a root
// directory over UDP; RRQ reads and WRQ writes, with a read-only + overwrite
// policy and path-traversal protection. Event-driven (QUdpSocket), no threads.
class TftpServer : public QObject
{
    Q_OBJECT

public:
    explicit TftpServer(QObject *parent = nullptr);
    ~TftpServer() override;

    void setRootDirectory(const QString &dir) { m_root = dir; }
    QString rootDirectory() const { return m_root; }
    void setReadOnly(bool readOnly) { m_readOnly = readOnly; }
    bool isReadOnly() const { return m_readOnly; }
    void setAllowOverwrite(bool allow) { m_allowOverwrite = allow; }

    // Binds the listening socket. Port 69 is the well-known TFTP port but needs
    // privileges on most systems; any port works for testing. Returns false and
    // reports via lastError() if the bind fails.
    bool start(quint16 port = 69,
               const QHostAddress &address = QHostAddress::Any);
    void stop();
    bool isRunning() const;
    quint16 port() const { return m_port; }
    QString lastError() const { return m_lastError; }

signals:
    void logMessage(const QString &message);
    void transferStarted(const QString &filename, bool writing,
                         const QHostAddress &peer);
    void transferFinished(const QString &filename, bool ok,
                          const QString &detail);

private slots:
    void onReadyRead();

private:
    // Resolves a requested name to an absolute path inside the root, or an empty
    // string if it would escape the root (path-traversal guard).
    QString resolvePath(const QString &name) const;
    void replyError(const QHostAddress &peer, quint16 peerPort, ErrorCode code,
                    const QString &message);
    void reap(TftpTransfer *t);

    QUdpSocket *m_socket = nullptr;
    QString m_root;
    bool m_readOnly = false;
    bool m_allowOverwrite = true;
    quint16 m_port = 0;
    QString m_lastError;
    QList<TftpTransfer *> m_transfers;
};

} // namespace termsync::core::tftp
