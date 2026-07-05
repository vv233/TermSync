#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <atomic>
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

    using ProgressFn = std::function<void(quint64 done, quint64 total)>;

    bool listDirectory(const QString &remotePath, QVector<SftpEntry> *entries);

    // Transfers report progress via `progress` and abort early if `*cancel`
    // becomes true (both optional).
    bool downloadFile(const QString &remotePath, const QString &localPath,
                      ProgressFn progress = {},
                      const std::atomic<bool> *cancel = nullptr);
    bool uploadFile(const QString &localPath, const QString &remotePath,
                    ProgressFn progress = {},
                    const std::atomic<bool> *cancel = nullptr);

    // Remote filesystem operations (M6).
    bool makeDirectory(const QString &remotePath);
    bool removeFile(const QString &remotePath);
    bool removeDirectory(const QString &remotePath);
    bool rename(const QString &fromPath, const QString &toPath);
    bool setPermissions(const QString &remotePath, quint32 mode);
    bool statSize(const QString &remotePath, quint64 *size);

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
