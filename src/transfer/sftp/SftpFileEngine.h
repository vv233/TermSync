#pragma once

#include "FileEngine.h"
#include "sync/SyncTypes.h"

namespace termsync::transfer {

// Blocking SFTP engine (libssh2). No Widgets dependency, so it sits behind the
// transfer worker/queue unchanged.
class SftpFileEngine : public FileEngine
{
public:
    SftpFileEngine();
    ~SftpFileEngine() override;

    SftpFileEngine(const SftpFileEngine &) = delete;
    SftpFileEngine &operator=(const SftpFileEngine &) = delete;

    bool connectToHost(const core::SshConnectionParams &params,
                       HostKeyVerifier verifier = {}) override;
    void disconnectFromHost() override;
    bool isConnected() const override;

    QString hostKeyFingerprint() const override { return m_hostKeyFingerprint; }
    QString lastError() const override { return m_lastError; }

    bool listDirectory(const QString &remotePath, QVector<FileEntry> *entries) override;

    bool downloadFile(const QString &remotePath, const QString &localPath,
                      ProgressFn progress = {},
                      const std::atomic<bool> *cancel = nullptr) override;
    bool uploadFile(const QString &localPath, const QString &remotePath,
                    ProgressFn progress = {},
                    const std::atomic<bool> *cancel = nullptr) override;

    bool makeDirectory(const QString &remotePath) override;
    bool removeFile(const QString &remotePath) override;
    bool removeDirectory(const QString &remotePath) override;
    bool rename(const QString &fromPath, const QString &toPath) override;
    bool setPermissions(const QString &remotePath, quint32 mode) override;
    bool statSize(const QString &remotePath, quint64 *size) override;
    // listRecursive is inherited from FileEngine (generic walk via listDirectory).

    // SCP transfers over the same authenticated session (M12). SCP is a
    // separate wire protocol from SFTP but shares the libssh2 session.
    bool scpDownload(const QString &remotePath, const QString &localPath,
                     ProgressFn progress = {},
                     const std::atomic<bool> *cancel = nullptr);
    bool scpUpload(const QString &localPath, const QString &remotePath,
                   quint32 mode = 0644, ProgressFn progress = {},
                   const std::atomic<bool> *cancel = nullptr);

private:
    bool downloadFileSequential(const QString &remotePath, const QString &localPath,
                                quint64 total, ProgressFn progress,
                                const std::atomic<bool> *cancel);
    bool uploadFileSequential(const QString &localPath, const QString &remotePath,
                              quint64 total, ProgressFn progress,
                              const std::atomic<bool> *cancel);
    bool downloadFileParallel(const QString &remotePath, const QString &localPath,
                              quint64 total, ProgressFn progress,
                              const std::atomic<bool> *cancel);
    bool uploadFileParallel(const QString &localPath, const QString &remotePath,
                            quint64 total, ProgressFn progress,
                            const std::atomic<bool> *cancel);
    bool downloadRange(const QString &remotePath, const QString &localPath,
                       quint64 offset, quint64 length,
                       std::atomic<quint64> *done, std::atomic<quint64> *reported,
                       quint64 total,
                       ProgressFn progress, const std::atomic<bool> *cancel,
                       const std::atomic<bool> *stop);
    bool uploadRange(const QString &localPath, const QString &remotePath,
                     quint64 offset, quint64 length,
                     std::atomic<quint64> *done, std::atomic<quint64> *reported,
                     quint64 total,
                     ProgressFn progress, const std::atomic<bool> *cancel,
                     const std::atomic<bool> *stop);
    bool connectSibling(SftpFileEngine *engine) const;

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
    core::SshConnectionParams m_params;
};

} // namespace termsync::transfer
