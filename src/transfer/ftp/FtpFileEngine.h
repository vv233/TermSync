#pragma once

#include "FileEngine.h"

namespace termsync::transfer {

// Blocking FTP / FTPS engine built on libcurl. Implements the same FileEngine
// surface as the SFTP engine so the transfer session, dual-pane browser and
// sync engine are protocol-agnostic.
//
// Directory listings are obtained via LIST and parsed as Unix `ls -l` output
// (the common server format); unparseable lines fall back to name-only.
//
// Status: plain FTP is verified end-to-end. Explicit FTPS (setExplicitTls)
// negotiates AUTH TLS but the data-channel over TLS still needs interop tuning
// against some servers (observed CURLE_PARTIAL_FILE on LIST) — a documented
// follow-up; certificate verification is also intentionally relaxed for now.
class FtpFileEngine : public FileEngine
{
public:
    FtpFileEngine();
    ~FtpFileEngine() override;

    FtpFileEngine(const FtpFileEngine &) = delete;
    FtpFileEngine &operator=(const FtpFileEngine &) = delete;

    // Enables explicit TLS (FTPS, "AUTH TLS" upgrade). Call before connect.
    void setExplicitTls(bool on) { m_explicitTls = on; }

    bool connectToHost(const core::SshConnectionParams &params,
                       HostKeyVerifier verifier = {}) override;
    void disconnectFromHost() override;
    bool isConnected() const override;

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

private:
    QString baseUrl() const;           // ftp[s]://host:port
    QString urlForPath(const QString &path, bool dirTrailingSlash) const;
    bool runQuote(const QStringList &commands); // SITE/MKD/DELE/... via CURLOPT_QUOTE
    void applyAuth(void *curl) const;  // sets user/pass/TLS on an easy handle
    void setError(const QString &message);

    QString m_host;
    quint16 m_port = 21;
    QString m_user;
    QString m_password;
    bool m_explicitTls = false;
    bool m_connected = false;
    QString m_lastError;
};

} // namespace termsync::transfer
