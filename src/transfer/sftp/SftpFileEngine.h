#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <functional>

#include "ssh/SshConnection.h"

namespace termsync::transfer {

struct SftpEntry
{
    QString name;
    QString longName;
    quint64 size = 0;
    quint32 permissions = 0;
    QDateTime modifiedAt;
    bool isDirectory = false;
    bool isSymlink = false;
};

// Blocking SFTP engine for M5. It intentionally has no Widgets dependency, so
// M6 can put it behind a transfer worker/queue without changing the protocol
// surface.
class SftpFileEngine
{
public:
    using HostKeyVerifier = std::function<bool(const QString &fingerprint)>;

    SftpFileEngine();
    ~SftpFileEngine();

    SftpFileEngine(const SftpFileEngine &) = delete;
    SftpFileEngine &operator=(const SftpFileEngine &) = delete;

    bool connectToHost(const core::SshConnectionParams &params,
                       HostKeyVerifier verifier = {});
    void disconnectFromHost();
    bool isConnected() const;

    QString hostKeyFingerprint() const { return m_hostKeyFingerprint; }
    QString lastError() const { return m_lastError; }

    bool listDirectory(const QString &remotePath, QVector<SftpEntry> *entries);
    bool downloadFile(const QString &remotePath, const QString &localPath);
    bool uploadFile(const QString &localPath, const QString &remotePath);

private:
    bool openSocket(const QString &host, quint16 port);
    bool authenticate(const core::SshConnectionParams &params);
    void setError(const QString &message);
    void emitFingerprint();
    void closeSocket();

    void *m_session = nullptr; // LIBSSH2_SESSION*
    void *m_sftp = nullptr;    // LIBSSH2_SFTP*
    quintptr m_socket = static_cast<quintptr>(-1);
    QString m_lastError;
    QString m_hostKeyFingerprint;
};

} // namespace termsync::transfer
