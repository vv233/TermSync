#pragma once

#include "FileEngine.h"
#include "sync/SyncTypes.h"

namespace termsync::transfer {

class RateLimiter; // token-bucket throttle, defined in the .cpp

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
    // Set the remote file's mtime (and atime) in seconds since epoch, so sync's
    // mtime/size comparison stays idempotent across runs.
    bool setModifiedTime(const QString &remotePath, qint64 secsSinceEpoch) override;
    // Run a command on the server over an SSH exec channel (raw/quote command).
    bool runCommand(const QString &command, QString *stdoutText, int *exitCode) override;
    // Streaming exec, used by the bulk folder-transfer path (tar over one
    // channel instead of N per-file SFTP round-trips).
    bool runCommandToFile(const QString &command, const QString &localPath,
                          ProgressFn progress = {},
                          const std::atomic<bool> *cancel = nullptr,
                          int *exitCode = nullptr) override;
    bool runCommandFromFile(const QString &command, const QString &localPath,
                            ProgressFn progress = {},
                            const std::atomic<bool> *cancel = nullptr,
                            int *exitCode = nullptr) override;
    void setRateLimitBytesPerSec(quint64 bytesPerSec) override { m_rateBytesPerSec = bytesPerSec; }
    void setResume(bool resume) override { m_resume = resume; }
    void setPauseFlag(const std::atomic<bool> *pause) override { m_pauseFlag = pause; }
    // "Relentless": on a dropped connection mid-transfer, reconnect (bounded
    // attempts with backoff) and continue instead of failing. Parallel lanes
    // resume per-range; the sequential path resumes from the contiguous prefix.
    void setRelentless(bool relentless) override { m_relentless = relentless; }
    void setPreservePermissions(bool preserve) override { m_preservePerms = preserve; }
    void setAsciiMode(bool ascii) override { m_asciiMode = ascii; }
    void setKeepaliveSeconds(int seconds) override { m_keepaliveSeconds = seconds; }
    int keepalive() override;

    // Symlink helpers (SFTP-specific). readlink returns the raw link target;
    // realpath canonicalises a path on the server; createSymlink makes one.
    bool readlink(const QString &remotePath, QString *target);
    bool realpath(const QString &remotePath, QString *resolved);
    bool createSymlink(const QString &target, const QString &linkPath);
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
                                quint64 total, quint64 startOffset, ProgressFn progress,
                                const std::atomic<bool> *cancel);
    bool uploadFileSequential(const QString &localPath, const QString &remotePath,
                              quint64 total, quint64 startOffset, ProgressFn progress,
                              const std::atomic<bool> *cancel);
    bool downloadFileParallel(const QString &remotePath, const QString &localPath,
                              quint64 total, quint64 startOffset, ProgressFn progress,
                              const std::atomic<bool> *cancel);
    bool uploadFileParallel(const QString &localPath, const QString &remotePath,
                            quint64 total, quint64 startOffset, ProgressFn progress,
                            const std::atomic<bool> *cancel);
    // rangeDone (nullable) accumulates bytes this lane has committed, so a
    // relentless retry can resume the lane from exactly where it dropped.
    bool downloadRange(const QString &remotePath, const QString &localPath,
                       quint64 offset, quint64 length,
                       std::atomic<quint64> *done, std::atomic<quint64> *reported,
                       std::atomic<quint64> *rangeDone, quint64 total,
                       ProgressFn progress, const std::atomic<bool> *cancel,
                       const std::atomic<bool> *stop);
    bool uploadRange(const QString &localPath, const QString &remotePath,
                     quint64 offset, quint64 length,
                     std::atomic<quint64> *done, std::atomic<quint64> *reported,
                     std::atomic<quint64> *rangeDone, quint64 total,
                     ProgressFn progress, const std::atomic<bool> *cancel,
                     const std::atomic<bool> *stop);
    bool connectSibling(SftpFileEngine *engine) const;
    bool reconnectForRetry(); // disconnect + reconnect the primary session
    // Effective cap: explicit setRateLimitBytesPerSec() wins, else the env default.
    quint64 effectiveRateBytesPerSec() const;

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
    quint64 m_rateBytesPerSec = 0;      // 0 = unlimited
    bool m_resume = false;              // resume from destination size on next transfer
    RateLimiter *m_limiter = nullptr;   // non-owning; shared by parallel siblings for one transfer
    const std::atomic<bool> *m_pauseFlag = nullptr; // non-owning; parks the transfer while true
    bool m_relentless = false;          // reconnect + continue on a dropped connection
    bool m_preservePerms = false;       // chmod remote to match local after upload
    bool m_asciiMode = false;           // translate line endings (text transfers)
    int m_keepaliveSeconds = 0;         // 0 = keepalive off

    bool asciiUpload(const QString &localPath, const QString &remotePath,
                     ProgressFn progress, const std::atomic<bool> *cancel);
    bool asciiDownload(const QString &remotePath, const QString &localPath,
                       ProgressFn progress, const std::atomic<bool> *cancel);
    void applyLocalPermissions(const QString &localPath, const QString &remotePath);
};

} // namespace termsync::transfer
