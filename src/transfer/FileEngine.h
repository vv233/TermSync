#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>
#include <atomic>
#include <functional>

#include "ssh/SshConnection.h"
#include "sync/SyncTypes.h"

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

    // Cap the aggregate transfer rate in bytes/second (0 = unlimited). Backends
    // that don't implement throttling ignore it.
    virtual void setRateLimitBytesPerSec(quint64) {}
    // When enabled, the next download/upload resumes from the destination's
    // current size instead of restarting from zero. Ignored by backends that
    // can't seek. The caller is responsible for the prefix being trustworthy.
    virtual void setResume(bool) {}
    // Optional external flag: while it reads true the active transfer parks
    // (holding its position, consuming no bandwidth) until it clears or the
    // cancel flag is set. Non-owning; must outlive the transfer.
    virtual void setPauseFlag(const std::atomic<bool> *) {}
    // When enabled, a dropped connection mid-transfer is retried (reconnect +
    // continue) rather than failing. Backends without reconnect ignore it.
    virtual void setRelentless(bool) {}
    // After a successful upload, set the remote file's mode to match the local
    // file (unix perms). No-op on backends that can't chmod.
    virtual void setPreservePermissions(bool) {}
    // Text mode: translate line endings during transfer (upload local->CRLF,
    // download CRLF->LF). Forces a byte-inexact sequential path.
    virtual void setAsciiMode(bool) {}
    // Keepalive interval in seconds (0 = off). Applied on connect.
    virtual void setKeepaliveSeconds(int) {}
    // Send one keepalive now; returns seconds until the next is due, or -1 if
    // unsupported / not connected. Callers poll this on an idle timer.
    virtual int keepalive() { return -1; }

    // Move within the same backend. Default is a server-side rename; a backend
    // may override with something cheaper. (Cross-host moves are a caller-level
    // download+delete, not this.)
    bool moveFile(const QString &fromPath, const QString &toPath)
    {
        return rename(fromPath, toPath);
    }

    // Recursively enumerates the tree under `root` into a sync::Listing keyed by
    // forward-slash paths relative to `root` (drives the sync engine). Uses the
    // virtual listDirectory, so it works for any backend. Overridable if a
    // backend has a faster native walk.
    virtual bool listRecursive(const QString &root, sync::Listing *out)
    {
        if (!out)
            return false;
        struct Pending { QString remote; QString relative; };
        QVector<Pending> stack{{root, QString()}};
        while (!stack.isEmpty()) {
            const Pending dir = stack.takeLast();
            QVector<FileEntry> entries;
            if (!listDirectory(dir.remote, &entries))
                return false;
            for (const FileEntry &e : entries) {
                if (e.name == "." || e.name == "..")
                    continue;
                const QString rel = dir.relative.isEmpty()
                                        ? e.name
                                        : dir.relative + '/' + e.name;
                const QString remote = dir.remote.endsWith('/')
                                           ? dir.remote + e.name
                                           : dir.remote + '/' + e.name;
                sync::SyncEntry se;
                se.isDir = e.isDirectory;
                se.size = e.size;
                se.mtime = e.modifiedAt.toSecsSinceEpoch();
                out->insert(rel, se);
                if (e.isDirectory && !e.isSymlink)
                    stack.push_back({remote, rel});
            }
        }
        return true;
    }
};

} // namespace termsync::transfer
