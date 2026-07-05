#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

#include "ssh/SshConnection.h"

namespace termsync::transfer {

// One directory entry, protocol-agnostic (used by both SFTP and FTP listings).
struct FileEntry
{
    QString name;
    QString longName;
    quint64 size = 0;
    quint32 permissions = 0;
    QDateTime modifiedAt;
    bool isDirectory = false;
    bool isSymlink = false;
};

// Back-compat alias: earlier SFTP code (and the UI) refer to SftpEntry.
using SftpEntry = FileEntry;

// Common surface for a remote file-transfer backend. SftpFileEngine (libssh2)
// and FtpFileEngine (libcurl) both implement it, so the transfer session, the
// dual-pane browser, and the sync engine are protocol-agnostic.
class FileEngine
{
public:
    using HostKeyVerifier = std::function<bool(const QString &fingerprint)>;
    using ProgressFn = std::function<void(quint64 done, quint64 total)>;

    virtual ~FileEngine() = default;

    virtual bool connectToHost(const core::SshConnectionParams &params,
                               HostKeyVerifier verifier = {}) = 0;
    virtual void disconnectFromHost() = 0;
    virtual bool isConnected() const = 0;

    virtual QString lastError() const = 0;
    // SSH-only; empty for FTP.
    virtual QString hostKeyFingerprint() const { return {}; }

    virtual bool listDirectory(const QString &remotePath,
                               QVector<FileEntry> *entries) = 0;
    virtual bool downloadFile(const QString &remotePath, const QString &localPath,
                              ProgressFn progress = {},
                              const std::atomic<bool> *cancel = nullptr) = 0;
    virtual bool uploadFile(const QString &localPath, const QString &remotePath,
                            ProgressFn progress = {},
                            const std::atomic<bool> *cancel = nullptr) = 0;

    virtual bool makeDirectory(const QString &remotePath) = 0;
    virtual bool removeFile(const QString &remotePath) = 0;
    virtual bool removeDirectory(const QString &remotePath) = 0;
    virtual bool rename(const QString &fromPath, const QString &toPath) = 0;
    // No-op / unsupported returns false; callers treat that as non-fatal.
    virtual bool setPermissions(const QString &remotePath, quint32 mode) = 0;
    virtual bool statSize(const QString &remotePath, quint64 *size) = 0;
};

} // namespace termsync::transfer
