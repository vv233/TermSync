#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include "AbstractTerminalConnection.h"
#include "net/ProxyClient.h"

class QThread;

namespace termsync::core {

// Authentication method for an SSH connection.
enum class SshAuthMethod {
    Password = 0,
    PublicKey = 1,           // private key file (optionally passphrase-protected)
    KeyboardInteractive = 2, // answered with `password` for single-prompt setups
    Agent = 3,               // try identities from a running SSH agent
};

// Parameters needed to open an SSH2 shell session.
struct SshConnectionParams
{
    QString host;
    quint16 port = 22;
    QString username;
    QString password;

    // Authentication (M9). For PublicKey, privateKeyPath is required; the
    // matching ".pub" is used if present, and passphrase decrypts the key.
    SshAuthMethod authMethod = SshAuthMethod::Password;
    QString privateKeyPath;
    QString passphrase;

    // Firewall/proxy to reach the host through (M17). None = direct connect.
    ProxyConfig proxy;

    // X11 forwarding (M11): request X11 on the shell channel and tunnel remote
    // X clients to an X server at x11Host:(6000 + x11Display), with ssh -X-style
    // MIT-MAGIC-COOKIE-1 spoofing (see core::x11). x11Host is normally the local
    // machine but may point elsewhere (e.g. a WSL-hosted X server).
    bool x11Forwarding = false;
    int x11Display = 0;
    QString x11Host = QStringLiteral("127.0.0.1");

    // Initial PTY size (updated later via SshConnection::resize()).
    int cols = 80;
    int rows = 24;
};

class SshWorker; // internal, runs on the worker thread

// SshConnection is the public, thread-safe-facing wrapper around a single
// libssh2 SSH2 session. All blocking network + libssh2 work happens on a
// dedicated worker thread (libssh2 sessions are not safe to touch from
// multiple threads); this object lives in the caller/UI thread and
// communicates with the worker purely through queued signals/slots.
//
// M2 scope: connect, password auth, one interactive shell channel with a
// PTY, and raw byte read/write. VT parsing (M3) and multi-channel/SFTP
// (M5) build on top of this class.
class SshConnection : public AbstractTerminalConnection
{
    Q_OBJECT

public:
    explicit SshConnection(QObject *parent = nullptr);
    ~SshConnection() override;

    // Begins connecting asynchronously. Results arrive via signals.
    // After the handshake, hostKeyFingerprint() is emitted and the connection
    // pauses until approveHostKey() is called — this is where trust-on-first-use
    // verification happens. Authentication does not proceed before approval, so
    // the password is never sent to an unverified host.
    void connectToHost(const SshConnectionParams &params);

    // Owner's decision on the host key (in response to hostKeyFingerprint).
    // accept=false aborts the connection.
    void approveHostKey(bool accept);

    void sendData(const QByteArray &data) override;
    void resize(int cols, int rows) override;
    void disconnectFromHost() override;
    bool isConnected() const override { return m_connected; }

signals:
    // Host key fingerprint seen during handshake (SHA-256, hex) — SSH-specific.
    void hostKeyFingerprint(const QString &sha256Hex);
    // Authentication failed (bad password, method not allowed, ...).
    void authenticationFailed(const QString &reason);
    // (connected / dataReceived / errorOccurred / disconnected are inherited.)

private:
    QThread *m_thread = nullptr;
    SshWorker *m_worker = nullptr;
    bool m_connected = false;
};

} // namespace termsync::core

// Needed so SshConnectionParams can cross threads via a queued slot call.
Q_DECLARE_METATYPE(termsync::core::SshConnectionParams)
